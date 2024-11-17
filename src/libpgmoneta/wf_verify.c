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
#include <management.h>
#include <security.h>
#include <utils.h>
#include <verify.h>
#include <workers.h>
#include <workflow.h>

/* system */
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

struct payload
{
   struct json* data;
   struct deque* failed;
   struct deque* all;
};

static int verify_setup(int, char*, struct deque*);
static int verify_execute(int, char*, struct deque*);
static int verify_teardown(int, char*, struct deque*);

static void do_verify(void* arg);

struct workflow*
pgmoneta_create_verify(void)
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
   struct deque* failed_deque = NULL;
   struct deque* all_deque = NULL;
   struct csv_reader* csv = NULL;
   struct workers* workers = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Verify (execute): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   base = pgmoneta_get_server_backup_identifier(server, (char*)pgmoneta_deque_get(nodes, NODE_LABEL));

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

   if (!strcasecmp((char*)pgmoneta_deque_get(nodes, NODE_FILES), NODE_ALL))
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
      struct payload* payload = NULL;
      struct json* j = NULL;

      payload = (struct payload*)malloc(sizeof(struct payload));

      if (payload == NULL)
      {
         goto error;
      }

      memset(payload, 0, sizeof(struct payload));

      if (pgmoneta_json_create(&j))
      {
         goto error;
      }

      pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_DIRECTORY, (uintptr_t)pgmoneta_deque_get(nodes, NODE_BACKUP_DATA), ValueString);
      pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_FILENAME, (uintptr_t)columns[0], ValueString);
      pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_ORIGINAL, (uintptr_t)columns[1], ValueString);
      pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_HASH_ALGORITHM, (uintptr_t)backup->hash_algoritm, ValueInt32);

      payload->data = j;
      payload->failed = failed_deque;
      payload->all = all_deque;

      if (number_of_workers > 0)
      {
         pgmoneta_workers_add(workers, do_verify, (void*)payload);
      }
      else
      {
         do_verify((void*)payload);
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

   pgmoneta_deque_add(nodes, NODE_FAILED, (uintptr_t)failed_deque, ValueDeque);
   pgmoneta_deque_add(nodes, NODE_ALL, (uintptr_t)all_deque, ValueDeque);

   pgmoneta_csv_reader_destroy(csv);

   free(backup);

   free(base);
   free(info_file);
   free(manifest_file);

   return 0;

error:

   pgmoneta_deque_add(nodes, NODE_FAILED, (uintptr_t)NULL, ValueDeque);
   pgmoneta_deque_add(nodes, NODE_ALL, (uintptr_t)NULL, ValueDeque);

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
   int ha = 0;
   struct payload* p = NULL;
   struct json* j = NULL;

   p = (struct payload*)arg;

   j = p->data;

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

   ha = (int)pgmoneta_json_get(j, MANAGEMENT_ARGUMENT_HASH_ALGORITHM);
   if (ha == HASH_ALGORITHM_SHA256)
   {
      if (!pgmoneta_create_sha256_file(f, &hash_cal))
      {
         if (strcmp(hash_cal, (char*)pgmoneta_json_get(j, MANAGEMENT_ARGUMENT_ORIGINAL)))
         {
            failed = true;
         }
      }
      else
      {
         failed = true;
      }
   }
   else if (ha == HASH_ALGORITHM_SHA384)
   {
      if (!pgmoneta_create_sha384_file(f, &hash_cal))
      {
         if (strcmp(hash_cal, (char*)pgmoneta_json_get(j, MANAGEMENT_ARGUMENT_ORIGINAL)))
         {
            failed = true;
         }
      }
      else
      {
         failed = true;
      }
   }
   else if (ha == HASH_ALGORITHM_SHA512)
   {
      if (!pgmoneta_create_sha512_file(f, &hash_cal))
      {
         if (strcmp(hash_cal, (char*)pgmoneta_json_get(j, MANAGEMENT_ARGUMENT_ORIGINAL)))
         {
            failed = true;
         }
      }
      else
      {
         failed = true;
      }
   }
   else if (ha == HASH_ALGORITHM_SHA224)
   {
      if (!pgmoneta_create_sha224_file(f, &hash_cal))
      {
         if (strcmp(hash_cal, (char*)pgmoneta_json_get(j, MANAGEMENT_ARGUMENT_ORIGINAL)))
         {
            failed = true;
         }
      }
      else
      {
         failed = true;
      }
   }
   else if (ha == HASH_ALGORITHM_CRC32C)
   {
      if (!pgmoneta_create_crc32c_file(f, &hash_cal))
      {
         if (strcmp(hash_cal, (char*)pgmoneta_json_get(j, MANAGEMENT_ARGUMENT_ORIGINAL)))
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

      pgmoneta_deque_add(p->failed, f, (uintptr_t)j, ValueJSON);
   }
   else if (p->all != NULL)
   {
      pgmoneta_deque_add(p->all, f, (uintptr_t)j, ValueJSON);
   }
   else
   {
      pgmoneta_json_destroy(j);
   }

   free(hash_cal);
   free(f);
   free(p);

   return;

error:
   pgmoneta_log_error("Unable to calculate hash for %s", f);

   free(hash_cal);
   free(f);
   free(p);
}
