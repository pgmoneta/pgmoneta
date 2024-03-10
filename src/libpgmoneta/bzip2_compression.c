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
#include <stddef.h>
#include <utils.h>
#include <workers.h>

/* system */
#include <bzlib.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_LENGTH 8192

static int bzip2_compress(char* from, int level, char* to);
static int bzip2_decompress(char* from, char* to);

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
            to = pgmoneta_append(to, ".bz2");

            if (!pgmoneta_create_worker_input(directory, from, to, level, workers, &wi))
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
         pgmoneta_delete_file(wi->from);
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
      pgmoneta_delete_file(wi->from);
   }

   free(wi);
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
      pgmoneta_delete_file(from);
   }

   return 0;

error:
   return 1;
}

static int
bzip2_compress(char* from, int level, char* to)
{
   FILE* from_ptr = NULL,
       * to_ptr = NULL;

   char buf[BUFFER_LENGTH] = {0};
   size_t buf_len = BUFFER_LENGTH;
   size_t length;
   int bzip2_err;

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
   FILE* from_ptr = NULL,
       * to_ptr = NULL;

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
         if (fwrite(buf, 1, length, to_ptr) != length)
         {
            goto error_unzip;
         }
      }

   }
   while (bzip2_err == BZ_STREAM_END && length == BUFFER_LENGTH);

error_unzip:
   BZ2_bzWriteClose(&bzip2_err, zip_file, 0, NULL, NULL);

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
