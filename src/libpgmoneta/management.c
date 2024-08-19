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
#include <management.h>
#include <network.h>
#include <stdint.h>
#include <utils.h>
#include <verify.h>

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
static int print_verify_json(struct json* json);
static int print_annotate_json(struct json* json);
static struct json* read_status_json(SSL* ssl, int socket);
static struct json* read_details_json(SSL* ssl, int socket);
static struct json* read_list_backup_json(SSL* ssl, int socket, char* server);
static struct json* read_delete_json(SSL* ssl, int socket, char* server, char* backup_id);
static struct json* read_info_json(SSL* ssl, int socket);
static struct json* read_verify_json(SSL* ssl, int socket);
static struct json* read_annotate_json(SSL* ssl, int socket);
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
pgmoneta_management_read_payload(int socket, signed char id, char** payload_s1, char** payload_s2, char** payload_s3, char** payload_s4, char** payload_s5)
{
   *payload_s1 = NULL;
   *payload_s2 = NULL;
   *payload_s3 = NULL;
   *payload_s4 = NULL;
   *payload_s5 = NULL;

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
      case MANAGEMENT_VERIFY:
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
      case MANAGEMENT_ANNOTATE:
         read_string("pgmoneta_management_read_payload", NULL, socket, payload_s1);
         read_string("pgmoneta_management_read_payload", NULL, socket, payload_s2);
         read_string("pgmoneta_management_read_payload", NULL, socket, payload_s3);
         read_string("pgmoneta_management_read_payload", NULL, socket, payload_s4);
         read_string("pgmoneta_management_read_payload", NULL, socket, payload_s5);
         break;
      default:
         goto error;
   }

   pgmoneta_log_trace("Management: %d", id);
   pgmoneta_log_trace("Payload 1: %s", *payload_s1);
   pgmoneta_log_trace("Payload 2: %s", *payload_s2);
   pgmoneta_log_trace("Payload 4: %s", *payload_s3);
   pgmoneta_log_trace("Payload 4: %s", *payload_s4);
   pgmoneta_log_trace("Payload 5: %s", *payload_s5);

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
      pgmoneta_json_destroy(json);
   }
   return 0;
error:
   if (json != NULL)
   {
      pgmoneta_json_destroy(json);
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
pgmoneta_management_verify(SSL* ssl, int socket, char* server, char* backup_id, char* directory, char* files)
{
   if (write_header(ssl, socket, MANAGEMENT_VERIFY))
   {
      pgmoneta_log_warn("pgmoneta_management_verify: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_verify", ssl, socket, server))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_verify", ssl, socket, backup_id))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_verify", ssl, socket, directory))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_verify", ssl, socket, files))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_read_verify(SSL* ssl, int socket, char output_format)
{
   struct json* json = read_verify_json(ssl, socket);

   if (json == NULL)
   {
      goto error;
   }

   if (output_format == COMMAND_OUTPUT_FORMAT_TEXT)
   {
      if (print_verify_json(json))
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
      pgmoneta_json_destroy(json);
   }

   return 0;

error:

   if (json != NULL)
   {
      pgmoneta_json_destroy(json);
   }
   return 1;
}

int
pgmoneta_management_write_verify(SSL* ssl, int socket, struct deque* failed, struct deque* all)
{
   struct deque_node* entry = NULL;

   if (write_int32("pgmoneta_management_write_verify", ssl, socket, (int)pgmoneta_deque_size(failed)))
   {
      goto error;
   }

   entry = pgmoneta_deque_head(failed);
   while (entry != NULL)
   {
      struct verify_entry* ve = (struct verify_entry*)pgmoneta_value_data(entry->data);

      if (write_string("pgmoneta_management_verify", ssl, socket, ve->filename))
      {
         goto error;
      }

      if (write_string("pgmoneta_management_verify", ssl, socket, ve->original))
      {
         goto error;
      }

      if (write_string("pgmoneta_management_verify", ssl, socket, ve->calculated))
      {
         goto error;
      }

      entry = pgmoneta_deque_next(failed, entry);
   }

   if (write_int32("pgmoneta_management_write_verify", ssl, socket, (int)pgmoneta_deque_size(all)))
   {
      goto error;
   }

   entry = pgmoneta_deque_head(all);
   while (entry != NULL)
   {
      struct verify_entry* ve = (struct verify_entry*)pgmoneta_value_data(entry->data);

      if (write_string("pgmoneta_management_verify", ssl, socket, ve->filename))
      {
         goto error;
      }
      if (write_string("pgmoneta_management_verify", ssl, socket, ve->original))
      {
         goto error;
      }

      entry = pgmoneta_deque_next(all, entry);
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
      pgmoneta_json_destroy(json);
   }
   return 0;
error:
   if (json != NULL)
   {
      pgmoneta_json_destroy(json);
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
      pgmoneta_json_destroy(json);
   }
   return 0;
error:
   if (json != NULL)
   {
      pgmoneta_json_destroy(json);
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
      pgmoneta_json_destroy(json);
   }
   return 0;
error:
   if (json != NULL)
   {
      pgmoneta_json_destroy(json);
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
      pgmoneta_json_destroy(json);
   }

   return 0;

error:

   if (json != NULL)
   {
      pgmoneta_json_destroy(json);
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

   if (number_of_backups == 0)
   {
      goto error;
   }

   if (!strcmp("oldest", backup))
   {
      bck = backups[0];
   }
   else if (!strcmp("newest", backup) || !strcmp("latest", backup))
   {
      bck = backups[number_of_backups - 1];
   }
   else
   {
      for (int i = 0; bck == NULL && i < number_of_backups; i++)
      {
         if (!strcmp(backups[i]->label, backup))
         {
            bck = backups[i];
         }
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

   if (write_string("pgmoneta_management_write_info", ssl, socket, bck->comments))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_write_info", ssl, socket, bck->extra))
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
pgmoneta_management_annotate(SSL* ssl, int socket, char* server, char* backup, char* command, char* key, char* comment)
{
   if (write_header(ssl, socket, MANAGEMENT_ANNOTATE))
   {
      pgmoneta_log_warn("pgmoneta_management_annotate: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_annotate", ssl, socket, server))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_annotate", ssl, socket, backup))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_annotate", ssl, socket, command))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_annotate", ssl, socket, key))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_annotate", ssl, socket, comment))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_read_annotate(SSL* ssl, int socket, char output_format)
{
   struct json* json = read_annotate_json(ssl, socket);

   if (json == NULL)
   {
      goto error;
   }

   if (output_format == COMMAND_OUTPUT_FORMAT_TEXT)
   {
      if (print_annotate_json(json))
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
      pgmoneta_json_destroy(json);
   }

   return 0;

error:

   if (json != NULL)
   {
      pgmoneta_json_destroy(json);
   }
   return 1;
}

int
pgmoneta_management_write_annotate(SSL* ssl, int socket, char* server, char* backup, char* comment)
{
   if (write_string("pgmoneta_management_write_annotate", ssl, socket, server))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_write_annotate", ssl, socket, backup))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_write_annotate", ssl, socket, comment))
   {
      goto error;
   }

   return 0;

error:

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
   time_t t;

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
      time(&t);
      atomic_store(&config->servers[srv].last_operation_time, (long long)t);
      atomic_fetch_add(&config->servers[srv].operation_count, 1);
      if (code)
      {
         atomic_fetch_add(&config->servers[srv].failed_operation_count, 1);
         atomic_store(&config->servers[srv].last_failed_operation_time, (long long)t);
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
   pgmoneta_json_create(&status);
   if (status == NULL || json == NULL)
   {
      goto error;
   }

   output = extract_command_output_json_object(json);

   if (read_bool("pgmoneta_management_read_status", ssl, socket, &offline))
   {
      goto error;
   }
   pgmoneta_json_put(status, "Mode", (uintptr_t)(offline == 0 ? "Running" : "Offline"), ValueString);

   if (read_uint64("pgmoneta_management_read_status", ssl, socket, &used_size))
   {
      goto error;
   }
   size_string = pgmoneta_bytes_to_string(used_size);
   pgmoneta_json_put(status, "Used space", (uintptr_t)size_string, ValueString);
   free(size_string);

   if (read_uint64("pgmoneta_management_read_status", ssl, socket, &free_size))
   {
      goto error;
   }
   size_string = pgmoneta_bytes_to_string(free_size);
   pgmoneta_json_put(status, "Free space", (uintptr_t)size_string, ValueString);
   free(size_string);

   if (read_uint64("pgmoneta_management_read_status", ssl, socket, &total_size))
   {
      goto error;
   }
   size_string = pgmoneta_bytes_to_string(total_size);
   pgmoneta_json_put(status, "Total space", (uintptr_t)size_string, ValueString);
   free(size_string);

   if (read_int32("pgmoneta_management_read_status", ssl, socket, &workers))
   {
      goto error;
   }

   pgmoneta_json_put(status, "Workers", (uintptr_t)workers, ValueInt32);

   if (read_int32("pgmoneta_management_read_status", ssl, socket, &num_servers))
   {
      goto error;
   }
   pgmoneta_json_put(status, "Number of servers", (uintptr_t)num_servers, ValueInt32);

   pgmoneta_json_create(&servers_array);
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
      pgmoneta_json_create(&server);
      pgmoneta_json_put(server, "Server", (uintptr_t)name, ValueString);
      pgmoneta_json_put(server, "Retention days", (uintptr_t)retention_days, ValueInt32);
      if (retention_weeks != -1)
      {
         pgmoneta_json_put(server, "Retention weeks", (uintptr_t)retention_weeks, ValueInt32);
      }
      if (retention_months != -1)
      {
         pgmoneta_json_put(server, "Retention months", (uintptr_t)retention_months, ValueInt32);
      }
      if (retention_years != -1)
      {
         pgmoneta_json_put(server, "Retention years", (uintptr_t)retention_years, ValueInt32);
      }
      pgmoneta_json_put(server, "Backups", (uintptr_t)number_of_directories, ValueInt32);
      pgmoneta_json_put(server, "Space", (uintptr_t)size_string, ValueString);
      pgmoneta_json_put(server, "Hot standby", (uintptr_t)hot_standby_size_string, ValueString);
      pgmoneta_json_put(server, "Workers", (uintptr_t)workers, ValueInt32);

      pgmoneta_json_append(servers_array, (uintptr_t)server, ValueJSON);

      free(size_string);
      size_string = NULL;

      free(hot_standby_size_string);
      hot_standby_size_string = NULL;

      free(name);
      name = NULL;
   }

   pgmoneta_json_put(status, "servers", (uintptr_t)servers_array, ValueJSON);
   pgmoneta_json_put(output, "status", (uintptr_t)status, ValueJSON);

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
   pgmoneta_json_create(&status);
   if (status == NULL || json == NULL)
   {
      goto error;
   }

   output = extract_command_output_json_object(json);

   if (read_bool("pgmoneta_management_read_details", ssl, socket, &offline))
   {
      goto error;
   }
   pgmoneta_json_put(status, "Mode", (uintptr_t)(offline == 0 ? "Running" : "Offline"), ValueString);

   if (read_uint64("pgmoneta_management_read_details", ssl, socket, &used_size))
   {
      goto error;
   }
   size_string = pgmoneta_bytes_to_string(used_size);
   pgmoneta_json_put(status, "Used space", (uintptr_t)size_string, ValueString);
   free(size_string);

   if (read_uint64("pgmoneta_management_read_details", ssl, socket, &free_size))
   {
      goto error;
   }
   size_string = pgmoneta_bytes_to_string(free_size);
   pgmoneta_json_put(status, "Free space", (uintptr_t)size_string, ValueString);
   free(size_string);

   if (read_uint64("pgmoneta_management_read_details", ssl, socket, &total_size))
   {
      goto error;
   }
   size_string = pgmoneta_bytes_to_string(total_size);
   pgmoneta_json_put(status, "Total space", (uintptr_t)size_string, ValueString);
   free(size_string);

   if (read_int32("pgmoneta_management_read_status", ssl, socket, &workers))
   {
      goto error;
   }
   pgmoneta_json_put(status, "Workers", (uintptr_t)workers, ValueInt32);

   if (read_int32("pgmoneta_management_read_details", ssl, socket, &num_servers))
   {
      goto error;
   }
   pgmoneta_json_put(status, "Number of servers", (uintptr_t)num_servers, ValueInt32);
   pgmoneta_json_create(&servers_array);
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
      pgmoneta_json_create(&server);
      pgmoneta_json_put(server, "Server", (uintptr_t)name, ValueString);
      pgmoneta_json_put(server, "Retention days", (uintptr_t)retention_days, ValueInt32);
      if (retention_weeks != -1)
      {
         pgmoneta_json_put(server, "Retention weeks", (uintptr_t)retention_weeks, ValueInt32);
      }
      if (retention_months != -1)
      {
         pgmoneta_json_put(server, "Retention months", (uintptr_t)retention_months, ValueInt32);
      }
      if (retention_years != -1)
      {
         pgmoneta_json_put(server, "Retention years", (uintptr_t)retention_years, ValueInt32);
      }
      pgmoneta_json_put(server, "Backups", (uintptr_t)number_of_backups, ValueInt32);
      pgmoneta_json_put(server, "Space", (uintptr_t)size_string, ValueString);
      pgmoneta_json_put(server, "Workers", (uintptr_t)workers, ValueInt32);
      pgmoneta_json_put(server, "Hot standby", (uintptr_t)hot_standby_size_string, ValueString);
      free(size_string);
      size_string = NULL;

      free(hot_standby_size_string);
      hot_standby_size_string = NULL;

      free(name);
      name = NULL;

      struct json* backups_array = NULL;
      pgmoneta_json_create(&backups_array);
      if (backups_array == NULL)
      {
         goto error;
      }
      for (int j = 0; j < number_of_backups; j++)
      {
         struct json* backup = NULL;

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

         pgmoneta_json_create(&backup);
         pgmoneta_json_put(backup, "Backup name", (uintptr_t)name, ValueString);
         if (valid != VALID_UNKNOWN)
         {
            pgmoneta_json_put(backup, "Backup", (uintptr_t)bck, ValueString);
            pgmoneta_json_put(backup, "Restore", (uintptr_t)res, ValueString);
            pgmoneta_json_put(backup, "WAL", (uintptr_t)ws, ValueString);
            pgmoneta_json_put(backup, "Delta", (uintptr_t)ds, ValueString);
            pgmoneta_json_put(backup, "Retain", (uintptr_t)(keep ? "Yes" : "No"), ValueString);
            pgmoneta_json_put(backup, "Valid", (uintptr_t)(valid == VALID_TRUE ? "Yes" : "No"), ValueString);
         }
         else
         {
            pgmoneta_json_put(backup, "Backup", (uintptr_t)"UNKNOWN", ValueString);
            pgmoneta_json_put(backup, "Restore", (uintptr_t)"UNKNOWN", ValueString);
            pgmoneta_json_put(backup, "WAL", (uintptr_t)"UNKNOWN", ValueString);
            pgmoneta_json_put(backup, "Delta", (uintptr_t)"UNKNOWN", ValueString);
            pgmoneta_json_put(backup, "Retain", (uintptr_t)"UNKNOWN", ValueString);
            pgmoneta_json_put(backup, "Valid", (uintptr_t)"UNKNOWN", ValueString);
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
         pgmoneta_json_append(backups_array, (uintptr_t)backup, ValueJSON);
      }
      pgmoneta_json_put(server, "backups", (uintptr_t)backups_array, ValueJSON);
      pgmoneta_json_append(servers_array, (uintptr_t)server, ValueJSON);
   }
   pgmoneta_json_put(status, "servers", (uintptr_t)servers_array, ValueJSON);
   pgmoneta_json_put(output, "status", (uintptr_t)status, ValueJSON);

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
   pgmoneta_json_create(&status);

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

   pgmoneta_json_put(status, "Server", (uintptr_t)(srv == -1 ? "Unknown" : server), ValueString);

   if (srv != -1)
   {
      if (read_int32("pgmoneta_management_read_delete", ssl, socket, &number_of_backups))
      {
         goto error;
      }
      pgmoneta_json_put(status, "Number of backups", (uintptr_t)number_of_backups, ValueInt32);
      pgmoneta_json_create(&backups_array);
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
         pgmoneta_json_create(&backup);
         if (backup == NULL)
         {
            goto error;
         }
         pgmoneta_json_put(backup, "Backup name", (uintptr_t)name, ValueString);
         pgmoneta_json_append(backups_array, (uintptr_t)backup, ValueJSON);
         free(name);
         name = NULL;
      }
   }

   pgmoneta_json_put(status, "backups", (uintptr_t)backups_array, ValueJSON);
   pgmoneta_json_put(output, "status", (uintptr_t)status, ValueJSON);
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
   char* comments = NULL;
   char* extra = NULL;
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
   pgmoneta_json_create(&info);

   if (info == NULL || json == NULL)
   {
      goto error;
   }

   output = extract_command_output_json_object(json);

   if (read_string("pgmoneta_management_read_info", ssl, socket, &label))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Label", (uintptr_t)label, ValueString);

   if (read_string("pgmoneta_management_read_info", ssl, socket, &wal))
   {
      goto error;
   }

   pgmoneta_json_put(info, "WAL", (uintptr_t)wal, ValueString);

   if (read_uint64("pgmoneta_management_read_info", ssl, socket, &u64))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Backup size", (uintptr_t)u64, ValueUInt64);

   if (read_uint64("pgmoneta_management_read_info", ssl, socket, &u64))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Restore size", (uintptr_t)u64, ValueUInt64);

   if (read_int32("pgmoneta_management_read_info", ssl, socket, &i32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Elapsed time", (uintptr_t)i32, ValueInt32);

   if (read_int32("pgmoneta_management_read_info", ssl, socket, &i32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Version", (uintptr_t)i32, ValueInt32);

   if (read_int32("pgmoneta_management_read_info", ssl, socket, &i32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Minor version", (uintptr_t)i32, ValueInt32);

   if (read_bool("pgmoneta_management_read_info", ssl, socket, &sc))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Keep", (uintptr_t)sc, ValueBool);

   if (read_bool("pgmoneta_management_read_info", ssl, socket, &sc))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Valid", (uintptr_t)sc, ValueBool);

   if (read_uint64("pgmoneta_management_read_info", ssl, socket, &u64))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Number of tablespaces", (uintptr_t)u64, ValueUInt64);
   number_of_tablespaces = (uint64_t)u64;

   pgmoneta_json_create(&tbl_array);

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

      pgmoneta_json_create(&tbl);

      if (tbl == NULL)
      {
         goto error;
      }

      pgmoneta_json_put(tbl, "Table space", (uintptr_t)tablespace, ValueString);
      pgmoneta_json_append(tbl_array, (uintptr_t)tbl, ValueJSON);

      free(tablespace);
      tablespace = NULL;
   }

   pgmoneta_json_put(info, "Table spaces", (uintptr_t)tbl_array, ValueJSON);

   if (read_uint32("pgmoneta_management_read_info", ssl, socket, &u32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Start LSN Hi32", (uintptr_t)u32, ValueUInt32);

   if (read_uint32("pgmoneta_management_read_info", ssl, socket, &u32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Start LSN Lo32", (uintptr_t)u32, ValueUInt32);

   if (read_uint32("pgmoneta_management_read_info", ssl, socket, &u32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "End LSN Hi32", (uintptr_t)u32, ValueUInt32);

   if (read_uint32("pgmoneta_management_read_info", ssl, socket, &u32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "End LSN Lo32", (uintptr_t)u32, ValueUInt32);

   if (read_uint32("pgmoneta_management_read_info", ssl, socket, &u32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Checkpoint LSN Hi32", (uintptr_t)u32, ValueUInt32);

   if (read_uint32("pgmoneta_management_read_info", ssl, socket, &u32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Checkpoint LSN Lo32", (uintptr_t)u32, ValueUInt32);

   if (read_uint32("pgmoneta_management_read_info", ssl, socket, &u32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Start timeline", (uintptr_t)u32, ValueUInt32);

   if (read_uint32("pgmoneta_management_read_info", ssl, socket, &u32))
   {
      goto error;
   }

   pgmoneta_json_put(info, "End timeline", (uintptr_t)u32, ValueUInt32);

   if (read_string("pgmoneta_management_read_info", ssl, socket, &extra))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Extra", (uintptr_t)extra, ValueString);

   if (read_string("pgmoneta_management_read_info", ssl, socket, &comments))
   {
      goto error;
   }

   pgmoneta_json_put(info, "Comments", (uintptr_t)comments, ValueString);

   pgmoneta_json_put(output, "info", (uintptr_t)info, ValueJSON);

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
   pgmoneta_json_create(&status);
   if (status == NULL || json == NULL)
   {
      goto error;
   }
   pgmoneta_json_create(&backups_array);
   if (backups_array == NULL)
   {
      goto error;
   }

   output = extract_command_output_json_object(json);
   if (read_int32("pgmoneta_management_read_list_backup", ssl, socket, &srv))
   {
      goto error;
   }
   pgmoneta_json_put(status, "Server", (uintptr_t)(srv == -1 ? "Unknown" : server), ValueString);
   if (srv != -1)
   {
      if (read_int32("pgmoneta_management_read_list_backup", ssl, socket, &number_of_backups))
      {
         goto error;
      }
      pgmoneta_json_put(status, "Number of backups", (uintptr_t)number_of_backups, ValueInt32);
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
         pgmoneta_json_create(&backup);
         pgmoneta_json_put(backup, "Backup name", (uintptr_t)name, ValueString);

         if (valid != VALID_UNKNOWN)
         {
            pgmoneta_json_put(backup, "Backup", (uintptr_t)bck, ValueString);
            pgmoneta_json_put(backup, "Restore", (uintptr_t)res, ValueString);
            pgmoneta_json_put(backup, "WAL", (uintptr_t)ws, ValueString);
            pgmoneta_json_put(backup, "Delta", (uintptr_t)ds, ValueString);
            pgmoneta_json_put(backup, "Retain", (uintptr_t)(keep ? "Yes" : "No"), ValueString);
            pgmoneta_json_put(backup, "Valid", (uintptr_t)(valid == VALID_TRUE ? "Yes" : "No"), ValueString);
         }
         else
         {
            pgmoneta_json_put(backup, "Backup", (uintptr_t)"UNKNOWN", ValueString);
            pgmoneta_json_put(backup, "Restore", (uintptr_t)"UNKNOWN", ValueString);
            pgmoneta_json_put(backup, "WAL", (uintptr_t)"UNKNOWN", ValueString);
            pgmoneta_json_put(backup, "Delta", (uintptr_t)"UNKNOWN", ValueString);
            pgmoneta_json_put(backup, "Retain", (uintptr_t)"UNKNOWN", ValueString);
            pgmoneta_json_put(backup, "Valid", (uintptr_t)"UNKNOWN", ValueString);
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
         pgmoneta_json_append(backups_array, (uintptr_t)backup, ValueJSON);
      }
   }
   else
   {
      int num = 0;
      pgmoneta_json_put(status, "Number of backups", (uintptr_t)num, ValueInt32);
   }
   pgmoneta_json_put(status, "backups", (uintptr_t)backups_array, ValueJSON);
   pgmoneta_json_put(output, "status", (uintptr_t)status, ValueJSON);
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
read_verify_json(SSL* ssl, int socket)
{
   int32_t number_of_failed = 0;
   int32_t number_of_all = 0;
   struct json* json = NULL;
   struct json* output = NULL;
   struct json* verify = NULL;
   struct json* failed_array = NULL;
   struct json* all_array = NULL;

   json = create_new_command_json_object("verify", true, "pgmoneta-cli");
   pgmoneta_json_create(&verify);
   if (verify == NULL || json == NULL)
   {
      goto error;
   }

   output = extract_command_output_json_object(json);

   pgmoneta_json_create(&failed_array);
   pgmoneta_json_create(&all_array);

   if (read_int32("pgmoneta_management_read_verify", ssl, socket, &number_of_failed))
   {
      goto error;
   }
   pgmoneta_json_put(verify, "Failed", (uintptr_t)number_of_failed, ValueInt32);

   if (number_of_failed > 0)
   {
      for (int32_t i = 0; i < number_of_failed; i++)
      {
         char* filename = NULL;
         char* original = NULL;
         char* calculated = NULL;
         struct json* entry = NULL;

         pgmoneta_json_create(&entry);

         if (read_string("pgmoneta_management_read_verify", ssl, socket, &filename))
         {
            goto error;
         }
         pgmoneta_json_put(entry, "File name", (uintptr_t)filename, ValueString);

         if (read_string("pgmoneta_management_read_verify", ssl, socket, &original))
         {
            goto error;
         }
         pgmoneta_json_put(entry, "Original", (uintptr_t)original, ValueString);

         if (read_string("pgmoneta_management_read_verify", ssl, socket, &calculated))
         {
            goto error;
         }
         pgmoneta_json_put(entry, "Calculated", (uintptr_t)calculated, ValueString);
         pgmoneta_json_append(failed_array, (uintptr_t)entry, ValueJSON);
         free(filename);
         filename = NULL;

         free(original);
         original = NULL;

         free(calculated);
         calculated = NULL;
      }
   }

   if (read_int32("pgmoneta_management_read_verify", ssl, socket, &number_of_all))
   {
      goto error;
   }
   pgmoneta_json_put(verify, "All", (uintptr_t)number_of_all, ValueInt32);

   if (number_of_all > 0)
   {
      for (int32_t i = 0; i < number_of_all; i++)
      {
         char* filename = NULL;
         char* hash = NULL;
         struct json* entry = NULL;

         pgmoneta_json_create(&entry);

         if (read_string("pgmoneta_management_read_verify", ssl, socket, &filename))
         {
            goto error;
         }
         pgmoneta_json_put(entry, "File name", (uintptr_t)filename, ValueString);

         if (read_string("pgmoneta_management_read_verify", ssl, socket, &hash))
         {
            goto error;
         }
         pgmoneta_json_put(entry, "Hash", (uintptr_t)hash, ValueString);
         pgmoneta_json_append(all_array, (uintptr_t)entry, ValueJSON);
         free(filename);
         filename = NULL;

         free(hash);
         hash = NULL;
      }
   }

   pgmoneta_json_put(verify, "failed", (uintptr_t)failed_array, ValueJSON);
   pgmoneta_json_put(verify, "all", (uintptr_t)all_array, ValueJSON);
   pgmoneta_json_put(output, "verify", (uintptr_t)verify, ValueJSON);

   return json;

error:

   if (json != NULL)
   {
      set_command_json_object_faulty(json, strerror(errno));
   }

   errno = 0;

   return json;
}

static struct json*
read_annotate_json(SSL* ssl, int socket)
{
   char* server = NULL;
   char* backup = NULL;
   char* comment = NULL;
   struct json* json = NULL;
   struct json* output = NULL;
   struct json* annotate = NULL;

   json = create_new_command_json_object("annotate", true, "pgmoneta-cli");
   pgmoneta_json_create(&annotate);

   if (annotate == NULL || json == NULL)
   {
      goto error;
   }

   output = extract_command_output_json_object(json);

   if (read_string("pgmoneta_management_read_annotate", ssl, socket, &server))
   {
      goto error;
   }

   pgmoneta_json_put(annotate, "Server", (uintptr_t)server, ValueString);

   if (read_string("pgmoneta_management_read_annotate", ssl, socket, &backup))
   {
      goto error;
   }

   pgmoneta_json_put(annotate, "Backup", (uintptr_t)backup, ValueString);

   if (read_string("pgmoneta_management_read_annotate", ssl, socket, &comment))
   {
      goto error;
   }

   pgmoneta_json_put(annotate, "Comment", (uintptr_t)comment, ValueString);

   pgmoneta_json_put(output, "annotate", (uintptr_t)annotate, ValueJSON);

   free(server);
   free(backup);
   free(comment);

   return json;

error:

   free(server);
   free(backup);
   free(comment);

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

   struct json* status = (struct json*)pgmoneta_json_get(output, "status");
   if (status == NULL)
   {
      return 1;
   }

   char* mode = (char*)pgmoneta_json_get(status, "Mode");
   if (mode != NULL)
   {
      printf("Mode             : %s\n", mode);
   }

   char* used_space = (char*)pgmoneta_json_get(status, "Used space");
   if (used_space != NULL)
   {
      printf("Used space       : %s\n", used_space);
   }

   char* free_space = (char*)pgmoneta_json_get(status, "Free space");
   if (free_space != NULL)
   {
      printf("Free space       : %s\n", free_space);
   }

   char* total_space = (char*)pgmoneta_json_get(status, "Total space");
   if (total_space != NULL)
   {
      printf("Total space      : %s\n", total_space);
   }

   int32_t workers = pgmoneta_json_get(status, "Workers");
   printf("Workers          : %" PRId32 "\n", workers);

   int32_t num_servers = pgmoneta_json_get(status, "Number of servers");
   printf("Number of servers: %" PRId32 "\n", num_servers);

   struct json* servers = (struct json*)pgmoneta_json_get(status, "servers");
   if (servers != NULL)
   {
      struct json_iterator* iter = NULL;
      pgmoneta_json_iterator_create(servers, &iter);
      while (pgmoneta_json_iterator_next(iter))
      {
         server = (struct json*) pgmoneta_value_data(iter->value);
         printf("Server           : %s\n", (char*)pgmoneta_json_get(server, "Server"));
         printf("  Retention      : ");
         printf("%" PRId32 " day(s) ", (int32_t)pgmoneta_json_get(server, "Retention days"));
         int32_t weeks = (int32_t)pgmoneta_json_get(server, "Retention weeks");
         if (weeks != 0)
         {
            printf("%" PRId32 " week(s) ", weeks);
         }
         int32_t months = (int32_t)pgmoneta_json_get(server, "Retention months");
         if (months != 0)
         {
            printf("%" PRId32 " month(s) ", months);
         }
         int32_t years = (int32_t)pgmoneta_json_get(server, "Retention years");
         if (years != 0)
         {
            printf("%" PRId32 " year(s) ", (int32_t)pgmoneta_json_get(server, "Retention years"));
         }
         printf("\n");
         printf("  Workers        : %" PRId32 "\n", (int32_t)pgmoneta_json_get(server, "Workers"));
         printf("  Backups        : %" PRId32 "\n", (int32_t)pgmoneta_json_get(server, "Backups"));
         printf("  Space          : %s\n", (char*)pgmoneta_json_get(server, "Space"));
         printf("  Hot standby    : %s\n", (char*)pgmoneta_json_get(server, "Hot standby"));
      }
      pgmoneta_json_iterator_destroy(iter);
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

   struct json* status = (struct json*)pgmoneta_json_get(output, "status");
   if (status == NULL)
   {
      return 1;
   }

   char* mode = (char*)pgmoneta_json_get(status, "Mode");
   if (mode != NULL)
   {
      printf("Mode             : %s\n", mode);
   }

   char* used_space = (char*)pgmoneta_json_get(status, "Used space");
   if (used_space != NULL)
   {
      printf("Used space       : %s\n", used_space);
   }

   char* free_space = (char*)pgmoneta_json_get(status, "Free space");
   if (free_space != NULL)
   {
      printf("Free space       : %s\n", free_space);
   }

   char* total_space = (char*)pgmoneta_json_get(status, "Total space");
   if (total_space != NULL)
   {
      printf("Total space      : %s\n", total_space);
   }

   int32_t workers = (int32_t)pgmoneta_json_get(status, "Workers");
   printf("Workers          : %" PRId32 "\n", workers);

   int32_t num_servers = (int32_t)pgmoneta_json_get(status, "Number of servers");
   printf("Number of servers: %" PRId32 "\n", num_servers);

   struct json* servers = (struct json*)pgmoneta_json_get(status, "servers");
   if (servers != NULL)
   {
      struct json_iterator* server_iter = NULL;
      pgmoneta_json_iterator_create(servers, &server_iter);
      while (pgmoneta_json_iterator_next(server_iter))
      {
         server = (struct json*)pgmoneta_value_data(server_iter->value);
         printf("Server           : %s\n", (char*)pgmoneta_json_get(server, "Server"));
         printf("  Retention      : ");
         printf("%" PRId32 " day(s) ", (int32_t)pgmoneta_json_get(server, "Retention days"));
         int32_t weeks = (int32_t)pgmoneta_json_get(server, "Retention weeks");
         if (weeks != 0)
         {
            printf("%" PRId32 " week(s) ", weeks);
         }
         int32_t months = (int32_t)pgmoneta_json_get(server, "Retention months");
         if (months != 0)
         {
            printf("%" PRId32 " month(s) ", months);
         }
         int32_t years = (int32_t)pgmoneta_json_get(server, "Retention years");
         if (years != 0)
         {
            printf("%" PRId32 " year(s) ", (int32_t)pgmoneta_json_get(server, "Retention years"));
         }
         printf("\n");
         printf("  Workers        : %" PRId32 "\n", (int32_t)pgmoneta_json_get(server, "Workers"));
         printf("  Backups        : %" PRId32 "\n", (int32_t)pgmoneta_json_get(server, "Backups"));

         struct json* backups = (struct json*)pgmoneta_json_get(server, "backups");
         if (backups != NULL)
         {
            struct json_iterator* backup_iter = NULL;
            pgmoneta_json_iterator_create(backups, &backup_iter);
            while (pgmoneta_json_iterator_next(backup_iter))
            {
               backup = (struct json*)pgmoneta_value_data(backup_iter->value);
               valid = (char*)pgmoneta_json_get(backup, "Valid");
               if (pgmoneta_compare_string(valid, "Unknown"))
               {
                  printf("                   %s (Unknown)\n", (char*)pgmoneta_json_get(backup, "Backup name"));
               }
               else
               {
                  printf("                   %s (Backup: %s Restore: %s WAL: %s Delta: %s Retain: %s Valid: %s)\n",
                         (char*)pgmoneta_json_get(backup, "Backup name"),
                         (char*)pgmoneta_json_get(backup, "Backup"),
                         (char*)pgmoneta_json_get(backup, "Restore"),
                         (char*)pgmoneta_json_get(backup, "WAL"),
                         (char*)pgmoneta_json_get(backup, "Delta"),
                         (char*)pgmoneta_json_get(backup, "Retain"),
                         (char*)pgmoneta_json_get(backup, "Valid"));
               }
            }
            pgmoneta_json_iterator_destroy(backup_iter);
            printf("  Space          : %s\n", (char*)pgmoneta_json_get(server, "Space"));
            printf("  Hot standby    : %s\n", (char*)pgmoneta_json_get(server, "Hot standby"));
         }
      }
      pgmoneta_json_iterator_destroy(server_iter);
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

   struct json* status = (struct json*)pgmoneta_json_get(output, "status");
   if (status == NULL)
   {
      return 1;
   }
   srv = (char*)pgmoneta_json_get(status, "Server");
   if (srv == NULL)
   {
      return 1;
   }
   printf("Server           : %s\n", srv);
   if (!pgmoneta_compare_string(srv, "Unknown"))
   {
      number_of_backups = (int32_t)pgmoneta_json_get(status, "Number of backups");
      printf("Number of backups: %" PRId32 "\n", number_of_backups);

      if (number_of_backups > 0)
      {
         printf("Backup           :\n");
         struct json* backups = (struct json*)pgmoneta_json_get(status, "backups");
         if (backups != NULL)
         {
            struct json_iterator* backup_iter = NULL;
            pgmoneta_json_iterator_create(backups, &backup_iter);
            while (pgmoneta_json_iterator_next(backup_iter))
            {
               backup = (struct json*)pgmoneta_value_data(backup_iter->value);
               printf("                   %s\n", (char*)pgmoneta_json_get(backup, "Backup name"));
            }
            pgmoneta_json_iterator_destroy(backup_iter);
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

   struct json* status = (struct json*)pgmoneta_json_get(output, "status");
   if (status == NULL)
   {
      return 1;
   }
   srv = (char*)pgmoneta_json_get(status, "Server");
   if (srv == NULL)
   {
      return 1;
   }

   number_of_backups = (int64_t)pgmoneta_json_get(status, "Number of backups");

   printf("Server           : %s\n", srv);
   if (!pgmoneta_compare_string(srv, "Unknown"))
   {
      printf("Number of backups: %" PRId64 "\n", number_of_backups);

      struct json* backups = (struct json*)pgmoneta_json_get(status, "backups");
      if (number_of_backups > 0)
      {
         printf("Backup           :\n");
      }
      struct json_iterator* backup_iter = NULL;
      pgmoneta_json_iterator_create(backups, &backup_iter);
      while (pgmoneta_json_iterator_next(backup_iter))
      {
         backup = (struct json*)pgmoneta_value_data(backup_iter->value);
         valid = (char*)pgmoneta_json_get(backup, "Valid");
         if (pgmoneta_compare_string(valid, "Unknown"))
         {
            printf("                   %s (Unknown)\n", (char*)pgmoneta_json_get(backup, "Backup name"));
         }
         else
         {
            printf("                   %s (Backup: %s Restore: %s WAL: %s Delta: %s Retain: %s Valid: %s)\n",
                   (char*)pgmoneta_json_get(backup, "Backup name"),
                   (char*)pgmoneta_json_get(backup, "Backup"),
                   (char*)pgmoneta_json_get(backup, "Restore"),
                   (char*)pgmoneta_json_get(backup, "WAL"),
                   (char*)pgmoneta_json_get(backup, "Delta"),
                   (char*)pgmoneta_json_get(backup, "Retain"),
                   (char*)pgmoneta_json_get(backup, "Valid"));
         }
      }
      pgmoneta_json_iterator_destroy(backup_iter);
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
   char* comments = NULL;
   char* extra = NULL;
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

   info = (struct json*)pgmoneta_json_get(output, "info");

   if (info == NULL)
   {
      return 1;
   }

   label = (char*)pgmoneta_json_get(info, "Label");
   if (label != NULL)
   {
      printf("Label                : %s\n", label);
   }

   wal = (char*)pgmoneta_json_get(info, "WAL");
   if (label != NULL)
   {
      printf("WAL                  : %s\n", wal);
   }

   comments = (char*)pgmoneta_json_get(info, "Comments");
   if (comments != NULL)
   {
      printf("Commments            : %s\n", comments);
   }

   extra = (char*)pgmoneta_json_get(info, "Extra");
   if (extra != NULL)
   {
      printf("Extra                : %s\n", extra);
   }

   printf("Backup size          : %" PRId64 "\n", (int64_t)pgmoneta_json_get(info, "Backup size"));
   printf("Restore size         : %" PRId64 "\n", (int64_t)pgmoneta_json_get(info, "Restore size"));
   printf("Elapsed time         : %d\n", (int)pgmoneta_json_get(info, "Elapsed time"));
   printf("Version              : %d\n", (int)pgmoneta_json_get(info, "Version"));
   printf("Minor version        : %d\n", (int)pgmoneta_json_get(info, "Minor version"));
   printf("Keep                 : %d\n", (bool)pgmoneta_json_get(info, "Keep"));
   printf("Valid                : %d\n", (bool)pgmoneta_json_get(info, "Valid"));

   number_of_tablespaces = (uint64_t)pgmoneta_json_get(info, "Number of tablespaces");
   printf("Number of tablespaces: %" PRId64 "\n", number_of_tablespaces);

   tablespaces = (struct json*)pgmoneta_json_get(info, "Table spaces");
   if (tablespaces != NULL)
   {
      struct json_iterator* iter = NULL;
      pgmoneta_json_iterator_create(tablespaces, &iter);
      while (pgmoneta_json_iterator_next(iter))
      {
         tablespace = (struct json*)pgmoneta_value_data(iter->value);
         printf("Table space          : %s\n", (char*)pgmoneta_json_get(tablespace, "Table space"));
      }
      pgmoneta_json_iterator_destroy(iter);
   }

   printf("Start LSN Hi32       : %X\n", (uint32_t)pgmoneta_json_get(info, "Start LSN Hi32"));
   printf("Start LSN Lo32       : %X\n", (uint32_t)pgmoneta_json_get(info, "Start LSN Lo32"));

   printf("End LSN Hi32         : %X\n", (uint32_t)pgmoneta_json_get(info, "End LSN Hi32"));
   printf("End LSN Lo32         : %X\n", (uint32_t)pgmoneta_json_get(info, "End LSN Lo32"));

   printf("Checkpoint LSN Hi32  : %X\n", (uint32_t)pgmoneta_json_get(info, "Checkpoint LSN Hi32"));
   printf("Checkpoint LSN Lo32  : %X\n", (uint32_t)pgmoneta_json_get(info, "Checkpoint LSN Lo32"));

   printf("Start timeline       : %u\n", (uint32_t)pgmoneta_json_get(info, "Start timeline"));
   printf("End timeline         : %u\n", (uint32_t)pgmoneta_json_get(info, "End timeline"));

   return 0;

}

static int
print_verify_json(struct json* json)
{
   struct json* output = NULL;
   struct json* verify = NULL;
   uint32_t number_of_failed;
   struct json* failed = NULL;
   uint32_t number_of_all;
   struct json* all = NULL;
   struct json_iterator* iter = NULL;

   if (!json)
   {
      return 1;
   }

   if (!json_command_name_equals_to(json, "verify"))
   {
      return 1;
   }

   output = extract_command_output_json_object(json);

   verify = (struct json*)pgmoneta_json_get(output, "verify");

   if (verify == NULL)
   {
      return 1;
   }

   failed = (struct json*)pgmoneta_json_get(verify, "failed");
   all = (struct json*)pgmoneta_json_get(verify, "all");
   if (failed == NULL || all == NULL)
   {
      return 1;
   }

   number_of_failed = pgmoneta_json_array_length(failed);
   printf("Number of failed        : %d\n", number_of_failed);
   pgmoneta_json_iterator_create(failed, &iter);
   while (pgmoneta_json_iterator_next(iter))
   {
      char* filename = NULL;
      char* original = NULL;
      char* calculated = NULL;
      struct json* entry = NULL;

      entry = (struct json*)pgmoneta_value_data(iter->value);

      filename = (char*)pgmoneta_json_get(entry, "File name");
      original = (char*)pgmoneta_json_get(entry, "Original");
      calculated = (char*)pgmoneta_json_get(entry, "Calculated");

      printf("File name : %s\n", filename);
      printf("Original  : %s\n", original);
      printf("Calculated: %s\n", calculated);
   }
   pgmoneta_json_iterator_destroy(iter);
   iter = NULL;

   number_of_all = pgmoneta_json_array_length(all);
   printf("Number of all           : %d\n", number_of_all);
   pgmoneta_json_iterator_create(all, &iter);
   for (int i = 0; i < number_of_all; i++)
   {
      char* filename = NULL;
      char* hash = NULL;
      struct json* entry = NULL;

      entry = (struct json*)pgmoneta_value_data(iter->value);

      filename = (char*)pgmoneta_json_get(entry, "File name");
      hash = (char*)pgmoneta_json_get(entry, "Hash");

      printf("File name : %s\n", filename);
      printf("Hash      : %s\n", hash);
   }
   pgmoneta_json_iterator_destroy(iter);
   return 0;
}

static int
print_annotate_json(struct json* json)
{
   char* server = NULL;
   char* backup = NULL;
   char* comment = NULL;
   struct json* output = NULL;
   struct json* annotate = NULL;

   if (!json)
   {
      return 1;
   }

   if (!json_command_name_equals_to(json, "annotate"))
   {
      return 1;
   }

   output = extract_command_output_json_object(json);

   annotate = (struct json*)pgmoneta_json_get(output, "annotate");

   if (annotate == NULL)
   {
      return 1;
   }

   server = (char*)pgmoneta_json_get(annotate, "Server");
   printf("Server : %s\n", server);

   backup = (char*)pgmoneta_json_get(annotate, "Backup");
   printf("Backup : %s\n", backup);

   comment = (char*)pgmoneta_json_get(annotate, "Comment");
   printf("Comment: %s\n", comment);

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
   pgmoneta_json_create(&json);

   if (!json)
   {
      goto error;
   }

   // the command structure
   pgmoneta_json_create(&command);
   if (!command)
   {
      goto error;
   }

   pgmoneta_json_put(command, JSON_TAG_COMMAND_NAME, (uintptr_t)command_name, ValueString);
   pgmoneta_json_put(command, JSON_TAG_COMMAND_STATUS, (uintptr_t)(success ? JSON_STRING_SUCCESS : JSON_STRING_ERROR), ValueString);
   pgmoneta_json_put(command, JSON_TAG_COMMAND_ERROR, !success, ValueBool);
   pgmoneta_json_put(command, JSON_TAG_APPLICATION_VERSION, (uintptr_t)VERSION, ValueString);

   // the output of the command, this has to be filled by the caller
   pgmoneta_json_create(&output);
   if (!output)
   {
      goto error;
   }

   pgmoneta_json_put(command, JSON_TAG_COMMAND_OUTPUT, (uintptr_t)output, ValueJSON);

   pgmoneta_json_create(&application);
   if (!application)
   {
      goto error;
   }
   pgmoneta_json_put(application, JSON_TAG_APPLICATION_NAME, (uintptr_t)executable_name, ValueString);
   // add objects to the whole json thing
   pgmoneta_json_put(json, "command", (uintptr_t)command, ValueJSON);
   pgmoneta_json_put(json, "application", (uintptr_t)application, ValueJSON);

   return json;
error:
   if (json)
   {
      pgmoneta_json_destroy(json);
   }

   return NULL;
}

static struct json*
extract_command_output_json_object(struct json* json)
{
   struct json* command = (struct json*)pgmoneta_json_get(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   return (struct json*)pgmoneta_json_get(command, JSON_TAG_COMMAND_OUTPUT);

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

   struct json* command = (struct json*)pgmoneta_json_get(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   char* cName = (char*)pgmoneta_json_get(command, JSON_TAG_COMMAND_NAME);
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

   struct json* command = (struct json*)pgmoneta_json_get(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   pgmoneta_json_put(command, JSON_TAG_COMMAND_STATUS, (uintptr_t)message, ValueString);
   pgmoneta_json_put(command, JSON_TAG_COMMAND_ERROR, true, ValueBool);

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

   struct json* command = (struct json*)pgmoneta_json_get(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   bool status = pgmoneta_json_get(command, JSON_TAG_COMMAND_ERROR);
   return status;

error:
   return false;
}

static void
print_and_free_json_object(struct json* json)
{
   pgmoneta_json_print(json, FORMAT_JSON);
   pgmoneta_json_destroy(json);
}
