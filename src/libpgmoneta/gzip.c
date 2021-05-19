/*
 * Copyright (C) 2021 Red Hat
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
#include <gzip.h>
#include <logging.h>
#include <utils.h>

/* system */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

void
pgmoneta_gzip_data(char* directory)
{
   char* cmd = NULL;
   int status;
   DIR *dir;
   struct dirent *entry;
   struct configuration* config;

   config = (struct configuration*)shmem;

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

         pgmoneta_gzip_data(path);
      }
      else
      {
         cmd = NULL;

         cmd = pgmoneta_append(cmd, "gzip -");
         cmd = pgmoneta_append_int(cmd, config->compression_level);
         cmd = pgmoneta_append(cmd, " ");
         cmd = pgmoneta_append(cmd, directory);
         cmd = pgmoneta_append(cmd, "/");
         cmd = pgmoneta_append(cmd, entry->d_name);
         
         status = system(cmd);

         if (status != 0)
         {
            pgmoneta_log_error("Gzip: Could not compress %s/%s", directory, entry->d_name);
            break;
         }

         free(cmd);
      }
   }

   closedir(dir);
}

void
pgmoneta_gzip_wal(char* directory)
{
   char* cmd = NULL;
   int status;
   DIR *dir;
   struct dirent *entry;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (!(dir = opendir(directory)))
   {
      return;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_REG)
      {
         if (pgmoneta_ends_with(entry->d_name, ".gz") || pgmoneta_ends_with(entry->d_name, ".partial"))
         {
            continue;
         }

         cmd = NULL;

         cmd = pgmoneta_append(cmd, "gzip -");
         cmd = pgmoneta_append_int(cmd, config->compression_level);
         cmd = pgmoneta_append(cmd, " ");
         cmd = pgmoneta_append(cmd, directory);
         cmd = pgmoneta_append(cmd, "/");
         cmd = pgmoneta_append(cmd, entry->d_name);
         
         status = system(cmd);

         if (status != 0)
         {
            pgmoneta_log_error("Gzip: Could not compress %s/%s", directory, entry->d_name);
            break;
         }

         free(cmd);
      }
   }

   closedir(dir);
}
