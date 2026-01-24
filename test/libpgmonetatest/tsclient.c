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
 *
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <brt.h>
#include <configuration.h>
#include <json.h>
#include <logging.h>
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


static int check_output_outcome(int socket, int expected_error, struct json** output);
static int get_connection();

int
pgmoneta_tsclient_backup(char* server, char* incremental, int expected_error)
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

   // Check the outcome field of the output
   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_list_backup(char* server, char* sort_order, struct json** response, int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket) || server == NULL)
   {
      goto error;
   }

   if (pgmoneta_management_request_list_backup(NULL, socket, server, sort_order, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, response))
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
pgmoneta_tsclient_restore(char* server, char* backup_id, char* position, int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket) || server == NULL)
   {
      goto error;
   }

   if (backup_id == NULL)
   {
      backup_id = "newest";
   }

   if (pgmoneta_management_request_restore(NULL, socket, server, backup_id, position, TEST_RESTORE_DIR, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_verify(char* server, char* backup_id, char* directory, char* files, int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket) || server == NULL)
   {
      goto error;
   }

   if (pgmoneta_management_request_verify(NULL, socket, server, backup_id, directory, files, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_archive(char* server, char* backup_id, char* position, char* directory, int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket) || server == NULL)
   {
      goto error;
   }

   if (pgmoneta_management_request_archive(NULL, socket, server, backup_id, position, directory, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_delete(char* server, char* backup_id, int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket) || server == NULL)
   {
      goto error;
   }

   if (!backup_id)
   {
      backup_id = "oldest";
   }

   if (pgmoneta_management_request_delete(NULL, socket, server, backup_id, false, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_force_delete(char* server, char* backup_id, int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket) || server == NULL)
   {
      goto error;
   }

   if (!backup_id)
   {
      backup_id = "oldest";
   }

   if (pgmoneta_management_request_delete(NULL, socket, server, backup_id, true, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_retain(char* server, char* backup_id, bool cascade, int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket) || server == NULL)
   {
      goto error;
   }

   if (pgmoneta_management_request_retain(NULL, socket, server, backup_id, cascade, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_expunge(char* server, char* backup_id, bool cascade, int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket) || server == NULL)
   {
      goto error;
   }

   if (pgmoneta_management_request_expunge(NULL, socket, server, backup_id, cascade, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_decrypt(char* path, int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket))
   {
      goto error;
   }

   if (pgmoneta_management_request_decrypt(NULL, socket, path, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_encrypt(char* path, int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket))
   {
      goto error;
   }

   if (pgmoneta_management_request_encrypt(NULL, socket, path, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_decompress(char* path, int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket))
   {
      goto error;
   }

   if (pgmoneta_management_request_decompress(NULL, socket, path, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_compress(char* path, int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket))
   {
      goto error;
   }

   if (pgmoneta_management_request_compress(NULL, socket, path, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_ping(int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket))
   {
      goto error;
   }

   if (pgmoneta_management_request_ping(NULL, socket, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_shutdown(int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket))
   {
      goto error;
   }

   if (pgmoneta_management_request_shutdown(NULL, socket, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_status(int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket))
   {
      goto error;
   }

   if (pgmoneta_management_request_status(NULL, socket, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_status_details(int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket))
   {
      goto error;
   }

   if (pgmoneta_management_request_status_details(NULL, socket, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_reload(int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket))
   {
      goto error;
   }

   if (pgmoneta_management_request_reload(NULL, socket, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_conf_ls(int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket))
   {
      goto error;
   }

   if (pgmoneta_management_request_conf_ls(NULL, socket, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_conf_get(char* config_key, int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket))
   {
      goto error;
   }

   if (pgmoneta_management_request_conf_get(NULL, socket, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_conf_set(char* config_key, char* config_value, int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket))
   {
      goto error;
   }

   if (pgmoneta_management_request_conf_set(NULL, socket, config_key, config_value, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_info(char* server, char* backup_id, int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket) || server == NULL)
   {
      goto error;
   }

   if (pgmoneta_management_request_info(NULL, socket, server, backup_id, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_annotate(char* server, char* backup_id, char* action, char* key, char* comment, int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket) || server == NULL)
   {
      goto error;
   }

   if (pgmoneta_management_request_annotate(NULL, socket, server, backup_id, action, key, comment, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
pgmoneta_tsclient_mode(char* server, char* action, int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket) || server == NULL)
   {
      goto error;
   }

   if (pgmoneta_management_request_mode(NULL, socket, server, action, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
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
check_output_outcome(int socket, int expected_error, struct json** output)
{
   struct json* read = NULL;
   struct json* outcome = NULL;
   bool status = false;
   int error = 0;

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

   if (!pgmoneta_json_contains_key(outcome, MANAGEMENT_ARGUMENT_STATUS))
   {
      goto error;
   }

   status = (bool)pgmoneta_json_get(outcome, MANAGEMENT_ARGUMENT_STATUS);

   if (expected_error == 0)
   {
      if (!status)
      {
         goto error;
      }
   }
   else
   {
      if (status)
      {
         goto error;
      }

      if (!pgmoneta_json_contains_key(outcome, MANAGEMENT_ARGUMENT_ERROR))
      {
          goto error;
      }

      error = (int)pgmoneta_json_get(outcome, MANAGEMENT_ARGUMENT_ERROR);
      if (error != expected_error)
      {
         pgmoneta_log_error("Expected error %d, got %d", expected_error, error);
         goto error;
      }
   }

   if (output != NULL)
   {
      *output = read;
   }
   else
   {
      pgmoneta_json_destroy(read);
   }
   return 0;
error:
   if (read)
   {
       pgmoneta_json_destroy(read);
   }
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
      if (pgmoneta_connect_unix_socket(config->common.unix_socket_dir, MAIN_UDS, &socket))
      {
         return -1;
      }
   }
   pgmoneta_log_info("%s %s %d", config->common.configuration_path, config->common.unix_socket_dir, socket);
   return socket;
}

int
pgmoneta_tsclient_reset(int expected_error)
{
   int socket = -1;

   socket = get_connection();
   if (!pgmoneta_socket_isvalid(socket))
   {
      goto error;
   }

   if (pgmoneta_management_request_reset(NULL, socket, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   if (check_output_outcome(socket, expected_error, NULL))
   {
      goto error;
   }

   pgmoneta_disconnect(socket);
   return 0;
error:
   pgmoneta_disconnect(socket);
   return 1;
}
