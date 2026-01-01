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
#include <logging.h>
#include <management.h>
#include <utils.h>
#include <zstandard_compression.h>

/* system */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zstd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define NAME                           "zstd"
#define ZSTD_DEFAULT_NUMBER_OF_WORKERS 4

static int zstd_compress(char* from, char* to, ZSTD_CCtx* cctx, size_t zin_size, void* zin, size_t zout_size, void* zout);
static int zstd_decompress(char* from, char* to, ZSTD_DCtx* dctx, size_t zin_size, void* zin, size_t zout_size, void* zout);

void
pgmoneta_zstandardc_data(char* directory, struct workers* workers)
{
   size_t zin_size = 0;
   void* zin = NULL;
   size_t zout_size = 0;
   void* zout = NULL;
   ZSTD_CCtx* cctx = NULL;
   char* from = NULL;
   char* to = NULL;
   DIR* dir = NULL;
   struct dirent* entry;
   int level;
   int ws;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (!(dir = opendir(directory)))
   {
      return;
   }

   level = config->compression_level;
   if (level < 1)
   {
      level = 1;
   }
   else if (level > 19)
   {
      level = 19;
   }

   ws = config->workers != 0 ? config->workers : ZSTD_DEFAULT_NUMBER_OF_WORKERS;

   zin_size = ZSTD_CStreamInSize();
   zin = malloc(zin_size);
   if (zin == NULL)
   {
      pgmoneta_log_error("ZSTD: Allocation failed (input buffer)");
      goto error;
   }

   zout_size = ZSTD_CStreamOutSize();
   zout = malloc(zout_size);
   if (zout == NULL)
   {
      pgmoneta_log_error("ZSTD: Allocation failed (output buffer)");
      goto error;
   }

   cctx = ZSTD_createCCtx();
   if (cctx == NULL)
   {
      pgmoneta_log_error("ZSTD: Could not create compression context");
      goto error;
   }

   ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, level);
   ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 1);
   ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, ws);

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

         pgmoneta_zstandardc_data(path, workers);
      }
      else if (entry->d_type == DT_REG)
      {
         if (pgmoneta_ends_with(entry->d_name, "backup_manifest") ||
             pgmoneta_ends_with(entry->d_name, "backup_label"))
         {
            continue;
         }

         if (!pgmoneta_is_compressed(entry->d_name) &&
             !pgmoneta_is_encrypted(entry->d_name))
         {
            from = NULL;

            from = pgmoneta_append(from, directory);
            from = pgmoneta_append(from, "/");
            from = pgmoneta_append(from, entry->d_name);

            to = NULL;

            to = pgmoneta_append(to, directory);
            to = pgmoneta_append(to, "/");
            to = pgmoneta_append(to, entry->d_name);
            to = pgmoneta_append(to, ".zstd");

            if (pgmoneta_exists(from))
            {
               if (zstd_compress(from, to, cctx, zin_size, zin, zout_size, zout))
               {
                  pgmoneta_log_error("ZSTD: Could not compress %s/%s", directory, entry->d_name);
                  break;
               }

               if (pgmoneta_exists(from))
               {
                  pgmoneta_delete_file(from, NULL);
               }
               pgmoneta_permission(to, 6, 0, 0);

               memset(zin, 0, zin_size);
               memset(zout, 0, zout_size);
            }

            free(from);
            free(to);

            from = NULL;
            to = NULL;
         }
      }
   }

   closedir(dir);

   ZSTD_freeCCtx(cctx);

   free(zin);
   free(zout);

   free(from);
   free(to);

   return;

error:

   if (dir != NULL)
   {
      closedir(dir);
   }

   if (cctx != NULL)
   {
      ZSTD_freeCCtx(cctx);
   }

   free(zin);
   free(zout);

   free(from);
   free(to);
}

void
pgmoneta_zstandardc_tablespaces(char* root, struct workers* workers)
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

         pgmoneta_zstandardc_data(path, workers);
      }
   }

   closedir(dir);
}

void
pgmoneta_zstandardc_wal(char* directory)
{
   size_t zin_size = 0;
   void* zin = NULL;
   size_t zout_size = 0;
   void* zout = NULL;
   ZSTD_CCtx* cctx = NULL;
   char* from = NULL;
   char* to = NULL;
   DIR* dir = NULL;
   struct dirent* entry;
   int level;
   int workers;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (!(dir = opendir(directory)))
   {
      return;
   }

   level = config->compression_level;
   if (level < 1)
   {
      level = 1;
   }
   else if (level > 19)
   {
      level = 19;
   }

   workers = config->workers != 0 ? config->workers : ZSTD_DEFAULT_NUMBER_OF_WORKERS;

   zin_size = ZSTD_CStreamInSize();
   zin = malloc(zin_size);
   if (zin == NULL)
   {
      pgmoneta_log_error("ZSTD: Allocation failed (input buffer)");
      goto error;
   }
   zout_size = ZSTD_CStreamOutSize();
   zout = malloc(zout_size);
   if (zout == NULL)
   {
      pgmoneta_log_error("ZSTD: Allocation failed (output buffer)");
      goto error;
   }

   cctx = ZSTD_createCCtx();
   if (cctx == NULL)
   {
      pgmoneta_log_error("ZSTD: Could not create compression context");
      goto error;
   }

   ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, level);
   ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 1);
   ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, workers);

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
         to = pgmoneta_append(to, ".zstd");

         if (pgmoneta_exists(from))
         {
            if (zstd_compress(from, to, cctx, zin_size, zin, zout_size, zout))
            {
               pgmoneta_log_error("ZSTD: Could not compress %s/%s", directory, entry->d_name);
               break;
            }

            if (pgmoneta_exists(from))
            {
               pgmoneta_delete_file(from, NULL);
            }
            pgmoneta_permission(to, 6, 0, 0);

            memset(zin, 0, zin_size);
            memset(zout, 0, zout_size);
         }

         free(from);
         free(to);

         from = NULL;
         to = NULL;
      }
   }

   closedir(dir);

   ZSTD_freeCCtx(cctx);

   free(zin);
   free(zout);

   free(from);
   free(to);

   return;

error:

   if (dir != NULL)
   {
      closedir(dir);
   }

   if (cctx != NULL)
   {
      ZSTD_freeCCtx(cctx);
   }

   free(zin);
   free(zout);

   free(from);
   free(to);
}

void
pgmoneta_zstandardd_request(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* from = NULL;
   char* orig = NULL;
   char* to = NULL;
   char* en = NULL;
   int ec = -1;
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
      ec = MANAGEMENT_ERROR_ZSTD_NOFILE;
      pgmoneta_log_error("ZSTD: No file for %s", from);
      goto error;
   }

   orig = pgmoneta_append(orig, from);
   to = pgmoneta_remove_suffix(orig, ".zstd");
   if (to == NULL)
   {
      ec = MANAGEMENT_ERROR_ALLOCATION;
      pgmoneta_log_error("ZSTD: Allocation error");
      goto error;
   }

   if (pgmoneta_zstandardd_file(from, to))
   {
      ec = MANAGEMENT_ERROR_ZSTD_ERROR;
      pgmoneta_log_error("ZSTD: Error zstd %s", from);
      goto error;
   }

   if (pgmoneta_exists(from))
   {
      pgmoneta_delete_file(from, NULL);
   }

   if (pgmoneta_management_create_response(payload, -1, &response))
   {
      ec = MANAGEMENT_ERROR_ALLOCATION;
      pgmoneta_log_error("ZSTD: Allocation error");
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
      ec = MANAGEMENT_ERROR_ZSTD_NETWORK;
      pgmoneta_log_error("ZSTD: Error sending response");
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("ZSTD: %s (Elapsed: %s)", from, elapsed);

   free(orig);
   free(to);
   free(elapsed);

   exit(0);

error:

   pgmoneta_management_response_error(ssl, client_fd, NULL,
                                      ec != -1 ? ec : MANAGEMENT_ERROR_ZSTD_ERROR, en != NULL ? en : NAME,
                                      compression, encryption, payload);

   free(orig);
   free(to);
   free(elapsed);

   exit(1);
}

int
pgmoneta_zstandardd_file(char* from, char* to)
{
   ZSTD_DCtx* dctx = NULL;
   size_t zin_size = 0;
   void* zin = NULL;
   size_t zout_size = 0;
   void* zout = NULL;

   if (pgmoneta_ends_with(from, ".zstd"))
   {
      zin_size = ZSTD_DStreamInSize();
      zin = malloc(zin_size);
      if (zin == NULL)
      {
         pgmoneta_log_error("ZSTD: Allocation failed (input buffer)");
         goto error;
      }

      zout_size = ZSTD_DStreamOutSize();
      zout = malloc(zout_size);
      if (zout == NULL)
      {
         pgmoneta_log_error("ZSTD: Allocation failed (output buffer)");
         goto error;
      }

      dctx = ZSTD_createDCtx();
      if (dctx == NULL)
      {
         pgmoneta_log_error("ZSTD: Could not create decompression context");
         goto error;
      }

      if (zstd_decompress(from, to, dctx, zin_size, zin, zout_size, zout))
      {
         pgmoneta_log_error("ZSTD: Could not decompress %s", from);
         goto error;
      }

      if (pgmoneta_exists(from))
      {
         pgmoneta_delete_file(from, NULL);
      }
      else
      {
         pgmoneta_log_debug("%s doesn't exist", from);
      }
   }
   else
   {
      goto error;
   }

   ZSTD_freeDCtx(dctx);

   free(zin);
   free(zout);

   return 0;

error:

   if (dctx != NULL)
   {
      ZSTD_freeDCtx(dctx);
   }

   free(zin);
   free(zout);

   return 1;
}

void
pgmoneta_zstandardd_directory(char* directory, struct workers* workers)
{
   ZSTD_DCtx* dctx = NULL;
   size_t zin_size = 0;
   void* zin = NULL;
   size_t zout_size = 0;
   void* zout = NULL;
   char* from = NULL;
   char* to = NULL;
   char* name = NULL;
   DIR* dir = NULL;
   struct dirent* entry;

   if (!(dir = opendir(directory)))
   {
      return;
   }

   zin_size = ZSTD_DStreamInSize();
   zin = malloc(zin_size);

   if (zin == NULL)
   {
      pgmoneta_log_error("ZSTD: Allocation failed (input buffer)");
      goto error;
   }

   zout_size = ZSTD_DStreamOutSize();
   zout = malloc(zout_size);

   if (zout == NULL)
   {
      pgmoneta_log_error("ZSTD: Allocation failed (output buffer)");
      goto error;
   }

   dctx = ZSTD_createDCtx();
   if (dctx == NULL)
   {
      pgmoneta_log_error("ZSTD: Could not create decompression context");
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

         pgmoneta_zstandardd_directory(path, workers);
      }
      else
      {
         if (pgmoneta_ends_with(entry->d_name, ".zstd"))
         {
            from = NULL;

            from = pgmoneta_append(from, directory);
            if (!pgmoneta_ends_with(from, "/"))
            {
               from = pgmoneta_append(from, "/");
            }
            from = pgmoneta_append(from, entry->d_name);

            name = pgmoneta_remove_suffix(entry->d_name, ".zstd");
            if (name == NULL)
            {
               pgmoneta_log_error("ZSTD: Allocation error");
               goto error;
            }

            to = NULL;

            to = pgmoneta_append(to, directory);
            if (!pgmoneta_ends_with(to, "/"))
            {
               to = pgmoneta_append(to, "/");
            }
            to = pgmoneta_append(to, name);

            if (zstd_decompress(from, to, dctx, zin_size, zin, zout_size, zout))
            {
               pgmoneta_log_error("ZSTD: Could not decompress %s/%s", directory, entry->d_name);
               break;
            }

            if (pgmoneta_exists(from))
            {
               pgmoneta_delete_file(from, NULL);
            }
            else
            {
               pgmoneta_log_debug("%s doesn't exist", from);
            }

            memset(zin, 0, zin_size);
            memset(zout, 0, zout_size);

            free(name);
            free(from);
            free(to);

            name = NULL;
            from = NULL;
            to = NULL;
         }
      }
   }

   closedir(dir);

   ZSTD_freeDCtx(dctx);

   free(zin);
   free(zout);

   free(from);
   free(to);
   free(name);

   return;

error:

   if (dir != NULL)
   {
      closedir(dir);
   }

   if (dctx != NULL)
   {
      ZSTD_freeDCtx(dctx);
   }

   free(zin);
   free(zout);

   free(name);
   free(from);
   free(to);
}

void
pgmoneta_zstandardc_request(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* from = NULL;
   char* to = NULL;
   char* en = NULL;
   int ec = -1;
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
      ec = MANAGEMENT_ERROR_ZSTD_NOFILE;
      pgmoneta_log_error("ZSTD: No file for %s", from);
      goto error;
   }

   to = pgmoneta_append(to, from);
   to = pgmoneta_append(to, ".zstd");
   if (to == NULL)
   {
      ec = MANAGEMENT_ERROR_ALLOCATION;
      pgmoneta_log_error("ZSTD: Allocation error");
      goto error;
   }

   if (pgmoneta_zstandardc_file(from, to))
   {
      ec = MANAGEMENT_ERROR_ZSTD_ERROR;
      pgmoneta_log_error("ZSTD: Error zstd %s", from);
      goto error;
   }

   if (pgmoneta_exists(from))
   {
      pgmoneta_delete_file(from, NULL);
   }

   if (pgmoneta_management_create_response(payload, -1, &response))
   {
      ec = MANAGEMENT_ERROR_ALLOCATION;
      pgmoneta_log_error("ZSTD: Allocation error");
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
      ec = MANAGEMENT_ERROR_ZSTD_NETWORK;
      pgmoneta_log_error("ZSTD: Error sending response");
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("ZSTD: %s (Elapsed: %s)", to, elapsed);

   free(to);
   free(elapsed);

   exit(0);

error:

   pgmoneta_management_response_error(ssl, client_fd, NULL,
                                      ec != -1 ? ec : MANAGEMENT_ERROR_ZSTD_ERROR, en != NULL ? en : NAME,
                                      compression, encryption, payload);

   free(to);
   free(elapsed);

   exit(1);
}

int
pgmoneta_zstandardc_file(char* from, char* to)
{
   size_t zin_size = 0;
   void* zin = NULL;
   size_t zout_size = 0;
   void* zout = NULL;
   ZSTD_CCtx* cctx = NULL;
   int level;
   int workers;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   level = config->compression_level;
   if (level < 1)
   {
      level = 1;
   }
   else if (level > 19)
   {
      level = 19;
   }

   workers = config->workers != 0 ? config->workers : ZSTD_DEFAULT_NUMBER_OF_WORKERS;

   zin_size = ZSTD_CStreamInSize();
   zin = malloc(zin_size);
   if (zin == NULL)
   {
      pgmoneta_log_error("ZSTD: Allocation failed (input buffer)");
      goto error;
   }
   zout_size = ZSTD_CStreamOutSize();
   zout = malloc(zout_size);
   if (zout == NULL)
   {
      pgmoneta_log_error("ZSTD: Allocation failed (output buffer)");
      goto error;
   }

   cctx = ZSTD_createCCtx();
   if (cctx == NULL)
   {
      pgmoneta_log_error("ZSTD: Could not create compression context");
      goto error;
   }

   ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, level);
   ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 1);
   ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, workers);

   if (zstd_compress(from, to, cctx, zin_size, zin, zout_size, zout))
   {
      goto error;
   }
   else
   {
      if (pgmoneta_exists(from))
      {
         pgmoneta_delete_file(from, NULL);
      }
      else
      {
         pgmoneta_log_debug("%s doesn't exist", from);
      }
      pgmoneta_permission(to, 6, 0, 0);
   }

   ZSTD_freeCCtx(cctx);

   free(zin);
   free(zout);

   return 0;

error:

   if (cctx != NULL)
   {
      ZSTD_freeCCtx(cctx);
   }

   free(zin);
   free(zout);

   return 1;
}

int
pgmoneta_zstdc_string(char* s, unsigned char** buffer, size_t* buffer_size)
{
   size_t input_size;
   size_t max_compressed_size;
   size_t compressed_size;

   input_size = strlen(s);
   max_compressed_size = ZSTD_compressBound(input_size);

   *buffer = (unsigned char*)malloc(max_compressed_size);
   if (*buffer == NULL)
   {
      pgmoneta_log_error("ZSTD: Allocation failed");
      return 1;
   }

   compressed_size = ZSTD_compress(*buffer, max_compressed_size, s, input_size, 1);
   if (ZSTD_isError(compressed_size))
   {
      pgmoneta_log_error("ZSTD: Compression error: %s", ZSTD_getErrorName(compressed_size));
      free(*buffer);
      return 1;
   }

   *buffer_size = compressed_size;

   return 0;
}

int
pgmoneta_zstdd_string(unsigned char* compressed_buffer, size_t compressed_size, char** output_string)
{
   size_t decompressed_size;
   size_t result;

   decompressed_size = ZSTD_getFrameContentSize(compressed_buffer, compressed_size);

   if (decompressed_size == ZSTD_CONTENTSIZE_ERROR)
   {
      pgmoneta_log_error("ZSTD: Not a valid compressed buffer");
      return 1;
   }
   if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN)
   {
      pgmoneta_log_error("ZSTD: Unknown decompressed size");
      return 1;
   }

   *output_string = (char*)malloc(decompressed_size + 1);
   if (*output_string == NULL)
   {
      pgmoneta_log_error("ZSTD: Allocation failed");
      return 1;
   }

   result = ZSTD_decompress(*output_string, decompressed_size, compressed_buffer, compressed_size);
   if (ZSTD_isError(result))
   {
      pgmoneta_log_error("ZSTD: Compression error: %s", ZSTD_getErrorName(result));
      free(*output_string);
      return 1;
   }

   (*output_string)[decompressed_size] = '\0';

   return 0;
}

static int
zstd_compress(char* from, char* to, ZSTD_CCtx* cctx, size_t zin_size, void* zin, size_t zout_size, void* zout)
{
   FILE* fin = NULL;
   FILE* fout = NULL;
   size_t toRead;

   fin = fopen(from, "rb");

   if (fin == NULL)
   {
      pgmoneta_log_error("ZSTD: Could not open input file %s: %s", from, strerror(errno));
      goto error;
   }

   fout = fopen(to, "wb");

   if (fout == NULL)
   {
      pgmoneta_log_error("ZSTD: Could not open output file %s: %s", to, strerror(errno));
      goto error;
   }

   toRead = zin_size;
   for (;;)
   {
      size_t read = fread(zin, sizeof(char), toRead, fin);
      int lastChunk = (read < toRead);
      ZSTD_EndDirective mode = lastChunk ? ZSTD_e_end : ZSTD_e_continue;
      ZSTD_inBuffer input = {zin, read, 0};
      int finished;
      do
      {
         ZSTD_outBuffer output = {zout, zout_size, 0};
         size_t remaining = ZSTD_compressStream2(cctx, &output, &input, mode);
         if (ZSTD_isError(remaining))
         {
            pgmoneta_log_error("ZSTD: Compression error: %s", ZSTD_getErrorName(remaining));
            goto error;
         }
         if (output.pos > 0)
         {
            size_t written = fwrite(zout, sizeof(char), output.pos, fout);
            if (written != output.pos)
            {
               pgmoneta_log_error("ZSTD: Write error while compressing %s: %s", to, strerror(errno));
               goto error;
            }
         }
         finished = lastChunk ? (remaining == 0) : (input.pos == input.size);
      }
      while (!finished);

      if (lastChunk)
      {
         break;
      }
   }

   fclose(fout);
   fclose(fin);

   return 0;

error:

   if (fout != NULL)
   {
      fclose(fout);
   }

   if (fin != NULL)
   {
      fclose(fin);
   }

   return 1;
}

static int
zstd_decompress(char* from, char* to, ZSTD_DCtx* dctx, size_t zin_size, void* zin, size_t zout_size, void* zout)
{
   FILE* fin = NULL;
   FILE* fout = NULL;
   size_t toRead;
   size_t read;
   size_t lastRet = 0;

   fin = fopen(from, "rb");

   if (fin == NULL)
   {
      pgmoneta_log_error("ZSTD: Could not open input file %s: %s", from, strerror(errno));
      goto error;
   }

   fout = fopen(to, "wb");

   if (fout == NULL)
   {
      pgmoneta_log_error("ZSTD: Could not open output file %s: %s", to, strerror(errno));
      goto error;
   }

   toRead = zin_size;
   while ((read = fread(zin, sizeof(char), toRead, fin)))
   {
      ZSTD_inBuffer input = (ZSTD_inBuffer){zin, read, 0};
      while (input.pos < input.size)
      {
         ZSTD_outBuffer output = (ZSTD_outBuffer){zout, zout_size, 0};
         size_t ret = ZSTD_decompressStream(dctx, &output, &input);
         if (ZSTD_isError(ret))
         {
            pgmoneta_log_error("ZSTD: Decompression error: %s", ZSTD_getErrorName(ret));
            goto error;
         }
         if (output.pos > 0)
         {
            size_t written = fwrite(zout, sizeof(char), output.pos, fout);
            if (written != output.pos)
            {
               pgmoneta_log_error("ZSTD: Write error while decompressing %s: %s", to, strerror(errno));
               goto error;
            }
         }
         lastRet = ret;
      }
   }

   if (lastRet != 0)
   {
      pgmoneta_log_error("ZSTD: Incomplete or corrupted frame");
      goto error;
   }

   fclose(fin);
   fclose(fout);

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