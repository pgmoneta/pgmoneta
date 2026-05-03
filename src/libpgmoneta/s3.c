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
#include <progress.h>
#include <s3.h>
#include <compression.h>
#include <info.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <restore.h>
#include <security.h>
#include <utils.h>
#include <wal.h>
#include <workflow.h>
#include <workflow_funcs.h>

#define NAME "s3"

static bool
s3_is_safe_prefix(char* prefix)
{
   if (pgmoneta_contains(prefix, "/") || pgmoneta_contains(prefix, ".") || pgmoneta_contains(prefix, ".."))
   {
      return false;
   }
   if (prefix[0] == '/')
   {
      return false;
   }

   if (strstr(prefix, "..") != NULL)
   {
      return false;
   }

   return true;
}

void
pgmoneta_list_s3_objects(int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* elapsed = NULL;
   char* en = NULL;
   char* prefix = NULL;
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
   struct json* request = NULL;

   config = (struct main_configuration*)shmem;
   request = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   prefix = (char*)pgmoneta_json_get(request, MANAGEMENT_ARGUMENT_S3_PREFIX);

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

   if (pgmoneta_art_insert(nodes, NODE_LABEL, (uintptr_t)(prefix != NULL ? prefix : ""), ValueString))
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

   pgmoneta_progress_setup(server, workflow, nodes, WORKFLOW_TYPE_S3_LIST);

   if (pgmoneta_workflow_execute(workflow, nodes, &en, &ec))
   {
      pgmoneta_log_error("List S3: Workflow failed for %s", config->common.servers[server].name);
      goto error;
   }
   if (pgmoneta_is_progress_enabled(server))
   {
      pgmoneta_progress_teardown(server);
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

   if (pgmoneta_is_progress_enabled(server))
   {
      pgmoneta_progress_teardown(server);
   }
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

void
pgmoneta_delete_s3_objects(int client_fd, int server, char* prefix, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* elapsed = NULL;
   char* en = NULL;
   int ec = -1;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   struct json* response = NULL;
   struct art* nodes = NULL;
   struct workflow* workflow = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   if (!s3_is_safe_prefix(prefix))
   {
      ec = MANAGEMENT_ERROR_DELETE_S3_INVALID_PREFIX;
      pgmoneta_log_error("S3 delete: invalid prefix for %s", config->common.servers[server].name);
      goto error;
   }

   if (pgmoneta_art_create(&nodes))
   {
      ec = MANAGEMENT_ERROR_DELETE_S3_WORKFLOW;
      goto error;
   }

   if (pgmoneta_art_insert(nodes, NODE_SERVER_ID, (uintptr_t)server, ValueInt32))
   {
      ec = MANAGEMENT_ERROR_DELETE_S3_WORKFLOW;
      goto error;
   }

   if (pgmoneta_art_insert(nodes, NODE_LABEL, (uintptr_t)prefix, ValueString))
   {
      ec = MANAGEMENT_ERROR_DELETE_S3_WORKFLOW;
      goto error;
   }

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_S3_DELETE, NULL);

   if (workflow == NULL)
   {
      ec = MANAGEMENT_ERROR_DELETE_S3_WORKFLOW;
      pgmoneta_log_error("S3 delete: S3 storage engine is not configured for %s", config->common.servers[server].name);
      goto error;
   }

   pgmoneta_progress_setup(server, workflow, nodes, WORKFLOW_TYPE_S3_DELETE);

   if (pgmoneta_workflow_execute(workflow, nodes, &en, &ec))
   {
      pgmoneta_log_error("S3 delete: workflow failed for %s", config->common.servers[server].name);
      goto error;
   }
   if (pgmoneta_is_progress_enabled(server))
   {
      pgmoneta_progress_teardown(server);
   }

   if (pgmoneta_management_create_response(payload, server, &response))
   {
      ec = MANAGEMENT_ERROR_ALLOCATION;
      goto error;
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->common.servers[server].name, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_S3_PREFIX, (uintptr_t)prefix, ValueString);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      ec = MANAGEMENT_ERROR_DELETE_S3_NETWORK;
      pgmoneta_log_error("S3 delete: error sending response for %s", config->common.servers[server].name);
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);
   pgmoneta_log_info("S3 delete: %s/%s (Elapsed: %s)", config->common.servers[server].name, prefix, elapsed);

   pgmoneta_json_destroy(payload);
   pgmoneta_art_destroy(nodes);
   pgmoneta_workflow_destroy(workflow);
   free(elapsed);

   pgmoneta_disconnect(client_fd);
   pgmoneta_stop_logging();
   exit(0);

error:

   if (pgmoneta_is_progress_enabled(server))
   {
      pgmoneta_progress_teardown(server);
   }
   pgmoneta_management_response_error(NULL, client_fd, config->common.servers[server].name,
                                      ec != -1 ? ec : MANAGEMENT_ERROR_DELETE_S3_ERROR, en != NULL ? en : NAME,
                                      compression, encryption, payload);

   pgmoneta_json_destroy(payload);
   pgmoneta_art_destroy(nodes);
   pgmoneta_workflow_destroy(workflow);
   free(elapsed);

   pgmoneta_disconnect(client_fd);
   pgmoneta_stop_logging();
   exit(1);
}
void
pgmoneta_restore_s3_objects(int client_fd, int server, char* prefix, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* elapsed = NULL;
   char* en = NULL;
   int ec = -1;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   char* directory = NULL;
   char* position = NULL;
   struct art* nodes = NULL;
   struct workflow* workflow = NULL;
   struct main_configuration* config;
   struct backup* backup = NULL;
   char* local_root = NULL;
   struct json* req = NULL;

   config = (struct main_configuration*)shmem;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   position = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_POSITION);
   directory = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_DIRECTORY);

   if (!s3_is_safe_prefix(prefix))
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_ERROR;
      pgmoneta_log_error("S3 restore: invalid prefix for %s", config->common.servers[server].name);
      goto error;
   }

   if (directory == NULL || strlen(directory) == 0)
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_ERROR;
      pgmoneta_log_error("S3 restore: missing target directory for %s/%s", config->common.servers[server].name, prefix);
      goto error;
   }

   if (pgmoneta_art_create(&nodes))
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_WORKFLOW;
      goto error;
   }

   if (pgmoneta_art_insert(nodes, NODE_SERVER_ID, (uintptr_t)server, ValueInt32))
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_WORKFLOW;
      goto error;
   }

   if (pgmoneta_art_insert(nodes, NODE_LABEL, (uintptr_t)prefix, ValueString))
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_WORKFLOW;
      goto error;
   }

   if (pgmoneta_art_insert(nodes, USER_IDENTIFIER, (uintptr_t)prefix, ValueString))
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_WORKFLOW;
      goto error;
   }

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_S3_RESTORE, NULL);

   if (workflow == NULL)
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_WORKFLOW;
      pgmoneta_log_error("S3 restore: S3 storage engine is not configured for %s", config->common.servers[server].name);
      goto error;
   }

   /* TODO: S3 restore progress only covers the staging workflow here.
    * The subsequent local restore runs via pgmoneta_restore_backup() and
    * needs its own progress lifecycle until both stages are unified. */

   if (pgmoneta_workflow_execute(workflow, nodes, &en, &ec))
   {
      pgmoneta_log_error("S3 restore: workflow failed for %s", config->common.servers[server].name);
      goto error;
   }
   pgmoneta_workflow_destroy(workflow);
   workflow = NULL;

   local_root = pgmoneta_get_server_backup_identifier(server, prefix);

   if (pgmoneta_workflow_nodes(server, prefix, nodes, &backup))
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_WORKFLOW;
      pgmoneta_log_error("S3 restore: could not load workflow nodes for %s/%s", config->common.servers[server].name, prefix);
      goto error;
   }

   if (pgmoneta_art_insert(nodes, USER_POSITION, (uintptr_t)position, ValueString))
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_WORKFLOW;
      pgmoneta_log_error("S3 restore: could not add restore position to art");
      goto error;
   }

   if (pgmoneta_art_insert(nodes, USER_DIRECTORY, (uintptr_t)directory, ValueString))
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_WORKFLOW;
      pgmoneta_log_error("S3 restore: could not add target directory to art");
      goto error;
   }

   if (pgmoneta_restore_backup(nodes) != 0)
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_WORKFLOW;
      pgmoneta_log_error("S3 restore: restore workflow failed for %s", config->common.servers[server].name);
      goto error;
   }

   pgmoneta_delete_directory(local_root);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_NETWORK;
      pgmoneta_log_error("S3 restore: error sending response for %s", config->common.servers[server].name);
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);
   pgmoneta_log_info("S3 restore: %s/%s (Elapsed: %s)", config->common.servers[server].name, prefix, elapsed);

   pgmoneta_json_destroy(payload);
   pgmoneta_art_destroy(nodes);
   pgmoneta_workflow_destroy(workflow);
   free(elapsed);

   pgmoneta_disconnect(client_fd);
   pgmoneta_stop_logging();
   free(local_root);
   exit(0);

error:

   pgmoneta_management_response_error(NULL, client_fd, config->common.servers[server].name,
                                      ec != -1 ? ec : MANAGEMENT_ERROR_RESTORE_S3_ERROR, en != NULL ? en : NAME,
                                      compression, encryption, payload);

   pgmoneta_json_destroy(payload);
   pgmoneta_art_destroy(nodes);
   pgmoneta_workflow_destroy(workflow);
   free(elapsed);
   if (local_root != NULL)
   {
      pgmoneta_delete_directory(local_root);
   }
   pgmoneta_disconnect(client_fd);
   pgmoneta_stop_logging();
   free(local_root);
   exit(1);
}
