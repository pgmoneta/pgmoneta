/*
 * Copyright (C) 2023 Red Hat
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
#include <utils.h>
#include <zstandard_compression.h>

/* system */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zstd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int zstd_compress(char* from, int level, char* to);
static int zstd_decompress(char* from, char* to);

void
pgmoneta_zstandardc_data(char* directory)
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
   else if (level > 19)
   {
      level = 19;
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

         pgmoneta_zstandardc_data(path);
      }
      else if (entry->d_type == DT_REG)
      {
         if (!pgmoneta_ends_with(entry->d_name, ".zstd") &&
             !pgmoneta_ends_with(entry->d_name, ".aes"))
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
               if (zstd_compress(from, level, to))
               {
                  pgmoneta_log_error("ZSTD: Could not compress %s/%s", directory, entry->d_name);
                  break;
               }

               pgmoneta_delete_file(from);
            }

            free(from);
            free(to);
         }
      }
   }

   closedir(dir);
}

void
pgmoneta_zstandardc_tablespaces(char* root)
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

         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0  || strcmp(entry->d_name, "data") == 0)
         {
            continue;
         }

         snprintf(path, sizeof(path), "%s/%s", root, entry->d_name);

         pgmoneta_zstandardc_data(path);
      }
   }

   closedir(dir);
}

void
pgmoneta_zstandardc_wal(char* directory)
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
   else if (level > 19)
   {
      level = 19;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_REG)
      {
         if (pgmoneta_ends_with(entry->d_name, ".zstd") ||
             pgmoneta_ends_with(entry->d_name, ".partial") ||
             pgmoneta_ends_with(entry->d_name, ".aes"))
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
            if (zstd_compress(from, level, to))
            {
               pgmoneta_log_error("ZSTD: Could not compress %s/%s", directory, entry->d_name);
               break;
            }

            pgmoneta_delete_file(from);
            pgmoneta_permission(to, 6, 0, 0);
         }

         free(from);
         free(to);
      }
   }

   closedir(dir);
}

void
pgmoneta_zstandardd_directory(char* directory)
{
   char* from = NULL;
   char* to = NULL;
   char* name = NULL;
   DIR* dir;
   struct dirent* entry;

   if (pgmoneta_ends_with(directory, "pg_tblspc"))
   {
      return;
   }

   if (!(dir = opendir(directory)))
   {
      return;
   }

   pgmoneta_log_info("%s", directory);

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

         pgmoneta_zstandardd_directory(path);
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

            name = malloc(strlen(entry->d_name) - 4);
            memset(name, 0, strlen(entry->d_name) - 4);
            memcpy(name, entry->d_name, strlen(entry->d_name) - 5);

            to = NULL;

            to = pgmoneta_append(to, directory);
            if (!pgmoneta_ends_with(to, "/"))
            {
               to = pgmoneta_append(to, "/");
            }
            to = pgmoneta_append(to, name);

            if (zstd_decompress(from, to))
            {
               pgmoneta_log_error("ZSTD: Could not decompress %s/%s", directory, entry->d_name);
               break;
            }

            pgmoneta_delete_file(from);

            free(name);
            free(from);
            free(to);
         }
      }
   }

   closedir(dir);
}

int
pgmoneta_zstandardc_file(char* from, char* to)
{
   int level;
   struct configuration* config;

   config = (struct configuration*)shmem;

   level = config->compression_level;
   if (level < 1)
   {
      level = 1;
   }
   else if (level > 19)
   {
      level = 19;
   }

   if (zstd_compress(from, level, to))
   {
      goto error;
   }
   else
   {
      pgmoneta_delete_file(from);
   }

   return 0;

error:

   return 1;
}

static int
zstd_compress(char* from, int level, char* to)
{
   FILE* fin = NULL;
   FILE* fout = NULL;
   size_t buffInSize = -1;
   void* buffIn = NULL;
   size_t buffOutSize = -1;
   void* buffOut = NULL;
   ZSTD_CCtx* cctx = NULL;

   fin = fopen(from, "rb");
   fout = fopen(to, "wb");
   buffInSize = ZSTD_CStreamInSize();
   buffIn = malloc(buffInSize);
   buffOutSize = ZSTD_CStreamOutSize();
   buffOut = malloc(buffOutSize);

   cctx = ZSTD_createCCtx();
   if (cctx == NULL)
   {
      goto error;
   }

   ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, level);
   ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 1);
   ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, 4);

   size_t toRead = buffInSize;
   for (;;)
   {
      size_t read = fread(buffIn, sizeof(char), toRead, fin);
      int lastChunk = (read < toRead);
      ZSTD_EndDirective mode = lastChunk ? ZSTD_e_end : ZSTD_e_continue;
      ZSTD_inBuffer input = {buffIn, read, 0};
      int finished;
      do
      {
         ZSTD_outBuffer output = {buffOut, buffOutSize, 0};
         size_t remaining = ZSTD_compressStream2(cctx, &output, &input, mode);
         fwrite(buffOut, sizeof(char), output.pos, fout);
         finished = lastChunk ? (remaining == 0) : (input.pos == input.size);
      }
      while (!finished);

      if (lastChunk)
      {
         break;
      }
   }

   ZSTD_freeCCtx(cctx);
   fclose(fout);
   fclose(fin);
   free(buffIn);
   free(buffOut);

   return 0;

error:

   if (cctx != NULL)
   {
      ZSTD_freeCCtx(cctx);
   }

   if (fout != NULL)
   {
      fclose(fout);
   }

   if (fin != NULL)
   {
      fclose(fin);
   }

   free(buffIn);
   free(buffOut);

   return 1;
}

static int
zstd_decompress(char* from, char* to)
{
   FILE* fin = NULL;
   size_t buffInSize = -1;
   void* buffIn = NULL;
   FILE* fout = NULL;
   size_t buffOutSize = -1;
   void* buffOut = NULL;
   ZSTD_DCtx* dctx = NULL;

   fin = fopen(from, "rb");
   buffInSize = ZSTD_DStreamInSize();
   buffIn = malloc(buffInSize);
   fout = fopen(to, "wb");;
   buffOutSize = ZSTD_DStreamOutSize();
   buffOut = malloc(buffOutSize);

   dctx = ZSTD_createDCtx();
   if (dctx == NULL)
   {
      goto error;
   }

   size_t toRead = buffInSize;
   size_t read;
   size_t lastRet = 0;
   while ((read = fread(buffIn, sizeof(char), toRead, fin)))
   {
      ZSTD_inBuffer input = {buffIn, read, 0};
      while (input.pos < input.size)
      {
         ZSTD_outBuffer output = {buffOut, buffOutSize, 0};
         size_t ret = ZSTD_decompressStream(dctx, &output, &input);
         fwrite(buffOut, sizeof(char), output.pos, fout);
         lastRet = ret;
      }
   }

   if (lastRet != 0)
   {
      goto error;
   }

   ZSTD_freeDCtx(dctx);
   fclose(fin);
   fclose(fout);
   free(buffIn);
   free(buffOut);

   return 0;

error:

   if (dctx != NULL)
   {
      ZSTD_freeDCtx(dctx);
   }

   if (fout != NULL)
   {
      fclose(fout);
   }

   if (fin != NULL)
   {
      fclose(fin);
   }

   free(buffIn);
   free(buffOut);

   return 1;
}
