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
#include <json.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <string.h>
#include <utils.h>
#include <verify.h>
#include <workflow.h>

/* system */
#include <stdlib.h>
#include <unistd.h>

void
pgmoneta_verify(SSL* ssl, int client_fd, int server, struct json* payload)
{
   char* backup_id = NULL;
   char* directory = NULL;
   char* files = NULL;
   char* elapsed = NULL;
   time_t start_time;
   time_t end_time;
   int total_seconds;
   char* id = NULL;
   struct workflow* workflow = NULL;
   struct workflow* current = NULL;
   struct deque* nodes = NULL;
   struct deque* f = NULL;
   struct deque* a = NULL;
   struct deque_iterator* fiter = NULL;
   struct deque_iterator* aiter = NULL;
   struct json* failed = NULL;
   struct json* all = NULL;
   struct json* req = NULL;
   struct json* response = NULL;
   struct json* filesj = NULL;
   struct configuration* config;

   pgmoneta_start_logging();

   config = (struct configuration*)shmem;

   start_time = time(NULL);

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   backup_id = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_BACKUP);
   directory = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_DIRECTORY);
   files = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_FILES);

   if (pgmoneta_deque_create(true, &nodes))
   {
      goto error;
   }

   if (pgmoneta_deque_add(nodes, "position", (uintptr_t)"", ValueString))
   {
      goto error;
   }

   if (pgmoneta_deque_add(nodes, "directory", (uintptr_t)directory, ValueString))
   {
      goto error;
   }

   if (pgmoneta_deque_add(nodes, "files", (uintptr_t)files, ValueString))
   {
      goto error;
   }

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_VERIFY);

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

   id = (char*)pgmoneta_deque_get(nodes, "identifier");

   f = (struct deque*)pgmoneta_deque_get(nodes, "failed");
   a = (struct deque*)pgmoneta_deque_get(nodes, "all");

   if (pgmoneta_json_create(&failed))
   {
      goto error;
   }

   if (pgmoneta_deque_iterator_create(f, &fiter))
   {
      goto error;
   }

   while (pgmoneta_deque_iterator_next(fiter))
   {
      struct json* j = NULL;

      if (pgmoneta_json_clone((struct json*)pgmoneta_value_data(fiter->value), &j))
      {
         goto error;
      }

      pgmoneta_json_append(failed, (uintptr_t)j, ValueJSON);
   }

   if (!strcasecmp(files, "all"))
   {
      pgmoneta_json_create(&all);

      if (pgmoneta_deque_iterator_create(a, &aiter))
      {
         goto error;
      }

      while (pgmoneta_deque_iterator_next(aiter))
      {
         struct json* j = NULL;

         if (pgmoneta_json_clone((struct json*)pgmoneta_value_data(aiter->value), &j))
         {
            goto error;
         }

         pgmoneta_json_append(all, (uintptr_t)j, ValueJSON);
      }
   }

   if (pgmoneta_management_create_response(payload, server, &response))
   {
      pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_ALLOCATION, payload);

      goto error;
   }

   if (pgmoneta_json_create(&filesj))
   {
      pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_ALLOCATION, payload);

      goto error;
   }

   pgmoneta_json_put(filesj, MANAGEMENT_ARGUMENT_FAILED, (uintptr_t)failed, ValueJSON);
   pgmoneta_json_put(filesj, MANAGEMENT_ARGUMENT_ALL, (uintptr_t)all, ValueJSON);

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)id, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->servers[server].name, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_FILES, (uintptr_t)filesj, ValueJSON);

   end_time = time(NULL);

   if (pgmoneta_management_response_ok(NULL, client_fd, start_time, end_time, payload))
   {
      pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_VERIFY_NETWORK, payload);
      pgmoneta_log_error("Verify: Error sending response for %s/%s", config->servers[server].name, backup_id);

      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_time, end_time, &total_seconds);

   pgmoneta_log_info("Verify: %s/%s (Elapsed: %s)", config->servers[server].name, id, elapsed);

   pgmoneta_deque_list(nodes);

   pgmoneta_deque_iterator_destroy(fiter);
   pgmoneta_deque_iterator_destroy(aiter);

   pgmoneta_deque_destroy(nodes);

   pgmoneta_json_destroy(payload);

   pgmoneta_workflow_delete(workflow);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   free(elapsed);

   exit(0);

error:

   pgmoneta_deque_iterator_destroy(fiter);
   pgmoneta_deque_iterator_destroy(aiter);

   pgmoneta_deque_destroy(nodes);

   pgmoneta_json_destroy(payload);

   pgmoneta_workflow_delete(workflow);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   free(elapsed);

   exit(1);
}
