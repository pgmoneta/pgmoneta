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
#include <info.h>
#include <json.h>
#include <logging.h>
#include <network.h>
#include <management.h>
#include <stdint.h>
#include <utils.h>

/* system */
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#define MANAGEMENT_HEADER_SIZE 1

/**
 * JSON related command tags, used to build and retrieve
 * a JSON piece of information related to a single command
 */
#define JSON_TAG_COMMAND "command"
#define JSON_TAG_COMMAND_NAME "name"
#define JSON_TAG_COMMAND_STATUS "status"
#define JSON_TAG_COMMAND_ERROR "error"
#define JSON_TAG_COMMAND_OUTPUT "output"
#define JSON_TAG_COMMAND_EXIT_STATUS "exit-status"

#define JSON_TAG_APPLICATION_NAME "name"
#define JSON_TAG_APPLICATION_VERSION_MAJOR "major"
#define JSON_TAG_APPLICATION_VERSION_MINOR "minor"
#define JSON_TAG_APPLICATION_VERSION_PATCH "patch"
#define JSON_TAG_APPLICATION_VERSION "version"

#define JSON_TAG_ARRAY_NAME "list"

/**
 * JSON pre-defined values
 */
#define JSON_STRING_SUCCESS "OK"
#define JSON_STRING_ERROR   "KO"
#define JSON_BOOL_SUCCESS   "0"
#define JSON_BOOL_ERROR     "1"

static int read_byte(char* prefix, SSL* ssl, int socket, signed char* c);
static int read_int32(char* prefix, SSL* ssl, int socket, int32_t* i);
static int read_uint32(char* prefix, SSL* ssl, int socket, uint32_t* i);
static int read_uint64(char* prefix, SSL* ssl, int socket, uint64_t* l);
static int read_bool(char* prefix, SSL* ssl, int socket, bool* b);
static int read_string(char* prefix, SSL* ssl, int socket, char** str);
static int write_int32(char* prefix, SSL* ssl, int socket, int32_t i);
static int write_uint32(char* prefix, SSL* ssl, int socket, uint32_t i);
static int write_uint64(char* prefix, SSL* ssl, int socket, uint64_t l);
static int write_bool(char* prefix, SSL* ssl, int socket, bool b);
static int write_string(char* prefix, SSL* ssl, int socket, char* str);
static int write_byte(char* prefix, SSL* ssl, int socket, signed char c);
static int read_complete(SSL* ssl, int socket, void* buf, size_t size);
static int write_complete(SSL* ssl, int socket, void* buf, size_t size);
static int write_socket(int socket, void* buf, size_t size);
static int write_ssl(SSL* ssl, void* buf, size_t size);
static int write_header(SSL* ssl, int fd, signed char type);
static int print_status_json(struct json* json);
static int print_details_json(struct json* json);
static int print_list_backup_json(struct json* json);
static int print_info_json(struct json* json);
static int print_delete_json(struct json* json);
static struct json* read_status_json(SSL* ssl, int socket);
static struct json* read_details_json(SSL* ssl, int socket);
static struct json* read_list_backup_json(SSL* ssl, int socket, char* server);
static struct json* read_delete_json(SSL* ssl, int socket, char* server, char* backup_id);
static struct json* read_info_json(SSL* ssl, int socket);
static struct json* create_new_command_json_object(char* command_name, bool success, char* executable_name);
static struct json* extract_command_output_json_object(struct json* json);
static bool json_command_name_equals_to(struct json* json, char* command_name);
static int set_command_json_object_faulty(struct json* json, char* message);
static bool is_command_json_object_faulty(struct json* json);
static void print_and_free_json_object(struct json* json);

int
pgmoneta_management_read_header(int socket, signed char* id)
{
   char header[MANAGEMENT_HEADER_SIZE];

   if (read_complete(NULL, socket, &header[0], sizeof(header)))
   {
      errno = 0;
      goto error;
   }

   *id = pgmoneta_read_byte(&(header));

   return 0;

error:

   *id = -1;

   return 1;
}

int
pgmoneta_management_read_payload(int socket, signed char id, char** payload_s1, char** payload_s2, char** payload_s3, char** payload_s4)
{
   *payload_s1 = NULL;
   *payload_s2 = NULL;
   *payload_s3 = NULL;
   *payload_s4 = NULL;

   switch (id)
   {
      case MANAGEMENT_BACKUP:
      case MANAGEMENT_LIST_BACKUP:
      case MANAGEMENT_DECRYPT:
      case MANAGEMENT_ENCRYPT:
      case MANAGEMENT_DECOMPRESS:
      case MANAGEMENT_COMPRESS:
         read_string("pgmoneta_management_read_payload", NULL, socket, payload_s1);
         break;
      case MANAGEMENT_RESTORE:
      case MANAGEMENT_ARCHIVE:
         read_string("pgmoneta_management_read_payload", NULL, socket, payload_s1);
         read_string("pgmoneta_management_read_payload", NULL, socket, payload_s2);
         read_string("pgmoneta_management_read_payload", NULL, socket, payload_s3);
         read_string("pgmoneta_management_read_payload", NULL, socket, payload_s4);
         break;
      case MANAGEMENT_DELETE:
      case MANAGEMENT_RETAIN:
      case MANAGEMENT_EXPUNGE:
      case MANAGEMENT_INFO:
         read_string("pgmoneta_management_read_payload", NULL, socket, payload_s1);
         read_string("pgmoneta_management_read_payload", NULL, socket, payload_s2);
         break;
      case MANAGEMENT_STOP:
      case MANAGEMENT_STATUS:
      case MANAGEMENT_STATUS_DETAILS:
      case MANAGEMENT_RESET:
      case MANAGEMENT_RELOAD:
      case MANAGEMENT_ISALIVE:
         break;
      default:
         goto error;
         break;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_backup(SSL* ssl, int socket, char* server)
{
   if (write_header(ssl, socket, MANAGEMENT_BACKUP))
   {
      pgmoneta_log_warn("pgmoneta_management_backup: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_backup", ssl, socket, server))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_list_backup(SSL* ssl, int socket, char* server)
{
   if (write_header(ssl, socket, MANAGEMENT_LIST_BACKUP))
   {
      pgmoneta_log_warn("pgmoneta_management_list_backup: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_list_backup", ssl, socket, server))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_read_list_backup(SSL* ssl, int socket, char* server, char output_format)
{
   struct json* json = read_list_backup_json(ssl, socket, server);
   if (json == NULL)
   {
      goto error;
   }
   if (output_format == COMMAND_OUTPUT_FORMAT_TEXT)
   {
      if (print_list_backup_json(json))
      {
         goto error;
      }
   }
   else if (output_format == COMMAND_OUTPUT_FORMAT_JSON)
   {
      print_and_free_json_object(json);
      json = NULL;
   }
   else
   {
      goto error;
   }

   if (json != NULL)
   {
      pgmoneta_json_free(json);
   }
   return 0;
error:
   if (json != NULL)
   {
      pgmoneta_json_free(json);
   }
   return 1;
}

int
pgmoneta_management_write_list_backup(SSL* ssl, int socket, int server)
{
   char* d = NULL;
   char* wal_dir = NULL;
   int32_t number_of_backups;
   struct backup** backups = NULL;
   int32_t nob;
   uint64_t wal;
   uint64_t delta;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (write_int32("pgmoneta_management_write_list_backup", ssl, socket, server))
   {
      goto error;
   }

   if (server != -1)
   {
      d = pgmoneta_get_server_backup(server);
      wal_dir = pgmoneta_get_server_wal(server);

      if (pgmoneta_get_backups(d, &number_of_backups, &backups))
      {
         write_int32("pgmoneta_management_write_list_backup", ssl, socket, 0);
         goto error;
      }

      nob = 0;
      for (int i = 0; i < number_of_backups; i++)
      {
         if (backups[i] != NULL)
         {
            nob++;
         }
      }

      if (write_int32("pgmoneta_management_write_list_backup", ssl, socket, nob))
      {
         goto error;
      }

      for (int i = 0; i < number_of_backups; i++)
      {
         if (backups[i] != NULL)
         {
            if (write_string("pgmoneta_management_write_list_backup", ssl, socket, backups[i]->label))
            {
               goto error;
            }

            if (write_byte("pgmoneta_management_write_list_backup", ssl, socket, backups[i]->keep ? 1 : 0))
            {
               goto error;
            }

            if (write_byte("pgmoneta_management_write_list_backup", ssl, socket, backups[i]->valid))
            {
               goto error;
            }
            if (write_uint64("pgmoneta_management_write_list_backup", ssl, socket, backups[i]->backup_size))
            {
               goto error;
            }

            if (write_uint64("pgmoneta_management_write_list_backup", ssl, socket, backups[i]->restore_size))
            {
               goto error;
            }

            wal = pgmoneta_number_of_wal_files(wal_dir, &backups[i]->wal[0], NULL);
            wal *= config->servers[server].wal_size;

            if (write_uint64("pgmoneta_management_write_list_backup", ssl, socket, wal))
            {
               goto error;
            }

            delta = 0;
            if (i > 0)
            {
               delta = pgmoneta_number_of_wal_files(wal_dir, &backups[i - 1]->wal[0], &backups[i]->wal[0]);
               delta *= config->servers[server].wal_size;
            }

            if (write_uint64("pgmoneta_management_write_list_backup", ssl, socket, delta))
            {
               goto error;
            }
         }
      }

      for (int i = 0; i < number_of_backups; i++)
      {
         free(backups[i]);
      }
      free(backups);
   }

   free(d);
   free(wal_dir);

   pgmoneta_management_process_result(ssl, socket, server, NULL, 0, false);

   return 0;

error:

   free(d);
   free(wal_dir);

   pgmoneta_management_process_result(ssl, socket, server, NULL, 1, false);

   return 1;
}

int
pgmoneta_management_restore(SSL* ssl, int socket, char* server, char* backup_id, char* position, char* directory)
{
   if (write_header(ssl, socket, MANAGEMENT_RESTORE))
   {
      pgmoneta_log_warn("pgmoneta_management_restore: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_restore", ssl, socket, server))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_restore", ssl, socket, backup_id))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_restore", ssl, socket, position))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_restore", ssl, socket, directory))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_archive(SSL* ssl, int socket, char* server, char* backup_id, char* position, char* directory)
{
   if (write_header(ssl, socket, MANAGEMENT_ARCHIVE))
   {
      pgmoneta_log_warn("pgmoneta_management_archive: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_archive", ssl, socket, server))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_archive", ssl, socket, backup_id))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_archive", ssl, socket, position))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_archive", ssl, socket, directory))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_delete(SSL* ssl, int socket, char* server, char* backup_id)
{
   if (write_header(ssl, socket, MANAGEMENT_DELETE))
   {
      pgmoneta_log_warn("pgmoneta_management_delete: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_delete", ssl, socket, server))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_delete", ssl, socket, backup_id))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_read_delete(SSL* ssl, int socket, char* server, char* backup_id, char output_format)
{
   struct json* json = read_delete_json(ssl, socket, server, backup_id);
   if (json == NULL)
   {
      goto error;
   }
   if (output_format == COMMAND_OUTPUT_FORMAT_TEXT)
   {
      if (print_delete_json(json))
      {
         goto error;
      }
   }
   else if (output_format == COMMAND_OUTPUT_FORMAT_JSON)
   {
      print_and_free_json_object(json);
      json = NULL;
   }
   else
   {
      goto error;
   }

   if (json != NULL)
   {
      pgmoneta_json_free(json);
   }
   return 0;
error:
   if (json != NULL)
   {
      pgmoneta_json_free(json);
   }
   return 1;
}

int
pgmoneta_management_write_delete(SSL* ssl, int socket, int server)
{
   char* d = NULL;
   int32_t number_of_backups = 0;
   char** array = NULL;

   if (write_int32("pgmoneta_management_write_delete", ssl, socket, server))
   {
      goto error;
   }

   if (server != -1)
   {
      d = pgmoneta_get_server_backup(server);

      if (pgmoneta_get_directories(d, &number_of_backups, &array))
      {
         write_int32("pgmoneta_management_write_delete", ssl, socket, 0);
         goto error;
      }

      if (write_int32("pgmoneta_management_write_delete", ssl, socket, number_of_backups))
      {
         goto error;
      }

      for (int i = 0; i < number_of_backups; i++)
      {
         if (write_string("pgmoneta_management_write_delete", ssl, socket, array[i]))
         {
            goto error;
         }
      }
   }

   for (int i = 0; i < number_of_backups; i++)
   {
      free(array[i]);
   }
   free(array);

   free(d);

   pgmoneta_management_process_result(ssl, socket, server, NULL, 0, false);

   return 0;

error:

   for (int i = 0; i < number_of_backups; i++)
   {
      free(array[i]);
   }
   free(array);

   free(d);

   pgmoneta_management_process_result(ssl, socket, server, NULL, 1, false);

   return 1;
}

int
pgmoneta_management_stop(SSL* ssl, int socket)
{
   if (write_header(ssl, socket, MANAGEMENT_STOP))
   {
      pgmoneta_log_warn("pgmoneta_management_stop: write: %d", socket);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_status(SSL* ssl, int socket)
{
   if (write_header(ssl, socket, MANAGEMENT_STATUS))
   {
      pgmoneta_log_warn("pgmoneta_management_status: write: %d", socket);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_read_status(SSL* ssl, int socket, char output_format)
{
   struct json* json = read_status_json(ssl, socket);
   if (json == NULL)
   {
      goto error;
   }
   if (output_format == COMMAND_OUTPUT_FORMAT_TEXT)
   {
      if (print_status_json(json))
      {
         goto error;
      }
   }
   else if (output_format == COMMAND_OUTPUT_FORMAT_JSON)
   {
      print_and_free_json_object(json);
      json = NULL;
   }
   else
   {
      goto error;
   }

   if (json != NULL)
   {
      pgmoneta_json_free(json);
   }
   return 0;
error:
   if (json != NULL)
   {
      pgmoneta_json_free(json);
   }
   return 1;
}

int
pgmoneta_management_write_status(SSL* ssl, int socket, bool offline)
{
   char* d = NULL;
   int32_t retention_days;
   int32_t retention_weeks;
   int32_t retention_months;
   int32_t retention_years;
   uint64_t used_size;
   uint64_t free_size;
   uint64_t total_size;
   uint64_t hot_standby_size;
   int32_t number_of_directories = 0;
   char** array = NULL;
   uint64_t server_size;
   struct configuration* config;

   config = (struct configuration*)shmem;

   d = NULL;
   d = pgmoneta_append(d, config->base_dir);
   d = pgmoneta_append(d, "/");

   used_size = pgmoneta_directory_size(d);

   free(d);
   d = NULL;

   free_size = pgmoneta_free_space(config->base_dir);
   total_size = pgmoneta_total_space(config->base_dir);

   if (write_bool("pgmoneta_management_write_status", ssl, socket, offline))
   {
      goto error;
   }

   if (write_uint64("pgmoneta_management_write_status", ssl, socket, used_size))
   {
      goto error;
   }

   if (write_uint64("pgmoneta_management_write_status", ssl, socket, free_size))
   {
      goto error;
   }

   if (write_uint64("pgmoneta_management_write_status", ssl, socket, total_size))
   {
      goto error;
   }

   if (write_int32("pgmoneta_management_write_status", ssl, socket, config->workers))
   {
      goto error;
   }

   if (write_int32("pgmoneta_management_write_status", ssl, socket, config->number_of_servers))
   {
      goto error;
   }

   for (int i = 0; i < config->number_of_servers; i++)
   {
      retention_days = config->servers[i].retention_days;
      if (retention_days <= 0)
      {
         retention_days = config->retention_days;
      }
      retention_weeks = config->servers[i].retention_weeks;
      if (retention_weeks <= 0)
      {
         retention_weeks = config->retention_weeks;
      }
      retention_months = config->servers[i].retention_months;
      if (retention_months <= 0)
      {
         retention_months = config->retention_months;
      }
      retention_years = config->servers[i].retention_years;
      if (retention_years <= 0)
      {
         retention_years = config->retention_years;
      }
      if (write_int32("pgmoneta_management_write_status", ssl, socket, retention_days))
      {
         goto error;
      }
      if (write_int32("pgmoneta_management_write_status", ssl, socket, retention_weeks))
      {
         goto error;
      }
      if (write_int32("pgmoneta_management_write_status", ssl, socket, retention_months))
      {
         goto error;
      }
      if (write_int32("pgmoneta_management_write_status", ssl, socket, retention_years))
      {
         goto error;
      }

      d = pgmoneta_get_server(i);

      server_size = pgmoneta_directory_size(d);

      if (write_uint64("pgmoneta_management_write_status", ssl, socket, server_size))
      {
         goto error;
      }

      free(d);
      d = NULL;

      if (strlen(config->servers[i].hot_standby) > 0)
      {
         hot_standby_size = pgmoneta_directory_size(config->servers[i].hot_standby);;
      }
      else
      {
         hot_standby_size = 0;
      }

      if (write_uint64("pgmoneta_management_write_status", ssl, socket, hot_standby_size))
      {
         goto error;
      }

      d = pgmoneta_get_server_backup(i);

      pgmoneta_get_directories(d, &number_of_directories, &array);

      if (write_int32("pgmoneta_management_write_status", ssl, socket, number_of_directories))
      {
         goto error;
      }

      if (write_string("pgmoneta_management_write_status", ssl, socket, config->servers[i].name))
      {
         goto error;
      }

      if (write_int32("pgmoneta_management_write_status", ssl, socket, config->servers[i].workers != -1 ? config->servers[i].workers : config->workers))
      {
         goto error;
      }

      for (int j = 0; j < number_of_directories; j++)
      {
         free(array[j]);
      }
      free(array);
      array = NULL;

      free(d);
      d = NULL;
   }

   return 0;

error:

   for (int i = 0; i < number_of_directories; i++)
   {
      free(array[i]);
   }
   free(array);

   free(d);

   return 1;
}

int
pgmoneta_management_details(SSL* ssl, int socket)
{
   if (write_header(ssl, socket, MANAGEMENT_STATUS_DETAILS))
   {
      pgmoneta_log_warn("pgmoneta_management_details: write: %d", socket);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_read_details(SSL* ssl, int socket, char output_format)
{
   struct json* json = read_details_json(ssl, socket);
   if (json == NULL)
   {
      goto error;
   }
   if (output_format == COMMAND_OUTPUT_FORMAT_TEXT)
   {
      if (print_details_json(json))
      {
         goto error;
      }
   }
   else if (output_format == COMMAND_OUTPUT_FORMAT_JSON)
   {
      print_and_free_json_object(json);
      json = NULL;
   }
   else
   {
      goto error;
   }

   if (json != NULL)
   {
      pgmoneta_json_free(json);
   }
   return 0;
error:
   if (json != NULL)
   {
      pgmoneta_json_free(json);
   }
   return 1;
}

int
pgmoneta_management_write_details(SSL* ssl, int socket, bool offline)
{
   char* d = NULL;
   char* wal_dir = NULL;
   int32_t retention_days;
   int32_t retention_weeks;
   int32_t retention_months;
   int32_t retention_years;
   uint64_t used_size;
   uint64_t free_size;
   uint64_t total_size;
   uint64_t hot_standby_size;
   int32_t number_of_backups = 0;
   struct backup** backups = NULL;
   uint64_t server_size;
   uint64_t wal;
   uint64_t delta;
   struct configuration* config;

   config = (struct configuration*)shmem;

   d = NULL;
   d = pgmoneta_append(d, config->base_dir);
   d = pgmoneta_append(d, "/");

   used_size = pgmoneta_directory_size(d);

   free(d);
   d = NULL;

   free_size = pgmoneta_free_space(config->base_dir);
   total_size = pgmoneta_total_space(config->base_dir);

   if (write_bool("pgmoneta_management_write_details", ssl, socket, offline))
   {
      goto error;
   }

   if (write_uint64("pgmoneta_management_write_details", ssl, socket, used_size))
   {
      goto error;
   }

   if (write_uint64("pgmoneta_management_write_details", ssl, socket, free_size))
   {
      goto error;
   }

   if (write_uint64("pgmoneta_management_write_details", ssl, socket, total_size))
   {
      goto error;
   }

   if (write_int32("pgmoneta_management_write_status", ssl, socket, config->workers))
   {
      goto error;
   }

   if (write_int32("pgmoneta_management_write_details", ssl, socket, config->number_of_servers))
   {
      goto error;
   }

   for (int i = 0; i < config->number_of_servers; i++)
   {
      wal_dir = pgmoneta_get_server_wal(i);

      if (write_string("pgmoneta_management_write_details", ssl, socket, config->servers[i].name))
      {
         goto error;
      }

      if (write_int32("pgmoneta_management_write_status", ssl, socket, config->servers[i].workers != -1 ? config->servers[i].workers : config->workers))
      {
         goto error;
      }

      retention_days = config->servers[i].retention_days;

      if (retention_days <= 0)
      {
         retention_days = config->retention_days;
      }

      retention_weeks = config->servers[i].retention_weeks;

      if (retention_weeks <= 0)
      {
         retention_weeks = config->retention_weeks;
      }

      retention_months = config->servers[i].retention_months;

      if (retention_months <= 0)
      {
         retention_months = config->retention_months;
      }

      retention_years = config->servers[i].retention_years;

      if (retention_years <= 0)
      {
         retention_years = config->retention_years;
      }

      if (write_int32("pgmoneta_management_write_details", ssl, socket, retention_days))
      {
         goto error;
      }

      if (write_int32("pgmoneta_management_write_details", ssl, socket, retention_weeks))
      {
         goto error;
      }

      if (write_int32("pgmoneta_management_write_details", ssl, socket, retention_months))
      {
         goto error;
      }

      if (write_int32("pgmoneta_management_write_details", ssl, socket, retention_years))
      {
         goto error;
      }

      d = pgmoneta_get_server(i);

      server_size = pgmoneta_directory_size(d);

      if (write_uint64("pgmoneta_management_write_details", ssl, socket, server_size))
      {
         goto error;
      }

      free(d);
      d = NULL;

      if (strlen(config->servers[i].hot_standby) > 0)
      {
         hot_standby_size = pgmoneta_directory_size(config->servers[i].hot_standby);;
      }
      else
      {
         hot_standby_size = 0;
      }

      if (write_uint64("pgmoneta_management_write_details", ssl, socket, hot_standby_size))
      {
         goto error;
      }

      d = pgmoneta_get_server_backup(i);

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      if (write_int32("pgmoneta_management_write_details", ssl, socket, number_of_backups))
      {
         goto error;
      }

      for (int j = 0; j < number_of_backups; j++)
      {
         if (backups[j] != NULL)
         {
            if (write_string("pgmoneta_management_write_details", ssl, socket, backups[j]->label))
            {
               goto error;
            }

            if (write_bool("pgmoneta_management_write_details", ssl, socket, backups[j]->keep))
            {
               goto error;
            }

            if (write_bool("pgmoneta_management_write_details", ssl, socket, backups[j]->valid))
            {
               goto error;
            }

            if (write_uint64("pgmoneta_management_write_details", ssl, socket, backups[j]->backup_size))
            {
               goto error;
            }

            if (write_uint64("pgmoneta_management_write_details", ssl, socket, backups[j]->restore_size))
            {
               goto error;
            }

            wal = pgmoneta_number_of_wal_files(wal_dir, &backups[j]->wal[0], NULL);
            wal *= config->servers[i].wal_size;

            if (write_uint64("pgmoneta_management_write_details", ssl, socket, wal))
            {
               goto error;
            }

            delta = 0;
            if (j > 0)
            {
               delta = pgmoneta_number_of_wal_files(wal_dir, &backups[j - 1]->wal[0], &backups[j]->wal[0]);
               delta *= config->servers[i].wal_size;
            }

            if (write_uint64("pgmoneta_management_write_details", ssl, socket, delta))
            {
               goto error;
            }
         }
      }

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);
      backups = NULL;

      free(d);
      d = NULL;

      free(wal_dir);
      wal_dir = NULL;
   }

   return 0;

error:

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(d);

   return 1;
}

int
pgmoneta_management_isalive(SSL* ssl, int socket)
{
   if (write_header(ssl, socket, MANAGEMENT_ISALIVE))
   {
      pgmoneta_log_warn("pgmoneta_management_isalive: write: %d", socket);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_read_isalive(SSL* ssl, int socket, int* status)
{
   char buf[4];

   memset(&buf, 0, sizeof(buf));

   if (read_complete(ssl, socket, &buf[0], sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_read_isalive: read: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   *status = pgmoneta_read_int32(&buf);

   return 0;

error:

   return 1;
}

int
pgmoneta_management_write_isalive(SSL* ssl, int socket)
{
   char buf[4];

   memset(&buf, 0, sizeof(buf));

   pgmoneta_write_int32(buf, 1);

   if (write_complete(ssl, socket, &buf, sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_write_isalive: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_reset(SSL* ssl, int socket)
{
   if (write_header(ssl, socket, MANAGEMENT_RESET))
   {
      pgmoneta_log_warn("pgmoneta_management_reset: write: %d", socket);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_reload(SSL* ssl, int socket)
{
   if (write_header(ssl, socket, MANAGEMENT_RELOAD))
   {
      pgmoneta_log_warn("pgmoneta_management_reload: write: %d", socket);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_retain(SSL* ssl, int socket, char* server, char* backup_id)
{
   if (write_header(ssl, socket, MANAGEMENT_RETAIN))
   {
      pgmoneta_log_warn("pgmoneta_management_retain: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_retain", ssl, socket, server))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_retain", ssl, socket, backup_id))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_expunge(SSL* ssl, int socket, char* server, char* backup_id)
{
   if (write_header(ssl, socket, MANAGEMENT_EXPUNGE))
   {
      pgmoneta_log_warn("pgmoneta_management_expunge: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_expunge", ssl, socket, server))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_expunge", ssl, socket, backup_id))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_decrypt(SSL* ssl, int socket, char* path)
{
   if (write_header(ssl, socket, MANAGEMENT_DECRYPT))
   {
      pgmoneta_log_warn("pgmoneta_management_decrypt: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_decrypt", ssl, socket, path))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_encrypt(SSL* ssl, int socket, char* path)
{
   if (write_header(ssl, socket, MANAGEMENT_ENCRYPT))
   {
      pgmoneta_log_warn("pgmoneta_management_encrypt: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_encrypt", ssl, socket, path))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_decompress(SSL* ssl, int socket, char* path)
{
   if (write_header(ssl, socket, MANAGEMENT_DECOMPRESS))
   {
      pgmoneta_log_warn("pgmoneta_management_decompress: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_decompress", ssl, socket, path))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_compress(SSL* ssl, int socket, char* path)
{
   if (write_header(ssl, socket, MANAGEMENT_COMPRESS))
   {
      pgmoneta_log_warn("pgmoneta_management_compress: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_compress", ssl, socket, path))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_info(SSL* ssl, int socket, char* server, char* backup)
{
   if (write_header(ssl, socket, MANAGEMENT_INFO))
   {
      pgmoneta_log_warn("pgmoneta_management_info: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_info", ssl, socket, server))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_info", ssl, socket, backup))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_read_info(SSL* ssl, int socket, char output_format)
{
   struct json* json = read_info_json(ssl, socket);

   if (json == NULL)
   {
      goto error;
   }

   if (output_format == COMMAND_OUTPUT_FORMAT_TEXT)
   {
      if (print_info_json(json))
      {
         goto error;
      }
   }
   else if (output_format == COMMAND_OUTPUT_FORMAT_JSON)
   {
      print_and_free_json_object(json);
      json = NULL;
   }
   else
   {
      goto error;
   }

   if (json != NULL)
   {
      pgmoneta_json_free(json);
   }

   return 0;

error:

   if (json != NULL)
   {
      pgmoneta_json_free(json);
   }
   return 1;
}

int
pgmoneta_management_write_info(SSL* ssl, int socket, char* server, char* backup)
{
   int srv = -1;
   char* d = NULL;
   int32_t number_of_backups = 0;
   struct backup** backups = NULL;
   struct backup* bck = NULL;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;

   for (int i = 0; srv == -1 && i < config->number_of_servers; i++)
   {
      if (!strcmp(config->servers[i].name, server))
      {
         srv = i;
      }
   }

   if (srv == -1)
   {
      goto error;
   }

   d = pgmoneta_get_server_backup(srv);

   number_of_backups = 0;
   backups = NULL;

   pgmoneta_get_backups(d, &number_of_backups, &backups);

   for (int i = 0; bck == NULL && i < number_of_backups; i++)
   {
      if (!strcmp(backups[i]->label, backup))
      {
         bck = backups[i];
      }
   }

   if (bck == NULL)
   {
      goto error;
   }

   if (write_string("pgmoneta_management_write_info", ssl, socket, bck->label))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_write_info", ssl, socket, bck->wal))
   {
      goto error;
   }

   if (write_uint64("pgmoneta_management_write_info", ssl, socket, bck->backup_size))
   {
      goto error;
   }

   if (write_uint64("pgmoneta_management_write_info", ssl, socket, bck->restore_size))
   {
      goto error;
   }

   if (write_int32("pgmoneta_management_write_info", ssl, socket, bck->elapsed_time))
   {
      goto error;
   }

   if (write_int32("pgmoneta_management_write_info", ssl, socket, bck->version))
   {
      goto error;
   }

   if (write_int32("pgmoneta_management_write_info", ssl, socket, bck->minor_version))
   {
      goto error;
   }

   if (write_byte("pgmoneta_management_write_info", ssl, socket, bck->keep))
   {
      goto error;
   }

   if (write_byte("pgmoneta_management_write_info", ssl, socket, bck->valid))
   {
      goto error;
   }

   if (write_uint64("pgmoneta_management_write_info", ssl, socket, bck->number_of_tablespaces))
   {
      goto error;
   }

   for (uint64_t i = 0; i < bck->number_of_tablespaces; i++)
   {
      if (write_string("pgmoneta_management_write_info", ssl, socket, bck->tablespaces[i]))
      {
         goto error;
      }
   }

   if (write_uint32("pgmoneta_management_write_info", ssl, socket, bck->start_lsn_hi32))
   {
      goto error;
   }

   if (write_uint32("pgmoneta_management_write_info", ssl, socket, bck->start_lsn_lo32))
   {
      goto error;
   }

   if (write_uint32("pgmoneta_management_write_info", ssl, socket, bck->end_lsn_hi32))
   {
      goto error;
   }

   if (write_uint32("pgmoneta_management_write_info", ssl, socket, bck->end_lsn_lo32))
   {
      goto error;
   }

   if (write_uint32("pgmoneta_management_write_info", ssl, socket, bck->checkpoint_lsn_hi32))
   {
      goto error;
   }

   if (write_uint32("pgmoneta_management_write_info", ssl, socket, bck->checkpoint_lsn_lo32))
   {
      goto error;
   }

   if (write_uint32("pgmoneta_management_write_info", ssl, socket, bck->start_timeline))
   {
      goto error;
   }

   if (write_uint32("pgmoneta_management_write_info", ssl, socket, bck->end_timeline))
   {
      goto error;
   }

   for (int j = 0; j < number_of_backups; j++)
   {
      free(backups[j]);
   }
   free(backups);

   free(d);

   return 0;

error:

   for (int j = 0; j < number_of_backups; j++)
   {
      free(backups[j]);
   }
   free(backups);

   free(d);

   return 1;
}

int
pgmoneta_management_read_int32(SSL* ssl, int socket, int* status)
{
   char buf[4];

   memset(&buf, 0, sizeof(buf));

   if (read_complete(ssl, socket, &buf[0], sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_read_int32: read: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   *status = pgmoneta_read_int32(&buf);

   return 0;

error:

   return 1;
}

int
pgmoneta_management_process_result(SSL* ssl, int socket, int srv, char* server, int code, bool send)
{
   struct configuration* config;
   time_t end_time;
   struct tm* end_time_info;
   char end_time_str[128];

   config = (struct configuration*)shmem;

   if (srv == -1)
   {
      for (int i = 0; i < config->number_of_servers; i++)
      {
         if (!strcmp(config->servers[i].name, server))
         {
            srv = i;
            break;
         }
      }
   }

   if (srv != -1)
   {
      time(&end_time);
      end_time_info = localtime(&end_time);
      memset(&end_time_str[0], 0, sizeof(end_time_str));
      strftime(&end_time_str[0], sizeof(end_time_str), "%Y%m%d%H%M%S", end_time_info);

      config->servers[srv].operation_count++;
      memcpy(config->servers[srv].last_operation_time, end_time_str, strlen(end_time_str));
      if (code)
      {
         config->servers[srv].failed_operation_count++;
         memcpy(config->servers[srv].last_failed_operation_time, end_time_str, strlen(end_time_str));
      }
   }

   if (send)
   {
      return pgmoneta_management_write_int32(ssl, socket, code);
   }
   return 0;
}

int
pgmoneta_management_write_int32(SSL* ssl, int socket, int code)
{
   char buf[4];

   memset(&buf, 0, sizeof(buf));

   pgmoneta_write_int32(buf, code);

   if (write_complete(ssl, socket, &buf, sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_write_int32: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
read_byte(char* prefix, SSL* ssl, int socket, signed char* c)
{
   char buf1[1] = {0};

   *c = 0;

   if (read_complete(ssl, socket, &buf1[0], sizeof(buf1)))
   {
      pgmoneta_log_warn("%s: read: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   *c = pgmoneta_read_byte(&buf1);

   return 0;

error:

   return 1;
}

static int
read_int32(char* prefix, SSL* ssl, int socket, int* i)
{
   char buf4[4] = {0};

   *i = 0;

   if (read_complete(ssl, socket, &buf4[0], sizeof(buf4)))
   {
      pgmoneta_log_warn("%s: read: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   *i = pgmoneta_read_int32(&buf4);

   return 0;

error:

   return 1;
}

static int
read_uint32(char* prefix, SSL* ssl, int socket, uint32_t* i)
{
   char buf4[4] = {0};

   *i = 0;

   if (read_complete(ssl, socket, &buf4[0], sizeof(buf4)))
   {
      pgmoneta_log_warn("%s: read: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   *i = pgmoneta_read_uint32(&buf4);

   return 0;

error:

   return 1;
}

static int
read_uint64(char* prefix, SSL* ssl, int socket, uint64_t* l)
{
   char buf8[8] = {0};

   *l = 0;

   if (read_complete(ssl, socket, &buf8[0], sizeof(buf8)))
   {
      pgmoneta_log_warn("%s: read: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   *l = pgmoneta_read_uint64(&buf8);

   return 0;

error:

   return 1;
}

static int
read_bool(char* prefix, SSL* ssl, int socket, bool* b)
{
   char buf1[1] = {0};

   *b = 0;

   if (read_complete(ssl, socket, &buf1[0], sizeof(buf1)))
   {
      pgmoneta_log_warn("%s: read: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   *b = pgmoneta_read_bool(&buf1);

   return 0;

error:

   return 1;
}

static int
read_string(char* prefix, SSL* ssl, int socket, char** str)
{
   char* s = NULL;
   char buf4[4] = {0};
   int size;

   *str = NULL;

   if (read_complete(ssl, socket, &buf4[0], sizeof(buf4)))
   {
      pgmoneta_log_warn("%s: read: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   size = pgmoneta_read_int32(&buf4);
   if (size > 0)
   {
      s = malloc(size + 1);

      if (s == NULL)
      {
         goto error;
      }

      memset(s, 0, size + 1);

      if (read_complete(ssl, socket, s, size))
      {
         pgmoneta_log_warn("%s: read: %d %s", prefix, socket, strerror(errno));
         errno = 0;
         goto error;
      }

      *str = s;
   }

   return 0;

error:

   free(s);

   return 1;
}

static int
write_int32(char* prefix, SSL* ssl, int socket, int i)
{
   char buf4[4] = {0};

   pgmoneta_write_int32(&buf4, i);
   if (write_complete(ssl, socket, &buf4, sizeof(buf4)))
   {
      pgmoneta_log_warn("%s: write: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
write_uint32(char* prefix, SSL* ssl, int socket, uint32_t i)
{
   char buf4[4] = {0};

   pgmoneta_write_uint32(&buf4, i);
   if (write_complete(ssl, socket, &buf4, sizeof(buf4)))
   {
      pgmoneta_log_warn("%s: write: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
write_uint64(char* prefix, SSL* ssl, int socket, uint64_t l)
{
   char buf8[8] = {0};

   pgmoneta_write_uint64(&buf8, l);
   if (write_complete(ssl, socket, &buf8, sizeof(buf8)))
   {
      pgmoneta_log_warn("%s: write: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
write_bool(char* prefix, SSL* ssl, int socket, bool b)
{
   char buf1[1] = {0};

   pgmoneta_write_bool(&buf1, b);
   if (write_complete(ssl, socket, &buf1, sizeof(buf1)))
   {
      pgmoneta_log_warn("%s: write: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
write_string(char* prefix, SSL* ssl, int socket, char* str)
{
   char buf4[4] = {0};

   pgmoneta_write_int32(&buf4, str != NULL ? strlen(str) : 0);
   if (write_complete(ssl, socket, &buf4, sizeof(buf4)))
   {
      pgmoneta_log_warn("%s: write: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   if (str != NULL)
   {
      if (write_complete(ssl, socket, str, strlen(str)))
      {
         pgmoneta_log_warn("%s: write: %d %s", prefix, socket, strerror(errno));
         errno = 0;
         goto error;
      }
   }

   return 0;

error:

   return 1;
}

static int
write_byte(char* prefix, SSL* ssl, int socket, signed char c)
{
   char buf1[1] = {0};

   pgmoneta_write_byte(&buf1, c);
   if (write_complete(ssl, socket, &buf1, sizeof(buf1)))
   {
      pgmoneta_log_warn("%s: write: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
read_complete(SSL* ssl, int socket, void* buf, size_t size)
{
   ssize_t r;
   size_t offset;
   size_t needs;
   int retries;

   offset = 0;
   needs = size;
   retries = 0;

read:
   if (ssl == NULL)
   {
      r = read(socket, buf + offset, needs);
   }
   else
   {
      r = SSL_read(ssl, buf + offset, needs);
   }

   if (r == -1)
   {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
         errno = 0;
         goto read;
      }

      goto error;
   }
   else if (r < (ssize_t)needs)
   {
      /* Sleep for 10ms */
      SLEEP(10000000L);

      if (retries < 100)
      {
         offset += r;
         needs -= r;
         retries++;
         goto read;
      }
      else
      {
         errno = EINVAL;
         goto error;
      }
   }

   return 0;

error:

   return 1;
}

static int
write_complete(SSL* ssl, int socket, void* buf, size_t size)
{
   if (ssl == NULL)
   {
      return write_socket(socket, buf, size);
   }

   return write_ssl(ssl, buf, size);
}

static int
write_socket(int socket, void* buf, size_t size)
{
   bool keep_write = false;
   ssize_t numbytes;
   int offset;
   ssize_t totalbytes;
   ssize_t remaining;

   numbytes = 0;
   offset = 0;
   totalbytes = 0;
   remaining = size;

   do
   {
      numbytes = write(socket, buf + offset, remaining);

      if (likely(numbytes == (ssize_t)size))
      {
         return 0;
      }
      else if (numbytes != -1)
      {
         offset += numbytes;
         totalbytes += numbytes;
         remaining -= numbytes;

         if (totalbytes == (ssize_t)size)
         {
            return 0;
         }

         pgmoneta_log_trace("Write %d - %zd/%zd vs %zd", socket, numbytes, totalbytes, size);
         keep_write = true;
         errno = 0;
      }
      else
      {
         switch (errno)
         {
            case EAGAIN:
               keep_write = true;
               errno = 0;
               break;
            default:
               keep_write = false;
               break;
         }
      }
   }
   while (keep_write);

   return 1;
}

static int
write_ssl(SSL* ssl, void* buf, size_t size)
{
   bool keep_write = false;
   ssize_t numbytes;
   int offset;
   ssize_t totalbytes;
   ssize_t remaining;

   numbytes = 0;
   offset = 0;
   totalbytes = 0;
   remaining = size;

   do
   {
      numbytes = SSL_write(ssl, buf + offset, remaining);

      if (likely(numbytes == (ssize_t)size))
      {
         return 0;
      }
      else if (numbytes > 0)
      {
         offset += numbytes;
         totalbytes += numbytes;
         remaining -= numbytes;

         if (totalbytes == (ssize_t)size)
         {
            return 0;
         }

         pgmoneta_log_trace("SSL/Write %d - %zd/%zd vs %zd", SSL_get_fd(ssl), numbytes, totalbytes, size);
         keep_write = true;
         errno = 0;
      }
      else
      {
         int err = SSL_get_error(ssl, numbytes);

         switch (err)
         {
            case SSL_ERROR_ZERO_RETURN:
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_CONNECT:
            case SSL_ERROR_WANT_ACCEPT:
            case SSL_ERROR_WANT_X509_LOOKUP:
#ifndef HAVE_OPENBSD
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
            case SSL_ERROR_WANT_ASYNC:
            case SSL_ERROR_WANT_ASYNC_JOB:
#if (OPENSSL_VERSION_NUMBER >= 0x10101000L)
            case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
#endif
#endif
               errno = 0;
               keep_write = true;
               break;
            case SSL_ERROR_SYSCALL:
               pgmoneta_log_error("SSL_ERROR_SYSCALL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_write = false;
               break;
            case SSL_ERROR_SSL:
               pgmoneta_log_error("SSL_ERROR_SSL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_write = false;
               break;
         }
         ERR_clear_error();

         if (!keep_write)
         {
            return 1;
         }
      }
   }
   while (keep_write);

   return 1;
}

static int
write_header(SSL* ssl, int socket, signed char type)
{
   char header[MANAGEMENT_HEADER_SIZE];

   pgmoneta_write_byte(&(header), type);

   return write_complete(ssl, socket, &(header), MANAGEMENT_HEADER_SIZE);
}

static struct json*
read_status_json(SSL* ssl, int socket)
{
   char* name = NULL;
   bool offline;
   uint64_t used_size;
   uint64_t free_size;
   uint64_t total_size;
   uint64_t server_size;
   uint64_t hot_standby_size;
   char* size_string;
   char* hot_standby_size_string;
   int32_t retention_days;
   int32_t retention_weeks;
   int32_t retention_months;
   int32_t retention_years;
   int32_t num_servers;
   int32_t number_of_directories;
   int32_t workers;
   struct json* json = NULL;
   struct json* output = NULL;
   struct json* status = NULL;
   struct json* servers_array = NULL;

   json = create_new_command_json_object("status", true, "pgmoneta-cli");
   pgmoneta_json_init(&status);
   if (status == NULL || json == NULL)
   {
      goto error;
   }

   output = extract_command_output_json_object(json);

   if (read_bool("pgmoneta_management_read_status", ssl, socket, &offline))
   {
      goto error;
   }
   pgmoneta_json_put(status, "Mode", (offline == 0 ? "Running" : "Offline"), ValueString);

   if (read_uint64("pgmoneta_management_read_status", ssl, socket, &used_size))
   {
      goto error;
   }
   size_string = pgmoneta_bytes_to_string(used_size);
   pgmoneta_json_put(status, "Used space", size_string, ValueString);
   free(size_string);

   if (read_uint64("pgmoneta_management_read_status", ssl, socket, &free_size))
   {
      goto error;
   }
   size_string = pgmoneta_bytes_to_string(free_size);
   pgmoneta_json_put(status, "Free space", size_string, ValueString);
   free(size_string);

   if (read_uint64("pgmoneta_management_read_status", ssl, socket, &total_size))
   {
      goto error;
   }
   size_string = pgmoneta_bytes_to_string(total_size);
   pgmoneta_json_put(status, "Total space", size_string, ValueString);
   free(size_string);

   if (read_int32("pgmoneta_management_read_status", ssl, socket, &workers))
   {
      goto error;
   }

   pgmoneta_json_put(status, "Workers", &workers, ValueInt32);

   if (read_int32("pgmoneta_management_read_status", ssl, socket, &num_servers))
   {
      goto error;
   }
   pgmoneta_json_put(status, "Number of servers", &num_servers, ValueInt32);

   pgmoneta_json_init(&servers_array);
   if (servers_array == NULL)
   {
      goto error;
   }
   for (int i = 0; i < num_servers; i++)
   {
      if (read_int32("pgmoneta_management_read_status", ssl, socket, &retention_days))
      {
         goto error;
      }

      if (read_int32("pgmoneta_management_read_status", ssl, socket, &retention_weeks))
      {
         goto error;
      }
      if (read_int32("pgmoneta_management_read_status", ssl, socket, &retention_months))
      {
         goto error;
      }
      if (read_int32("pgmoneta_management_read_status", ssl, socket, &retention_years))
      {
         goto error;
      }

      if (read_uint64("pgmoneta_management_read_status", ssl, socket, &server_size))
      {
         goto error;
      }

      if (read_uint64("pgmoneta_management_read_status", ssl, socket, &hot_standby_size))
      {
         goto error;
      }

      size_string = pgmoneta_bytes_to_string(server_size);
      hot_standby_size_string = pgmoneta_bytes_to_string(hot_standby_size);

      if (read_int32("pgmoneta_management_read_status", ssl, socket, &number_of_directories))
      {
         goto error;
      }

      if (read_string("pgmoneta_management_read_status", ssl, socket, &name))
      {
         goto error;
      }

      if (read_int32("pgmoneta_management_read_status", ssl, socket, &workers))
      {
         goto error;
      }
      struct json* server = NULL;
      pgmoneta_json_init(&server);
      pgmoneta_json_put(server, "Server", &name[0], ValueString);
      pgmoneta_json_put(server, "Retention days", &retention_days, ValueInt32);
      if (retention_weeks != -1)
      {
         pgmoneta_json_put(server, "Retention weeks", &retention_weeks, ValueInt32);
      }
      if (retention_months != -1)
      {
         pgmoneta_json_put(server, "Retention months", &retention_months, ValueInt32);
      }
      if (retention_years != -1)
      {
         pgmoneta_json_put(server, "Retention years", &retention_years, ValueInt32);
      }
      pgmoneta_json_put(server, "Backups", &number_of_directories, ValueInt32);
      pgmoneta_json_put(server, "Space", size_string, ValueString);
      pgmoneta_json_put(server, "Hot standby", hot_standby_size_string, ValueString);
      pgmoneta_json_put(server, "Workers", &workers, ValueInt32);

      pgmoneta_json_append(servers_array, server, ValueObject);

      free(size_string);
      size_string = NULL;

      free(hot_standby_size_string);
      hot_standby_size_string = NULL;

      free(name);
      name = NULL;
   }

   pgmoneta_json_put(status, "servers", servers_array, ValueObject);
   pgmoneta_json_put(output, "status", status, ValueObject);

   return json;

error:
   // return json anyway with error code set
   if (json != NULL)
   {
      set_command_json_object_faulty(json, strerror(errno));
   }
   errno = 0;
   return json;
}

static struct json*
read_details_json(SSL* ssl, int socket)
{
   char* name = NULL;
   bool offline;
   uint64_t used_size;
   uint64_t free_size;
   uint64_t total_size;
   uint64_t server_size;
   uint64_t hot_standby_size;
   char* size_string;
   char* hot_standby_size_string;
   int32_t retention_days;
   int32_t retention_weeks;
   int32_t retention_months;
   int32_t retention_years;
   int32_t num_servers;
   int32_t number_of_backups;
   uint64_t backup_size;
   uint64_t restore_size;
   uint64_t wal_size;
   uint64_t delta_size;
   bool keep;
   int8_t valid;
   char* bck = NULL;
   char* res = NULL;
   char* ws = NULL;
   char* ds = NULL;
   int32_t workers;
   struct json* json = NULL;
   struct json* output = NULL;
   struct json* status = NULL;
   struct json* servers_array = NULL;

   json = create_new_command_json_object("details", true, "pgmoneta-cli");
   pgmoneta_json_init(&status);
   if (status == NULL || json == NULL)
   {
      goto error;
   }

   output = extract_command_output_json_object(json);

   if (read_bool("pgmoneta_management_read_details", ssl, socket, &offline))
   {
      goto error;
   }
   pgmoneta_json_put(status, "Mode", (offline == 0 ? "Running" : "Offline"), ValueString);

   if (read_uint64("pgmoneta_management_read_details", ssl, socket, &used_size))
   {
      goto error;
   }
   size_string = pgmoneta_bytes_to_string(used_size);
   pgmoneta_json_put(status, "Used space", size_string, ValueString);
   free(size_string);

   if (read_uint64("pgmoneta_management_read_details", ssl, socket, &free_size))
   {
      goto error;
   }
   size_string = pgmoneta_bytes_to_string(free_size);
   pgmoneta_json_put(status, "Free space", size_string, ValueString);
   free(size_string);

   if (read_uint64("pgmoneta_management_read_details", ssl, socket, &total_size))
   {
      goto error;
   }
   size_string = pgmoneta_bytes_to_string(total_size);
   pgmoneta_json_put(status, "Total space", size_string, ValueString);
   free(size_string);

   if (read_int32("pgmoneta_management_read_status", ssl, socket, &workers))
   {
      goto error;
   }
   pgmoneta_json_put(status, "Workers", &workers, ValueInt32);

   if (read_int32("pgmoneta_management_read_details", ssl, socket, &num_servers))
   {
      goto error;
   }
   pgmoneta_json_put(status, "Number of servers", &num_servers, ValueInt32);
   pgmoneta_json_init(&servers_array);
   if (servers_array == NULL)
   {
      goto error;
   }
   for (int i = 0; i < num_servers; i++)
   {
      if (read_string("pgmoneta_management_read_details", ssl, socket, &name))
      {
         goto error;
      }

      if (read_int32("pgmoneta_management_read_status", ssl, socket, &workers))
      {
         goto error;
      }

      if (read_int32("pgmoneta_management_read_details", ssl, socket, &retention_days))
      {
         goto error;
      }
      if (read_int32("pgmoneta_management_read_details", ssl, socket, &retention_weeks))
      {
         goto error;
      }
      if (read_int32("pgmoneta_management_read_details", ssl, socket, &retention_months))
      {
         goto error;
      }
      if (read_int32("pgmoneta_management_read_details", ssl, socket, &retention_years))
      {
         goto error;
      }

      if (read_uint64("pgmoneta_management_read_details", ssl, socket, &server_size))
      {
         goto error;
      }

      if (read_uint64("pgmoneta_management_read_details", ssl, socket, &hot_standby_size))
      {
         goto error;
      }

      size_string = pgmoneta_bytes_to_string(server_size);
      hot_standby_size_string = pgmoneta_bytes_to_string(hot_standby_size);

      if (read_int32("pgmoneta_management_read_details", ssl, socket, &number_of_backups))
      {
         goto error;
      }
      struct json* server = NULL;
      pgmoneta_json_init(&server);
      pgmoneta_json_put(server, "Server", &name[0], ValueString);
      pgmoneta_json_put(server, "Retention days", &retention_days, ValueInt32);
      if (retention_weeks != -1)
      {
         pgmoneta_json_put(server, "Retention weeks", &retention_weeks, ValueInt32);
      }
      if (retention_months != -1)
      {
         pgmoneta_json_put(server, "Retention months", &retention_months, ValueInt32);
      }
      if (retention_years != -1)
      {
         pgmoneta_json_put(server, "Retention years", &retention_years, ValueInt32);
      }
      pgmoneta_json_put(server, "Backups", &number_of_backups, ValueInt32);
      pgmoneta_json_put(server, "Space", size_string, ValueString);
      pgmoneta_json_put(server, "Workers", &workers, ValueInt32);
      pgmoneta_json_put(server, "Hot standby", hot_standby_size_string, ValueString);
      free(size_string);
      size_string = NULL;

      free(hot_standby_size_string);
      hot_standby_size_string = NULL;

      free(name);
      name = NULL;

      struct json* backups_array = NULL;
      pgmoneta_json_init(&backups_array);
      if (backups_array == NULL)
      {
         goto error;
      }
      for (int j = 0; j < number_of_backups; j++)
      {
         if (read_string("pgmoneta_management_read_details", ssl, socket, &name))
         {
            goto error;
         }

         if (read_bool("pgmoneta_management_read_details", ssl, socket, &keep))
         {
            goto error;
         }

         if (read_byte("pgmoneta_management_read_details", ssl, socket, &valid))
         {
            goto error;
         }

         if (read_uint64("pgmoneta_management_read_details", ssl, socket, &backup_size))
         {
            goto error;
         }

         bck = pgmoneta_bytes_to_string(backup_size);
         pgmoneta_log_info("backup_size = %s", bck);

         if (read_uint64("pgmoneta_management_read_details", ssl, socket, &restore_size))
         {
            goto error;
         }

         res = pgmoneta_bytes_to_string(restore_size);

         if (read_uint64("pgmoneta_management_read_details", ssl, socket, &wal_size))
         {
            goto error;
         }

         ws = pgmoneta_bytes_to_string(wal_size);

         if (read_uint64("pgmoneta_management_read_details", ssl, socket, &delta_size))
         {
            goto error;
         }

         ds = pgmoneta_bytes_to_string(delta_size);
         pgmoneta_log_info(">>%s", ds);
         struct json* backup = NULL;
         pgmoneta_json_init(&backup);
         pgmoneta_json_put(backup, "Backup name", name, ValueString);
         if (valid != VALID_UNKNOWN)
         {
            pgmoneta_json_put(backup, "Backup", bck, ValueString);
            pgmoneta_json_put(backup, "Restore", res, ValueString);
            pgmoneta_json_put(backup, "WAL", ws, ValueString);
            pgmoneta_json_put(backup, "Delta", ds, ValueString);
            pgmoneta_json_put(backup, "Retain", keep ? "Yes" : "No", ValueString);
            pgmoneta_json_put(backup, "Valid", valid == VALID_TRUE ? "Yes" : "No", ValueString);
         }
         else
         {
            pgmoneta_json_put(backup, "Backup", "UNKNOWN", ValueString);
            pgmoneta_json_put(backup, "Restore", "UNKNOWN", ValueString);
            pgmoneta_json_put(backup, "WAL", "UNKNOWN", ValueString);
            pgmoneta_json_put(backup, "Delta", "UNKNOWN", ValueString);
            pgmoneta_json_put(backup, "Retain", "UNKNOWN", ValueString);
            pgmoneta_json_put(backup, "Valid", "UNKNOWN", ValueString);
         }

         free(bck);
         bck = NULL;

         free(res);
         res = NULL;

         free(ws);
         ws = NULL;

         free(ds);
         ds = NULL;

         free(name);
         name = NULL;
         pgmoneta_json_append(backups_array, backup, ValueObject);
      }
      pgmoneta_json_put(server, "backups", backups_array, ValueObject);
      pgmoneta_json_append(servers_array, server, ValueObject);
   }
   pgmoneta_json_put(status, "servers", servers_array, ValueObject);
   pgmoneta_json_put(output, "status", status, ValueObject);

   return json;
error:
   free(bck);
   free(res);
   free(ws);
   free(ds);
   free(name);
   // return json anyway with error code set
   if (json != NULL)
   {
      set_command_json_object_faulty(json, strerror(errno));
   }
   errno = 0;
   return json;
}

static struct json*
read_delete_json(SSL* ssl, int socket, char* server, char* backup_id)
{
   char* name = NULL;
   int srv;
   int32_t number_of_backups;
   struct json* json = NULL;
   struct json* output = NULL;
   struct json* status = NULL;
   struct json* backups_array = NULL;

   json = create_new_command_json_object("delete", true, "pgmoneta-cli");
   pgmoneta_json_init(&status);

   pgmoneta_log_debug("Delete: %s/%s", server, backup_id);

   if (status == NULL || json == NULL)
   {
      goto error;
   }

   output = extract_command_output_json_object(json);

   if (read_int32("pgmoneta_management_read_delete", ssl, socket, &srv))
   {
      goto error;
   }

   pgmoneta_json_put(status, "Server", srv == -1 ? "Unknown" : server, ValueString);

   if (srv != -1)
   {
      if (read_int32("pgmoneta_management_read_delete", ssl, socket, &number_of_backups))
      {
         goto error;
      }
      pgmoneta_json_put(status, "Number of backups", &number_of_backups, ValueInt32);
      pgmoneta_json_init(&backups_array);
      if (backups_array == NULL)
      {
         goto error;
      }
      for (int i = 0; i < number_of_backups; i++)
      {
         if (read_string("pgmoneta_management_read_delete", ssl, socket, &name))
         {
            goto error;
         }
         struct json* backup = NULL;
         pgmoneta_json_init(&backup);
         if (backup == NULL)
         {
            goto error;
         }
         pgmoneta_json_put(backup, "Backup name", name, ValueString);
         pgmoneta_json_append(backups_array, backup, ValueObject);
         free(name);
         name = NULL;
      }
   }

   pgmoneta_json_put(status, "backups", backups_array, ValueObject);
   pgmoneta_json_put(output, "status", status, ValueObject);
   return json;

error:
   free(name);

   // return json anyway with error code set
   if (json != NULL)
   {
      set_command_json_object_faulty(json, strerror(errno));
   }
   errno = 0;
   return json;
}

static struct json*
read_info_json(SSL* ssl, int socket)
{
   char* label = NULL;
   char* wal = NULL;
   bool sc;
   int32_t i32;
   uint32_t u32;
   uint64_t u64;
   uint64_t number_of_tablespaces;
   char* tablespace = NULL;
   struct json* tbl_array = NULL;
   struct json* json = NULL;
   struct json* output = NULL;
   struct json* info = NULL;

   json = create_new_command_json_object("info", true, "pgmoneta-cli");
   pgmoneta_json_init(&info);

   if (info == NULL || json == NULL)
   {
      goto error;
   }

   output = extract_command_output_json_object(json);

   if (read_string("pgmoneta_management_read_info", ssl, socket, &label))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Label", label, ValueString);

   if (read_string("pgmoneta_management_read_info", ssl, socket, &wal))
   {
      goto error;
   }

   pgmoneta_json_put(info, "WAL", wal, ValueString);

   if (read_uint64("pgmoneta_management_read_info", ssl, socket, &u64))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Backup size", &u64, ValueUInt64);

   if (read_uint64("pgmoneta_management_read_info", ssl, socket, &u64))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Restore size", &u64, ValueUInt64);

   if (read_int32("pgmoneta_management_read_info", ssl, socket, &i32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Elapsed time", &i32, ValueInt32);

   if (read_int32("pgmoneta_management_read_info", ssl, socket, &i32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Version", &i32, ValueInt32);

   if (read_int32("pgmoneta_management_read_info", ssl, socket, &i32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Minor version", &i32, ValueInt32);

   if (read_bool("pgmoneta_management_read_info", ssl, socket, &sc))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Keep", &sc, ValueBool);

   if (read_bool("pgmoneta_management_read_info", ssl, socket, &sc))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Valid", &sc, ValueBool);

   if (read_uint64("pgmoneta_management_read_info", ssl, socket, &u64))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Number of tablespaces", &u64, ValueUInt64);
   number_of_tablespaces = (uint64_t)u64;

   pgmoneta_json_init(&tbl_array);

   if (tbl_array == NULL)
   {
      goto error;
   }

   for (uint64_t i = 0; i < number_of_tablespaces; i++)
   {
      struct json* tbl = NULL;

      if (read_string("pgmoneta_management_read_info", ssl, socket, &tablespace))
      {
         goto error;
      }

      pgmoneta_json_init(&tbl);

      if (tbl == NULL)
      {
         goto error;
      }

      pgmoneta_json_put(tbl, "Table space", tablespace, ValueString);
      pgmoneta_json_append(tbl_array, tbl, ValueObject);

      free(tablespace);
      tablespace = NULL;
   }

   pgmoneta_json_put(info, "Table spaces", tbl_array, ValueObject);

   if (read_uint32("pgmoneta_management_read_info", ssl, socket, &u32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Start LSN Hi32", &u32, ValueUInt32);

   if (read_uint32("pgmoneta_management_read_info", ssl, socket, &u32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Start LSN Lo32", &u32, ValueUInt32);

   if (read_uint32("pgmoneta_management_read_info", ssl, socket, &u32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "End LSN Hi32", &u32, ValueUInt32);

   if (read_uint32("pgmoneta_management_read_info", ssl, socket, &u32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "End LSN Lo32", &u32, ValueUInt32);

   if (read_uint32("pgmoneta_management_read_info", ssl, socket, &u32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Checkpoint LSN Hi32", &u32, ValueUInt32);

   if (read_uint32("pgmoneta_management_read_info", ssl, socket, &u32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Checkpoint LSN Lo32", &u32, ValueUInt32);

   if (read_uint32("pgmoneta_management_read_info", ssl, socket, &u32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Start timeline", &u32, ValueUInt32);

   if (read_uint32("pgmoneta_management_read_info", ssl, socket, &u32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "End timeline", &u32, ValueUInt32);

   pgmoneta_json_put(output, "info", info, ValueObject);

   free(label);
   free(wal);

   return json;

error:

   free(label);
   free(wal);

   if (json != NULL)
   {
      set_command_json_object_faulty(json, strerror(errno));
   }

   errno = 0;

   return json;
}

static struct json*
read_list_backup_json(SSL* ssl, int socket, char* server)
{
   char* name = NULL;
   int srv;
   int32_t number_of_backups;
   bool keep;
   int8_t valid;
   uint64_t backup_size;
   uint64_t restore_size;
   uint64_t wal_size;
   uint64_t delta_size;
   char* bck = NULL;
   char* res = NULL;
   char* ws = NULL;
   char* ds = NULL;
   struct json* json = NULL;
   struct json* output = NULL;
   struct json* status = NULL;
   struct json* backups_array = NULL;
   json = create_new_command_json_object("list-backup", true, "pgmoneta-cli");
   pgmoneta_json_init(&status);
   if (status == NULL || json == NULL)
   {
      goto error;
   }
   pgmoneta_json_init(&backups_array);
   if (backups_array == NULL)
   {
      goto error;
   }

   output = extract_command_output_json_object(json);
   if (read_int32("pgmoneta_management_read_list_backup", ssl, socket, &srv))
   {
      goto error;
   }
   pgmoneta_json_put(status, "Server", (srv == -1 ? "Unknown" : server), ValueString);
   if (srv != -1)
   {
      if (read_int32("pgmoneta_management_read_list_backup", ssl, socket, &number_of_backups))
      {
         goto error;
      }
      pgmoneta_json_put(status, "Number of backups", &number_of_backups, ValueInt32);
      for (int i = 0; i < number_of_backups; i++)
      {
         if (read_string("pgmoneta_management_read_list_backup", ssl, socket, &name))
         {
            pgmoneta_log_error("read error in name");
            goto error;
         }

         if (read_bool("pgmoneta_management_read_list_backup", ssl, socket, &keep))
         {
            goto error;
         }

         if (read_byte("pgmoneta_management_read_list_backup", ssl, socket, &valid))
         {
            goto error;
         }

         if (read_uint64("pgmoneta_management_read_list_backup", ssl, socket, &backup_size))
         {
            pgmoneta_log_error("read error in backup_size");
            goto error;
         }

         bck = pgmoneta_bytes_to_string(backup_size);

         if (read_uint64("pgmoneta_management_read_list_backup", ssl, socket, &restore_size))
         {
            pgmoneta_log_error("read error in restore_size");
            goto error;
         }

         res = pgmoneta_bytes_to_string(restore_size);

         if (read_uint64("pgmoneta_management_read_list_backup", ssl, socket, &wal_size))
         {
            pgmoneta_log_error("read error in wal_size");
            goto error;
         }

         ws = pgmoneta_bytes_to_string(wal_size);

         if (read_uint64("pgmoneta_management_read_list_backup", ssl, socket, &delta_size))
         {
            pgmoneta_log_error("read error in delta_size");
            goto error;
         }

         ds = pgmoneta_bytes_to_string(delta_size);
         struct json* backup = NULL;
         pgmoneta_json_init(&backup);
         pgmoneta_json_put(backup, "Backup name", name, ValueString);

         if (valid != VALID_UNKNOWN)
         {
            pgmoneta_json_put(backup, "Backup", bck, ValueString);
            pgmoneta_json_put(backup, "Restore", res, ValueString);
            pgmoneta_json_put(backup, "WAL", ws, ValueString);
            pgmoneta_json_put(backup, "Delta", ds, ValueString);
            pgmoneta_json_put(backup, "Retain", keep ? "Yes" : "No", ValueString);
            pgmoneta_json_put(backup, "Valid", valid == VALID_TRUE ? "Yes" : "No", ValueString);
         }
         else
         {
            pgmoneta_json_put(backup, "Backup", "UNKNOWN", ValueString);
            pgmoneta_json_put(backup, "Restore", "UNKNOWN", ValueString);
            pgmoneta_json_put(backup, "WAL", "UNKNOWN", ValueString);
            pgmoneta_json_put(backup, "Delta", "UNKNOWN", ValueString);
            pgmoneta_json_put(backup, "Retain", "UNKNOWN", ValueString);
            pgmoneta_json_put(backup, "Valid", "UNKNOWN", ValueString);
         }

         free(bck);
         bck = NULL;

         free(res);
         res = NULL;

         free(ws);
         ws = NULL;

         free(ds);
         ds = NULL;

         free(name);
         name = NULL;
         pgmoneta_json_append(backups_array, backup, ValueObject);
      }
   }
   else
   {
      int num = 0;
      pgmoneta_json_put(status, "Number of backups", &num, ValueInt32);
   }
   pgmoneta_json_put(status, "backups", backups_array, ValueObject);
   pgmoneta_json_put(output, "status", status, ValueObject);
   return json;

error:
   free(bck);
   free(res);
   free(ws);
   free(ds);
   free(name);
   // return json anyway with error code set
   if (json != NULL)
   {
      set_command_json_object_faulty(json, strerror(errno));
   }
   errno = 0;
   return json;
}

static int
print_status_json(struct json* json)
{
   struct json* server = NULL;
   if (!json)
   {
      return 1;
   }

   if (!json_command_name_equals_to(json, "status"))
   {
      return 1;
   }

   struct json* output = extract_command_output_json_object(json);

   struct json* status = pgmoneta_json_get_json_object(output, "status");
   if (status == NULL)
   {
      return 1;
   }

   char* mode = pgmoneta_json_get_string(status, "Mode");
   if (mode != NULL)
   {
      printf("Mode             : %s\n", mode);
   }

   char* used_space = pgmoneta_json_get_string(status, "Used space");
   if (used_space != NULL)
   {
      printf("Used space       : %s\n", used_space);
   }

   char* free_space = pgmoneta_json_get_string(status, "Free space");
   if (free_space != NULL)
   {
      printf("Free space       : %s\n", free_space);
   }

   char* total_space = pgmoneta_json_get_string(status, "Total space");
   if (total_space != NULL)
   {
      printf("Total space      : %s\n", total_space);
   }

   int32_t workers = pgmoneta_json_get_int32(status, "Workers");
   printf("Workers          : %" PRId32 "\n", workers);

   int32_t num_servers = pgmoneta_json_get_int32(status, "Number of servers");
   printf("Number of servers: %" PRId32 "\n", num_servers);

   struct json* servers = pgmoneta_json_get_json_object(status, "servers");
   if (servers != NULL)
   {
      for (int i = 0; i < pgmoneta_json_array_length(servers); i++)
      {
         server = pgmoneta_json_array_get(servers, i);
         printf("Server           : %s\n", pgmoneta_json_get_string(server, "Server"));
         printf("  Retention      : ");
         printf("%" PRId32 " day(s) ", pgmoneta_json_get_int32(server, "Retention days"));
         int32_t weeks = pgmoneta_json_get_int32(server, "Retention weeks");
         if (weeks != 0)
         {
            printf("%" PRId32 " week(s) ", weeks);
         }
         int32_t months = pgmoneta_json_get_int32(server, "Retention months");
         if (months != 0)
         {
            printf("%" PRId32 " month(s) ", months);
         }
         int32_t years = pgmoneta_json_get_int32(server, "Retention years");
         if (years != 0)
         {
            printf("%" PRId32 " year(s) ", pgmoneta_json_get_int32(server, "Retention years"));
         }
         printf("\n");
         printf("  Workers        : %" PRId32 "\n", pgmoneta_json_get_int32(server, "Workers"));
         printf("  Backups        : %" PRId32 "\n", pgmoneta_json_get_int32(server, "Backups"));
         printf("  Space          : %s\n", pgmoneta_json_get_string(server, "Space"));
         printf("  Hot standby    : %s\n", pgmoneta_json_get_string(server, "Hot standby"));
      }
   }
   return 0;
}

static int
print_details_json(struct json* json)
{
   struct json* server = NULL;
   struct json* backup = NULL;
   char* valid = NULL;
   if (!json)
   {
      return 1;
   }

   if (!json_command_name_equals_to(json, "details"))
   {
      return 1;
   }

   struct json* output = extract_command_output_json_object(json);

   struct json* status = pgmoneta_json_get_json_object(output, "status");
   if (status == NULL)
   {
      return 1;
   }

   char* mode = pgmoneta_json_get_string(status, "Mode");
   if (mode != NULL)
   {
      printf("Mode             : %s\n", mode);
   }

   char* used_space = pgmoneta_json_get_string(status, "Used space");
   if (used_space != NULL)
   {
      printf("Used space       : %s\n", used_space);
   }

   char* free_space = pgmoneta_json_get_string(status, "Free space");
   if (free_space != NULL)
   {
      printf("Free space       : %s\n", free_space);
   }

   char* total_space = pgmoneta_json_get_string(status, "Total space");
   if (total_space != NULL)
   {
      printf("Total space      : %s\n", total_space);
   }

   int32_t workers = pgmoneta_json_get_int32(status, "Workers");
   printf("Workers          : %" PRId32 "\n", workers);

   int32_t num_servers = pgmoneta_json_get_int32(status, "Number of servers");
   printf("Number of servers: %" PRId32 "\n", num_servers);

   struct json* servers = pgmoneta_json_get_json_object(status, "servers");
   if (servers != NULL)
   {
      for (int i = 0; i < pgmoneta_json_array_length(servers); i++)
      {
         server = pgmoneta_json_array_get(servers, i);
         printf("Server           : %s\n", pgmoneta_json_get_string(server, "Server"));
         printf("  Retention      : ");
         printf("%" PRId32 " day(s) ", pgmoneta_json_get_int32(server, "Retention days"));
         int32_t weeks = pgmoneta_json_get_int32(server, "Retention weeks");
         if (weeks != 0)
         {
            printf("%" PRId32 " week(s) ", weeks);
         }
         int32_t months = pgmoneta_json_get_int32(server, "Retention months");
         if (months != 0)
         {
            printf("%" PRId32 " month(s) ", months);
         }
         int32_t years = pgmoneta_json_get_int32(server, "Retention years");
         if (years != 0)
         {
            printf("%" PRId32 " year(s) ", pgmoneta_json_get_int32(server, "Retention years"));
         }
         printf("\n");
         printf("  Workers        : %" PRId32 "\n", pgmoneta_json_get_int32(server, "Workers"));
         printf("  Backups        : %" PRId32 "\n", pgmoneta_json_get_int32(server, "Backups"));

         struct json* backups = pgmoneta_json_get_json_object(server, "backups");
         if (backups != NULL)
         {
            for (int j = 0; j < pgmoneta_json_array_length(backups); j++)
            {
               backup = pgmoneta_json_array_get(backups, j);
               valid = pgmoneta_json_get_string(backup, "Valid");
               if (pgmoneta_compare_string(valid, "Unknown"))
               {
                  printf("                   %s (Unknown)\n", pgmoneta_json_get_string(backup, "Backup name"));
               }
               else
               {
                  printf("                   %s (Backup: %s Restore: %s WAL: %s Delta: %s Retain: %s Valid: %s)\n",
                         pgmoneta_json_get_string(backup, "Backup name"),
                         pgmoneta_json_get_string(backup, "Backup"),
                         pgmoneta_json_get_string(backup, "Restore"),
                         pgmoneta_json_get_string(backup, "WAL"),
                         pgmoneta_json_get_string(backup, "Delta"),
                         pgmoneta_json_get_string(backup, "Retain"),
                         pgmoneta_json_get_string(backup, "Valid"));
               }
            }
            printf("  Space          : %s\n", pgmoneta_json_get_string(server, "Space"));
            printf("  Hot standby    : %s\n", pgmoneta_json_get_string(server, "Hot standby"));
         }
      }
   }
   return 0;
}

static int
print_delete_json(struct json* json)
{
   struct json* backup = NULL;
   char* srv = NULL;
   int32_t number_of_backups = 0;
   if (!json || is_command_json_object_faulty(json))
   {
      return 1;
   }

   if (!json_command_name_equals_to(json, "delete"))
   {
      return 1;
   }

   struct json* output = extract_command_output_json_object(json);

   struct json* status = pgmoneta_json_get_json_object(output, "status");
   if (status == NULL)
   {
      return 1;
   }
   srv = pgmoneta_json_get_string(status, "Server");
   if (srv == NULL)
   {
      return 1;
   }
   printf("Server           : %s\n", srv);
   if (!pgmoneta_compare_string(srv, "Unknown"))
   {
      number_of_backups = pgmoneta_json_get_int32(status, "Number of backups");
      printf("Number of backups: %" PRId32 "\n", number_of_backups);

      if (number_of_backups > 0)
      {
         printf("Backup           :\n");
         struct json* backups = pgmoneta_json_get_json_object(status, "backups");
         if (backups != NULL)
         {
            for (int i = 0; i < pgmoneta_json_array_length(backups); i++)
            {
               backup = pgmoneta_json_array_get(backups, i);
               printf("                   %s\n", pgmoneta_json_get_string(backup, "Backup name"));
            }
         }
      }
   }
   return 0;
}

static int
print_list_backup_json(struct json* json)
{
   struct json* backup = NULL;
   char* valid = NULL;
   char* srv = NULL;
   int64_t number_of_backups = 0;
   if (!json || is_command_json_object_faulty(json))
   {
      return 1;
   }

   if (!json_command_name_equals_to(json, "list-backup"))
   {
      return 1;
   }

   struct json* output = extract_command_output_json_object(json);

   struct json* status = pgmoneta_json_get_json_object(output, "status");
   if (status == NULL)
   {
      return 1;
   }
   srv = pgmoneta_json_get_string(status, "Server");
   if (srv == NULL)
   {
      return 1;
   }

   number_of_backups = pgmoneta_json_get_int32(status, "Number of backups");

   printf("Server           : %s\n", srv);
   if (!pgmoneta_compare_string(srv, "Unknown"))
   {
      printf("Number of backups: %" PRId64 "\n", number_of_backups);

      struct json* backups = pgmoneta_json_get_json_object(status, "backups");
      if (number_of_backups > 0)
      {
         printf("Backup           :\n");
      }
      for (int i = 0; i < pgmoneta_json_array_length(backups); i++)
      {
         backup = pgmoneta_json_array_get(backups, i);
         valid = pgmoneta_json_get_string(backup, "Valid");
         if (pgmoneta_compare_string(valid, "Unknown"))
         {
            printf("                   %s (Unknown)\n", pgmoneta_json_get_string(backup, "Backup name"));
         }
         else
         {
            printf("                   %s (Backup: %s Restore: %s WAL: %s Delta: %s Retain: %s Valid: %s)\n",
                   pgmoneta_json_get_string(backup, "Backup name"),
                   pgmoneta_json_get_string(backup, "Backup"),
                   pgmoneta_json_get_string(backup, "Restore"),
                   pgmoneta_json_get_string(backup, "WAL"),
                   pgmoneta_json_get_string(backup, "Delta"),
                   pgmoneta_json_get_string(backup, "Retain"),
                   pgmoneta_json_get_string(backup, "Valid"));
         }
      }
   }

   return 0;
}

static int
print_info_json(struct json* json)
{
   struct json* output = NULL;
   struct json* info = NULL;
   struct json* tablespaces = NULL;
   struct json* tablespace = NULL;
   char* label = NULL;
   char* wal = NULL;
   uint64_t number_of_tablespaces;

   if (!json)
   {
      return 1;
   }

   if (!json_command_name_equals_to(json, "info"))
   {
      return 1;
   }

   output = extract_command_output_json_object(json);

   info = pgmoneta_json_get_json_object(output, "info");

   if (info == NULL)
   {
      return 1;
   }

   label = pgmoneta_json_get_string(info, "Label");
   if (label != NULL)
   {
      printf("Label                : %s\n", label);
   }

   wal = pgmoneta_json_get_string(info, "WAL");
   if (label != NULL)
   {
      printf("WAL                  : %s\n", wal);
   }

   printf("Backup size          : %" PRId64 "\n", pgmoneta_json_get_uint64(info, "Backup size"));
   printf("Restore size         : %" PRId64 "\n", pgmoneta_json_get_uint64(info, "Restore size"));
   printf("Elapsed time         : %d\n", (int)pgmoneta_json_get_int32(info, "Elapsed time"));
   printf("Version              : %d\n", (int)pgmoneta_json_get_int32(info, "Version"));
   printf("Minor version        : %d\n", (int)pgmoneta_json_get_int32(info, "Minor version"));
   printf("Keep                 : %d\n", (bool)pgmoneta_json_get_bool(info, "Keep"));
   printf("Valid                : %d\n", (char)pgmoneta_json_get_bool(info, "Valid"));

   number_of_tablespaces = (uint64_t)pgmoneta_json_get_int64(info, "Number of tablespaces");
   printf("Number of tablespaces: %" PRId64 "\n", number_of_tablespaces);

   tablespaces = pgmoneta_json_get_json_object(info, "Table spaces");
   if (tablespaces != NULL)
   {
      for (int i = 0; i < pgmoneta_json_array_length(tablespaces); i++)
      {
         tablespace = pgmoneta_json_array_get(tablespaces, i);
         printf("Table space          : %s\n", pgmoneta_json_get_string(tablespace, "Table space"));
      }
   }

   printf("Start LSN Hi32       : %X\n", pgmoneta_json_get_uint32(info, "Start LSN Hi32"));
   printf("Start LSN Lo32       : %X\n", pgmoneta_json_get_uint32(info, "Start LSN Lo32"));

   printf("End LSN Hi32         : %X\n", pgmoneta_json_get_uint32(info, "End LSN Hi32"));
   printf("End LSN Lo32         : %X\n", pgmoneta_json_get_uint32(info, "End LSN Lo32"));

   printf("Checkpoint LSN Hi32  : %X\n", pgmoneta_json_get_uint32(info, "Checkpoint LSN Hi32"));
   printf("Checkpoint LSN Lo32  : %X\n", pgmoneta_json_get_uint32(info, "Checkpoint LSN Lo32"));

   printf("Start timeline       : %u\n", pgmoneta_json_get_uint32(info, "Start timeline"));
   printf("End timeline         : %u\n", pgmoneta_json_get_uint32(info, "End timeline"));

   return 0;

}

static struct json*
create_new_command_json_object(char* command_name, bool success, char* executable_name)
{
   struct json* json = NULL;
   struct json* command = NULL;
   struct json* output = NULL;
   struct json* application = NULL;
   // root of the JSON structure
   pgmoneta_json_init(&json);

   if (!json)
   {
      goto error;
   }

   // the command structure
   pgmoneta_json_init(&command);
   if (!command)
   {
      goto error;
   }

   pgmoneta_json_put(command, JSON_TAG_COMMAND_NAME, command_name, ValueString);
   pgmoneta_json_put(command, JSON_TAG_COMMAND_STATUS, success ? JSON_STRING_SUCCESS : JSON_STRING_ERROR, ValueString);
   pgmoneta_json_put(command, JSON_TAG_COMMAND_ERROR, success ? JSON_BOOL_SUCCESS : JSON_BOOL_ERROR, ValueString);
   pgmoneta_json_put(command, JSON_TAG_APPLICATION_VERSION, VERSION, ValueString);

   // the output of the command, this has to be filled by the caller
   pgmoneta_json_init(&output);
   if (!output)
   {
      goto error;
   }

   pgmoneta_json_put(command, JSON_TAG_COMMAND_OUTPUT, output, ValueObject);

   pgmoneta_json_init(&application);
   if (!application)
   {
      goto error;
   }
   pgmoneta_json_put(application, JSON_TAG_APPLICATION_NAME, executable_name, ValueString);
   // add objects to the whole json thing
   pgmoneta_json_put(json, "command", command, ValueObject);
   pgmoneta_json_put(json, "application", application, ValueObject);

   return json;
error:
   if (json)
   {
      pgmoneta_json_free(json);
   }

   return NULL;
}

static struct json*
extract_command_output_json_object(struct json* json)
{
   struct json* command = pgmoneta_json_get_json_object(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   return pgmoneta_json_get_json_object(command, JSON_TAG_COMMAND_OUTPUT);

error:
   return NULL;
}

static bool
json_command_name_equals_to(struct json* json, char* command_name)
{
   if (!json || !command_name || strlen(command_name) <= 0)
   {
      goto error;
   }

   struct json* command = pgmoneta_json_get_json_object(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   char* cName = pgmoneta_json_get_string(command, JSON_TAG_COMMAND_NAME);
   if (!cName)
   {
      goto error;
   }

   return !strncmp(command_name, cName, MISC_LENGTH);

error:
   return false;
}

static int
set_command_json_object_faulty(struct json* json, char* message)
{
   if (!json)
   {
      goto error;
   }

   struct json* command = pgmoneta_json_get_json_object(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   pgmoneta_json_put(command, JSON_TAG_COMMAND_STATUS, message, ValueString);
   pgmoneta_json_put(command, JSON_TAG_COMMAND_ERROR, JSON_BOOL_ERROR, ValueString);

   return 0;

error:
   return 1;
}

static bool
is_command_json_object_faulty(struct json* json)
{
   if (!json)
   {
      goto error;
   }

   struct json* command = pgmoneta_json_get_json_object(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   int64_t status = pgmoneta_json_get_int64(command, JSON_TAG_COMMAND_ERROR);

   return status == 0 ? false : true;

error:
   return false;
}

static void
print_and_free_json_object(struct json* json)
{
   pgmoneta_json_print(json, 2);
   pgmoneta_json_free(json);
}