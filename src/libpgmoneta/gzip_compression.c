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
#include <gzip_compression.h>
#include <json.h>
#include <logging.h>
#include <management.h>
#include <utils.h>
#include <workers.h>

/* system */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_LENGTH 8192

static int gz_compress(char* from, int level, char* to);
static int gz_decompress(char* from, char* to);

static void do_gz_compress(void* arg);
static void do_gz_decompress(void* arg);

void
pgmoneta_gzip_data(char* directory, struct workers* workers)
{
   char* from = NULL;
   char* to = NULL;
   DIR* dir;
   struct dirent* entry;
   int level;
   struct worker_input* wi = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (!(dir = opendir(directory)))
   {
      return;
   }

   level = config->compression_level;
   if (level < 1)
   {
      level = 1;
   }
   else if (level > 9)
   {
      level = 9;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_DIR)
      {
         char path[1024];

         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }

         snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

         pgmoneta_gzip_data(path, workers);
      }
      else if (entry->d_type == DT_REG)
      {
         if (!pgmoneta_is_file_archive(entry->d_name))
         {
            from = NULL;

            from = pgmoneta_append(from, directory);
            from = pgmoneta_append(from, "/");
            from = pgmoneta_append(from, entry->d_name);

            to = NULL;

            to = pgmoneta_append(to, directory);
            to = pgmoneta_append(to, "/");
            to = pgmoneta_append(to, entry->d_name);
            to = pgmoneta_append(to, ".gz");

            if (!pgmoneta_create_worker_input(directory, from, to, level, workers, &wi))
            {
               if (workers != NULL)
               {
                  pgmoneta_workers_add(workers, do_gz_compress, (void*)wi);
               }
               else
               {
                  do_gz_compress(wi);
               }
            }

            free(from);
            free(to);
         }
      }
   }

   closedir(dir);
}

static void
do_gz_compress(void* arg)
{
   struct worker_input* wi = NULL;

   wi = (struct worker_input*)arg;

   if (pgmoneta_exists(wi->from))
   {
      if (gz_compress(wi->from, wi->level, wi->to))
      {
         pgmoneta_log_error("Gzip: Could not compress %s", wi->from);
      }
      else
      {
         pgmoneta_delete_file(wi->from, NULL);
      }
   }

   free(wi);
}

void
pgmoneta_gzip_tablespaces(char* root, struct workers* workers)
{
   DIR* dir;
   struct dirent* entry;

   if (!(dir = opendir(root)))
   {
      return;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_DIR)
      {
         char path[1024];

         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "data") == 0)
         {
            continue;
         }

         snprintf(path, sizeof(path), "%s/%s", root, entry->d_name);

         pgmoneta_gzip_data(path, workers);
      }
   }

   closedir(dir);
}

void
pgmoneta_gzip_wal(char* directory)
{
   char* from = NULL;
   char* to = NULL;
   DIR* dir;
   struct dirent* entry;
   int level;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (!(dir = opendir(directory)))
   {
      return;
   }

   level = config->compression_level;
   if (level < 1)
   {
      level = 1;
   }
   else if (level > 9)
   {
      level = 9;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (pgmoneta_ends_with(entry->d_name, "backup_label"))
      {
         continue;
      }
      if (entry->d_type == DT_REG)
      {
         if (pgmoneta_is_file_archive(entry->d_name) ||
             pgmoneta_ends_with(entry->d_name, ".partial") ||
             pgmoneta_ends_with(entry->d_name, ".history"))
         {
            continue;
         }

         from = NULL;

         from = pgmoneta_append(from, directory);
         from = pgmoneta_append(from, "/");
         from = pgmoneta_append(from, entry->d_name);

         to = NULL;

         to = pgmoneta_append(to, directory);
         to = pgmoneta_append(to, "/");
         to = pgmoneta_append(to, entry->d_name);
         to = pgmoneta_append(to, ".gz");

         if (pgmoneta_exists(from))
         {
            if (gz_compress(from, level, to))
            {
               pgmoneta_log_error("Gzip: Could not compress %s/%s", directory, entry->d_name);
               break;
            }

            pgmoneta_delete_file(from, NULL);
            pgmoneta_permission(to, 6, 0, 0);
         }

         free(from);
         free(to);
      }
   }

   closedir(dir);
}

void
pgmoneta_gzip_request(SSL* ssl, int client_fd, struct json* payload)
{
   char* from = NULL;
   char* to = NULL;
   char* elapsed = NULL;
   time_t start_time;
   time_t end_time;
   int total_seconds;
   struct json* req = NULL;
   struct json* response = NULL;

   start_time = time(NULL);

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   from = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_SOURCE_FILE);

   if (!pgmoneta_exists(from))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_GZIP_NOFILE, payload);
      pgmoneta_log_error("GZip: No file for %s", from);
      goto error;
   }

   to = pgmoneta_append(to, from);
   to = pgmoneta_append(to, ".gz");
   if (to == NULL)
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ALLOCATION, payload);
      pgmoneta_log_error("GZip: Allocation error");
      goto error;
   }

   if (pgmoneta_gzip_file(from, to))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_GZIP_ERROR, payload);
      pgmoneta_log_error("GZip: Error gzip %s", from);
      goto error;
   }

   pgmoneta_delete_file(from, NULL);

   if (pgmoneta_management_create_response(payload, &response))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ALLOCATION, payload);
      pgmoneta_log_error("GZip: Allocation error");
      goto error;
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_DESTINATION_FILE, (uintptr_t)to, ValueString);

   end_time = time(NULL);

   if (pgmoneta_management_response_ok(NULL, client_fd, start_time, end_time, payload))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_GZIP_NETWORK, payload);
      pgmoneta_log_error("GZip: Error sending response");
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_time, end_time, &total_seconds);

   pgmoneta_log_info("GZip: %s (Elapsed: %s)", from, elapsed);

   free(to);
   free(elapsed);

   exit(0);

error:

   free(to);
   free(elapsed);

   exit(1);
}

int
pgmoneta_gzip_file(char* from, char* to)
{
   int level;
   struct configuration* config;

   config = (struct configuration*)shmem;

   level = config->compression_level;
   if (level < 1)
   {
      level = 1;
   }
   else if (level > 9)
   {
      level = 9;
   }

   if (gz_compress(from, level, to))
   {
      goto error;
   }
   else
   {
      pgmoneta_delete_file(from, NULL);
   }

   return 0;

error:

   return 1;
}

void
pgmoneta_gunzip_request(SSL* ssl, int client_fd, struct json* payload)
{
   char* from = NULL;
   char* orig = NULL;
   char* to = NULL;
   char* elapsed = NULL;
   time_t start_time;
   time_t end_time;
   int total_seconds;
   struct json* req = NULL;
   struct json* response = NULL;

   start_time = time(NULL);

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   from = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_SOURCE_FILE);

   if (!pgmoneta_exists(from))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_GZIP_NOFILE, payload);
      pgmoneta_log_error("GZip: No file for %s", from);
      goto error;
   }

   orig = pgmoneta_append(orig, from);
   to = pgmoneta_remove_suffix(orig, ".gz");
   if (to == NULL)
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ALLOCATION, payload);
      pgmoneta_log_error("GZip: Allocation error");
      goto error;
   }

   if (pgmoneta_gunzip_file(from, to))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_GZIP_ERROR, payload);
      pgmoneta_log_error("GZip: Error gunzip %s", from);
      goto error;
   }

   pgmoneta_delete_file(from, NULL);

   if (pgmoneta_management_create_response(payload, &response))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ALLOCATION, payload);
      pgmoneta_log_error("GZip: Allocation error");
      goto error;
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_DESTINATION_FILE, (uintptr_t)to, ValueString);

   end_time = time(NULL);

   if (pgmoneta_management_response_ok(NULL, client_fd, start_time, end_time, payload))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_GZIP_NETWORK, payload);
      pgmoneta_log_error("GZip: Error sending response");
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_time, end_time, &total_seconds);

   pgmoneta_log_info("GZip: %s (Elapsed: %s)", from, elapsed);

   free(orig);
   free(to);
   free(elapsed);

   exit(0);

error:

   free(orig);
   free(to);
   free(elapsed);

   exit(1);
}

int
pgmoneta_gunzip_file(char* from, char* to)
{
   if (pgmoneta_ends_with(from, ".gz"))
   {
      if (gz_decompress(from, to))
      {
         pgmoneta_log_error("Gzip: Could not decompress %s", from);
         goto error;
      }

      pgmoneta_delete_file(from, NULL);
   }
   else
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

void
pgmoneta_gunzip_data(char* directory, struct workers* workers)
{
   char* from = NULL;
   char* to = NULL;
   char* name = NULL;
   DIR* dir;
   struct worker_input* wi = NULL;
   struct dirent* entry;

   if (!(dir = opendir(directory)))
   {
      return;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_DIR)
      {
         char path[1024];

         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }

         snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

         pgmoneta_gunzip_data(path, workers);
      }
      else
      {
         if (pgmoneta_ends_with(entry->d_name, ".gz"))
         {
            from = NULL;

            from = pgmoneta_append(from, directory);
            from = pgmoneta_append(from, "/");
            from = pgmoneta_append(from, entry->d_name);

            name = malloc(strlen(entry->d_name) - 2);

            if (name == NULL)
            {
               goto error;
            }

            memset(name, 0, strlen(entry->d_name) - 2);
            memcpy(name, entry->d_name, strlen(entry->d_name) - 3);

            to = NULL;

            to = pgmoneta_append(to, directory);
            to = pgmoneta_append(to, "/");
            to = pgmoneta_append(to, name);

            if (!pgmoneta_create_worker_input(directory, from, to, 0, workers, &wi))
            {
               if (workers != NULL)
               {
                  pgmoneta_workers_add(workers, do_gz_decompress, (void*)wi);
               }
               else
               {
                  do_gz_decompress(wi);
               }
            }

            free(name);
            free(from);
            free(to);
         }
      }
   }

   closedir(dir);
   return;

error:

   if (dir != NULL)
   {
      closedir(dir);
   }
}

static void
do_gz_decompress(void* arg)
{
   struct worker_input* wi = NULL;

   wi = (struct worker_input*)arg;

   if (gz_decompress(wi->from, wi->to))
   {
      pgmoneta_log_error("Gzip: Could not decompress %s", wi->from);
   }
   else
   {
      pgmoneta_delete_file(wi->from, NULL);
   }

   free(wi);
}

static int
gz_compress(char* from, int level, char* to)
{
   char buf[BUFFER_LENGTH];
   FILE* in = NULL;
   char mode[4];
   gzFile out = NULL;
   size_t length;

   in = fopen(from, "rb");
   if (in == NULL)
   {
      goto error;
   }

   memset(&mode[0], 0, sizeof(mode));
   mode[0] = 'w';
   mode[1] = 'b';
   mode[2] = '0' + level;

   out = gzopen(to, mode);
   if (out == NULL)
   {
      goto error;
   }

   do
   {
      length = fread(buf, 1, sizeof(buf), in);

      if (ferror(in))
      {
         goto error;
      }

      if (length > 0)
      {
         if (gzwrite(out, buf, (unsigned)length) != (int)length)
         {
            goto error;
         }
      }
   }
   while (length > 0);

   fclose(in);
   in = NULL;

   if (gzclose(out) != Z_OK)
   {
      out = NULL;
      goto error;
   }

   return 0;

error:

   if (in != NULL)
   {
      fclose(in);
   }

   if (out != NULL)
   {
      gzclose(out);
   }

   return 1;
}

static int
gz_decompress(char* from, char* to)
{
   char buf[BUFFER_LENGTH];
   FILE* out = NULL;
   char mode[3];
   gzFile in = NULL;
   size_t length;

   memset(&mode[0], 0, sizeof(mode));
   mode[0] = 'r';
   mode[1] = 'b';

   in = gzopen(from, mode);
   if (in == NULL)
   {
      goto error;
   }

   out = fopen(to, "wb");
   if (out == NULL)
   {
      goto error;
   }

   do
   {
      length = (int)gzread(in, buf, (unsigned)BUFFER_LENGTH);

      if (length > 0)
      {
         if (fwrite(buf, 1, length, out) != length)
         {
            goto error;
         }
      }
   }
   while (length > 0);

   if (gzclose(in) != Z_OK)
   {
      in = NULL;
      goto error;
   }

   fclose(out);

   return 0;

error:

   if (in != NULL)
   {
      gzclose(in);
   }

   if (out != NULL)
   {
      fclose(out);
   }

   return 1;
}
