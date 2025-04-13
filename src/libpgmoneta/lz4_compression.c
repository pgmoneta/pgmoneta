/*
 * Copyright (C) 2021 Red Hat
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <logging.h>
#include <lz4.h>
#include <lz4_compression.h>
#include <management.h>
#include <utils.h>

/* system */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define NAME "lz4"

static int lz4_compress(char* from, char* to);
static int lz4_decompress(char* from, char* to);

static void do_lz4_compress(struct worker_common* wc);
static void do_lz4_decompress(struct worker_common* wc);

int
pgmoneta_lz4c_data(char* directory, struct workers* workers)
{
   char* from = NULL;
   char* to = NULL;
   DIR* dir;
   struct worker_input* wi = NULL;
   struct dirent* entry;

   if (!(dir = opendir(directory)))
   {
      goto error;
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

         pgmoneta_lz4c_data(path, workers);
      }
      else if (entry->d_type == DT_REG)
      {
         if (pgmoneta_ends_with(entry->d_name, "backup_manifest") ||
             pgmoneta_ends_with(entry->d_name, "backup_label"))
         {
            continue;
         }

         from = pgmoneta_append(from, directory);
         from = pgmoneta_append(from, "/");
         from = pgmoneta_append(from, entry->d_name);

         to = pgmoneta_append(to, directory);
         to = pgmoneta_append(to, "/");
         to = pgmoneta_append(to, entry->d_name);
         to = pgmoneta_append(to, ".lz4");

         if (!pgmoneta_create_worker_input(directory, from, to, 0, workers, &wi))
         {
            if (workers != NULL)
            {
               if (workers->outcome)
               {
                  pgmoneta_workers_add(workers, do_lz4_compress, (struct worker_common*)wi);
               }
            }
            else
            {
               do_lz4_compress((struct worker_common*)wi);
            }
         }
         else
         {
            goto error;
         }

         free(from);
         free(to);

         from = NULL;
         to = NULL;
      }
   }

   closedir(dir);

   free(from);
   free(to);

   return 0;

error:

   if (dir != NULL)
   {
      closedir(dir);
   }

   free(from);
   free(to);

   return 1;
}

static void
do_lz4_compress(struct worker_common* wc)
{
   struct worker_input* wi = (struct worker_input*)wc;

   if (pgmoneta_exists(wi->from))
   {
      if (lz4_compress(wi->from, wi->to))
      {
         pgmoneta_log_error("LZ4: Could not compress %s", wi->from);
      }
      else
      {
         pgmoneta_delete_file(wi->from, NULL);
      }
   }

   free(wi);
}

void
pgmoneta_lz4c_wal(char* directory)
{
   char* from = NULL;
   char* to = NULL;
   DIR* dir;
   struct dirent* entry;

   if (!(dir = opendir(directory)))
   {
      return;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_REG)
      {
         if (pgmoneta_is_compressed(entry->d_name) ||
             pgmoneta_is_encrypted(entry->d_name) ||
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
         to = pgmoneta_append(to, ".lz4");

         lz4_compress(from, to);

         if (pgmoneta_exists(from))
         {
            pgmoneta_delete_file(from, NULL);
         }
         else
         {
            pgmoneta_log_debug("%s doesn't exists", from);
         }
         pgmoneta_permission(to, 6, 0, 0);

         free(from);
         free(to);

         from = NULL;
         to = NULL;
      }
   }

   closedir(dir);

   free(from);
   free(to);
}

void
pgmoneta_lz4c_tablespaces(char* root, struct workers* workers)
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

         pgmoneta_lz4c_data(path, workers);
      }
   }

   closedir(dir);
}

int
pgmoneta_lz4d_data(char* directory, struct workers* workers)
{
   char* from = NULL;
   char* to = NULL;
   char* name = NULL;
   DIR* dir;
   struct worker_input* wi = NULL;
   struct dirent* entry;

   if (!(dir = opendir(directory)))
   {
      goto error;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_DIR || entry->d_type == DT_LNK)
      {
         char path[1024];

         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }

         snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

         pgmoneta_lz4d_data(path, workers);
      }
      else
      {
         from = pgmoneta_append(from, directory);
         from = pgmoneta_append(from, "/");
         from = pgmoneta_append(from, entry->d_name);

         name = malloc(strlen(entry->d_name) - 3);

         if (name == NULL)
         {
            goto error;
         }

         memset(name, 0, strlen(entry->d_name) - 3);
         memcpy(name, entry->d_name, strlen(entry->d_name) - 4);

         to = pgmoneta_append(to, directory);
         to = pgmoneta_append(to, "/");
         to = pgmoneta_append(to, name);

         if (!pgmoneta_create_worker_input(directory, from, to, 0, workers, &wi))
         {
            if (workers != NULL)
            {
               if (workers->outcome)
               {
                  pgmoneta_workers_add(workers, do_lz4_decompress, (struct worker_common*)wi);
               }
            }
            else
            {
               do_lz4_decompress((struct worker_common*)wi);
            }
         }
         else
         {
            goto error;
         }

         free(name);
         free(from);
         free(to);

         name = NULL;
         from = NULL;
         to = NULL;
      }
   }

   closedir(dir);

   free(name);
   free(from);
   free(to);

   return 0;

error:

   if (dir != NULL)
   {
      closedir(dir);
   }

   free(name);
   free(from);
   free(to);

   return 1;
}

static void
do_lz4_decompress(struct worker_common* wc)
{
   struct worker_input* wi = (struct worker_input*)wc;

   if (pgmoneta_exists(wi->from))
   {
      if (lz4_decompress(wi->from, wi->to))
      {
         pgmoneta_log_error("LZ4: Could not decompress %s", wi->from);
      }
      else
      {
         pgmoneta_delete_file(wi->from, NULL);
      }
   }

   free(wi);
}

void
pgmoneta_lz4d_request(SSL* ssl __attribute__((unused)), int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
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

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   from = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_SOURCE_FILE);

   if (!pgmoneta_exists(from))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_LZ4_NOFILE, NAME, compression, encryption, payload);
      pgmoneta_log_error("LZ4: No file for %s", from);
      goto error;
   }

   orig = pgmoneta_append(orig, from);
   to = pgmoneta_remove_suffix(orig, ".lz4");
   if (to == NULL)
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ALLOCATION, NAME, compression, encryption, payload);
      pgmoneta_log_error("LZ4: Allocation error");
      goto error;
   }

   if (pgmoneta_lz4d_file(from, to))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_LZ4_ERROR, NAME, compression, encryption, payload);
      pgmoneta_log_error("LZ4: Error lz4 %s", from);
      goto error;
   }

   if (pgmoneta_exists(from))
   {
      pgmoneta_delete_file(from, NULL);
   }
   else
   {
      pgmoneta_log_debug("%s doesn't exists", from);
   }

   if (pgmoneta_management_create_response(payload, -1, &response))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ALLOCATION, NAME, compression, encryption, payload);
      pgmoneta_log_error("LZ4: Allocation error");
      goto error;
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_DESTINATION_FILE, (uintptr_t)to, ValueString);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_LZ4_NETWORK, NAME, compression, encryption, payload);
      pgmoneta_log_error("LZ4: Error sending response");
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("LZ4: %s (Elapsed: %s)", from, elapsed);

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
pgmoneta_lz4d_file(char* from, char* to)
{
   if (pgmoneta_ends_with(from, ".lz4"))
   {
      if (lz4_decompress(from, to))
      {
         pgmoneta_log_error("LZ4: Could not decompress %s", from);
         goto error;
      }

      if (pgmoneta_exists(from))
      {
         pgmoneta_delete_file(from, NULL);
      }
      else
      {
         pgmoneta_log_debug("%s doesn't exists", from);
      }
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
pgmoneta_lz4c_request(SSL* ssl __attribute__((unused)), int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* from = NULL;
   char* to = NULL;
   char* elapsed = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   struct json* req = NULL;
   struct json* response = NULL;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   from = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_SOURCE_FILE);

   if (!pgmoneta_exists(from))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_LZ4_NOFILE, NAME, compression, encryption, payload);
      pgmoneta_log_error("LZ4: No file for %s", from);
      goto error;
   }

   to = pgmoneta_append(to, from);
   to = pgmoneta_append(to, ".lz4");
   if (to == NULL)
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ALLOCATION, NAME, compression, encryption, payload);
      pgmoneta_log_error("LZ4: Allocation error");
      goto error;
   }

   if (pgmoneta_lz4c_file(from, to))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_LZ4_ERROR, NAME, compression, encryption, payload);
      pgmoneta_log_error("LZ4: Error lz4 %s", from);
      goto error;
   }

   if (pgmoneta_exists(from))
   {
      pgmoneta_delete_file(from, NULL);
   }
   else
   {
      pgmoneta_log_debug("%s doesn't exists", from);
   }

   if (pgmoneta_management_create_response(payload, -1, &response))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ALLOCATION, NAME, compression, encryption, payload);
      pgmoneta_log_error("LZ4: Allocation error");
      goto error;
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_DESTINATION_FILE, (uintptr_t)to, ValueString);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_LZ4_NETWORK, NAME, compression, encryption, payload);
      pgmoneta_log_error("LZ4: Error sending response");
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("LZ4: %s (Elapsed: %s)", from, elapsed);

   free(to);
   free(elapsed);

   exit(0);

error:

   free(to);
   free(elapsed);

   exit(1);
}

int
pgmoneta_lz4c_file(char* from, char* to)
{
   if (pgmoneta_exists(from))
   {
      if (lz4_compress(from, to))
      {
         pgmoneta_log_error("LZ4: Could not compress %s", from);
      }
      else
      {
         pgmoneta_delete_file(from, NULL);
      }
   }

   return 0;
}

static int
lz4_compress(char* from, char* to)
{
   LZ4_stream_t* lz4Stream = NULL;
   FILE* fin = NULL;
   FILE* fout = NULL;
   char buffIn[2][BLOCK_BYTES];
   int buffInIndex = 0;
   char buffOut[LZ4_COMPRESSBOUND(BLOCK_BYTES)];

   lz4Stream = LZ4_createStream();
   fin = fopen(from, "rb");

   if (fin == NULL)
   {
      goto error;
   }

   fout = fopen(to, "wb");

   if (fout == NULL)
   {
      goto error;
   }

   for (;;)
   {
      size_t read = fread(buffIn[buffInIndex], sizeof(char), BLOCK_BYTES, fin);
      if (read == 0)
      {
         break;
      }

      int compression = LZ4_compress_fast_continue(lz4Stream, buffIn[buffInIndex], buffOut, read, sizeof(buffOut), 1);
      if (compression <= 0)
      {
         break;
      }

      fwrite(&compression, sizeof(compression), 1, fout);
      fwrite(buffOut, sizeof(char), (size_t)compression, fout);

      buffInIndex = (buffInIndex + 1) % 2;
   }

   fclose(fout);
   fclose(fin);
   LZ4_freeStream(lz4Stream);

   return 0;

error:

   if (fin != NULL)
   {
      fclose(fin);
   }

   if (fout != NULL)
   {
      fclose(fout);
   }

   return 1;
}

static int
lz4_decompress(char* from, char* to)
{
   LZ4_streamDecode_t lz4StreamDecodeBody;
   LZ4_streamDecode_t* lz4StreamDecode = NULL;
   FILE* fin = NULL;
   FILE* fout = NULL;
   char buffIn[2][BLOCK_BYTES];
   int buffInIndex = 0;
   char buffOut[LZ4_COMPRESSBOUND(BLOCK_BYTES)];
   size_t read = 0;

   lz4StreamDecode = &lz4StreamDecodeBody;
   fin = fopen(from, "rb");

   if (fin == NULL)
   {
      goto error;
   }

   fout = fopen(to, "wb");

   if (fout == NULL)
   {
      goto error;
   }

   LZ4_setStreamDecode(lz4StreamDecode, NULL, 0);

   for (;;)
   {
      int compression = 0;

      //If return value 1,read bytes == sizeof(int)
      //If return value 0,read bytes  < sizeof(int)
      read = fread(&compression, 1, sizeof(compression), fin);
      if (read == 0)
      {
         break;
      }
      if (read < sizeof(compression))
      {
         pgmoneta_log_error("lz4_decompression from file compression bytes < sizeof(int)");
         goto error;
      }

      read = fread(buffOut, sizeof(char), compression, fin);
      if (read == 0)
      {
         break;
      }

      int decompression = LZ4_decompress_safe_continue(lz4StreamDecode, buffOut, buffIn[buffInIndex], compression, BLOCK_BYTES);
      if (decompression <= 0)
      {
         break;
      }

      fwrite(buffIn[buffInIndex], sizeof(char), decompression, fout);

      buffInIndex = (buffInIndex + 1) % 2;
   }

   fclose(fout);
   fclose(fin);

   return 0;

error:
   if (fin != NULL)
   {
      fclose(fin);
   }

   if (fout != NULL)
   {
      fclose(fout);
   }

   return 1;
}

int
pgmoneta_lz4c_string(char* s, unsigned char** buffer, size_t* buffer_size)
{
   size_t input_size;
   size_t max_compressed_size;
   int compressed_size;

   input_size = strlen(s);
   max_compressed_size = LZ4_compressBound(input_size);

   *buffer = (unsigned char*)malloc(max_compressed_size);
   if (*buffer == NULL)
   {
      pgmoneta_log_error("LZ4: Allocation failed");
      return 1;
   }

   compressed_size = LZ4_compress_default(s, (char*)*buffer, input_size, max_compressed_size);
   if (compressed_size <= 0)
   {
      pgmoneta_log_error("LZ4: Compress failed");
      free(*buffer);
      return 1;
   }

   *buffer_size = (size_t)compressed_size;

   return 0;
}

int
pgmoneta_lz4d_string(unsigned char* compressed_buffer, size_t compressed_size, char** output_string)
{
   size_t max_decompressed_size;
   int decompressed_size;

   max_decompressed_size = compressed_size * 4;

   *output_string = (char*)malloc(max_decompressed_size);
   if (*output_string == NULL)
   {
      pgmoneta_log_error("LZ4: Allocation failed");
      return 1;
   }

   decompressed_size = LZ4_decompress_safe((char*)compressed_buffer, *output_string, compressed_size, max_decompressed_size);
   if (decompressed_size < 0)
   {
      pgmoneta_log_error("LZ4: Decompress failed");
      free(*output_string);
      return 1;
   }

   (*output_string)[decompressed_size] = '\0';

   return 0;
}
