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
#include <deque.h>
#include <info.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <restore.h>
#include <string.h>
#include <utils.h>
#include <value.h>
#include <workflow.h>

/* system */
#include <stdlib.h>
#include <unistd.h>

static char* restore_last_files_names[] = {"/global/pg_control"};

int
pgmoneta_get_restore_last_files_names(char*** output)
{
   int number_of_elements = 0;
   number_of_elements = sizeof(restore_last_files_names) / sizeof(restore_last_files_names[0]);

   *output = (char**)malloc((number_of_elements + 1) * sizeof(char*));
   if (*output == NULL)
   {
      return 1;
   }

   for (int i = 0; i < number_of_elements; i++)
   {
      (*output)[i] = strdup(restore_last_files_names[i]);
      if ((*output)[i] == NULL)
      {
         return 1;
      }
   }
   (*output)[number_of_elements] = NULL;

   return 0;
}

void
pgmoneta_restore(SSL* ssl, int client_fd, int server, struct json* payload)
{
   char* backup_id = NULL;
   char* position = NULL;
   char* directory = NULL;
   char* elapsed = NULL;
   time_t start_time;
   time_t end_time;
   int total_seconds = 0;
   char* output = NULL;
   char* id = NULL;
   char* server_backup = NULL;
   struct backup* backup = NULL;
   struct json* req = NULL;
   struct json* response = NULL;
   struct configuration* config;

   pgmoneta_start_logging();

   config = (struct configuration*)shmem;

   start_time = time(NULL);

   atomic_fetch_add(&config->active_restores, 1);
   atomic_fetch_add(&config->servers[server].active_restore, 1);

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   backup_id = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_BACKUP);
   position = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_POSITION);
   directory = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_DIRECTORY);

   if (!pgmoneta_restore_backup(server, backup_id, position, directory, &output, &id))
   {
      if (pgmoneta_management_create_response(payload, server, &response))
      {
         pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_ALLOCATION, payload);

         goto error;
      }

      server_backup = pgmoneta_get_server_backup(server);

      if (pgmoneta_get_backup(server_backup, id, &backup))
      {
         pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_RESTORE_ERROR, payload);

         goto error;
      }

      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->servers[server].name, ValueString);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)id, ValueString);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP_SIZE, (uintptr_t)backup->backup_size, ValueUInt64);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_RESTORE_SIZE, (uintptr_t)backup->restore_size, ValueUInt64);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_COMMENTS, (uintptr_t)backup->comments, ValueString);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_COMPRESSION, (uintptr_t)backup->compression, ValueInt32);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_ENCRYPTION, (uintptr_t)backup->encryption, ValueInt32);

      end_time = time(NULL);

      if (pgmoneta_management_response_ok(NULL, client_fd, start_time, end_time, payload))
      {
         pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_RESTORE_NETWORK, payload);
         pgmoneta_log_error("Restore: Error sending response for %s", config->servers[server].name);

         goto error;
      }

      elapsed = pgmoneta_get_timestamp_string(start_time, end_time, &total_seconds);
      pgmoneta_log_info("Restore: %s/%s (Elapsed: %s)", config->servers[server].name, id, elapsed);
   }

   pgmoneta_json_destroy(payload);

   pgmoneta_disconnect(client_fd);

   atomic_fetch_sub(&config->servers[server].active_restore, 1);
   atomic_fetch_sub(&config->active_restores, 1);

   pgmoneta_stop_logging();

   free(backup);
   free(elapsed);
   free(server_backup);
   free(output);
   free(id);

   exit(0);

error:

   pgmoneta_json_destroy(payload);

   pgmoneta_disconnect(client_fd);

   atomic_fetch_sub(&config->servers[server].active_restore, 1);
   atomic_fetch_sub(&config->active_restores, 1);

   pgmoneta_stop_logging();

   free(backup);
   free(elapsed);
   free(server_backup);
   free(output);
   free(id);

   exit(1);
}

int
pgmoneta_restore_backup(int server, char* backup_id, char* position, char* directory, char** output, char** identifier)
{
   char* o = NULL;
   char* ident = NULL;
   struct workflow* workflow = NULL;
   struct workflow* current = NULL;
   struct deque* nodes = NULL;

   *output = NULL;
   *identifier = NULL;

   pgmoneta_deque_create(false, &nodes);

   if (pgmoneta_deque_add(nodes, "position", (uintptr_t)position, ValueString))
   {
      goto error;
   }

   if (pgmoneta_deque_add(nodes, "directory", (uintptr_t)directory, ValueString))
   {
      goto error;
   }

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_RESTORE);

   current = workflow;
   while (current != NULL)
   {
      if (current->setup(server, backup_id, nodes))
      {
         goto error;
      }
      current = current->next;
   }

   current = workflow;
   while (current != NULL)
   {
      if (current->execute(server, backup_id, nodes))
      {
         goto error;
      }
      current = current->next;
   }

   current = workflow;
   while (current != NULL)
   {
      if (current->teardown(server, backup_id, nodes))
      {
         goto error;
      }
      current = current->next;
   }

   o = (char*)pgmoneta_deque_get(nodes, "output");

   if (o == NULL)
   {
      goto error;
   }

   ident = (char*)pgmoneta_deque_get(nodes, "identifier");

   if (ident == NULL)
   {
      goto error;
   }

   *output = malloc(strlen(o) + 1);

   if (*output == NULL)
   {
      goto error;
   }

   memset(*output, 0, strlen(o) + 1);
   memcpy(*output, o, strlen(o));

   *identifier = malloc(strlen(ident) + 1);

   if (*identifier == NULL)
   {
      goto error;
   }

   memset(*identifier, 0, strlen(ident) + 1);
   memcpy(*identifier, ident, strlen(ident));

   pgmoneta_workflow_delete(workflow);

   pgmoneta_deque_destroy(nodes);

   return 0;

error:
   pgmoneta_workflow_delete(workflow);

   pgmoneta_deque_destroy(nodes);

   return 1;
}
