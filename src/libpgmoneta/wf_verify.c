/*
 * Copyright (C) 2025 The pgmoneta community
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
#include "value.h"
#include <pgmoneta.h>
#include <csv.h>
#include <logging.h>
#include <management.h>
#include <security.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

static char* verify_name(void);
static int verify_execute(char*, struct art*);

static void do_verify(struct worker_common* wc);

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
   return "Verify";
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
   if (pgmoneta_log_is_enabled(PGMONETA_LOGGING_LEVEL_DEBUG1))
   {
      char* a = NULL;
      a = pgmoneta_art_to_string(nodes, FORMAT_TEXT, NULL, 0);
      pgmoneta_log_debug("(Tree)\n%s", a);
      free(a);
   }
   assert(nodes != NULL);
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

   if (pgmoneta_deque_create(true, &failed_deque))
   {
      goto error;
   }

   if (!strcasecmp((char*)pgmoneta_art_search(nodes, USER_FILES), NODE_ALL))
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

   if (pgmoneta_csv_reader_init(manifest_file, &csv))
   {
      goto error;
   }

   while (pgmoneta_csv_next_row(csv, &number_of_columns, &columns))
   {
      struct worker_input* payload = NULL;
      struct json* j = NULL;

      if (pgmoneta_create_worker_input(NULL, NULL, NULL, -1, workers, &payload))
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
         if (workers->outcome)
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
   if (workers != NULL && !workers->outcome)
   {
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

   wi->data = NULL;
   wi->failed = NULL;
   wi->all = NULL;

   free(hash_cal);
   free(f);
   free(wi);
}
