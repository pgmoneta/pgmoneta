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
#include <gzip_compression.h>
#include <logging.h>
#include <utils.h>

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

void
pgmoneta_gzip_data(char* directory)
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
      if (entry->d_type == DT_DIR)
      {
         char path[1024];

         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }

         snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

         pgmoneta_gzip_data(path);
      }
      else if (entry->d_type == DT_REG)
      {
         if (!pgmoneta_ends_with(entry->d_name, ".gz") &&
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
            to = pgmoneta_append(to, ".gz");

            if (pgmoneta_exists(from))
            {
               if (gz_compress(from, level, to))
               {
                  pgmoneta_log_error("Gzip: Could not compress %s/%s", directory, entry->d_name);
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
pgmoneta_gzip_tablespaces(char* root)
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

         pgmoneta_gzip_data(path);
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
      if (entry->d_type == DT_REG)
      {
         if (pgmoneta_ends_with(entry->d_name, ".gz") ||
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
         to = pgmoneta_append(to, ".gz");

         if (pgmoneta_exists(from))
         {
            if (gz_compress(from, level, to))
            {
               pgmoneta_log_error("Gzip: Could not compress %s/%s", directory, entry->d_name);
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
pgmoneta_gunzip_data(char* directory)
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

         pgmoneta_gunzip_data(path);
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
            memset(name, 0, strlen(entry->d_name) - 2);
            memcpy(name, entry->d_name, strlen(entry->d_name) - 3);

            to = NULL;

            to = pgmoneta_append(to, directory);
            to = pgmoneta_append(to, "/");
            to = pgmoneta_append(to, name);

            if (gz_decompress(from, to))
            {
               pgmoneta_log_error("Gzip: Could not decompress %s/%s", directory, entry->d_name);
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
      pgmoneta_delete_file(from);
   }

   return 0;

error:

   return 1;
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
         if (gzwrite(out, buf, (unsigned)length) != length)
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
   if (in == NULL)
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
