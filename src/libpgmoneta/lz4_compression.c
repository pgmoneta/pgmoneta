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
#include <logging.h>
#include <lz4_compression.h>
#include <pgmoneta.h>
#include <utils.h>

/* system */
#include <dirent.h>
#include "lz4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int lz4_compress(char* from, char* to);
static int lz4_decompress(char* from, char* to);

void
pgmoneta_lz4c_data(char* directory)
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
      if (entry->d_type == DT_DIR)
      {
         char path[1024];

         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }

         snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

         pgmoneta_lz4c_data(path);
      }
      else
      {
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

         pgmoneta_delete_file(from);

         free(from);
         free(to);
      }
   }

   closedir(dir);
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
         if (pgmoneta_ends_with(entry->d_name, ".lz4") ||
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
         to = pgmoneta_append(to, ".lz4");

         lz4_compress(from, to);

         pgmoneta_delete_file(from);
         pgmoneta_permission(to, 6, 0, 0);

         free(from);
         free(to);
      }
   }

   closedir(dir);
}

void
pgmoneta_lz4d_data(char* directory)
{
   char* from = NULL;
   char* to = NULL;
   char* name = NULL;
   DIR* dir;
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

         pgmoneta_lz4d_data(path);
      }
      else
      {
         from = NULL;

         from = pgmoneta_append(from, directory);
         from = pgmoneta_append(from, "/");
         from = pgmoneta_append(from, entry->d_name);

         name = malloc(strlen(entry->d_name) - 3);
         memset(name, 0, strlen(entry->d_name) - 3);
         memcpy(name, entry->d_name, strlen(entry->d_name) - 4);

         to = NULL;

         to = pgmoneta_append(to, directory);
         to = pgmoneta_append(to, "/");
         to = pgmoneta_append(to, name);

         lz4_decompress(from, to);

         pgmoneta_delete_file(from);

         free(name);
         free(from);
         free(to);
      }
   }

   closedir(dir);
}

int
pgmoneta_lz4c_file(char* from, char* to)
{
   lz4_compress(from, to);

   pgmoneta_delete_file(from);

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
   fout = fopen(to, "wb");

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

   lz4StreamDecode = &lz4StreamDecodeBody;
   fin = fopen(from, "rb");
   fout = fopen(to, "wb");

   LZ4_setStreamDecode(lz4StreamDecode, NULL, 0);

   for (;;)
   {
      int compression = 0;
      fread(&compression, sizeof(compression), 1, fin);

      fread(buffOut, sizeof(char), compression, fin);

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
}
