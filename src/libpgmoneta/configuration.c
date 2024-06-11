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
#include <configuration.h>
#include <logging.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>
#include <aes.h>
#include <io.h>

/* system */
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef HAVE_LINUX
#include <systemd/sd-daemon.h>
#endif

#define LINE_LENGTH 512

static void extract_key_value(char* str, char** key, char** value);
static int as_int(char* str, int* i);
static int as_bool(char* str, bool* b);
static int as_logging_type(char* str);
static int as_logging_level(char* str);
static int as_logging_mode(char* str);
static int as_hugepage(char* str);
static int as_compression(char* str);
static int as_storage_engine(char* str);
static char* as_ciphers(char* str);
static int as_encryption_mode(char* str);
static unsigned int as_update_process_title(char* str, unsigned int default_policy);
static int as_logging_rotation_size(char* str, int* size);
static int as_logging_rotation_age(char* str, int* age);
static int as_seconds(char* str, int* age, int default_age);
static int as_bytes(char* str, int* bytes, int default_bytes);
static int as_retention(char* str, int* days, int* weeks, int* months, int* years);
static int as_create_slot(char* str, int* create_slot);

static int transfer_configuration(struct configuration* config, struct configuration* reload);
static void copy_server(struct server* dst, struct server* src);
static void copy_user(struct user* dst, struct user* src);
static int restart_int(char* name, int e, int n);
static int restart_string(char* name, char* e, char* n);

static bool is_empty_string(char* s);
static int remove_leading_whitespace_and_comments(char* s, char** trimmed_line);

/**
 *
 */
int
pgmoneta_init_configuration(void* shm)
{
   struct configuration* config;

   config = (struct configuration*)shm;

   config->running = true;

   config->compression_type = COMPRESSION_CLIENT_ZSTD;
   config->compression_level = 3;

   config->encryption = ENCRYPTION_NONE;

   config->storage_engine = STORAGE_ENGINE_LOCAL;

   config->workers = 0;

   config->retention_days = 7;
   config->retention_weeks = -1;
   config->retention_months = -1;
   config->retention_years = -1;

   config->tls = false;

   config->blocking_timeout = 30;
   config->authentication_timeout = 5;

   config->buffer_size = DEFAULT_BUFFER_SIZE;
   config->keep_alive = true;
   config->nodelay = true;
   config->non_blocking = true;
   config->backlog = 16;
   config->hugepage = HUGEPAGE_TRY;

   config->update_process_title = UPDATE_PROCESS_TITLE_VERBOSE;

   config->log_type = PGMONETA_LOGGING_TYPE_CONSOLE;
   config->log_level = PGMONETA_LOGGING_LEVEL_INFO;
   config->log_mode = PGMONETA_LOGGING_MODE_APPEND;
   atomic_init(&config->log_lock, STATE_FREE);

   config->backup_max_rate = 0;
   config->network_max_rate = 0;

   return 0;
}

/**
 *
 */
int
pgmoneta_read_configuration(void* shm, char* filename)
{
   FILE* file;
   char section[LINE_LENGTH];
   char line[LINE_LENGTH];
   char* trimmed_line = NULL;
   char* key = NULL;
   char* value = NULL;
   char* ptr = NULL;
   size_t max;
   struct configuration* config;
   int idx_server = 0;
   struct server srv = {0};

   file = pgmoneta_open_file(filename, "r");

   if (!file)
   {
      return 1;
   }

   memset(&section, 0, LINE_LENGTH);
   config = (struct configuration*)shm;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line))
      {
         if (!remove_leading_whitespace_and_comments(line, &trimmed_line))
         {
            if (is_empty_string(trimmed_line))
            {
               free(trimmed_line);
               trimmed_line = NULL;
               continue;
            }
         }
         else
         {
            goto error;
         }

         if (trimmed_line[0] == '[')
         {
            ptr = strchr(trimmed_line, ']');
            if (ptr)
            {
               memset(&section, 0, LINE_LENGTH);
               max = ptr - trimmed_line - 1;
               if (max > MISC_LENGTH - 1)
               {
                  max = MISC_LENGTH - 1;
               }
               memcpy(&section, trimmed_line + 1, max);
               if (strcmp(section, "pgmoneta"))
               {
                  if (idx_server > 0 && idx_server <= NUMBER_OF_SERVERS)
                  {
                     memcpy(&(config->servers[idx_server - 1]), &srv, sizeof(struct server));
                  }
                  else if (idx_server > NUMBER_OF_SERVERS)
                  {
                     warnx("Maximum number of servers exceeded");
                  }

                  memset(&srv, 0, sizeof(struct server));
                  memcpy(&srv.name, &section, strlen(section));

                  atomic_init(&srv.backup, false);
                  atomic_init(&srv.delete, false);
                  atomic_init(&srv.wal, false);
                  srv.wal_streaming = false;
                  srv.valid = false;
                  srv.cur_timeline = 1; // by default current timeline is 1
                  memset(srv.wal_shipping, 0, MAX_PATH);
                  srv.workers = -1;
                  srv.backup_max_rate = -1;
                  srv.network_max_rate = -1;

                  idx_server++;
               }
            }
         }
         else
         {
            extract_key_value(trimmed_line, &key, &value);

            if (key && value)
            {
               bool unknown = false;

               /* printf("|%s|%s|\n", key, value); */

               if (!strcmp(key, "host"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->host, value, max);
                  }
                  else if (strlen(section) > 0)
                  {
                     max = strlen(section);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.name, section, max);
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.host, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "port"))
               {
                  if (strlen(section) > 0)
                  {
                     if (as_int(value, &srv.port))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "user"))
               {
                  if (strlen(section) > 0)
                  {
                     max = strlen(section);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.name, section, max);
                     max = strlen(value);
                     if (max > MAX_USERNAME_LENGTH - 1)
                     {
                        max = MAX_USERNAME_LENGTH - 1;
                     }
                     memcpy(&srv.username, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "wal_slot"))
               {
                  if (strlen(section) > 0)
                  {
                     max = strlen(section);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.name, section, max);
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.wal_slot, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "create_slot"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }

                     if (as_create_slot(value, &config->create_slot))
                     {
                        unknown = true;
                     }
                  }
                  else if (strlen(section) > 0)
                  {
                     max = strlen(section);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.name, section, max);
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }

                     if (as_create_slot(value, &srv.create_slot))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "follow"))
               {
                  if (strlen(section) > 0)
                  {
                     max = strlen(section);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.name, section, max);
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.follow, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "base_dir"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(&config->base_dir[0], value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "wal_shipping"))
               {
                  if (strcmp(section, "pgmoneta") && strlen(section) > 0)
                  {
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(&srv.wal_shipping[0], value, max);
                  }
               }
               else if (!strcmp(key, "hot_standby"))
               {
                  if (strlen(section) > 0)
                  {
                     max = strlen(section);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.name, section, max);
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(&srv.hot_standby, value, max);
                  }
               }
               else if (!strcmp(key, "hot_standby_overrides"))
               {
                  if (strlen(section) > 0)
                  {
                     max = strlen(section);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.name, section, max);
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(&srv.hot_standby_overrides, value, max);
                  }
               }
               else if (!strcmp(key, "metrics"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     if (as_int(value, &config->metrics))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "metrics_cache_max_size"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     if (as_bytes(value, &config->metrics_cache_max_size, 0))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "metrics_cache_max_age"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     if (as_seconds(value, &config->metrics_cache_max_age, 0))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "management"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     if (as_int(value, &config->management))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "tls"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     if (as_bool(value, &config->tls))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "tls_ca_file"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->tls_ca_file, value, max);
                  }
                  else if (strlen(section) > 0)
                  {
                     max = strlen(section);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.name, section, max);
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.tls_ca_file, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "tls_cert_file"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->tls_cert_file, value, max);
                  }
                  else if (strlen(section) > 0)
                  {
                     max = strlen(section);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.name, section, max);
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.tls_cert_file, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "tls_key_file"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->tls_key_file, value, max);
                  }
                  else if (strlen(section) > 0)
                  {
                     max = strlen(section);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.name, section, max);
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.tls_key_file, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "blocking_timeout"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     if (as_int(value, &config->blocking_timeout))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "pidfile"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->pidfile, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "update_process_title"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     config->update_process_title = as_update_process_title(value, UPDATE_PROCESS_TITLE_VERBOSE);
                  }
                  else
                  {
                     unknown = false;
                  }
               }
               else if (!strcmp(key, "workers"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     if (as_int(value, &config->workers))
                     {
                        unknown = true;
                     }
                  }
                  if (strlen(section) > 0)
                  {
                     if (as_int(value, &srv.workers))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "log_type"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     config->log_type = as_logging_type(value);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "log_level"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     config->log_level = as_logging_level(value);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "log_path"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->log_path, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "log_rotation_size"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     if (as_logging_rotation_size(value, &config->log_rotation_size))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "log_rotation_age"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     if (as_logging_rotation_age(value, &config->log_rotation_size))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "log_line_prefix"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->log_line_prefix, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "log_mode"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     config->log_mode = as_logging_mode(value);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "unix_socket_dir"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->unix_socket_dir, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "libev"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->libev, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "buffer_size"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     if (as_int(value, &config->buffer_size))
                     {
                        unknown = true;
                     }
                     if (config->buffer_size > MAX_BUFFER_SIZE)
                     {
                        config->buffer_size = MAX_BUFFER_SIZE;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "keep_alive"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     if (as_bool(value, &config->keep_alive))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "nodelay"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     if (as_bool(value, &config->nodelay))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "non_blocking"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     if (as_bool(value, &config->non_blocking))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "backlog"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     if (as_int(value, &config->backlog))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "hugepage"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     config->hugepage = as_hugepage(value);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "compression"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     config->compression_type = as_compression(value);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "compression_level"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     if (as_int(value, &config->compression_level))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "storage_engine"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     config->storage_engine = as_storage_engine(value);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "ssh_hostname"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->ssh_hostname, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "ssh_username"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->ssh_username, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "ssh_base_dir"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(&config->ssh_base_dir[0], value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "ssh_ciphers"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     char* ciphers = as_ciphers(value);

                     max = strlen(ciphers);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&config->ssh_ciphers[0], ciphers, max);

                     free(ciphers);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "s3_aws_region"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->s3_aws_region, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "s3_access_key_id"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->s3_access_key_id, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "s3_secret_access_key"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->s3_secret_access_key, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "s3_bucket"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->s3_bucket, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "s3_base_dir"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(config->s3_base_dir, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "azure_storage_account"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->azure_storage_account, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "azure_container"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->azure_container, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "azure_shared_key"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->azure_shared_key, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "azure_base_dir"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(config->azure_base_dir, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "retention"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     config->retention_days = -1;
                     config->retention_weeks = -1;
                     config->retention_months = -1;
                     config->retention_years = -1;
                     if (as_retention(value, &config->retention_days,
                                      &config->retention_weeks,
                                      &config->retention_months,
                                      &config->retention_years))
                     {
                        unknown = true;
                     }
                  }
                  else if (strlen(section) > 0)
                  {
                     srv.retention_days = -1;
                     srv.retention_weeks = -1;
                     srv.retention_months = -1;
                     srv.retention_years = -1;
                     if (as_retention(value, &srv.retention_days,
                                      &srv.retention_weeks,
                                      &srv.retention_months,
                                      &srv.retention_years))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "encryption"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     config->encryption = as_encryption_mode(value);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "backup_max_rate"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     if (as_int(value, &config->backup_max_rate))
                     {
                        unknown = true;
                     }
                  }
                  else if (strlen(section) > 0)
                  {
                     max = strlen(section);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.name, section, max);
                     if (as_int(value, &srv.backup_max_rate))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "network_max_rate"))
               {
                  if (!strcmp(section, "pgmoneta"))
                  {
                     if (as_int(value, &config->network_max_rate))
                     {
                        unknown = true;
                     }
                  }
                  else if (strlen(section) > 0)
                  {
                     max = strlen(section);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.name, section, max);
                     if (as_int(value, &srv.network_max_rate))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else
               {
                  unknown = true;
               }

               if (unknown)
               {
                  warnx("Unknown: Section=%s, Key=%s, Value=%s", strlen(section) > 0 ? section : "<unknown>", key, value);
               }

               free(key);
               free(value);
               key = NULL;
               value = NULL;
            }
            else
            {
               warnx("Unknown: Section=%s, Line=%s", strlen(section) > 0 ? section : "<unknown>", line);
            }
         }
      }
      free(trimmed_line);
      trimmed_line = NULL;
   }

   if (strlen(srv.name) > 0)
   {
      memcpy(&(config->servers[idx_server - 1]), &srv, sizeof(struct server));
   }

   config->number_of_servers = idx_server;

   fclose(file);

   return 0;

error:

   free(trimmed_line);
   trimmed_line = NULL;
   if (file)
   {
      fclose(file);
   }

   return 1;
}

/**
 *
 */
int
pgmoneta_validate_configuration(void* shm)
{
   bool found = false;
   struct stat st;
   struct configuration* config;

   config = (struct configuration*)shm;

   if (strlen(config->host) == 0)
   {
      pgmoneta_log_fatal("No host defined");
      return 1;
   }

   if (strlen(config->unix_socket_dir) == 0)
   {
      pgmoneta_log_fatal("No unix_socket_dir defined");
      return 1;
   }

   if (stat(config->unix_socket_dir, &st) == 0 && S_ISDIR(st.st_mode))
   {
      /* Ok */
   }
   else
   {
      pgmoneta_log_fatal("unix_socket_dir is not a directory (%s)", config->unix_socket_dir);
      return 1;
   }

   if (strlen(config->base_dir) == 0)
   {
      pgmoneta_log_fatal("No base directory defined");
      return 1;
   }

   if (stat(config->base_dir, &st) == 0 && S_ISDIR(st.st_mode))
   {
      /* Ok */
   }
   else
   {
      if (!pgmoneta_exists(config->base_dir))
      {
         if (pgmoneta_mkdir(config->base_dir))
         {
            pgmoneta_log_fatal("Can not create %s", config->base_dir);
            return 1;
         }
      }
      else
      {
         pgmoneta_log_fatal("base_dir is not a directory (%s)", config->base_dir);
         return 1;
      }
   }

   if (config->retention_years != -1 && config->retention_years < 1)
   {
      pgmoneta_log_fatal("%d is an invalid year configuration", config->retention_years);
      return 1;
   }
   if (config->retention_months != -1)
   {
      if (config->retention_years != -1)
      {
         if (config->retention_months < 1 || config->retention_months > 12)
         {
            pgmoneta_log_fatal("%d is an invalid month configuration", config->retention_months);
            return 1;
         }
      }
      else if (config->retention_months < 1)
      {
         pgmoneta_log_fatal("%d is an invalid month configuration", config->retention_months);
         return 1;
      }
   }

   if (config->retention_weeks != -1)
   {
      if (config->retention_months != -1)
      {
         if (config->retention_weeks < 1 || config->retention_weeks > 4)
         {
            pgmoneta_log_fatal("%d is an invalid week configuration", config->retention_weeks);
            return 1;
         }
      }
      else if (config->retention_weeks < 1)
      {
         pgmoneta_log_fatal("%d is an invalid week configuration", config->retention_weeks);
         return 1;
      }
   }
   if (config->retention_days < 1)
   {
      pgmoneta_log_fatal("retention days should be at least 1");
      return 1;
   }

   if (config->backlog < 16)
   {
      config->backlog = 16;
   }

   if (config->number_of_servers <= 0)
   {
      pgmoneta_log_fatal("No servers defined");
      return 1;
   }

   if (config->compression_type == COMPRESSION_CLIENT_GZIP || config->compression_type == COMPRESSION_SERVER_GZIP)
   {
      if (config->compression_level < 1)
      {
         config->compression_level = 1;
      }
      else if (config->compression_level > 9)
      {
         config->compression_level = 9;
      }
   }
   else if (config->compression_type == COMPRESSION_CLIENT_ZSTD || config->compression_type == COMPRESSION_SERVER_ZSTD)
   {
      if (config->compression_level < -131072)
      {
         config->compression_level = -131072;
      }
      else if (config->compression_level > 22)
      {
         config->compression_level = 22;
      }
   }
   else if (config->compression_type == COMPRESSION_CLIENT_LZ4 || config->compression_type == COMPRESSION_SERVER_LZ4)
   {
      if (config->compression_level < 1)
      {
         config->compression_level = 1;
      }
      else if (config->compression_level > 12)
      {
         config->compression_level = 12;
      }
   }
   else if (config->compression_type == COMPRESSION_CLIENT_BZIP2)
   {
      if (config->compression_level < 1)
      {
         config->compression_level = 1;
      }
      else if (config->compression_level > 9)
      {
         config->compression_level = 9;
      }
   }

   if (config->workers < 0)
   {
      config->workers = 0;
   }

   for (int i = 0; i < config->number_of_servers; i++)
   {
      if (!strcmp(config->servers[i].name, "pgmoneta"))
      {
         pgmoneta_log_fatal("pgmoneta is a reserved word for a host");
         return 1;
      }

      if (!strcmp(config->servers[i].name, "all"))
      {
         pgmoneta_log_fatal("all is a reserved word for a host");
         return 1;
      }

      if (strlen(config->servers[i].host) == 0)
      {
         pgmoneta_log_fatal("No host defined for %s", config->servers[i].name);
         return 1;
      }

      if (config->servers[i].port == 0)
      {
         pgmoneta_log_fatal("No port defined for %s", config->servers[i].name);
         return 1;
      }

      if (strlen(config->servers[i].username) == 0)
      {
         pgmoneta_log_fatal("No user defined for %s", config->servers[i].name);
         return 1;
      }

      if (strlen(config->servers[i].wal_slot) == 0)
      {
         pgmoneta_log_fatal("No WAL slot defined for %s", config->servers[i].name);
         return 1;
      }

      if (strlen(config->servers[i].follow) > 0)
      {
         found = false;
         for (int j = 0; !found && j < config->number_of_servers; j++)
         {
            if (!strcmp(config->servers[i].follow, config->servers[j].name))
            {
               found = true;
            }
         }

         if (!found)
         {
            pgmoneta_log_fatal("Invalid follow value for %s", config->servers[i].name);
            return 1;
         }
      }

      if (config->servers[i].workers < -1)
      {
         config->servers[i].workers = -1;
      }

      if (config->servers[i].backup_max_rate < -1)
      {
         config->servers[i].backup_max_rate = -1;
      }

      if (config->servers[i].network_max_rate < -1)
      {
         config->servers[i].network_max_rate = -1;
      }
   }

   return 0;
}

/**
 *
 */
int
pgmoneta_read_users_configuration(void* shm, char* filename)
{
   FILE* file;
   char line[LINE_LENGTH];
   char* trimmed_line = NULL;
   int index;
   char* master_key = NULL;
   char* username = NULL;
   char* password = NULL;
   char* decoded = NULL;
   int decoded_length = 0;
   char* ptr = NULL;
   struct configuration* config;

   file = pgmoneta_open_file(filename, "r");

   if (!file)
   {
      goto error;
   }
   if (pgmoneta_get_master_key(&master_key))
   {
      goto masterkey;
   }

   index = 0;
   config = (struct configuration*)shm;

   while (fgets(line, sizeof(line), file))
   {

      if (!is_empty_string(line))
      {
         if (!remove_leading_whitespace_and_comments(line, &trimmed_line))
         {
            if (is_empty_string(trimmed_line))
            {
               free(trimmed_line);
               trimmed_line = NULL;
               continue;
            }
         }
         else
         {
            goto error;
         }

         ptr = strtok(trimmed_line, ":");

         username = ptr;

         ptr = strtok(NULL, ":");

         if (ptr == NULL)
         {
            goto error;
         }

         if (pgmoneta_base64_decode(ptr, strlen(ptr), &decoded, &decoded_length))
         {
            goto error;
         }

         if (pgmoneta_decrypt(decoded, decoded_length, master_key, &password, ENCRYPTION_AES_256_CBC))
         {
            goto error;
         }

         if (strlen(username) < MAX_USERNAME_LENGTH &&
             strlen(password) < MAX_PASSWORD_LENGTH)
         {
            memcpy(&config->users[index].username, username, strlen(username));
            memcpy(&config->users[index].password, password, strlen(password));
         }
         else
         {
            warnx("pgmoneta: Invalid USER entry");
            warnx("%s", line);
         }

         free(password);
         free(decoded);

         password = NULL;
         decoded = NULL;

         index++;

      }
      free(trimmed_line);
      trimmed_line = NULL;
   }

   config->number_of_users = index;

   if (config->number_of_users > NUMBER_OF_USERS)
   {
      goto above;
   }

   free(master_key);

   fclose(file);

   return 0;

error:

   free(trimmed_line);
   trimmed_line = NULL;
   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 1;

masterkey:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 2;

above:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 3;
}

/**
 *
 */
int
pgmoneta_validate_users_configuration(void* shm)
{
   struct configuration* config;

   config = (struct configuration*)shm;

   if (config->number_of_users <= 0)
   {
      pgmoneta_log_fatal("No users defined");
      return 1;
   }

   for (int i = 0; i < config->number_of_servers; i++)
   {
      bool found = false;

      for (int j = 0; !found && j < config->number_of_users; j++)
      {
         if (!strcmp(config->servers[i].username, config->users[j].username))
         {
            found = true;
         }
      }

      if (!found)
      {
         pgmoneta_log_fatal("Unknown user (\'%s\') defined for %s", config->servers[i].username, config->servers[i].name);
         return 1;
      }
   }

   return 0;
}

/**
 *
 */
int
pgmoneta_read_admins_configuration(void* shm, char* filename)
{
   FILE* file;
   char line[LINE_LENGTH];
   char* trimmed_line = NULL;
   int index;
   char* master_key = NULL;
   char* username = NULL;
   char* password = NULL;
   char* decoded = NULL;
   int decoded_length = 0;
   char* ptr = NULL;
   struct configuration* config;

   file = pgmoneta_open_file(filename, "r");

   if (!file)
   {
      goto error;
   }

   if (pgmoneta_get_master_key(&master_key))
   {
      goto masterkey;
   }

   index = 0;
   config = (struct configuration*)shm;

   while (fgets(line, sizeof(line), file))
   {

      if (!is_empty_string(line))
      {
         if (!remove_leading_whitespace_and_comments(line, &trimmed_line))
         {
            if (is_empty_string(trimmed_line))
            {
               free(trimmed_line);
               trimmed_line = NULL;
               continue;
            }
         }
         else
         {
            goto error;
         }

         ptr = strtok(trimmed_line, ":");

         username = ptr;

         ptr = strtok(NULL, ":");

         if (ptr == NULL)
         {
            goto error;
         }

         if (pgmoneta_base64_decode(ptr, strlen(ptr), &decoded, &decoded_length))
         {
            goto error;
         }

         if (pgmoneta_decrypt(decoded, decoded_length, master_key, &password, ENCRYPTION_AES_256_CBC))
         {
            goto error;
         }

         if (strlen(username) < MAX_USERNAME_LENGTH &&
             strlen(password) < MAX_PASSWORD_LENGTH)
         {
            memcpy(&config->admins[index].username, username, strlen(username));
            memcpy(&config->admins[index].password, password, strlen(password));
         }
         else
         {
            warnx("pgmoneta: Invalid ADMIN entry");
            warnx("%s", line);
         }

         free(password);
         free(decoded);

         password = NULL;
         decoded = NULL;

         index++;

      }
      free(trimmed_line);
      trimmed_line = NULL;
   }

   config->number_of_admins = index;

   if (config->number_of_admins > NUMBER_OF_ADMINS)
   {
      goto above;
   }

   free(master_key);

   fclose(file);

   return 0;

error:

   free(trimmed_line);
   trimmed_line = NULL;
   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 1;

masterkey:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 2;

above:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 3;
}

/**
 *
 */
int
pgmoneta_validate_admins_configuration(void* shm)
{
   struct configuration* config;

   config = (struct configuration*)shm;

   if (config->management > 0 && config->number_of_admins == 0)
   {
      pgmoneta_log_warn("Remote management enabled, but no admins are defined");
   }
   else if (config->management == 0 && config->number_of_admins > 0)
   {
      pgmoneta_log_warn("Remote management disabled, but admins are defined");
   }

   return 0;
}

int
pgmoneta_reload_configuration(void)
{
   size_t reload_size;
   struct configuration* reload = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_trace("Configuration: %s", config->configuration_path);
   pgmoneta_log_trace("Users: %s", config->users_path);
   pgmoneta_log_trace("Admins: %s", config->admins_path);

   reload_size = sizeof(struct configuration);

   if (pgmoneta_create_shared_memory(reload_size, HUGEPAGE_OFF, (void**)&reload))
   {
      goto error;
   }

   pgmoneta_init_configuration((void*)reload);

   if (pgmoneta_read_configuration((void*)reload, config->configuration_path))
   {
      goto error;
   }

   if (pgmoneta_read_users_configuration((void*)reload, config->users_path))
   {
      goto error;
   }

   if (strcmp("", config->admins_path))
   {
      if (pgmoneta_read_admins_configuration((void*)reload, config->admins_path))
      {
         goto error;
      }
   }

   if (pgmoneta_validate_configuration(reload))
   {
      goto error;
   }

   if (pgmoneta_validate_users_configuration(reload))
   {
      goto error;
   }

   if (pgmoneta_validate_admins_configuration(reload))
   {
      goto error;
   }

   if (transfer_configuration(config, reload))
   {
      goto error;
   }

   pgmoneta_destroy_shared_memory((void*)reload, reload_size);

   pgmoneta_log_debug("Reload: Success");

   return 0;

error:
   if (reload != NULL)
   {
      pgmoneta_destroy_shared_memory((void*)reload, reload_size);
   }

   pgmoneta_log_debug("Reload: Failure");

   return 1;
}

static void
extract_key_value(char* str, char** key, char** value)
{
   char* equal = NULL;
   char* end = NULL;
   char* ptr = NULL;
   char left[MISC_LENGTH];
   char right[MISC_LENGTH];
   bool start_left = false;
   bool start_right = false;
   int idx = 0;
   int i = 0;
   char c = 0;
   char* k = NULL;
   char* v = NULL;

   *key = NULL;
   *value = NULL;

   memset(left, 0, sizeof(left));
   memset(right, 0, sizeof(right));

   equal = strchr(str, '=');

   if (equal != NULL)
   {
      i = 0;
      while (true)
      {
         ptr = str + i;
         if (ptr != equal)
         {
            c = *(str + i);
            if (c == '\t' || c == ' ' || c == '\"' || c == '\'')
            {
               /* Skip */
            }
            else
            {
               start_left = true;
            }

            if (start_left)
            {
               left[idx] = c;
               idx++;
            }
         }
         else
         {
            break;
         }
         i++;
      }

      end = strchr(str, '\n');
      idx = 0;

      for (int i = 0; i < strlen(equal); i++)
      {
         ptr = equal + i;
         if (ptr != end)
         {
            c = *(ptr);
            if (c == '=' || c == ' ' || c == '\t' || c == '\"' || c == '\'')
            {
               /* Skip */
            }
            else
            {
               start_right = true;
            }

            if (start_right)
            {
               if (c != '#')
               {
                  right[idx] = c;
                  idx++;
               }
               else
               {
                  break;
               }
            }
         }
         else
         {
            break;
         }
      }

      for (int i = strlen(left); i >= 0; i--)
      {
         if (left[i] == '\t' || left[i] == ' ' || left[i] == '\0' || left[i] == '\"' || left[i] == '\'')
         {
            left[i] = '\0';
         }
         else
         {
            break;
         }
      }

      for (int i = strlen(right); i >= 0; i--)
      {
         if (right[i] == '\t' || right[i] == ' ' || right[i] == '\0' || right[i] == '\r' || right[i] == '\"' || right[i] == '\'')
         {
            right[i] = '\0';
         }
         else
         {
            break;
         }
      }

      k = calloc(1, strlen(left) + 1);

      if (k == NULL)
      {
         goto error;
      }

      v = calloc(1, strlen(right) + 1);

      if (v == NULL)
      {
         goto error;
      }

      memcpy(k, left, strlen(left));
      memcpy(v, right, strlen(right));

      *key = k;
      *value = v;
   }

   return;

error:

   free(k);
   free(v);
}

static int
as_int(char* str, int* i)
{
   char* endptr;
   long val;

   errno = 0;
   val = strtol(str, &endptr, 10);

   if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) || (errno != 0 && val == 0))
   {
      goto error;
   }

   if (str == endptr)
   {
      goto error;
   }

   if (*endptr != '\0')
   {
      goto error;
   }

   *i = (int)val;

   return 0;

error:

   errno = 0;

   return 1;
}

static int
as_bool(char* str, bool* b)
{
   if (!strcasecmp(str, "true") || !strcasecmp(str, "on") || !strcasecmp(str, "yes") || !strcasecmp(str, "1"))
   {
      *b = true;
      return 0;
   }

   if (!strcasecmp(str, "false") || !strcasecmp(str, "off") || !strcasecmp(str, "no") || !strcasecmp(str, "0"))
   {
      *b = false;
      return 0;
   }

   return 1;
}

static int
as_logging_type(char* str)
{
   if (!strcasecmp(str, "console"))
   {
      return PGMONETA_LOGGING_TYPE_CONSOLE;
   }

   if (!strcasecmp(str, "file"))
   {
      return PGMONETA_LOGGING_TYPE_FILE;
   }

   if (!strcasecmp(str, "syslog"))
   {
      return PGMONETA_LOGGING_TYPE_SYSLOG;
   }

   return 0;
}

static int
as_logging_level(char* str)
{
   size_t size = 0;
   int debug_level = 1;
   char* debug_value = NULL;

   if (!strncasecmp(str, "debug", strlen("debug")))
   {
      if (strlen(str) > strlen("debug"))
      {
         size = strlen(str) - strlen("debug");
         debug_value = (char*)malloc(size + 1);

         if (debug_value == NULL)
         {
            goto done;
         }

         memset(debug_value, 0, size + 1);
         memcpy(debug_value, str + 5, size);
         if (as_int(debug_value, &debug_level))
         {
            // cannot parse, set it to 1
            debug_level = 1;
         }
         free(debug_value);
      }

      if (debug_level <= 1)
      {
         return PGMONETA_LOGGING_LEVEL_DEBUG1;
      }
      else if (debug_level == 2)
      {
         return PGMONETA_LOGGING_LEVEL_DEBUG2;
      }
      else if (debug_level == 3)
      {
         return PGMONETA_LOGGING_LEVEL_DEBUG3;
      }
      else if (debug_level == 4)
      {
         return PGMONETA_LOGGING_LEVEL_DEBUG4;
      }
      else if (debug_level >= 5)
      {
         return PGMONETA_LOGGING_LEVEL_DEBUG5;
      }
   }

   if (!strcasecmp(str, "info"))
   {
      return PGMONETA_LOGGING_LEVEL_INFO;
   }

   if (!strcasecmp(str, "warn"))
   {
      return PGMONETA_LOGGING_LEVEL_WARN;
   }

   if (!strcasecmp(str, "error"))
   {
      return PGMONETA_LOGGING_LEVEL_ERROR;
   }

   if (!strcasecmp(str, "fatal"))
   {
      return PGMONETA_LOGGING_LEVEL_FATAL;
   }

done:

   return PGMONETA_LOGGING_LEVEL_INFO;
}

static int
as_logging_mode(char* str)
{
   if (!strcasecmp(str, "a") || !strcasecmp(str, "append"))
   {
      return PGMONETA_LOGGING_MODE_APPEND;
   }

   if (!strcasecmp(str, "c") || !strcasecmp(str, "create"))
   {
      return PGMONETA_LOGGING_MODE_CREATE;
   }

   return PGMONETA_LOGGING_MODE_APPEND;
}

static int
as_hugepage(char* str)
{
   if (!strcasecmp(str, "off"))
   {
      return HUGEPAGE_OFF;
   }

   if (!strcasecmp(str, "try"))
   {
      return HUGEPAGE_TRY;
   }

   if (!strcasecmp(str, "on"))
   {
      return HUGEPAGE_ON;
   }

   return HUGEPAGE_OFF;
}

static int
as_compression(char* str)
{
   if (!strcasecmp(str, "none"))
   {
      return COMPRESSION_NONE;
   }

   if (!strcasecmp(str, "gzip") || !strcasecmp(str, "client-gzip"))
   {
      return COMPRESSION_CLIENT_GZIP;
   }

   if (!strcasecmp(str, "server-gzip"))
   {
      return COMPRESSION_SERVER_GZIP;
   }

   if (!strcasecmp(str, "zstd") || !strcasecmp(str, "client-zstd"))
   {
      return COMPRESSION_CLIENT_ZSTD;
   }

   if (!strcasecmp(str, "server-zstd"))
   {
      return COMPRESSION_SERVER_ZSTD;
   }

   if (!strcasecmp(str, "lz4") || !strcasecmp(str, "client-lz4"))
   {
      return COMPRESSION_CLIENT_LZ4;
   }

   if (!strcasecmp(str, "server-lz4"))
   {
      return COMPRESSION_SERVER_LZ4;
   }

   if (!strcasecmp(str, "bz2") || !strcasecmp(str, "client-bz2"))
   {
      return COMPRESSION_CLIENT_BZIP2;
   }

   return COMPRESSION_CLIENT_ZSTD;
}

static int
as_retention(char* str, int* days, int* weeks, int* months, int* years)
{
   // make a deep copy because the parsing break the input string
   char* copied_str = (char*)malloc(strlen(str) + 1);

   if (copied_str == NULL)
   {
      goto error;
   }

   memset(copied_str, 0, strlen(str) + 1);
   memcpy(copied_str, str, strlen(str));

   char* token = strtok(copied_str, ",");
   if (token == NULL)
   {
      goto error;
   }
   while (*token != '\0' && isspace((unsigned char)*token))
   {
      token++;
   }
   if (*token == '\0')
   {
      // allowing spaces or empty values
      *days = -1;
   }
   else
   {
      // remove trailing spaces
      size_t len = strlen(token);
      while (len > 0 && isspace(token[len - 1]))
      {
         token[len - 1] = '\0';
         len--;
      }
      if (as_int(token, days))
      {
         if (!strcmp(token, "X") || !strcmp(token, "x") || !strcmp(token, "-"))
         {
            *days = -1;
         }
         else
         {
            goto error;
         }
      }
      if (*days < 0)
      {
         goto error;
      }
      if (*days == 0)
      {
         *days = -1;
      }
   }
   token = strtok(NULL, ",");
   if (token == NULL)
   {
      // input stops on days
      free(copied_str);
      return 0;
   }
   while (*token != '\0' && isspace((unsigned char)*token))
   {
      token++;
   }
   if (*token == '\0')
   {
      *weeks = -1;
   }
   else
   {
      // remove trailing spaces
      size_t len = strlen(token);
      while (len > 0 && isspace(token[len - 1]))
      {
         token[len - 1] = '\0';
         len--;
      }
      if (as_int(token, weeks))
      {
         if (!strcmp(token, "X") || !strcmp(token, "x") || !strcmp(token, "-"))
         {
            *weeks = -1;
         }
         else
         {
            goto error;
         }
      }
      if (*weeks < 0)
      {
         goto error;
      }
      if (*weeks == 0)
      {
         *weeks = -1;
      }
   }
   token = strtok(NULL, ",");
   if (token == NULL)
   {
      // input stops on weeks
      free(copied_str);
      return 0;
   }
   while (*token != '\0' && isspace((unsigned char)*token))
   {
      token++;
   }
   if (*token == '\0')
   {
      *weeks = -1;
   }
   else
   {
      // remove trailing spaces
      size_t len = strlen(token);
      while (len > 0 && isspace(token[len - 1]))
      {
         token[len - 1] = '\0';
         len--;
      }
      if (as_int(token, months))
      {
         if (!strcmp(token, "X") || !strcmp(token, "x") || !strcmp(token, "-"))
         {
            *months = -1;
         }
         else
         {
            goto error;
         }
      }
      if (*months < 0)
      {
         goto error;
      }
      if (*months == 0)
      {
         *months = -1;
      }
   }
   token = strtok(NULL, ",");
   if (token == NULL)
   {
      // input stops on months
      free(copied_str);
      return 0;
   }
   while (*token != '\0' && isspace((unsigned char)*token))
   {
      token++;
   }
   if (*token == '\0')
   {
      *years = -1;
   }
   else
   {
      // remove trailing spaces
      size_t len = strlen(token);
      while (len > 0 && isspace(token[len - 1]))
      {
         token[len - 1] = '\0';
         len--;
      }
      if (as_int(token, years))
      {
         if (!strcmp(token, "X") || !strcmp(token, "x") || !strcmp(token, "-"))
         {
            *years = -1;
         }
         else
         {
            goto error;
         }
      }
      if (*years < 0)
      {
         goto error;
      }
      if (*years == 0)
      {
         *years = -1;
      }
   }
   free(copied_str);
   return 0;
error:
   errno = 0;
   free(copied_str);
   return 1;
}

static int
as_storage_engine(char* str)
{
   int STORAGE_ENGINE_TYPES = STORAGE_ENGINE_LOCAL;
   char* token = NULL;
   char* delimiter = ",";
   int i = 0, j = 0;
   while (str[i])
   {
      if (str[i] != ' ' && str[i] != '\t')
      {
         str[j++] = str[i];
      }
      i++;
   }
   str[j] = '\0';
   token = strtok(str, delimiter);
   while (token != NULL)
   {
      if (!strcasecmp(token, "local"))
      {
         STORAGE_ENGINE_TYPES |= STORAGE_ENGINE_LOCAL;
      }
      else if (!strcasecmp(token, "ssh"))
      {
         STORAGE_ENGINE_TYPES |= STORAGE_ENGINE_SSH;
      }
      else if (!strcasecmp(token, "s3"))
      {
         STORAGE_ENGINE_TYPES |= STORAGE_ENGINE_S3;
      }
      else if (!strcasecmp(token, "azure"))
      {
         STORAGE_ENGINE_TYPES |= STORAGE_ENGINE_AZURE;
      }
      token = strtok(NULL, delimiter);
   }
   return STORAGE_ENGINE_TYPES;
}

static char*
as_ciphers(char* str)
{
   char* converted = NULL;
   char* ptr = NULL;
   char* result = NULL;

   converted = pgmoneta_remove_whitespace(str);

   if (converted == str)
   {
      return strdup("aes256-ctr,aes192-ctr,aes128-ctr");
   }

   ptr = strtok(converted, ",");
   while (ptr != NULL)
   {
      if (strcmp("aes-256-ctr", ptr) == 0)
      {
         result = pgmoneta_append(result, "aes256-ctr");
      }
      else if (strcmp("aes-192-ctr", ptr) == 0)
      {
         result = pgmoneta_append(result, "aes192-ctr");
      }
      else if (strcmp("aes-128-ctr", ptr) == 0)
      {
         result = pgmoneta_append(result, "aes128-ctr");
      }
      else if (strcmp("aes-256-cbc", ptr) == 0 || strcmp("aes-256", ptr) == 0)
      {
         result = pgmoneta_append(result, "aes256-cbc");
      }
      else if (strcmp("aes-192-cbc", ptr) == 0 || strcmp("aes-192", ptr) == 0)
      {
         result = pgmoneta_append(result, "aes192-cbc");
      }
      else if (strcmp("aes-128-cbc", ptr) == 0 || strcmp("aes-128", ptr) == 0)
      {
         result = pgmoneta_append(result, "aes128-cbc");
      }
      else if (strcmp("aes", ptr) == 0)
      {
         result = pgmoneta_append(result, "aes256-cbc");
      }
      else
      {
         result = pgmoneta_append(result, ptr);
      }

      ptr = strtok(NULL, ",");
      if (ptr != NULL)
      {
         result = pgmoneta_append(result, ",");
      }
   }

   free(converted);

   return result;
}

/**
 * Utility function to understand the setting for updating
 * the process title.
 *
 * @param str the value obtained by the configuration parsing
 * @param default_policy a value to set when the configuration cannot be
 * understood
 *
 * @return The policy
 */
static unsigned int
as_update_process_title(char* str, unsigned int default_policy)
{
   if (is_empty_string(str))
   {
      return default_policy;
   }

   if (!strncmp(str, "never", MISC_LENGTH) || !strncmp(str, "off", MISC_LENGTH))
   {
      return UPDATE_PROCESS_TITLE_NEVER;
   }
   else if (!strncmp(str, "strict", MISC_LENGTH))
   {
      return UPDATE_PROCESS_TITLE_STRICT;
   }
   else if (!strncmp(str, "minimal", MISC_LENGTH))
   {
      return UPDATE_PROCESS_TITLE_MINIMAL;
   }
   else if (!strncmp(str, "verbose", MISC_LENGTH) || !strncmp(str, "full", MISC_LENGTH))
   {
      return UPDATE_PROCESS_TITLE_VERBOSE;
   }

   // not a valid setting
   return default_policy;
}

/**
 * Parses a string to see if it contains
 * a valid value for log rotation size.
 * Returns 0 if parsing ok, 1 otherwise.
 *
 */
static int
as_logging_rotation_size(char* str, int* size)
{
   return as_bytes(str, size, PGMONETA_LOGGING_ROTATION_DISABLED);
}

/**
 * Parses the log_rotation_age string.
 * The string accepts
 * - s for seconds
 * - m for minutes
 * - h for hours
 * - d for days
 * - w for weeks
 *
 * The default is expressed in seconds.
 * The function sets the number of rotationg age as minutes.
 * Returns 1 for errors, 0 for correct parsing.
 *
 */
static int
as_logging_rotation_age(char* str, int* age)
{
   return as_seconds(str, age, PGMONETA_LOGGING_ROTATION_DISABLED);
}

/**
 * Parses an age string, providing the resulting value as seconds.
 * An age string is expressed by a number and a suffix that indicates
 * the multiplier. Accepted suffixes, case insensitive, are:
 * - s for seconds
 * - m for minutes
 * - h for hours
 * - d for days
 * - w for weeks
 *
 * The default is expressed in seconds.
 *
 * @param str the value to parse as retrieved from the configuration
 * @param age a pointer to the value that is going to store
 *        the resulting number of seconds
 * @param default_age a value to set when the parsing is unsuccesful

 */
static int
as_seconds(char* str, int* age, int default_age)
{
   int multiplier = 1;
   int index;
   char value[MISC_LENGTH];
   bool multiplier_set = false;
   int i_value = default_age;

   if (is_empty_string(str))
   {
      *age = default_age;
      return 0;
   }

   index = 0;
   for (int i = 0; i < strlen(str); i++)
   {
      if (isdigit(str[i]))
      {
         value[index++] = str[i];
      }
      else if (isalpha(str[i]) && multiplier_set)
      {
         // another extra char not allowed
         goto error;
      }
      else if (isalpha(str[i]) && !multiplier_set)
      {
         if (str[i] == 's' || str[i] == 'S')
         {
            multiplier = 1;
            multiplier_set = true;
         }
         else if (str[i] == 'm' || str[i] == 'M')
         {
            multiplier = 60;
            multiplier_set = true;
         }
         else if (str[i] == 'h' || str[i] == 'H')
         {
            multiplier = 3600;
            multiplier_set = true;
         }
         else if (str[i] == 'd' || str[i] == 'D')
         {
            multiplier = 24 * 3600;
            multiplier_set = true;
         }
         else if (str[i] == 'w' || str[i] == 'W')
         {
            multiplier = 24 * 3600 * 7;
            multiplier_set = true;
         }
      }
      else
      {
         // do not allow alien chars
         goto error;
      }
   }

   value[index] = '\0';
   if (!as_int(value, &i_value))
   {
      // sanity check: the value
      // must be a positive number!
      if (i_value >= 0)
      {
         *age = i_value * multiplier;
      }
      else
      {
         goto error;
      }

      return 0;
   }
   else
   {
error:
      *age = default_age;
      return 1;
   }
}

/**
 * Converts a "size string" into the number of bytes.
 *
 * Valid strings have one of the suffixes:
 * - b for bytes (default)
 * - k for kilobytes
 * - m for megabytes
 * - g for gigabytes
 *
 * The default is expressed always as bytes.
 * Uppercase letters work too.
 * If no suffix is specified, the value is expressed as bytes.
 *
 * @param str the string to parse (e.g., "2M")
 * @param bytes the value to set as result of the parsing stage
 * @param default_bytes the default value to set when the parsing cannot proceed
 * @return 1 if parsing is unable to understand the string, 0 is parsing is
 *         performed correctly (or almost correctly, e.g., empty string)
 */
static int
as_bytes(char* str, int* bytes, int default_bytes)
{
   int multiplier = 1;
   int index;
   char value[MISC_LENGTH];
   bool multiplier_set = false;
   int i_value = default_bytes;

   if (is_empty_string(str))
   {
      *bytes = default_bytes;
      return 0;
   }

   index = 0;
   for (int i = 0; i < strlen(str); i++)
   {
      if (isdigit(str[i]))
      {
         value[index++] = str[i];
      }
      else if (isalpha(str[i]) && multiplier_set)
      {
         // allow a 'B' suffix on a multiplier
         // like for instance 'MB', but don't allow it
         // for bytes themselves ('BB')
         if (multiplier == 1 || (str[i] != 'b' && str[i] != 'B'))
         {
            // another non-digit char not allowed
            goto error;
         }
      }
      else if (isalpha(str[i]) && !multiplier_set)
      {
         if (str[i] == 'M' || str[i] == 'm')
         {
            multiplier = 1024 * 1024;
            multiplier_set = true;
         }
         else if (str[i] == 'G' || str[i] == 'g')
         {
            multiplier = 1024 * 1024 * 1024;
            multiplier_set = true;
         }
         else if (str[i] == 'K' || str[i] == 'k')
         {
            multiplier = 1024;
            multiplier_set = true;
         }
         else if (str[i] == 'B' || str[i] == 'b')
         {
            multiplier = 1;
            multiplier_set = true;
         }
      }
      else
      {
         // do not allow alien chars
         goto error;
      }
   }

   value[index] = '\0';
   if (!as_int(value, &i_value))
   {
      // sanity check: the value
      // must be a positive number!
      if (i_value >= 0)
      {
         *bytes = i_value * multiplier;
      }
      else
      {
         goto error;
      }

      return 0;
   }
   else
   {
error:
      *bytes = default_bytes;
      return 1;
   }
}

static int
as_encryption_mode(char* str)
{
   if (!strcasecmp(str, "none"))
   {
      return ENCRYPTION_NONE;
   }

   if (!strcasecmp(str, "aes") || !strcasecmp(str, "aes-256") || !strcasecmp(str, "aes-256-cbc"))
   {
      return ENCRYPTION_AES_256_CBC;
   }

   if (!strcasecmp(str, "aes-192") || !strcasecmp(str, "aes-192-cbc"))
   {
      return ENCRYPTION_AES_192_CBC;
   }

   if (!strcasecmp(str, "aes-128") || !strcasecmp(str, "aes-128-cbc"))
   {
      return ENCRYPTION_AES_128_CBC;
   }

   if (!strcasecmp(str, "aes-256-ctr"))
   {
      return ENCRYPTION_AES_256_CTR;
   }

   if (!strcasecmp(str, "aes-192-ctr"))
   {
      return ENCRYPTION_AES_192_CTR;
   }

   if (!strcasecmp(str, "aes-128-ctr"))
   {
      return ENCRYPTION_AES_128_CTR;
   }

   warnx("Unknown encryption mode: %s", str);

   return ENCRYPTION_NONE;
}

static int
as_create_slot(char* str, int* create_slot)
{
   if (!strcasecmp(str, "true") || !strcasecmp(str, "on") || !strcasecmp(str, "yes") || !strcasecmp(str, "1"))
   {
      *create_slot = CREATE_SLOT_YES;
      return 0;
   }

   if (!strcasecmp(str, "false") || !strcasecmp(str, "off") || !strcasecmp(str, "no") || !strcasecmp(str, "0"))
   {
      *create_slot = CREATE_SLOT_NO;
      return 0;
   }

   *create_slot = CREATE_SLOT_UNDEFINED;

   return 1;
}

static int
transfer_configuration(struct configuration* config, struct configuration* reload)
{
#ifdef HAVE_LINUX
   sd_notify(0, "RELOADING=1");
#endif

   memcpy(config->host, reload->host, MISC_LENGTH);
   config->metrics = reload->metrics;
   config->metrics_cache_max_age = reload->metrics_cache_max_age;
   restart_int("metrics_cache_max_size", config->metrics_cache_max_size, reload->metrics_cache_max_size);
   config->management = reload->management;

   /* base_dir */
   restart_string("base_dir", config->base_dir, reload->base_dir);

   config->create_slot = reload->create_slot;

   config->compression_type = reload->compression_type;
   config->compression_level = reload->compression_level;

   config->retention_days = reload->retention_days;
   config->retention_weeks = reload->retention_weeks;
   config->retention_months = reload->retention_months;
   config->retention_years = reload->retention_years;

   /* log_type */
   restart_int("log_type", config->log_type, reload->log_type);
   config->log_level = reload->log_level;
   // if the log main parameters have changed, we need
   // to restart the logging system
   if (strncmp(config->log_path, reload->log_path, MISC_LENGTH) || config->log_rotation_size != reload->log_rotation_size || config->log_rotation_age != reload->log_rotation_age || config->log_mode != reload->log_mode)
   {
      pgmoneta_log_debug("Log restart triggered!");
      pgmoneta_stop_logging();
      config->log_rotation_size = reload->log_rotation_size;
      config->log_rotation_age = reload->log_rotation_age;
      config->log_mode = reload->log_mode;
      memcpy(config->log_line_prefix, reload->log_line_prefix, MISC_LENGTH);
      memcpy(config->log_path, reload->log_path, MISC_LENGTH);
      pgmoneta_start_logging();
   }
   /* log_lock */

   config->tls = reload->tls;
   memcpy(config->tls_cert_file, reload->tls_cert_file, MISC_LENGTH);
   memcpy(config->tls_key_file, reload->tls_key_file, MISC_LENGTH);
   memcpy(config->tls_ca_file, reload->tls_ca_file, MISC_LENGTH);

   config->blocking_timeout = reload->blocking_timeout;
   config->authentication_timeout = reload->authentication_timeout;
   /* pidfile */
   restart_string("pidfile", config->pidfile, reload->pidfile);

   /* libev */
   restart_string("libev", config->libev, reload->libev);
   config->buffer_size = reload->buffer_size;
   config->keep_alive = reload->keep_alive;
   config->nodelay = reload->nodelay;
   config->non_blocking = reload->non_blocking;
   config->backlog = reload->backlog;
   /* hugepage */
   restart_int("hugepage", config->hugepage, reload->hugepage);

   /* update_process_title */
   restart_int("update_process_title", config->update_process_title, reload->update_process_title);

   /* unix_socket_dir */
   restart_string("unix_socket_dir", config->unix_socket_dir, reload->unix_socket_dir);

   memset(&config->servers[0], 0, sizeof(struct server) * NUMBER_OF_SERVERS);
   for (int i = 0; i < reload->number_of_servers; i++)
   {
      copy_server(&config->servers[i], &reload->servers[i]);
   }
   config->number_of_servers = reload->number_of_servers;

   memset(&config->users[0], 0, sizeof(struct user) * NUMBER_OF_USERS);
   for (int i = 0; i < reload->number_of_users; i++)
   {
      copy_user(&config->users[i], &reload->users[i]);
   }
   config->number_of_users = reload->number_of_users;

   memset(&config->admins[0], 0, sizeof(struct user) * NUMBER_OF_ADMINS);
   for (int i = 0; i < reload->number_of_admins; i++)
   {
      copy_user(&config->admins[i], &reload->admins[i]);
   }
   config->number_of_admins = reload->number_of_admins;

   config->workers = reload->workers;
   config->backup_max_rate = reload->backup_max_rate;
   config->network_max_rate = reload->network_max_rate;

   /* prometheus */

#ifdef HAVE_LINUX
   sd_notify(0, "READY=1");
#endif

   return 0;
}

static void
copy_server(struct server* dst, struct server* src)
{
   memcpy(&dst->name[0], &src->name[0], MISC_LENGTH);
   memcpy(&dst->host[0], &src->host[0], MISC_LENGTH);
   dst->port = src->port;
   memcpy(&dst->username[0], &src->username[0], MAX_USERNAME_LENGTH);
   dst->create_slot = src->create_slot;
   memcpy(&dst->wal_slot[0], &src->wal_slot[0], MISC_LENGTH);
   memcpy(&dst->follow[0], &src->follow[0], MISC_LENGTH);
   memcpy(&dst->wal_shipping[0], &src->wal_shipping[0], MAX_PATH);
   memcpy(&dst->hot_standby[0], &src->hot_standby[0], MAX_PATH);
   dst->cur_timeline = src->cur_timeline;
   dst->retention_days = src->retention_days;
   dst->retention_weeks = src->retention_weeks;
   dst->retention_months = src->retention_months;
   dst->retention_years = src->retention_years;
   /* dst->backup = src->backup; */
   /* dst->delete = src->delete; */
   dst->wal_streaming = src->wal_streaming;
   /* dst->valid = src->valid; */
   memcpy(&dst->current_wal_filename[0], &src->current_wal_filename[0], MISC_LENGTH);
   memcpy(&dst->current_wal_lsn[0], &src->current_wal_lsn[0], MISC_LENGTH);
   dst->workers = src->workers;
   dst->backup_max_rate = src->backup_max_rate;
   dst->network_max_rate = src->network_max_rate;
}

static void
copy_user(struct user* dst, struct user* src)
{
   memcpy(&dst->username[0], &src->username[0], MAX_USERNAME_LENGTH);
   memcpy(&dst->password[0], &src->password[0], MAX_PASSWORD_LENGTH);
}

static int
restart_int(char* name, int e, int n)
{
   if (e != n)
   {
      pgmoneta_log_info("Restart required for %s - Existing %d New %d", name, e, n);
      return 1;
   }

   return 0;
}

static int
restart_string(char* name, char* e, char* n)
{
   if (strcmp(e, n))
   {
      pgmoneta_log_info("Restart required for %s - Existing %s New %s", name, e, n);
      return 1;
   }

   return 0;
}

static bool
is_empty_string(char* s)
{
   if (s == NULL)
   {
      return true;
   }

   if (!strcmp(s, ""))
   {
      return true;
   }

   for (int i = 0; i < strlen(s); i++)
   {
      if (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')
      {
         /* Ok */
      }
      else
      {
         return false;
      }
   }

   return true;
}

static int
remove_leading_whitespace_and_comments(char* s, char** trimmed_line)
{
   // Find the index of the first non-whitespace character
   int i = 0;
   int last_non_whitespace_index = -1;
   char* result = NULL; // Temporary variable to hold the trimmed line
   while (s[i] != '\0' && isspace(s[i]))
   {
      i++;
   }

   // Loop through the string starting from non-whitespace character
   for (; s[i] != '\0'; i++)
   {
      if (s[i] == ';' || s[i] == '#')
      {
         result = pgmoneta_append_char(result, '\0');
         break; // Break loop if a comment character is encountered
      }
      if (!isspace(s[i]))
      {
         last_non_whitespace_index = i; // Update the index of the last non-whitespace character
      }
      result = pgmoneta_append_char(result, s[i]); // Append the current character to result
      if (result == NULL)
      {
         goto error;
      }
   }
   if (last_non_whitespace_index != -1)
   {
      result[last_non_whitespace_index + 1] = '\0'; // Null-terminate the string at the last non-whitespace character
   }

   *trimmed_line = result; // Assign result to trimmed_line

   return 0;

error:
   free(result); // Free memory in case of error
   *trimmed_line = NULL;
   return 1;
}
