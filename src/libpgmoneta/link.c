/*
 * Copyright (C) 2025 The pgmoneta community
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
#include <info.h>
#include <link.h>
#include <logging.h>
#include <utils.h>
#include <workers.h>

/* system */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

static void do_link(struct worker_input* wi);
static void do_relink(struct worker_input* wi);
static void do_comparefiles(struct worker_input* wi);
static char* trim_suffix(char* str);

int
pgmoneta_link_manifest(char* base_from, char* base_to, char* from, struct art* changed, struct art* added, struct workers* workers)
{
   DIR* from_dir = opendir(from);
   char* from_entry = NULL;
   char* from_file = NULL;
   char* from_file_trimmed = NULL;
   char* to_entry = NULL;
   struct dirent* entry;
   struct stat statbuf;

   if (from_dir == NULL)
   {
      goto error;
   }

   while ((entry = readdir(from_dir)))
   {
      if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      {
         continue;
      }

      from_entry = pgmoneta_append(from_entry, from);
      if (!pgmoneta_ends_with(from, "/"))
      {
         from_entry = pgmoneta_append(from_entry, "/");
      }
      from_entry = pgmoneta_append(from_entry, entry->d_name);

      if (!stat(from_entry, &statbuf))
      {
         if (S_ISDIR(statbuf.st_mode))
         {
            pgmoneta_link_manifest(base_from, base_to, from_entry, changed, added, workers);
         }
         else
         {
            struct worker_input* wi = NULL;
            from_file = pgmoneta_remove_prefix(from_entry, base_from);
            from_file_trimmed = trim_suffix(from_file);
            // file in newer dir is not added nor changed, nor is an incremental file
            if (!pgmoneta_art_contains_key(added, (unsigned char*)from_file_trimmed, strlen(from_file_trimmed) + 1) &&
                !pgmoneta_art_contains_key(changed, (unsigned char*)from_file_trimmed, strlen(from_file_trimmed) + 1) &&
                !pgmoneta_is_incremental_path(from_file_trimmed))
            {
               to_entry = pgmoneta_append(to_entry, base_to);
               if (!pgmoneta_ends_with(to_entry, "/"))
               {
                  to_entry = pgmoneta_append(to_entry, "/");
               }
               to_entry = pgmoneta_append(to_entry, from_file);
               if (pgmoneta_create_worker_input(NULL, from_entry, to_entry, 0, workers, &wi))
               {
                  goto error;
               }

               if (workers != NULL)
               {
                  if (workers->outcome)
                  {
                     pgmoneta_workers_add(workers, do_link, wi);
                  }
               }
               else
               {
                  do_link(wi);
               }
            }
         }
      }

      free(from_entry);
      free(from_file_trimmed);
      free(from_file);
      free(to_entry);

      from_entry = NULL;
      to_entry = NULL;
      from_file = NULL;
      from_file_trimmed = NULL;
   }

   closedir(from_dir);

   return 0;

error:

   if (from_dir != NULL)
   {
      closedir(from_dir);
   }

   free(from_entry);
   free(from_file_trimmed);
   free(from_file);
   free(to_entry);

   return 1;
}

static void
do_link(struct worker_input* wi)
{
   if (pgmoneta_exists(wi->to))
   {
      if (pgmoneta_exists(wi->from))
      {
         pgmoneta_delete_file(wi->from, NULL);
      }
      else
      {
         pgmoneta_log_debug("%s doesn't exists", wi->from);
      }
      pgmoneta_symlink_file(wi->from, wi->to);
   }
   else
   {
      pgmoneta_log_debug("%s doesn't exists", wi->to);
   }

   free(wi);
}

int
pgmoneta_relink(char* from, char* to, struct workers* workers)
{
   DIR* from_dir = opendir(from);
   DIR* to_dir = opendir(to);
   char* from_entry = NULL;
   char* to_entry = NULL;
   struct dirent* entry;
   struct stat statbuf;

   if (from_dir == NULL)
   {
      goto error;
   }

   if (to_dir == NULL)
   {
      goto error;
   }

   while ((entry = readdir(from_dir)))
   {
      if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      {
         continue;
      }

      from_entry = pgmoneta_append(from_entry, from);
      if (!pgmoneta_ends_with(from, "/"))
      {
         from_entry = pgmoneta_append(from_entry, "/");
      }
      from_entry = pgmoneta_append(from_entry, entry->d_name);

      to_entry = pgmoneta_append(to_entry, to);
      if (!pgmoneta_ends_with(to, "/"))
      {
         to_entry = pgmoneta_append(to_entry, "/");
      }
      to_entry = pgmoneta_append(to_entry, entry->d_name);

      if (!lstat(from_entry, &statbuf))
      {
         if (S_ISDIR(statbuf.st_mode))
         {
#ifdef DEBUG
            pgmoneta_log_trace("FILETRACKER | %s | %s | %s | %s |", from_entry, to_entry, "Dir ", "Dir ");
#endif
            pgmoneta_relink(from_entry, to_entry, workers);
         }
         else
         {
            struct worker_input* wi = NULL;

            if (pgmoneta_create_worker_input(NULL, from_entry, to_entry, 0, workers, &wi))
            {
               goto error;
            }

            if (workers != NULL)
            {
               if (workers->outcome)
               {
                  pgmoneta_workers_add(workers, do_relink, wi);
               }
            }
            else
            {
               do_relink(wi);
            }
         }
      }

      free(from_entry);
      free(to_entry);

      from_entry = NULL;
      to_entry = NULL;
   }

   if (from_dir != NULL)
   {
      closedir(from_dir);
   }

   if (to_dir != NULL)
   {
      closedir(to_dir);
   }

   return 0;

error:

   if (from_dir != NULL)
   {
      closedir(from_dir);
   }

   if (to_dir != NULL)
   {
      closedir(to_dir);
   }

   free(from_entry);
   free(to_entry);

   return 1;
}

static void
do_relink(struct worker_input* wi)
{
   char* link = NULL;

#ifdef DEBUG
   if (!pgmoneta_exists(wi->from))
   {
      pgmoneta_log_trace("FILETRACKER | Unk  | %s |", wi->from);
   }

   if (!pgmoneta_exists(wi->to))
   {
      pgmoneta_log_trace("FILETRACKER | Unk  | %s |", wi->to);
   }
#endif

   if (pgmoneta_is_symlink(wi->to))
   {
      if (pgmoneta_is_file(wi->from))
      {
#ifdef DEBUG
         pgmoneta_log_trace("FILETRACKER | %s | %s | %s | %s |", wi->from, wi->to,
                            pgmoneta_is_symlink(wi->from) ? "Syml" : "File", pgmoneta_is_symlink(wi->to) ? "Syml" : "File");

         pgmoneta_log_trace("FILETRACKER | Del  | %s |", wi->to);
         pgmoneta_log_trace("FILETRACKER | Copy | %s | %s |", wi->from, wi->to);
#endif
         if (pgmoneta_exists(wi->to))
         {
            pgmoneta_delete_file(wi->to, NULL);
         }
         else
         {
            pgmoneta_log_debug("%s doesn't exists", wi->to);
         }
         pgmoneta_copy_file(wi->from, wi->to, wi->workers);
      }
      else
      {
         link = pgmoneta_get_symlink(wi->from);

         if (link != NULL)
         {
            if (pgmoneta_exists(wi->to))
            {
               pgmoneta_delete_file(wi->to, NULL);
            }
            else
            {
               pgmoneta_log_debug("%s doesn't exists", wi->to);
            }
            pgmoneta_symlink_file(wi->to, link);
#ifdef DEBUG
            pgmoneta_log_trace("FILETRACKER | Lnk | %s | %s |", wi->to, pgmoneta_is_symlink_valid(wi->to) ? "Yes " : "No  ");
            pgmoneta_log_trace("FILETRACKER | Lnk | %s | %s |", link, pgmoneta_is_symlink_valid(link) ? "Yes " : "No  ");
#endif

            free(link);
         }
         else
         {
            pgmoneta_log_debug("%s -> %s", wi->from, wi->to);
         }
      }
   }
   else
   {
      pgmoneta_log_debug("do_relink: %s -> %s", wi->from, wi->to);
   }

   free(wi);
}

int
pgmoneta_link_comparefiles(char* from, char* to, struct workers* workers)
{
   DIR* from_dir = opendir(from);
   char* from_entry = NULL;
   char* to_entry = NULL;
   struct dirent* entry;
   struct stat statbuf;

   if (from_dir == NULL)
   {
      goto error;
   }

   while ((entry = readdir(from_dir)))
   {
      if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") || !strcmp(entry->d_name, "data"))
      {
         continue;
      }

      from_entry = pgmoneta_append(from_entry, from);
      if (!pgmoneta_ends_with(from_entry, "/"))
      {
         from_entry = pgmoneta_append(from_entry, "/");
      }
      from_entry = pgmoneta_append(from_entry, entry->d_name);

      to_entry = pgmoneta_append(to_entry, to);
      if (!pgmoneta_ends_with(to_entry, "/"))
      {
         to_entry = pgmoneta_append(to_entry, "/");
      }
      to_entry = pgmoneta_append(to_entry, entry->d_name);

      if (!stat(from_entry, &statbuf))
      {
         if (S_ISDIR(statbuf.st_mode))
         {
            pgmoneta_link_comparefiles(from_entry, to_entry, workers);
         }
         else
         {
            struct worker_input* wi = NULL;

            if (pgmoneta_create_worker_input(NULL, from_entry, to_entry, 0, workers, &wi))
            {
               goto error;
            }

            if (workers != NULL)
            {
               if (workers->outcome)
               {
                  pgmoneta_workers_add(workers, do_comparefiles, wi);
               }
            }
            else
            {
               do_comparefiles(wi);
            }
         }
      }

      free(from_entry);
      free(to_entry);

      from_entry = NULL;
      to_entry = NULL;
   }

   closedir(from_dir);

   return 0;

error:

   if (from_dir != NULL)
   {
      closedir(from_dir);
   }

   free(from_entry);
   free(to_entry);

   return 1;
}

static void
do_comparefiles(struct worker_input* wi)
{
   bool equal;

   equal = pgmoneta_compare_files(wi->from, wi->to);

   if (equal)
   {
      if (pgmoneta_exists(wi->from))
      {
         pgmoneta_delete_file(wi->from, NULL);
      }
      else
      {
         pgmoneta_log_debug("%s doesn't exists", wi->from);
      }
      pgmoneta_symlink_file(wi->from, wi->to);
   }

   free(wi);
}

static char*
trim_suffix(char* str)
{
   char* res = NULL;
   struct configuration* config;

   config = (struct configuration*) shmem;
   int len = 0;
   if (str == NULL)
   {
      return NULL;
   }
   len = strlen(str) + 1;

   if (!pgmoneta_compare_string(str, "backup_label") &&
       !pgmoneta_compare_string(str, "backup_manifest"))
   {
      switch (config->compression_type)
      {
         case COMPRESSION_CLIENT_GZIP:
         case COMPRESSION_SERVER_GZIP:
            len -= 3;
            break;
         case COMPRESSION_CLIENT_ZSTD:
         case COMPRESSION_SERVER_ZSTD:
            len -= 5;
            break;
         case COMPRESSION_CLIENT_LZ4:
         case COMPRESSION_SERVER_LZ4:
         case COMPRESSION_CLIENT_BZIP2:
            len -= 4;
            break;
      }
      if (config->encryption != ENCRYPTION_NONE)
      {
         len -= 4;
      }
   }

   res = malloc(len);
   memset(res, 0, len);
   memcpy(res, str, len - 1);
   return res;
}
