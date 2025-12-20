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
 *
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <brt.h>
#include <configuration.h>
#include <json.h>
#include <management.h>
#include <network.h>
#include <tsclient.h>
#include <tscommon.h>
#include <utils.h>
#include <value.h>

/* system */
#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "logging.h"

static int check_output_outcome(int socket);
static int get_connection();

int
pgmoneta_tsclient_backup(char* server, char* incremental)
{
   int socket = -1;

   socket = get_connection();
   // Security Checks
   if (!pgmoneta_socket_isvalid(socket) || server == NULL)
   {
      goto error;
   }
   // Create a backup request to the main server
   if (pgmoneta_management_request_backup(NULL, socket, server, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, incremental, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   // Check the outcome field of the output, if true success, else failure
   if (check_output_outcome(socket))
   {
      goto error;
   }

   // Wait for 1 second to avoid invoking backup too frequently
   sleep(1);

   pgmoneta_disconnect(socket);
   return 0;
error:
   pgmoneta_disconnect(socket);
   return 1;
}

int
pgmoneta_tsclient_restore(char* server, char* backup_id, char* position)
{
   char* restore_path = NULL;
   int socket = -1;

   socket = get_connection();
   // Security Checks
   if (!pgmoneta_socket_isvalid(socket) || server == NULL)
   {
      goto error;
   }

   // Fallbacks
   if (backup_id == NULL)
   {
      backup_id = "newest";
   }

   // Create a restore request to the main server
   if (pgmoneta_management_request_restore(NULL, socket, server, backup_id, position, TEST_RESTORE_DIR, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   // Check the outcome field of the output, if true success, else failure
   if (check_output_outcome(socket))
   {
      goto error;
   }

   free(restore_path);
   pgmoneta_disconnect(socket);
   return 0;
error:
   free(restore_path);
   pgmoneta_disconnect(socket);
   return 1;
}

int
pgmoneta_tsclient_delete(char* server, char* backup_id)
{
   int socket = -1;

   socket = get_connection();
   // Security Checks
   if (!pgmoneta_socket_isvalid(socket) || server == NULL)
   {
      return 1;
   }

   // Fallbacks
   if (!backup_id)
   {
      backup_id = "oldest";
   }

   // Create a delete request to the main server
   if (pgmoneta_management_request_delete(NULL, socket, server, backup_id, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   // Check the outcome field of the output, if true success, else failure
   if (check_output_outcome(socket))
   {
      goto error;
   }

   pgmoneta_disconnect(socket);
   return 0;
error:
   pgmoneta_disconnect(socket);
   return 1;
}

int
pgmoneta_tsclient_reload()
{
   int socket = -1;

   socket = get_connection();
   // Security Checks
   if (!pgmoneta_socket_isvalid(socket))
   {
      return 1;
   }

   // Create a reload request to the main server
   if (pgmoneta_management_request_reload(NULL, socket, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   // Check the outcome field of the output, if true success, else failure
   if (check_output_outcome(socket))
   {
      goto error;
   }

   pgmoneta_disconnect(socket);
   return 0;
error:
   pgmoneta_disconnect(socket);
   return 1;
}

static int
check_output_outcome(int socket)
{
   struct json* read = NULL;
   struct json* outcome = NULL;

   if (pgmoneta_management_read_json(NULL, socket, NULL, NULL, &read))
   {
      goto error;
   }

   char* string = pgmoneta_json_to_string(read, FORMAT_JSON, NULL, 0);
   pgmoneta_log_info("outcome string %s", string);
   free(string);

   if (!pgmoneta_json_contains_key(read, MANAGEMENT_CATEGORY_OUTCOME))
   {
      goto error;
   }

   outcome = (struct json*)pgmoneta_json_get(read, MANAGEMENT_CATEGORY_OUTCOME);
   if (!pgmoneta_json_contains_key(outcome, MANAGEMENT_ARGUMENT_STATUS) || !(bool)pgmoneta_json_get(outcome, MANAGEMENT_ARGUMENT_STATUS))
   {
      goto error;
   }

   pgmoneta_json_destroy(read);
   return 0;
error:
   pgmoneta_json_destroy(read);
   return 1;
}

static int
get_connection()
{
   int socket = -1;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;
   if (strlen(config->common.configuration_path))
   {
      if (pgmoneta_connect_unix_socket(config->unix_socket_dir, MAIN_UDS, &socket))
      {
         return -1;
      }
   }
   pgmoneta_log_info("%s %s %d", config->common.configuration_path, config->unix_socket_dir, socket);
   return socket;
}
