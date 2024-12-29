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
#include <bzip2_compression.h>
#include <logging.h>
#include <management.h>
#include <utils.h>
#include <workers.h>

/* system */
#include <bzlib.h>
#include <dirent.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_LENGTH 8192

static int bzip2_compress(char* from, int level, char* to);
static int bzip2_decompress(char* from, char* to);
static int bzip2_decompress_file(char* from, char* to);

static void do_bzip2_compress(void* arg);
static void do_bzip2_decompress(void* arg);

void
pgmoneta_bzip2_data(char* directory, struct workers* workers)
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
      if (pgmoneta_ends_with(entry->d_name, "backup_manifest"))
      {
         continue;
      }

      if (entry->d_type == DT_DIR)
      {
         char path[MAX_PATH];

         if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
         {
            continue;
         }

         snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

         pgmoneta_bzip2_data(path, workers);
      }
      else if (entry->d_type == DT_REG)
      {
         if (pgmoneta_ends_with(entry->d_name, "backup_label"))
         {
            continue;
         }
         if (!pgmoneta_is_compressed_archive(entry->d_name) && !pgmoneta_is_encrypted_archive(entry->d_name))
         {
            from = NULL;

            from = pgmoneta_append(from, directory);
            from = pgmoneta_append(from, "/");
            from = pgmoneta_append(from, entry->d_name);

            to = NULL;

            to = pgmoneta_append(to, directory);
            to = pgmoneta_append(to, "/");
            to = pgmoneta_append(to, entry->d_name);
            to = pgmoneta_append(to, ".bz2");

            if (!pgmoneta_create_worker_input(directory, from, to, level, true, workers, &wi))
            {
               if (workers != NULL)
               {
                  pgmoneta_workers_add(workers, do_bzip2_compress, (void*)wi);
               }
               else
               {
                  do_bzip2_compress(wi);
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
do_bzip2_compress(void* arg)
{
   struct worker_input* wi = NULL;

   wi = (struct worker_input*)arg;

   if (pgmoneta_exists(wi->from))
   {
      if (bzip2_compress(wi->from, wi->level, wi->to))
      {
         pgmoneta_log_error("Bzip2: Could not compress %s", wi->from);
      }
      else
      {
         pgmoneta_delete_file(wi->from, true, NULL);
      }
   }

   free(wi);
}

void
pgmoneta_bzip2_tablespaces(char* root, struct workers* workers)
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

         pgmoneta_bzip2_data(path, workers);
      }
   }

   closedir(dir);
}

void
pgmoneta_bzip2_wal(char* directory)
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
      if (entry->d_type == DT_REG)
      {
         if (pgmoneta_is_compressed_archive(entry->d_name) ||
             pgmoneta_is_encrypted_archive(entry->d_name) ||
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
         to = pgmoneta_append(to, ".bz2");

         if (pgmoneta_exists(from))
         {
            if (bzip2_compress(from, level, to))
            {
               pgmoneta_log_error("Bzip2: Could not compress %s/%s", directory, entry->d_name);
               break;
            }
         }

         free(from);
         free(to);
      }
   }

   closedir(dir);
}

void
pgmoneta_bunzip2_data(char* directory, struct workers* workers)
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
         char path[MAX_PATH];

         if (!strcmp(entry->d_name, ".") || strcmp(entry->d_name, ".."))
         {
            continue;
         }

         snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

         pgmoneta_bunzip2_data(path, workers);
      }
      else
      {
         if (pgmoneta_ends_with(entry->d_name, ".bz2"))
         {
            from = NULL;

            from = pgmoneta_append(from, entry->d_name);
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

            if (!pgmoneta_create_worker_input(directory, from, to, 0, true, workers, &wi))
            {
               if (workers != NULL)
               {
                  pgmoneta_workers_add(workers, do_bzip2_decompress, (void*)wi);
               }
               else
               {
                  do_bzip2_decompress(wi);
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

void
pgmoneta_bunzip2_request(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* from = NULL;
   char* orig = NULL;
   char* to = NULL;
   char* elapsed = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   struct json* req = NULL;
   struct json* response = NULL;

   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   from = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_SOURCE_FILE);

   if (!pgmoneta_exists(from))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_BZIP2_NOFILE, compression, encryption, payload);
      pgmoneta_log_error("BZIP: No file for %s", from);
      goto error;
   }

   orig = pgmoneta_append(orig, from);
   to = pgmoneta_remove_suffix(orig, ".bz2");
   if (to == NULL)
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ALLOCATION, compression, encryption, payload);
      pgmoneta_log_error("BZIP: Allocation error");
      goto error;
   }

   if (bzip2_decompress(from, to))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_BZIP2_ERROR, compression, encryption, payload);
      pgmoneta_log_error("BZIP: Error bunzip2 %s", from);
      goto error;
   }

   pgmoneta_delete_file(from, true, NULL);

   if (pgmoneta_management_create_response(payload, -1, &response))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ALLOCATION, compression, encryption, payload);
      pgmoneta_log_error("BZIP: Allocation error");
      goto error;
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_DESTINATION_FILE, (uintptr_t)to, ValueString);

   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_BZIP2_NETWORK, compression, encryption, payload);
      pgmoneta_log_error("BZIP: Error sending response");
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("BZIP: %s (Elapsed: %s)", from, elapsed);

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

static void
do_bzip2_decompress(void* arg)
{
   struct worker_input* wi = NULL;

   wi = (struct worker_input*)arg;

   if (bzip2_decompress(wi->from, wi->to))
   {
      pgmoneta_log_error("Bzip2: Could not decompress %s", wi->from);
   }
   else
   {
      pgmoneta_delete_file(wi->from, true, NULL);
   }

   free(wi);
}

void
pgmoneta_bzip2_request(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* from = NULL;
   char* to = NULL;
   char* elapsed = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   struct json* req = NULL;
   struct json* response = NULL;

   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   from = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_SOURCE_FILE);

   if (!pgmoneta_exists(from))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_BZIP2_NOFILE, compression, encryption, payload);
      pgmoneta_log_error("BZIP: No file for %s", from);
      goto error;
   }

   to = pgmoneta_append(to, from);
   to = pgmoneta_append(to, ".bz2");
   if (to == NULL)
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ALLOCATION, compression, encryption, payload);
      pgmoneta_log_error("BZIP: Allocation error");
      goto error;
   }

   if (pgmoneta_bzip2_file(from, to))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_BZIP2_ERROR, compression, encryption, payload);
      pgmoneta_log_error("BZIP: Error bzip2 %s", from);
      goto error;
   }

   pgmoneta_delete_file(from, true, NULL);

   if (pgmoneta_management_create_response(payload, -1, &response))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ALLOCATION, compression, encryption, payload);
      pgmoneta_log_error("BZIP: Allocation error");
      goto error;
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_DESTINATION_FILE, (uintptr_t)to, ValueString);

   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_BZIP2_NETWORK, compression, encryption, payload);
      pgmoneta_log_error("BZIP: Error sending response");
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("BZIP: %s (Elapsed: %s)", from, elapsed);

   free(to);
   free(elapsed);

   exit(0);

error:

   free(to);
   free(elapsed);

   exit(1);
}

int
pgmoneta_bzip2_file(char* from, char* to)
{
   int level;
   struct configuration* config;

   config = (struct configuration*) shmem;

   level = config->compression_level;
   if (level < 1)
   {
      level = 1;
   }
   else if (level > 9)
   {
      level = 9;
   }

   if (bzip2_compress(from, level, to))
   {
      goto error;
   }
   else
   {
      pgmoneta_delete_file(from, true, NULL);
   }

   return 0;

error:
   return 1;
}

static int
bzip2_compress(char* from, int level, char* to)
{
   FILE* from_ptr = NULL;
   FILE* to_ptr = NULL;

   char buf[BUFFER_LENGTH] = {0};
   size_t buf_len = BUFFER_LENGTH;
   size_t length;
   int bzip2_err = 1;

   from_ptr = fopen(from, "r");
   if (!from_ptr)
   {
      goto error;
   }

   to_ptr = fopen(to, "wb+");
   if (!to_ptr)
   {
      goto error;
   }

   bzip2_err = BZ_OK;

   BZFILE* zip_file = BZ2_bzWriteOpen(&bzip2_err, to_ptr, level, 0, 0);
   if (bzip2_err != BZ_OK)
   {
      goto error_zip;
   }

   while ((length = fread(buf, sizeof(char), buf_len, from_ptr)) > 0)
   {
      BZ2_bzWrite(&bzip2_err, zip_file, buf, strlen(buf));
      if (bzip2_err != BZ_OK)
      {
         goto error_zip;
      }

      memset(buf, 0, buf_len);
   }

   BZ2_bzWriteClose(&bzip2_err, zip_file, 0, NULL, NULL);

   fclose(from_ptr);
   fclose(to_ptr);

   return 0;

error_zip:
   BZ2_bzWriteClose(&bzip2_err, zip_file, 0, NULL, NULL);

error:
   if (from_ptr)
   {
      fclose(from_ptr);
   }

   if (to_ptr)
   {
      fclose(to_ptr);
   }

   return 1;
}

static int
bzip2_decompress(char* from, char* to)
{
   FILE* from_ptr = NULL;
   FILE* to_ptr = NULL;

   char buf[BUFFER_LENGTH] = {0};
   size_t buf_len = BUFFER_LENGTH;
   int length = 0;
   int bzip2_err;
   BZFILE* zip_file = NULL;

   from_ptr = fopen(from, "r");
   if (!from_ptr)
   {
      goto error;
   }

   to_ptr = fopen(to, "wb+");
   if (!to_ptr)
   {
      goto error;
   }

   zip_file = BZ2_bzReadOpen(&bzip2_err, from_ptr, 0, 0, NULL, 0);
   if (bzip2_err != BZ_OK)
   {
      goto error_unzip;
   }

   do
   {
      length = BZ2_bzRead(&bzip2_err, zip_file, buf, (int) buf_len);
      if (bzip2_err != BZ_OK && bzip2_err != BZ_STREAM_END)
      {
         goto error_unzip;
      }

      if (length > 0)
      {
         if (fwrite(buf, 1, length, to_ptr) != (size_t)length)
         {
            goto error_unzip;
         }
      }

   }
   while (bzip2_err == BZ_STREAM_END && length == BUFFER_LENGTH);

   BZ2_bzReadClose(&bzip2_err, zip_file);

   fclose(from_ptr);
   fclose(to_ptr);

   return 0;

error_unzip:
   if (zip_file != NULL)
   {
      BZ2_bzReadClose(&bzip2_err, zip_file);
   }

error:
   if (to_ptr)
   {
      fclose(to_ptr);
   }

   if (from_ptr)
   {
      fclose(from_ptr);
   }

   return 1;
}

static int
bzip2_decompress_file(char* from, char* to)
{
   FILE* from_ptr = NULL;
   FILE* to_ptr = NULL;

   char buf[BUFFER_LENGTH] = {0};
   size_t buf_len = BUFFER_LENGTH;
   int length = 0;
   int bzip2_err = 1;
   BZFILE* zip_file = NULL;

   from_ptr = fopen(from, "r");
   if (!from_ptr)
   {
      goto error;
   }

   to_ptr = fopen(to, "wb+");
   if (!to_ptr)
   {
      goto error;
   }

   zip_file = BZ2_bzReadOpen(&bzip2_err, from_ptr, 0, 0, NULL, 0);
   if (bzip2_err != BZ_OK)
   {
      goto error_unzip;
   }

   do
   {
      length = BZ2_bzRead(&bzip2_err, zip_file, buf, (int)buf_len);
      if (bzip2_err != BZ_OK && bzip2_err != BZ_STREAM_END)
      {
         goto error_unzip;
      }

      if (length > 0)
      {
         if (fwrite(buf, 1, length, to_ptr) != (size_t)length)
         {
            goto error_unzip;
         }
      }

   }
   while (bzip2_err != BZ_STREAM_END);

   BZ2_bzReadClose(&bzip2_err, zip_file);
   zip_file = NULL;

   fclose(from_ptr);
   fclose(to_ptr);

   return 0;

error_unzip:
   if (zip_file)
   {
      BZ2_bzReadClose(&bzip2_err, zip_file);
   }

error:
   if (to_ptr)
   {
      fclose(to_ptr);
   }

   if (from_ptr)
   {
      fclose(from_ptr);
   }

   return 1;
}

int
pgmoneta_bunzip2_file(char* from, char* to)
{
   if (pgmoneta_ends_with(from, ".bz2"))
   {
      if (bzip2_decompress_file(from, to))
      {
         pgmoneta_log_error("Bzip2: Could not decompress %s", from);
         goto error;
      }

      pgmoneta_delete_file(from, true, NULL);
   }
   else
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_bzip2_string(char* s, unsigned char** buffer, size_t* buffer_size)
{
   size_t source_len;
   unsigned int dest_len;
   int bzip2_err;

   source_len = strlen(s);
   dest_len = source_len + (source_len * 0.01) + 600;

   *buffer = (unsigned char*)malloc(dest_len);
   if (!*buffer)
   {
      pgmoneta_log_error("Bzip2: Allocation failed");
      return 1;
   }

   bzip2_err = BZ2_bzBuffToBuffCompress((char*)(*buffer), &dest_len, s, source_len, 9, 0, 0);
   if (bzip2_err != BZ_OK)
   {
      pgmoneta_log_error("Bzip2: Compress failed");
      free(*buffer);
      return 1;
   }

   *buffer_size = dest_len;
   return 0;
}

int
pgmoneta_bunzip2_string(unsigned char* compressed_buffer, size_t compressed_size, char** output_string)
{
   int bzip2_err;
   unsigned int estimated_size = compressed_size * 10;
   unsigned int new_size;

   *output_string = (char*)malloc(estimated_size);
   if (!*output_string)
   {
      pgmoneta_log_error("Bzip2: Allocation failed");
      return 1;
   }

   bzip2_err = BZ2_bzBuffToBuffDecompress(*output_string, &estimated_size, (char*)compressed_buffer, compressed_size, 0, 0);

   if (bzip2_err == BZ_OUTBUFF_FULL)
   {
      new_size = estimated_size * 2;
      char* temp = realloc(*output_string, new_size);

      if (!temp)
      {
         pgmoneta_log_error("Bzip2: Reallocation failed");
         free(*output_string);
         return 1;
      }

      *output_string = temp;

      bzip2_err = BZ2_bzBuffToBuffDecompress(*output_string, &new_size, (char*)compressed_buffer, compressed_size, 0, 0);
      if (bzip2_err != BZ_OK)
      {
         pgmoneta_log_error("Bzip2: Decompress failed");
         free(*output_string);
         return 1;
      }
      estimated_size = new_size;
   }
   else if (bzip2_err != BZ_OK)
   {
      pgmoneta_log_error("Bzip2: Decompress failed");
      free(*output_string);
      return 1;
   }

   return 0;
}
