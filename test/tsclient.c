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
#include <configuration.h>
#include <json.h>
#include <management.h>
#include <network.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>
#include <value.h>
#include <tsclient.h>

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

char project_directory[BUFFER_SIZE];

static int check_output_outcome(int socket);
static int get_connection();
static char* get_configuration_path();
static char* get_restore_path();

int
pgmoneta_tsclient_init(char* base_dir)
{
    struct main_configuration* config = NULL;
    int ret;
    size_t size;
    char* configuration_path = NULL;
    size_t configuration_path_length = 0;

    memset(project_directory, 0, sizeof(project_directory));
    memcpy(project_directory, base_dir, strlen(base_dir));

    configuration_path = get_configuration_path();

    // Create the shared memory for the configuration
    size = sizeof(struct main_configuration);
    if (pgmoneta_create_shared_memory(size, HUGEPAGE_OFF, &shmem))
    {
        goto error;
    }
    pgmoneta_init_main_configuration(shmem);
    // Try reading configuration from the configuration path
    if (configuration_path != NULL)
    {
        ret = pgmoneta_read_main_configuration(shmem, configuration_path);
        if (ret)
        {
            goto error;
        }

        config = (struct main_configuration*)shmem;
    }
    else
    {
        goto error;
    } 

    free(configuration_path);
    return 0;
error:
    free(configuration_path);
    return 1;
}

int
pgmoneta_tsclient_destroy()
{
    size_t size;

    size = sizeof(struct main_configuration);
    return pgmoneta_destroy_shared_memory(shmem, size);
}

int
pgmoneta_tsclient_execute_backup(char* server, char* incremental)
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

    pgmoneta_disconnect(socket);
    return 0;
error:
    pgmoneta_disconnect(socket);
    return 1;
}

int
pgmoneta_tsclient_execute_restore(char* server, char* backup_id, char* position)
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

    restore_path = get_restore_path();
    // Create a restore request to the main server
    if (pgmoneta_management_request_restore(NULL, socket, server, backup_id, position, restore_path, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
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
pgmoneta_tsclient_execute_delete(char* server, char* backup_id)
{
    int socket = -1;

    socket = get_connection();
    // Security Checks
    if (!pgmoneta_socket_isvalid(socket) || server == NULL)
    {
        return 1;
    }

    // Fallbacks
    if (!backup_id) {
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

static int 
check_output_outcome(int socket)
{
    struct json* read = NULL;
    struct json* outcome = NULL;

    if (pgmoneta_management_read_json(NULL, socket, NULL, NULL, &read))
    {
        goto error;
    }
    
    if (!pgmoneta_json_contains_key(read, MANAGEMENT_CATEGORY_OUTCOME))
    {
        goto error;
    }

    outcome = (struct json*)pgmoneta_json_get(read, MANAGEMENT_CATEGORY_OUTCOME);
    if (!pgmoneta_json_contains_key(outcome, MANAGEMENT_ARGUMENT_STATUS) && !(bool)pgmoneta_json_get(outcome, MANAGEMENT_ARGUMENT_STATUS))
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
    if (!strlen(config->common.configuration_path))
    {
        if (pgmoneta_connect_unix_socket(config->unix_socket_dir, MAIN_UDS, &socket))
        {
            return -1;
        }
    }
    return socket;
}

static char*
get_restore_path()
{
   char* restore_path = NULL;
   int project_directory_length = strlen(project_directory);
   int restore_trail_length = strlen(PGMONETA_RESTORE_TRAIL);

   restore_path = (char*)calloc(project_directory_length + restore_trail_length + 1, sizeof(char));

   memcpy(restore_path, project_directory, project_directory_length);
   memcpy(restore_path + project_directory_length, PGMONETA_RESTORE_TRAIL, restore_trail_length);

   return restore_path;
}

static char*
get_configuration_path()
{
   char* configuration_path = NULL;
   int project_directory_length = strlen(project_directory);
   int configuration_trail_length = strlen(PGMONETA_CONFIGURATION_TRAIL);

   configuration_path = (char*)calloc(project_directory_length + configuration_trail_length + 1, sizeof(char));

   memcpy(configuration_path, project_directory, project_directory_length);
   memcpy(configuration_path + project_directory_length, PGMONETA_CONFIGURATION_TRAIL, configuration_trail_length);

   return configuration_path;
}
