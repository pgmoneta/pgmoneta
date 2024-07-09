/*
 * Copyright (C) 2024 The pgmoneta community
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
#include <deque.h>
#include <logging.h>
#include <security.h>
#include <utils.h>
#include <verify.h>
#include <workers.h>
#include <workflow.h>

/* system */
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

static int verify_setup(int, char*, struct deque*);
static int verify_execute(int, char*, struct deque*);
static int verify_teardown(int, char*, struct deque*);

static void do_verify(void* arg);

struct workflow*
pgmoneta_workflow_create_verify(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->setup = &verify_setup;
   wf->execute = &verify_execute;
   wf->teardown = &verify_teardown;
   wf->next = NULL;

   return wf;
}

static int
verify_setup(int server, char* identifier, struct deque* nodes)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Verify (setup): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   return 0;
}

static int
verify_execute(int server, char* identifier, struct deque* nodes)
{
   char* base = NULL;
   char* info_file = NULL;
   char* manifest_file = NULL;
   int number_of_columns = 0;
   char** columns = NULL;
   int number_of_workers = 0;
   struct backup* backup = NULL;
   struct csv_reader* csv = NULL;
   struct deque* failed_deque = NULL;
   struct deque* all_deque = NULL;
   struct workers* workers = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Verify (execute): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   base = pgmoneta_get_server_backup_identifier(server, (char*)pgmoneta_deque_get(nodes, "identifier"));

   info_file = pgmoneta_append(info_file, base);
   if (!pgmoneta_ends_with(info_file, "/"))
   {
      info_file = pgmoneta_append(info_file, "/");
   }
   info_file = pgmoneta_append(info_file, "backup.info");
   
   manifest_file = pgmoneta_append(manifest_file, base);
   if (!pgmoneta_ends_with(manifest_file, "/"))
   {
      manifest_file = pgmoneta_append(manifest_file, "/");
   }
   manifest_file = pgmoneta_append(manifest_file, "backup.manifest");
   
   pgmoneta_get_backup_file(info_file, &backup);

   if (pgmoneta_deque_create(true, &failed_deque))
   {
      goto error;
   }

   if (!strcasecmp((char*)pgmoneta_deque_get(nodes, "files"), "all"))
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
      struct verify_entry* ve = NULL;

      ve = (struct verify_entry*)malloc(sizeof(struct verify_entry));

      memset(ve, 0, sizeof(struct verify_entry));
      memcpy(ve->directory, (char*)pgmoneta_deque_get(nodes, "to"), strlen(pgmoneta_deque_get(nodes, "to")));
      memcpy(ve->filename, columns[0], strlen(columns[0]));
      memcpy(ve->original, columns[1], strlen(columns[1]));

      ve->hash_algoritm = backup->hash_algoritm;

      ve->failed = failed_deque;
      ve->all = all_deque;

      if (number_of_workers > 0)
      {
         pgmoneta_workers_add(workers, do_verify, (void*)ve);
      }
      else
      {
         do_verify((void*)ve);
      }

      free(columns);
      columns = NULL;
   }

   if (number_of_workers > 0)
   {
      pgmoneta_workers_wait(workers);
      pgmoneta_workers_destroy(workers);
   }

   pgmoneta_deque_list(failed_deque);
   pgmoneta_deque_list(all_deque);

   pgmoneta_deque_add(nodes, "failed", failed_deque);
   pgmoneta_deque_add(nodes, "all", all_deque);

   pgmoneta_csv_reader_destroy(csv);

   free(backup);

   free(base);
   free(info_file);
   free(manifest_file);

   return 0;

error:

   pgmoneta_deque_destroy(failed_deque);
   pgmoneta_deque_destroy(all_deque);

   pgmoneta_csv_reader_destroy(csv);

   free(backup);

   free(base);
   free(info_file);
   free(manifest_file);

   return 1;
}

static int
verify_teardown(int server, char* identifier, struct deque* nodes)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Verify (teardown): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   return 0;
}

static void
do_verify(void* arg)
{
   char* f = NULL;
   char* hash_cal = NULL;
   bool failed = false;
   struct verify_entry* ve = NULL;

   ve = (struct verify_entry*)arg;

   f = pgmoneta_append(f, ve->directory);
   if (!pgmoneta_ends_with(f, "/"))
   {
      f = pgmoneta_append(f, "/");
   }
   f = pgmoneta_append(f, ve->filename);

   if (!pgmoneta_exists(f))
   {
      goto error;
   }

   if (ve->hash_algoritm == HASH_ALGORITHM_SHA256)
   {
      if (!pgmoneta_create_sha256_file(f, &hash_cal))
      {
         if (strcmp(ve->original, hash_cal))
         {
            failed = true;
         }
      }
      else
      {
         failed = true;
      }
   }
   else if (ve->hash_algoritm == HASH_ALGORITHM_SHA384)
   {
      if (!pgmoneta_create_sha384_file(f, &hash_cal))
      {
         if (strcmp(ve->original, hash_cal))
         {
            failed = true;
         }
      }
      else
      {
         failed = true;
      }
   }
   else if (ve->hash_algoritm == HASH_ALGORITHM_SHA512)
   {
      if (!pgmoneta_create_sha512_file(f, &hash_cal))
      {
         if (strcmp(ve->original, hash_cal))
         {
            failed = true;
         }
      }
      else
      {
         failed = true;
      }
   }
   else if (ve->hash_algoritm == HASH_ALGORITHM_SHA224)
   {
      if (!pgmoneta_create_sha224_file(f, &hash_cal))
      {
         if (strcmp(ve->original, hash_cal))
         {
            failed = true;
         }
      }
      else
      {
         failed = true;
      }
   }
   else if (ve->hash_algoritm == HASH_ALGORITHM_CRC32C)
   {
      if (!pgmoneta_create_crc32c_file(f, &hash_cal))
      {
         if (strcmp(ve->original, hash_cal))
         {
            failed = true;
         }
      }
      else
      {
         failed = true;
      }
   }
   else
   {
      goto error;
   }

   memcpy(ve->calculated, hash_cal, strlen(hash_cal));

   if (failed)
   {
      pgmoneta_deque_put(ve->failed, f, ve, sizeof(struct verify_entry));
   }
   else
   {
      if (ve->all != NULL)
      {
         pgmoneta_deque_put(ve->all, f, ve, sizeof(struct verify_entry));
      }
   }

   free(ve);
   free(hash_cal);
   free(f);

   return;

error:
   pgmoneta_log_error("Unable to calculate hash for %s", f);

   free(ve);
   free(hash_cal);
   free(f);
}
