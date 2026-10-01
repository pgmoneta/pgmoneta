/*
 * Copyright (C) 2026 The pgmoneta community
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <csv.h>
#include <logging.h>
#include <management.h>
#include <manifest.h>
#include <pagechecksum.h>
#include <progress.h>
#include <security.h>
#include <utils.h>
#include <value.h>
#include <walfile/pg_control.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

static char* verify_name(void);
static int verify_execute(char*, struct art*);

static void do_verify(struct worker_common* wc);

/*
 * What the cluster this backup came from was built with, read from the
 * backup's own pg_control rather than from the server as it is configured
 * now, because a backup can be older than the current settings.
 *
 * Set before the workers start and only read after that. Verify runs in a
 * process of its own, since main.c forks for it, so this is not shared with
 * any other verification.
 */
static bool page_checksums = false;
static size_t page_block_size = 0;
static uint32_t page_relseg_size = 0;

struct workflow*
pgmoneta_create_verify(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->name = &verify_name;
   wf->setup = &pgmoneta_common_setup;
   wf->execute = &verify_execute;
   wf->teardown = &pgmoneta_common_teardown;
   wf->next = NULL;

   return wf;
}

static char*
verify_name(void)
{
   return WORKFLOW_NAME_VERIFY;
}

static int
verify_execute(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   char* base = NULL;
   char* manifest_file = NULL;
   int number_of_columns = 0;
   char** columns = NULL;
   int number_of_workers = 0;
   struct deque* failed_deque = NULL;
   struct deque* all_deque = NULL;
   struct csv_reader* csv = NULL;
   struct workers* workers = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   pgmoneta_dump_art(nodes);

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
   assert(pgmoneta_art_contains_key(nodes, USER_FILES));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("Verify (execute): %s/%s", config->common.servers[server].name, label);

   base = pgmoneta_get_server_backup(server);

   manifest_file = pgmoneta_append(manifest_file, base);
   if (!pgmoneta_ends_with(manifest_file, "/"))
   {
      manifest_file = pgmoneta_append(manifest_file, "/");
   }
   manifest_file = pgmoneta_append(manifest_file, label);
   manifest_file = pgmoneta_append(manifest_file, "/");
   manifest_file = pgmoneta_append(manifest_file, "backup.manifest");

   if (pgmoneta_is_progress_enabled(server))
   {
      struct deque* manifest_paths = NULL;

      if (pgmoneta_manifest_get_paths(manifest_file, &manifest_paths))
      {
         pgmoneta_log_error("Verify: Unable to get manifest paths for progress tracking: %s", manifest_file);
         goto error;
      }
      pgmoneta_progress_set_total(server, pgmoneta_deque_size(manifest_paths));
      pgmoneta_deque_destroy(manifest_paths);
   }
   /* Whether the pages in this backup carry checksums at all is a property of
    * the cluster it was taken from, so it comes out of the backup's own
    * pg_control. A cluster built without them leaves pd_checksum meaningless
    * and there is nothing to check. */
   page_checksums = false;
   {
      struct control_file_data* controldata = NULL;

      if (!pgmoneta_read_control_data(server, (char*)pgmoneta_art_search(nodes, NODE_TARGET_BASE), &controldata) &&
          controldata != NULL)
      {
         uint32_t blcksz = 0;
         uint32_t relseg_size = 0;
         uint32_t checksum_version = 0;

         pgmoneta_control_data_page_layout(controldata, &blcksz, &relseg_size, &checksum_version);

         if (checksum_version != 0 && blcksz > 0 && relseg_size > 0)
         {
            page_checksums = true;
            page_block_size = (size_t)blcksz;
            page_relseg_size = relseg_size;

            pgmoneta_log_debug("Verify: Page checksums enabled, block size %zu, %u blocks per segment",
                               page_block_size, page_relseg_size);
         }
         else
         {
            pgmoneta_log_debug("Verify: Page checksums not enabled for this backup");
         }

         free(controldata);
      }
      else
      {
         /* Without pg_control there is no way to know the block size or
          * whether checksums were on, and guessing either would turn healthy
          * pages into reported corruption. The hashes are still checked. */
         pgmoneta_log_warn("Verify: Could not read pg_control, skipping page checksums");
      }
   }

   if (pgmoneta_deque_create(true, &failed_deque))
   {
      goto error;
   }

   char* user_files = (char*)pgmoneta_art_search(nodes, USER_FILES);
   if (user_files != NULL && !strcasecmp(user_files, NODE_ALL))
   {
      if (pgmoneta_deque_create(true, &all_deque))
      {
         goto error;
      }
   }

   number_of_workers = pgmoneta_get_number_of_workers(server);
   if (number_of_workers > 0)
   {
      pgmoneta_workers_initialize(number_of_workers, &workers);
   }

   if (pgmoneta_is_binary_file(manifest_file))
   {
      pgmoneta_log_error("Verify: Manifest file is not a text file");
      goto error;
   }

   if (pgmoneta_csv_reader_init(manifest_file, &csv))
   {
      goto error;
   }

   int line_number = 0;

   while (pgmoneta_csv_next_row(csv, &number_of_columns, &columns))
   {
      struct worker_input* payload = NULL;
      struct json* j = NULL;

      line_number++;

      /* Column check - heap buffer overflow prevention */
      if (number_of_columns != 2)
      {
         pgmoneta_log_error("Verify: Invalid manifest at line %d", line_number);
         goto error;
      }

      /* Empty filename or hash */
      if (columns[0] == NULL || strlen(columns[0]) == 0 ||
          columns[1] == NULL || strlen(columns[1]) == 0)
      {
         pgmoneta_log_error("Verify: Invalid manifest at line %d", line_number);
         goto error;
      }

      /* directory rows carry a trailing slash and are not files */
      if (pgmoneta_ends_with(columns[0], "/"))
      {
         free(columns);
         columns = NULL;
         continue;
      }

      if (pgmoneta_create_worker_input(NULL, NULL, NULL, server, workers, &payload))
      {
         goto error;
      }

      if (pgmoneta_json_create(&j))
      {
         goto error;
      }

      pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_DIRECTORY, (uintptr_t)pgmoneta_art_search(nodes, NODE_TARGET_BASE), ValueString);
      pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_FILENAME, (uintptr_t)columns[0], ValueString);
      pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_ORIGINAL, (uintptr_t)columns[1], ValueString);
      pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_HASH_ALGORITHM, (uintptr_t)"SHA512", ValueString);

      payload->data = j;
      payload->failed = failed_deque;
      payload->all = all_deque;

      if (number_of_workers > 0)
      {
         if (pgmoneta_workers_outcome_ok(workers))
         {
            pgmoneta_workers_add(workers, do_verify, (struct worker_common*)payload);
         }
      }
      else
      {
         do_verify((struct worker_common*)payload);
      }

      free(columns);
      columns = NULL;
   }

   pgmoneta_workers_wait(workers);
   if (workers != NULL && !pgmoneta_workers_outcome_ok(workers))
   {
      pgmoneta_workers_transfer_failures(workers, nodes);
      goto error;
   }
   pgmoneta_workers_destroy(workers);

   pgmoneta_deque_list(failed_deque);
   pgmoneta_deque_list(all_deque);

   pgmoneta_art_insert(nodes, NODE_FAILED, (uintptr_t)failed_deque, ValueDeque);
   pgmoneta_art_insert(nodes, NODE_ALL, (uintptr_t)all_deque, ValueDeque);

   pgmoneta_csv_reader_destroy(csv);

   free(base);
   free(manifest_file);

   return 0;

error:

   if (number_of_workers > 0)
   {
      pgmoneta_workers_destroy(workers);
   }

   pgmoneta_art_insert(nodes, NODE_FAILED, (uintptr_t)NULL, ValueDeque);
   pgmoneta_art_insert(nodes, NODE_ALL, (uintptr_t)NULL, ValueDeque);

   pgmoneta_deque_destroy(failed_deque);
   pgmoneta_deque_destroy(all_deque);

   pgmoneta_csv_reader_destroy(csv);

   free(columns);
   free(base);
   free(manifest_file);

   return 1;
}

static void
do_verify(struct worker_common* wc)
{
   struct worker_input* wi = (struct worker_input*)wc;
   char* f = NULL;
   char* hash_cal = NULL;
   bool failed = false;
   struct json* j = NULL;

   j = wi->data;

   f = pgmoneta_append(f, (char*)pgmoneta_json_get(j, MANAGEMENT_ARGUMENT_DIRECTORY));
   if (!pgmoneta_ends_with(f, "/"))
   {
      f = pgmoneta_append(f, "/");
   }
   f = pgmoneta_append(f, (char*)pgmoneta_json_get(j, MANAGEMENT_ARGUMENT_FILENAME));

   if (!pgmoneta_exists(f))
   {
      goto error;
   }

   if (!pgmoneta_create_sha512_file(f, &hash_cal))
   {
      if (strcmp(hash_cal, (char*)pgmoneta_json_get(j, MANAGEMENT_ARGUMENT_ORIGINAL)))
      {
         failed = true;
      }
   }
   else
   {
      goto error;
   }

   /* The hash says the file is the one that was backed up. The page checksums
    * say the pages inside it were not already damaged when it was. A file that
    * failed its hash is reported for that and not checked twice. */
   if (!failed && page_checksums)
   {
      char* relative = (char*)pgmoneta_json_get(j, MANAGEMENT_ARGUMENT_FILENAME);
      uint32_t segno = 0;

      if (pgmoneta_page_checksummable(relative, &segno))
      {
         uint32_t blockno = 0;
         uint16_t computed = 0;
         uint16_t stored = 0;
         int number_of_bad = 0;

         if (pgmoneta_page_verify_file(f, page_block_size, page_relseg_size, segno,
                                       &blockno, &computed, &stored, &number_of_bad))
         {
            char status[MISC_LENGTH];

            memset(&status[0], 0, sizeof(status));

            if (number_of_bad > 0)
            {
               pgmoneta_log_error("Verify: %s: block %u: calculated checksum %04X but block contains %04X",
                                  relative, blockno, computed, stored);

               pgmoneta_snprintf(&status[0], sizeof(status),
                                 "Page checksum failed for %d block%s, first at %u: calculated %04X but block contains %04X",
                                 number_of_bad, number_of_bad == 1 ? "" : "s", blockno, computed, stored);
            }
            else
            {
               /* Read the file to verify the hash a moment ago, so this is not
                * a missing file: it is not a whole number of blocks. */
               pgmoneta_log_error("Verify: %s: could not be read as whole blocks", relative);

               pgmoneta_snprintf(&status[0], sizeof(status),
                                 "Page checksums could not be checked: the file is not a whole number of blocks");
            }

            pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_STATUS, (uintptr_t)&status[0], ValueString);

            failed = true;
         }
      }
   }

   if (failed)
   {
      if (hash_cal != NULL && strlen(hash_cal) > 0)
      {
         pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_CALCULATED, (uintptr_t)hash_cal, ValueString);
      }
      else
      {
         failed = true;
         pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_CALCULATED, (uintptr_t)"Unknown", ValueString);
      }

      pgmoneta_deque_add(wi->failed, f, (uintptr_t)j, ValueJSON);
   }
   else if (wi->all != NULL)
   {
      pgmoneta_deque_add(wi->all, f, (uintptr_t)j, ValueJSON);
   }
   else
   {
      pgmoneta_json_destroy(j);
   }

   if (pgmoneta_is_progress_enabled(wi->level))
   {
      pgmoneta_progress_increment(wi->level, 1);
   }

   wi->data = NULL;
   wi->failed = NULL;
   wi->all = NULL;

   free(hash_cal);
   free(f);
   free(wi);

   return;

error:
   pgmoneta_log_error("Unable to calculate hash for %s", f);

   pgmoneta_json_destroy(wi->data);

   if (pgmoneta_is_progress_enabled(wi->level))
   {
      pgmoneta_progress_increment(wi->level, 1);
   }

   wi->data = NULL;
   wi->failed = NULL;
   wi->all = NULL;

   free(hash_cal);
   free(f);
   free(wi);
}
