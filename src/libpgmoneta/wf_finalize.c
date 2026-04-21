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
#include <art.h>
#include <deque.h>
#include <info.h>
#include <json.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <pgmoneta.h>
#include <security.h>
#include <utils.h>
#include <value.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <stdlib.h>

static char* finalize_name(void);
static int finalize_setup(char*, struct art*);
static int finalize_execute(char*, struct art*);
static int finalize_teardown(char*, struct art*);

static int finalize_backup_response(int server, struct art* nodes);
static int finalize_restore_response(int server, struct art* nodes);
static int finalize_verify_response(int server, struct art* nodes);

struct workflow*
pgmoneta_create_finalize(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->name = &finalize_name;
   wf->setup = &finalize_setup;
   wf->execute = &finalize_execute;
   wf->teardown = &finalize_teardown;
   wf->next = NULL;

   return wf;
}

static char*
finalize_name(void)
{
   return "Finalize";
}

static int
finalize_setup(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   int type = -1;
   bool active = false;
   struct main_configuration* config;

   if (!pgmoneta_art_contains_key(nodes, NODE_FINALIZE_TYPE))
   {
      return 0;
   }

   config = (struct main_configuration*)shmem;

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   type = (int)pgmoneta_art_search(nodes, NODE_FINALIZE_TYPE);

   if (!atomic_compare_exchange_strong(&config->common.servers[server].repository, &active, true))
   {
      pgmoneta_log_info("Finalize: Server %s is active", config->common.servers[server].name);
      pgmoneta_log_debug("Backup=%s, Restore=%s, Archive=%s, Delete=%s, Retention=%s",
                         config->common.servers[server].active_backup ? "Yes" : "No",
                         config->common.servers[server].active_restore ? "Yes" : "No",
                         config->common.servers[server].active_archive ? "Yes" : "No",
                         config->common.servers[server].active_delete ? "Yes" : "No",
                         config->common.servers[server].active_retention ? "Yes" : "No");
      return WORKFLOW_RESULT_FATAL;
   }

   switch (type)
   {
      case FINALIZE_TYPE_BACKUP:
         config->common.servers[server].active_backup = true;
         break;
      case FINALIZE_TYPE_RESTORE:
         config->common.servers[server].active_restore = true;
         break;
      case FINALIZE_TYPE_VERIFY:
         break;
      default:
         break;
   }

   pgmoneta_art_insert(nodes, NODE_FINALIZE_VALUE, (uintptr_t)true, ValueBool);

   pgmoneta_log_debug("Finalize (setup): %s", config->common.servers[server].name);

   return 0;
}

static int
finalize_execute(char* name __attribute__((unused)), struct art* nodes __attribute__((unused)))
{
   return 0;
}

static int
finalize_teardown(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   int type = -1;
   int client_fd = -1;
   uint8_t compression = 0;
   uint8_t encryption = 0;
   SSL* ssl = NULL;
   struct json* payload = NULL;
   struct timespec start_t;
   struct timespec end_t;
   struct main_configuration* config;

   if (!pgmoneta_art_contains_key(nodes, NODE_FINALIZE_TYPE))
   {
      return 0;
   }

   config = (struct main_configuration*)shmem;

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   type = (int)pgmoneta_art_search(nodes, NODE_FINALIZE_TYPE);

   /* Assert mandatory nodes */
   assert(pgmoneta_art_contains_key(nodes, NODE_CLIENT_FD));
   assert(pgmoneta_art_contains_key(nodes, NODE_PAYLOAD));
   assert(pgmoneta_art_contains_key(nodes, NODE_COMPRESSION));
   assert(pgmoneta_art_contains_key(nodes, NODE_ENCRYPTION));
   assert(pgmoneta_art_contains_key(nodes, NODE_START_TIME));

   client_fd = (int)pgmoneta_art_search(nodes, NODE_CLIENT_FD);
   ssl = (SSL*)pgmoneta_art_search(nodes, NODE_SSL);
   payload = (struct json*)pgmoneta_art_search(nodes, NODE_PAYLOAD);
   compression = (uint8_t)(uintptr_t)pgmoneta_art_search(nodes, NODE_COMPRESSION);
   encryption = (uint8_t)(uintptr_t)pgmoneta_art_search(nodes, NODE_ENCRYPTION);
   start_t = *(struct timespec*)pgmoneta_art_search(nodes, NODE_START_TIME);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   double total_seconds = 0.0;
   char* elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   /* Send the recorded result */
   if (pgmoneta_art_contains_key(nodes, NODE_ERROR_CODE))
   {
      int ec = (int)pgmoneta_art_search(nodes, NODE_ERROR_CODE);

      pgmoneta_management_response_error(ssl, client_fd, config->common.servers[server].name,
                                         ec, finalize_name(),
                                         compression, encryption, payload);
   }
   else
   {
      if (type == FINALIZE_TYPE_BACKUP && pgmoneta_art_contains_key(nodes, NODE_BACKUP))
      {
         struct backup* b = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);
         b->total_elapsed_time = total_seconds;
      }

      switch (type)
      {
         case FINALIZE_TYPE_BACKUP:
            finalize_backup_response(server, nodes);
            pgmoneta_log_info("Backup: %s/%s (Elapsed: %s)", config->common.servers[server].name, (char*)pgmoneta_art_search(nodes, NODE_LABEL), elapsed);
            break;
         case FINALIZE_TYPE_RESTORE:
            finalize_restore_response(server, nodes);
            pgmoneta_log_info("Restore: %s/%s (Elapsed: %s)", config->common.servers[server].name, ((struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP))->label, elapsed);
            break;
         case FINALIZE_TYPE_VERIFY:
            finalize_verify_response(server, nodes);
            pgmoneta_log_info("Verify: %s/%s (Elapsed: %s)", config->common.servers[server].name, (char*)pgmoneta_art_search(nodes, NODE_LABEL), elapsed);
            break;
         default:
            break;
      }

      pgmoneta_management_response_ok(ssl, client_fd, start_t, end_t, compression, encryption, payload);
   }

   free(elapsed);

   if ((bool)pgmoneta_art_search(nodes, NODE_FINALIZE_VALUE))
   {
      switch (type)
      {
         case FINALIZE_TYPE_BACKUP:
            config->common.servers[server].active_backup = false;
            atomic_store(&config->common.servers[server].repository, false);
            break;
         case FINALIZE_TYPE_RESTORE:
            config->common.servers[server].active_restore = false;
            atomic_store(&config->common.servers[server].repository, false);
            break;
         case FINALIZE_TYPE_VERIFY:
            atomic_store(&config->common.servers[server].repository, false);
            break;
         default:
            break;
      }

      pgmoneta_art_insert(nodes, NODE_FINALIZE_VALUE, (uintptr_t)false, ValueBool);

      pgmoneta_log_debug("Finalize (teardown): %s", config->common.servers[server].name);
   }

   return 0;
}

static int
finalize_backup_response(int server, struct art* nodes)
{
   char* backup_data = NULL;
   char* server_backup = NULL;
   char* label = NULL;
   struct backup* backup = NULL;
   struct json* payload = NULL;
   struct json* response = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP_DATA));
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_BACKUP));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
   assert(pgmoneta_art_contains_key(nodes, NODE_PAYLOAD));

   backup = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);
   backup_data = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_DATA);
   server_backup = (char*)pgmoneta_art_search(nodes, NODE_SERVER_BACKUP);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);
   payload = (struct json*)pgmoneta_art_search(nodes, NODE_PAYLOAD);

   backup->backup_size = pgmoneta_directory_size(backup_data);
   pgmoneta_save_info(server_backup, backup);

   if (pgmoneta_management_create_response(payload, server, &response))
   {
      return 1;
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->common.servers[server].name, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)label, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP_SIZE, (uintptr_t)backup->backup_size, ValueUInt64);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_RESTORE_SIZE, (uintptr_t)backup->restore_size, ValueUInt64);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BIGGEST_FILE_SIZE, (uintptr_t)backup->biggest_file_size, ValueUInt64);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_COMPRESSION, (uintptr_t)backup->compression, ValueInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_ENCRYPTION, (uintptr_t)backup->encryption, ValueInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_VALID, (uintptr_t)backup->valid, ValueInt8);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_INCREMENTAL, (uintptr_t)backup->type, ValueBool);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_INCREMENTAL_PARENT, (uintptr_t)backup->parent_label, ValueString);

   return 0;
}

static int
finalize_restore_response(int server, struct art* nodes)
{
   struct backup* backup = NULL;
   struct json* payload = NULL;
   struct json* response = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP));
   assert(pgmoneta_art_contains_key(nodes, NODE_PAYLOAD));

   backup = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);
   payload = (struct json*)pgmoneta_art_search(nodes, NODE_PAYLOAD);

   if (pgmoneta_management_create_response(payload, server, &response))
   {
      return 1;
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->common.servers[server].name, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)backup->label, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP_SIZE, (uintptr_t)backup->backup_size, ValueUInt64);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_RESTORE_SIZE, (uintptr_t)backup->restore_size, ValueUInt64);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BIGGEST_FILE_SIZE, (uintptr_t)backup->biggest_file_size, ValueUInt64);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_COMMENTS, (uintptr_t)backup->comments, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_COMPRESSION, (uintptr_t)backup->compression, ValueInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_ENCRYPTION, (uintptr_t)backup->encryption, ValueInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_INCREMENTAL, (uintptr_t)backup->type, ValueBool);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_INCREMENTAL_PARENT, (uintptr_t)backup->parent_label, ValueString);

   return 0;
}

static int
finalize_verify_response(int server, struct art* nodes)
{
   char* label = NULL;
   struct json* payload = NULL;
   struct json* response = NULL;
   struct json* filesj = NULL;
   struct json* failed = NULL;
   struct json* all = NULL;
   struct deque* f = NULL;
   struct deque* a = NULL;
   struct deque_iterator* fiter = NULL;
   struct deque_iterator* aiter = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
   assert(pgmoneta_art_contains_key(nodes, NODE_FAILED));
   assert(pgmoneta_art_contains_key(nodes, NODE_PAYLOAD));

   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);
   f = (struct deque*)pgmoneta_art_search(nodes, NODE_FAILED);
   payload = (struct json*)pgmoneta_art_search(nodes, NODE_PAYLOAD);

   if (pgmoneta_art_contains_key(nodes, NODE_ALL))
   {
      a = (struct deque*)pgmoneta_art_search(nodes, NODE_ALL);
   }

   if (pgmoneta_json_create(&failed))
   {
      return 1;
   }

   if (pgmoneta_deque_iterator_create(f, &fiter))
   {
      return 1;
   }

   while (pgmoneta_deque_iterator_next(fiter))
   {
      struct json* j = NULL;

      if (pgmoneta_json_clone((struct json*)pgmoneta_value_data(fiter->value), &j))
      {
         pgmoneta_deque_iterator_destroy(fiter);
         return 1;
      }

      pgmoneta_json_append(failed, (uintptr_t)j, ValueJSON);
   }

   pgmoneta_deque_iterator_destroy(fiter);

   if (a != NULL)
   {
      if (pgmoneta_json_create(&all))
      {
         return 1;
      }

      if (pgmoneta_deque_iterator_create(a, &aiter))
      {
         return 1;
      }

      while (pgmoneta_deque_iterator_next(aiter))
      {
         struct json* j = NULL;

         if (pgmoneta_json_clone((struct json*)pgmoneta_value_data(aiter->value), &j))
         {
            pgmoneta_deque_iterator_destroy(aiter);
            return 1;
         }

         pgmoneta_json_append(all, (uintptr_t)j, ValueJSON);
      }

      pgmoneta_deque_iterator_destroy(aiter);
   }

   if (pgmoneta_management_create_response(payload, server, &response))
   {
      return 1;
   }

   if (pgmoneta_json_create(&filesj))
   {
      return 1;
   }

   pgmoneta_json_put(filesj, MANAGEMENT_ARGUMENT_FAILED, (uintptr_t)failed, ValueJSON);
   pgmoneta_json_put(filesj, MANAGEMENT_ARGUMENT_ALL, (uintptr_t)all, ValueJSON);

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)label, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->common.servers[server].name, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_FILES, (uintptr_t)filesj, ValueJSON);

   return 0;
}
