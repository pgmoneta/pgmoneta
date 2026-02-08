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
#include <aes.h>
#include <art.h>
#include <s3.h>
#include <compression.h>
#include <info.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <security.h>
#include <utils.h>
#include <wal.h>
#include <workflow.h>

#define NAME "s3"
void
pgmoneta_list_s3_objects(int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* elapsed = NULL;
   char* en = NULL;
   int ec = -1;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   struct json* response = NULL;
   struct json* objects_json = NULL;
   struct deque* objects = NULL;
   struct deque_iterator* diter = NULL;
   struct art* nodes = NULL;
   struct workflow* workflow = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   if (pgmoneta_art_create(&nodes))
   {
      ec = MANAGEMENT_ERROR_LIST_S3_WORKFLOW;
      goto error;
   }

   if (pgmoneta_art_insert(nodes, NODE_SERVER_ID, (uintptr_t)server, ValueInt32))
   {
      ec = MANAGEMENT_ERROR_LIST_S3_WORKFLOW;
      goto error;
   }

   if (pgmoneta_art_insert(nodes, NODE_LABEL, (uintptr_t)"", ValueString))
   {
      ec = MANAGEMENT_ERROR_LIST_S3_WORKFLOW;
      goto error;
   }

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_S3_LIST, NULL);

   if (workflow == NULL)
   {
      ec = MANAGEMENT_ERROR_LIST_S3_WORKFLOW;
      pgmoneta_log_error("List S3: S3 storage engine is not configured for %s", config->common.servers[server].name);
      goto error;
   }

   if (pgmoneta_workflow_execute(workflow, nodes, &en, &ec))
   {
      pgmoneta_log_error("List S3: Workflow failed for %s", config->common.servers[server].name);
      goto error;
   }

   objects = (struct deque*)pgmoneta_art_search(nodes, NODE_S3_OBJECTS);

   if (pgmoneta_management_create_response(payload, server, &response))
   {
      ec = MANAGEMENT_ERROR_ALLOCATION;
      goto error;
   }

   if (pgmoneta_json_create(&objects_json))
   {
      ec = MANAGEMENT_ERROR_LIST_S3_JSON_VALUE;
      goto error;
   }

   if (objects != NULL)
   {
      if (pgmoneta_deque_iterator_create(objects, &diter))
      {
         ec = MANAGEMENT_ERROR_LIST_S3_JSON_VALUE;
         goto error;
      }

      while (pgmoneta_deque_iterator_next(diter))
      {
         struct json* obj = NULL;

         if (pgmoneta_json_create(&obj))
         {
            ec = MANAGEMENT_ERROR_LIST_S3_JSON_VALUE;
            goto error;
         }

         pgmoneta_json_put(obj, MANAGEMENT_ARGUMENT_S3_KEY, (uintptr_t)pgmoneta_value_data(diter->value), ValueString);
         pgmoneta_json_append(objects_json, (uintptr_t)obj, ValueJSON);
      }
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->common.servers[server].name, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_S3_OBJECTS, (uintptr_t)objects_json, ValueJSON);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      ec = MANAGEMENT_ERROR_LIST_S3_NETWORK;
      pgmoneta_log_error("List S3: Error sending response for %s", config->common.servers[server].name);
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);
   pgmoneta_log_info("List S3: %s (Elapsed: %s)", config->common.servers[server].name, elapsed);

   pgmoneta_json_destroy(payload);
   pgmoneta_art_destroy(nodes);
   pgmoneta_workflow_destroy(workflow);
   free(elapsed);

   pgmoneta_disconnect(client_fd);
   pgmoneta_stop_logging();
   exit(0);

error:

   pgmoneta_management_response_error(NULL, client_fd, config->common.servers[server].name,
                                      ec != -1 ? ec : MANAGEMENT_ERROR_LIST_S3_ERROR, en != NULL ? en : NAME,
                                      compression, encryption, payload);

   pgmoneta_json_destroy(payload);
   pgmoneta_art_destroy(nodes);
   pgmoneta_workflow_destroy(workflow);
   free(elapsed);

   pgmoneta_disconnect(client_fd);
   pgmoneta_stop_logging();
   exit(1);
}
