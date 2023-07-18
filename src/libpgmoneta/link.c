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
#include <link.h>
#include <info.h>
#include <logging.h>
#include <utils.h>

/* system */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

void
pgmoneta_link(char* from, char* to)
{
   DIR* from_dir = opendir(from);
   DIR* to_dir = opendir(to);
   char* from_entry = NULL;
   char* to_entry = NULL;
   struct dirent* entry;
   struct stat statbuf;

   if (from_dir == NULL)
   {
      goto done;
   }

   if (to_dir == NULL)
   {
      goto done;
   }

   while ((entry = readdir(from_dir)))
   {
      if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") || !strcmp(entry->d_name, "pg_tblspc"))
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

      if (!stat(from_entry, &statbuf))
      {
         if (S_ISDIR(statbuf.st_mode))
         {
            pgmoneta_link(from_entry, to_entry);
         }
         else
         {
            if (pgmoneta_exists(to))
            {
               bool equal = pgmoneta_compare_files(from_entry, to_entry);

               if (equal)
               {
                  pgmoneta_delete_file(from_entry);
                  pgmoneta_symlink_file(from_entry, to_entry);
               }
            }
         }
      }

      free(from_entry);
      free(to_entry);

      from_entry = NULL;
      to_entry = NULL;
   }

done:

   if (from_dir != NULL)
   {
      closedir(from_dir);
   }

   if (to_dir != NULL)
   {
      closedir(to_dir);
   }
}

void
pgmoneta_relink(char* from, char* to)
{
   DIR* from_dir = opendir(from);
   DIR* to_dir = opendir(to);
   char* from_entry = NULL;
   char* to_entry = NULL;
   char* link = NULL;
   struct dirent* entry;
   struct stat statbuf;

   if (from_dir == NULL)
   {
      goto done;
   }

   if (to_dir == NULL)
   {
      goto done;
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
            pgmoneta_relink(from_entry, to_entry);
         }
         else
         {
            if (pgmoneta_is_symlink(to_entry))
            {
               if (pgmoneta_is_file(from_entry))
               {
                  pgmoneta_delete_file(to_entry);
                  pgmoneta_copy_file(from_entry, to_entry);
               }
               else
               {
                  link = pgmoneta_get_symlink(from_entry);

                  pgmoneta_delete_file(to_entry);
                  pgmoneta_symlink_file(to_entry, link);

                  free(link);
               }
            }
         }
      }

      free(from_entry);
      free(to_entry);

      from_entry = NULL;
      to_entry = NULL;
   }

done:

   if (from_dir != NULL)
   {
      closedir(from_dir);
   }

   if (to_dir != NULL)
   {
      closedir(to_dir);
   }
}

void
pgmoneta_link_tablespaces(char* root)
{
   DIR* from_dir = opendir(root);
   char* from_entry = NULL;
   char* to_entry = NULL;
   struct dirent* entry;
   struct stat statbuf;

   if (from_dir == NULL)
   {
      goto done;
   }

   while ((entry = readdir(from_dir)))
   {
      if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") || !strcmp(entry->d_name, "data"))
      {
         continue;
      }

      from_entry = pgmoneta_append(from_entry, root);
      if (!pgmoneta_ends_with(from_entry, "/"))
      {
         from_entry = pgmoneta_append(from_entry, "/");
      }
      from_entry = pgmoneta_append(from_entry, entry->d_name);

      to_entry = pgmoneta_append(to_entry, "../../");
      to_entry = pgmoneta_append(to_entry, entry->d_name);

      if (!stat(from_entry, &statbuf))
      {
         if (S_ISDIR(statbuf.st_mode))
         {
            pgmoneta_link(from_entry, to_entry);
         }
         else
         {
            bool equal = pgmoneta_compare_files(from_entry, to_entry);

            if (equal)
            {
               pgmoneta_delete_file(from_entry);
               pgmoneta_symlink_file(from_entry, to_entry);
            }
         }
      }

      free(from_entry);
      free(to_entry);

      from_entry = NULL;
      to_entry = NULL;
   }

done:

   if (from_dir != NULL)
   {
      closedir(from_dir);
   }
}
