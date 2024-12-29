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
#include <backup.h>
#include <deque.h>
#include <info.h>
#include <json.h>
#include <logging.h>
#include <management.h>
#include <message.h>
#include <network.h>
#include <utils.h>
#include <value.h>
#include <workflow.h>

/* system */
#include <stdatomic.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

void
pgmoneta_backup(int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* payload)
{
   bool active = false;
   char date_str[128];
   char* date = NULL;
   char* elapsed = NULL;
   struct tm* time_info;
   struct timespec start_t;
   struct timespec end_t;
   time_t curr_t;
   double total_seconds;
   char* server_backup = NULL;
   char* root = NULL;
   char* d = NULL;
   unsigned long size;
   struct workflow* workflow = NULL;
   struct workflow* current = NULL;
   struct deque* nodes = NULL;
   struct backup* backup = NULL;
   struct json* response = NULL;
   struct configuration* config;

   pgmoneta_start_logging();

   config = (struct configuration*)shmem;

   if (!config->servers[server].valid)
   {
      pgmoneta_log_error("Backup: Server %s is not in a valid configuration", config->servers[server].name);
      pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_BACKUP_INVALID, compression, encryption, payload);

      goto error;
   }

   if (!config->servers[server].wal_streaming)
   {
      pgmoneta_log_error("Backup: Server %s is not WAL streaming", config->servers[server].name);
      pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_BACKUP_WAL, compression, encryption, payload);

      goto error;
   }

   if (!atomic_compare_exchange_strong(&config->servers[server].backup, &active, true))
   {
      pgmoneta_log_info("Backup: Active backup for server %s", config->servers[server].name);
      pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_BACKUP_ACTIVE, compression, encryption, payload);

      goto done;
   }

   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);

   curr_t = time(NULL);
   memset(&date_str[0], 0, sizeof(date_str));
   time_info = localtime(&curr_t);
   strftime(&date_str[0], sizeof(date_str), "%Y%m%d%H%M%S", time_info);

   date = pgmoneta_append(date, &date_str[0]);

   server_backup = pgmoneta_get_server_backup(server);
   root = pgmoneta_get_server_backup_identifier(server, date);

   pgmoneta_mkdir(root);

   d = pgmoneta_get_server_backup_identifier_data(server, date);

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_BACKUP, NULL);

   pgmoneta_deque_create(false, &nodes);

   current = workflow;
   while (current != NULL)
   {
      if (current->setup(server, date, nodes))
      {
         pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_BACKUP_SETUP, compression, encryption, payload);

         goto error;
      }
      current = current->next;
   }

   current = workflow;
   while (current != NULL)
   {
      if (current->execute(server, date, nodes))
      {
         pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_BACKUP_EXECUTE, compression, encryption, payload);

         goto error;
      }
      current = current->next;
   }

   current = workflow;
   while (current != NULL)
   {
      if (current->teardown(server, date, nodes))
      {
         pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_BACKUP_TEARDOWN, compression, encryption, payload);

         goto error;
      }
      current = current->next;
   }

   size = pgmoneta_directory_size(d);
   pgmoneta_update_info_unsigned_long(root, INFO_BACKUP, size);

   if (pgmoneta_management_create_response(payload, server, &response))
   {
      pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_ALLOCATION, compression, encryption, payload);

      goto error;
   }

   if (pgmoneta_get_backup(server_backup, date, &backup))
   {
      pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_BACKUP_ERROR, compression, encryption, payload);

      goto error;
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->servers[server].name, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)date, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP_SIZE, (uintptr_t)backup->backup_size, ValueUInt64);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_RESTORE_SIZE, (uintptr_t)backup->restore_size, ValueUInt64);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_COMPRESSION, (uintptr_t)backup->compression, ValueInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_ENCRYPTION, (uintptr_t)backup->encryption, ValueInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_VALID, (uintptr_t)backup->valid, ValueInt8);

   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_update_info_double(root, INFO_ELAPSED, total_seconds);

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_BACKUP_NETWORK, compression, encryption, payload);
      pgmoneta_log_error("Backup: Error sending response for %s", config->servers[server].name);

      goto error;
   }

   pgmoneta_log_info("Backup: %s/%s (Elapsed: %s)", config->servers[server].name, date, elapsed);

   atomic_store(&config->servers[server].backup, false);

done:

   pgmoneta_json_destroy(payload);

   pgmoneta_workflow_destroy(workflow);

   pgmoneta_deque_destroy(nodes);

   free(date);
   free(backup);
   free(elapsed);
   free(server_backup);
   free(root);
   free(d);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(0);

error:

   pgmoneta_json_destroy(payload);

   pgmoneta_workflow_destroy(workflow);

   pgmoneta_deque_destroy(nodes);

   free(date);
   free(backup);
   free(elapsed);
   free(server_backup);
   free(root);
   free(d);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(1);
}

void
pgmoneta_list_backup(int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* d = NULL;
   char* wal_dir = NULL;
   char* elapsed = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   int32_t number_of_backups = 0;
   struct backup** backups = NULL;
   uint64_t wal = 0;
   uint64_t delta = 0;
   struct json* response = NULL;
   struct deque* jl = NULL;
   struct json* j = NULL;
   struct json* bcks = NULL;
   struct deque_iterator* diter = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);

   if (pgmoneta_deque_create(false, &jl))
   {
      pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_LIST_BACKUP_DEQUE_CREATE, compression, encryption, payload);
      pgmoneta_log_error("List backup: Error creating the deque for %s", config->servers[server].name);

      goto error;
   }

   d = pgmoneta_get_server_backup(server);
   wal_dir = pgmoneta_get_server_wal(server);

   if (pgmoneta_get_backups(d, &number_of_backups, &backups))
   {
      pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_LIST_BACKUP_BACKUPS, compression, encryption, payload);
      pgmoneta_log_error("List backup: Unable to get backups for %s", config->servers[server].name);

      goto error;
   }

   for (int i = 0; i < number_of_backups; i++)
   {
      if (backups[i] != NULL)
      {
         if (pgmoneta_json_create(&j))
         {
            goto json_error;
         }

         if (pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->servers[server].name, ValueString))
         {
            goto json_error;
         }

         if (pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)backups[i]->label, ValueString))
         {
            goto json_error;
         }

         if (pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_KEEP, (uintptr_t)backups[i]->keep, ValueBool))
         {
            goto json_error;
         }

         if (pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_VALID, (uintptr_t)backups[i]->valid, ValueInt8))
         {
            goto json_error;
         }

         if (pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_BACKUP_SIZE, (uintptr_t)backups[i]->backup_size, ValueUInt64))
         {
            goto json_error;
         }

         if (pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_RESTORE_SIZE, (uintptr_t)backups[i]->restore_size, ValueUInt64))
         {
            goto json_error;
         }

         if (pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_COMPRESSION, (uintptr_t)backups[i]->compression, ValueInt32))
         {
            goto json_error;
         }

         if (pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_ENCRYPTION, (uintptr_t)backups[i]->encryption, ValueInt32))
         {
            goto json_error;
         }

         if (pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_COMMENTS, (uintptr_t)backups[i]->comments, ValueString))
         {
            goto json_error;
         }

         wal = pgmoneta_number_of_wal_files(wal_dir, &backups[i]->wal[0], NULL);
         wal *= config->servers[server].wal_size;

         if (pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_WAL, (uintptr_t)wal, ValueUInt64))
         {
            goto json_error;
         }

         delta = 0;

         if (i > 0)
         {
            delta = pgmoneta_number_of_wal_files(wal_dir, &backups[i - 1]->wal[0], &backups[i]->wal[0]);
            delta *= config->servers[server].wal_size;
         }

         if (pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_WAL, (uintptr_t)delta, ValueUInt64))
         {
            goto json_error;
         }

         if (pgmoneta_deque_add(jl, NULL, (uintptr_t)j, ValueJSON))
         {
            goto json_error;
         }

         j = NULL;
      }
   }

   if (pgmoneta_management_create_response(payload, server, &response))
   {
      pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_ALLOCATION, compression, encryption, payload);

      goto error;
   }

   if (pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_NUMBER_OF_BACKUPS, (uintptr_t)number_of_backups, ValueUInt32))
   {
      goto json_error;
   }

   if (pgmoneta_json_create(&bcks))
   {
      goto error;
   }

   if (pgmoneta_deque_iterator_create(jl, &diter))
   {
      goto error;
   }

   while (pgmoneta_deque_iterator_next(diter))
   {
      pgmoneta_json_append(bcks, (uintptr_t)pgmoneta_value_data(diter->value), ValueJSON);
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->servers[server].name, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUPS, (uintptr_t)bcks, ValueJSON);

   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_LIST_BACKUP_NETWORK, compression, encryption, payload);
      pgmoneta_log_error("List backup: Error sending response for %s", config->servers[server].name);

      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);
   pgmoneta_log_info("List backup: %s (Elapsed: %s)", config->servers[server].name, elapsed);

   pgmoneta_json_destroy(payload);

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(d);
   free(wal_dir);
   free(elapsed);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(0);

json_error:

   pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_LIST_BACKUP_JSON_VALUE, compression, encryption, payload);
   pgmoneta_log_error("List backup: Error creating a JSON value for %s", config->servers[server].name);

error:

   pgmoneta_json_destroy(payload);

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(d);
   free(wal_dir);
   free(elapsed);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(1);
}

void
pgmoneta_delete_backup(int client_fd, int srv, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* identifier = NULL;
   char* elapsed = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   struct json* req = NULL;
   struct json* response = NULL;
   struct workflow* workflow = NULL;
   struct workflow* current = NULL;
   struct deque* nodes = NULL;
   struct configuration* config;

   pgmoneta_start_logging();

   config = (struct configuration*)shmem;

   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);

   if (pgmoneta_deque_create(false, &nodes))
   {
      goto error;
   }

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   identifier = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_BACKUP);

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_DELETE_BACKUP, NULL);

   current = workflow;
   while (current != NULL)
   {
      if (current->setup(srv, identifier, nodes))
      {
         pgmoneta_management_response_error(NULL, client_fd, config->servers[srv].name, MANAGEMENT_ERROR_DELETE_SETUP, compression, encryption, payload);

         goto error;
      }
      current = current->next;
   }

   current = workflow;
   while (current != NULL)
   {
      if (current->execute(srv, identifier, nodes))
      {
         pgmoneta_management_response_error(NULL, client_fd, config->servers[srv].name, MANAGEMENT_ERROR_DELETE_EXECUTE, compression, encryption, payload);

         goto error;
      }
      current = current->next;
   }

   current = workflow;
   while (current != NULL)
   {
      if (current->teardown(srv, identifier, nodes))
      {
         pgmoneta_management_response_error(NULL, client_fd, config->servers[srv].name, MANAGEMENT_ERROR_DELETE_TEARDOWN, compression, encryption, payload);

         goto error;
      }
      current = current->next;
   }

   if (pgmoneta_management_create_response(payload, srv, &response))
   {
      pgmoneta_management_response_error(NULL, client_fd, config->servers[srv].name, MANAGEMENT_ERROR_ALLOCATION, compression, encryption, payload);

      goto error;
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->servers[srv].name, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)pgmoneta_deque_get(nodes, NODE_LABEL), ValueString);

   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      pgmoneta_management_response_error(NULL, client_fd, config->servers[srv].name, MANAGEMENT_ERROR_DELETE_NETWORK, compression, encryption, payload);
      pgmoneta_log_error("Delete: Error sending response for %s", config->servers[srv].name);

      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("Delete: %s/%s (Elapsed: %s)", config->servers[srv].name,
                     (uintptr_t)pgmoneta_deque_get(nodes, NODE_LABEL), elapsed);

   pgmoneta_deque_destroy(nodes);

   pgmoneta_json_destroy(payload);

   pgmoneta_workflow_destroy(workflow);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(0);

error:

   pgmoneta_management_response_error(NULL, client_fd, config->servers[srv].name, MANAGEMENT_ERROR_DELETE_ERROR, compression, encryption, payload);
   pgmoneta_log_warn("Delete: Failed for %s/%s", config->servers[srv].name,
                     (uintptr_t)pgmoneta_deque_get(nodes, NODE_LABEL));

   pgmoneta_deque_destroy(nodes);

   pgmoneta_json_destroy(payload);

   pgmoneta_workflow_destroy(workflow);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(1);
}

int
pgmoneta_get_backup_max_rate(int server)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->servers[server].backup_max_rate != -1)
   {
      return config->servers[server].backup_max_rate;
   }

   return config->backup_max_rate;
}
