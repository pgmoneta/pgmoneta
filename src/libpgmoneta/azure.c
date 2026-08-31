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
#include <art.h>
#include <azure.h>
#include <info.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <progress.h>
#include <restore.h>
#include <storage.h>
#include <utils.h>
#include <workflow.h>
#include <workflow_funcs.h>

/* system */
#include <stdlib.h>
#include <string.h>

#define NAME "azure"

static bool
azure_is_safe_label(char* label)
{
   if (label == NULL || strlen(label) == 0)
   {
      return false;
   }

   if (pgmoneta_contains(label, "/") || pgmoneta_contains(label, "."))
   {
      return false;
   }

   return true;
}

void
pgmoneta_restore_azure_objects(int client_fd, int server, char* label, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* elapsed = NULL;
   char* en = NULL;
   int ec = -1;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   char* directory = NULL;
   char* position = NULL;
   char* local_data = NULL;
   struct art* nodes = NULL;
   struct backup* backup = NULL;
   struct json* req = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   position = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_POSITION);
   directory = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_DIRECTORY);

   if (!azure_is_safe_label(label))
   {
      ec = MANAGEMENT_ERROR_RESTORE_AZURE_ERROR;
      pgmoneta_log_error("Azure restore: invalid label for %s", config->common.servers[server].name);
      goto error;
   }

   if (directory == NULL || strlen(directory) == 0)
   {
      ec = MANAGEMENT_ERROR_RESTORE_AZURE_ERROR;
      pgmoneta_log_error("Azure restore: missing target directory for %s/%s",
                         config->common.servers[server].name, label);
      goto error;
   }

   if (pgmoneta_art_create(&nodes))
   {
      ec = MANAGEMENT_ERROR_RESTORE_AZURE_ERROR;
      goto error;
   }

   if (pgmoneta_art_insert(nodes, NODE_SERVER_ID, (uintptr_t)server, ValueInt32))
   {
      ec = MANAGEMENT_ERROR_RESTORE_AZURE_ERROR;
      goto error;
   }

   if (pgmoneta_art_insert(nodes, NODE_LABEL, (uintptr_t)label, ValueString))
   {
      ec = MANAGEMENT_ERROR_RESTORE_AZURE_ERROR;
      goto error;
   }

   if (pgmoneta_art_insert(nodes, USER_IDENTIFIER, (uintptr_t)label, ValueString))
   {
      ec = MANAGEMENT_ERROR_RESTORE_AZURE_ERROR;
      goto error;
   }

   if (pgmoneta_storage_remote_download(server, label))
   {
      ec = MANAGEMENT_ERROR_RESTORE_AZURE_DOWNLOAD;
      pgmoneta_log_error("Azure restore: failed to stage %s/%s",
                         config->common.servers[server].name, label);
      goto error;
   }

   if (pgmoneta_workflow_nodes(server, label, nodes, &backup))
   {
      ec = MANAGEMENT_ERROR_RESTORE_AZURE_ERROR;
      pgmoneta_log_error("Azure restore: could not load workflow nodes for %s/%s",
                         config->common.servers[server].name, label);
      goto error;
   }

   if (pgmoneta_art_insert(nodes, USER_POSITION, (uintptr_t)position, ValueString))
   {
      ec = MANAGEMENT_ERROR_RESTORE_AZURE_ERROR;
      pgmoneta_log_error("Azure restore: could not add restore position to art");
      goto error;
   }

   if (pgmoneta_art_insert(nodes, USER_DIRECTORY, (uintptr_t)directory, ValueString))
   {
      ec = MANAGEMENT_ERROR_RESTORE_AZURE_ERROR;
      pgmoneta_log_error("Azure restore: could not add target directory to art");
      goto error;
   }

   if (pgmoneta_restore_backup(nodes) != 0)
   {
      ec = MANAGEMENT_ERROR_RESTORE_AZURE_ERROR;
      pgmoneta_log_error("Azure restore: restore workflow failed for %s",
                         config->common.servers[server].name);
      goto error;
   }

   local_data = pgmoneta_get_server_backup_identifier_data(server, label);
   pgmoneta_delete_directory(local_data);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      ec = MANAGEMENT_ERROR_RESTORE_AZURE_NETWORK;
      pgmoneta_log_error("Azure restore: error sending response for %s",
                         config->common.servers[server].name);
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);
   pgmoneta_log_info("Azure restore: %s/%s (Elapsed: %s)",
                     config->common.servers[server].name, label, elapsed);

   pgmoneta_json_destroy(payload);
   pgmoneta_art_destroy(nodes);
   free(elapsed);
   free(local_data);

   pgmoneta_disconnect(client_fd);
   pgmoneta_stop_logging();
   exit(0);

error:

   pgmoneta_management_response_error_with_nodes(NULL, client_fd, config->common.servers[server].name,
                                                 ec != -1 ? ec : MANAGEMENT_ERROR_RESTORE_AZURE_ERROR,
                                                 en != NULL ? en : NAME,
                                                 compression, encryption, payload, nodes);

   pgmoneta_json_destroy(payload);
   pgmoneta_art_destroy(nodes);
   free(elapsed);

   if (local_data != NULL)
   {
      pgmoneta_delete_directory(local_data);
   }

   pgmoneta_disconnect(client_fd);
   pgmoneta_stop_logging();
   free(local_data);
   exit(1);
}
