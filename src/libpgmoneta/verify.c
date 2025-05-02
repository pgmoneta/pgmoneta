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
#include <pgmoneta.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <stdlib.h>
#include <unistd.h>

#define NAME "verify"

void
pgmoneta_verify(SSL* ssl, int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* identifier = NULL;
   char* directory = NULL;
   char* files = NULL;
   char* elapsed = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   char* label = NULL;
   struct backup* backup = NULL;
   struct workflow* workflow = NULL;
   struct workflow* current = NULL;
   struct art* nodes = NULL;
   struct deque* f = NULL;
   struct deque* a = NULL;
   struct deque_iterator* fiter = NULL;
   struct deque_iterator* aiter = NULL;
   struct json* failed = NULL;
   struct json* all = NULL;
   struct json* req = NULL;
   struct json* response = NULL;
   struct json* filesj = NULL;
   struct main_configuration* config;

   pgmoneta_start_logging();

   config = (struct main_configuration*)shmem;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   identifier = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_BACKUP);
   directory = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_DIRECTORY);
   files = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_FILES);

   if (pgmoneta_art_create(&nodes))
   {
      goto error;
   }

   if (pgmoneta_art_insert(nodes, USER_POSITION, (uintptr_t)"", ValueString))
   {
      goto error;
   }

   if (pgmoneta_art_insert(nodes, USER_DIRECTORY, (uintptr_t)directory, ValueString))
   {
      goto error;
   }

   if (pgmoneta_art_insert(nodes, USER_FILES, (uintptr_t)files, ValueString))
   {
      goto error;
   }

   if (pgmoneta_workflow_nodes(server, identifier, nodes, &backup))
   {
      goto error;
   }

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_VERIFY, server, backup);

   current = workflow;
   while (current != NULL)
   {
      if (current->setup(current->name(), nodes))
      {
         goto error;
      }
      current = current->next;
   }

   current = workflow;
   while (current != NULL)
   {
      if (current->execute(current->name(), nodes))
      {
         goto error;
      }
      current = current->next;
   }

   current = workflow;
   while (current != NULL)
   {
      if (current->teardown(current->name(), nodes))
      {
         goto error;
      }
      current = current->next;
   }

   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   f = (struct deque*)pgmoneta_art_search(nodes, NODE_FAILED);
   a = (struct deque*)pgmoneta_art_search(nodes, NODE_ALL);

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
      pgmoneta_management_response_error(ssl, client_fd, config->common.servers[server].name, MANAGEMENT_ERROR_ALLOCATION, NAME, compression, encryption, payload);

      goto error;
   }

   if (pgmoneta_json_create(&filesj))
   {
      pgmoneta_management_response_error(ssl, client_fd, config->common.servers[server].name, MANAGEMENT_ERROR_ALLOCATION, NAME, compression, encryption, payload);

      goto error;
   }

   pgmoneta_json_put(filesj, MANAGEMENT_ARGUMENT_FAILED, (uintptr_t)failed, ValueJSON);
   pgmoneta_json_put(filesj, MANAGEMENT_ARGUMENT_ALL, (uintptr_t)all, ValueJSON);

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)label, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->common.servers[server].name, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_FILES, (uintptr_t)filesj, ValueJSON);

   pgmoneta_delete_directory((char*)pgmoneta_art_search(nodes, NODE_TARGET_BASE));

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(ssl, client_fd, start_t, end_t, compression, encryption, payload))
   {
      pgmoneta_management_response_error(ssl, client_fd, config->common.servers[server].name, MANAGEMENT_ERROR_VERIFY_NETWORK, NAME, compression, encryption, payload);
      pgmoneta_log_error("Verify: Error sending response for %s/%s", config->common.servers[server].name, identifier);

      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("Verify: %s/%s (Elapsed: %s)", config->common.servers[server].name, label, elapsed);

   pgmoneta_deque_iterator_destroy(fiter);
   pgmoneta_deque_iterator_destroy(aiter);

   pgmoneta_art_destroy(nodes);

   pgmoneta_json_destroy(payload);

   pgmoneta_workflow_destroy(workflow);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   free(elapsed);

   exit(0);

error:

   pgmoneta_delete_directory((char*)pgmoneta_art_search(nodes, NODE_TARGET_BASE));

   pgmoneta_deque_iterator_destroy(fiter);
   pgmoneta_deque_iterator_destroy(aiter);

   pgmoneta_art_destroy(nodes);

   pgmoneta_json_destroy(payload);

   pgmoneta_workflow_destroy(workflow);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   free(elapsed);

   exit(1);
}
