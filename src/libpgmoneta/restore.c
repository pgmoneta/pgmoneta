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
#include <info.h>
#include <logging.h>
#include <restore.h>
#include <utils.h>
#include <zstandard.h>

/* system */
#include <stdlib.h>
#include <unistd.h>

void
pgmoneta_restore(int server, char* backup_id, char* position, char* directory, char** argv)
{
   char elapsed[128];
   time_t start_time;
   int total_seconds;
   int hours;
   int minutes;
   int seconds;
   char* output = NULL;
   char* id = NULL;
   struct configuration* config;

   pgmoneta_start_logging();

   config = (struct configuration*)shmem;

   pgmoneta_set_proc_title(1, argv, "restore", config->servers[server].name);

   start_time = time(NULL);

   if (!pgmoneta_restore_backup("Restore", server, backup_id, position, directory, &output, &id))
   {
      total_seconds = (int)difftime(time(NULL), start_time);
      hours = total_seconds / 3600;
      minutes = (total_seconds % 3600) / 60;
      seconds = total_seconds % 60;

      memset(&elapsed[0], 0, sizeof(elapsed));
      sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

      pgmoneta_log_info("Restore: %s/%s (Elapsed: %s)", config->servers[server].name, id, &elapsed[0]);
   }

   pgmoneta_stop_logging();

   free(output);
   free(id);

   free(backup_id);
   free(position);
   free(directory);

   exit(0);
}

int
pgmoneta_restore_backup(char* prefix, int server, char* backup_id, char* position, char* directory, char** output, char** identifier)
{
   char* o = NULL;
   char* ident = NULL;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   char* d = NULL;
   char* from = NULL;
   char* to = NULL;
   char* id = NULL;
   char* origwal = NULL;
   char* waldir = NULL;
   char* waltarget = NULL;
   int number_of_wal_files = 0;
   char** wal_files = NULL;
   char* start = NULL;
   struct configuration* config;

   *output = NULL;
   *identifier = NULL;

   config = (struct configuration*)shmem;

   if (!strcmp(backup_id, "oldest"))
   {
      d = NULL;
      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[server].name);
      d = pgmoneta_append(d, "/backup/");

      if (pgmoneta_get_backups(d, &number_of_backups, &backups))
      {
         goto error;
      }

      for (int i = 0; id == NULL && i < number_of_backups; i++)
      {
         if (backups[i] != NULL && backups[i]->valid)
         {
            id = backups[i]->label;
         }
      }
   }
   else if (!strcmp(backup_id, "latest") || !strcmp(backup_id, "newest"))
   {
      d = NULL;
      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[server].name);
      d = pgmoneta_append(d, "/backup/");

      if (pgmoneta_get_backups(d, &number_of_backups, &backups))
      {
         goto error;
      }

      for (int i = number_of_backups - 1; id == NULL && i >= 0; i--)
      {
         if (backups[i] != NULL && backups[i]->valid)
         {
            id = backups[i]->label;
         }
      }
   }
   else
   {
      id = backup_id;
   }

   if (id == NULL)
   {
      pgmoneta_log_error("%s: No identifier for %s", prefix, config->servers[server].name);
      goto error;
   }

   from = pgmoneta_append(from, config->base_dir);
   from = pgmoneta_append(from, "/");
   from = pgmoneta_append(from, config->servers[server].name);
   from = pgmoneta_append(from, "/backup/");
   from = pgmoneta_append(from, id);
   from = pgmoneta_append(from, "/data");

   if (!pgmoneta_exists(from))
   {
      pgmoneta_log_error("%s: Unknown identifier for %s/%s", prefix, config->servers[server].name, id);
      goto error;
   }

   to = pgmoneta_append(to, directory);
   to = pgmoneta_append(to, "/");
   to = pgmoneta_append(to, config->servers[server].name);
   to = pgmoneta_append(to, "-");
   to = pgmoneta_append(to, id);
   to = pgmoneta_append(to, "/");

   pgmoneta_delete_directory(to);

   if (pgmoneta_copy_directory(from, to))
   {
      pgmoneta_log_error("%s: Could not restore %s/%s", prefix, config->servers[server].name, id);
      goto error;
   }
   else
   {
      if (position != NULL)
      {
         if (!strcmp(position, "current") || !strcmp(position, "immediate"))
         {
            origwal = pgmoneta_append(origwal, config->base_dir);
            origwal = pgmoneta_append(origwal, "/");
            origwal = pgmoneta_append(origwal, config->servers[server].name);
            origwal = pgmoneta_append(origwal, "/backup/");
            origwal = pgmoneta_append(origwal, id);
            origwal = pgmoneta_append(origwal, "/data/pg_wal/");

            waldir = pgmoneta_append(waldir, config->base_dir);
            waldir = pgmoneta_append(waldir, "/");
            waldir = pgmoneta_append(waldir, config->servers[server].name);
            waldir = pgmoneta_append(waldir, "/wal/");

            waltarget = pgmoneta_append(waltarget, directory);
            waltarget = pgmoneta_append(waltarget, "/");
            waltarget = pgmoneta_append(waltarget, config->servers[server].name);
            waltarget = pgmoneta_append(waltarget, "-");
            waltarget = pgmoneta_append(waltarget, id);
            waltarget = pgmoneta_append(waltarget, "/pg_wal/");

            number_of_wal_files = 0;
            wal_files = NULL;

            pgmoneta_get_files(origwal, &number_of_wal_files, &wal_files);

            pgmoneta_basename_file(wal_files[0], &start);

            pgmoneta_copy_wal_files(waldir, waltarget, start);
         }
         else
         {
            pgmoneta_log_warn("%s: Unknown position parameter %s", prefix, position);
         }
      }

      if (config->compression_type == COMPRESSION_GZIP)
      {
         pgmoneta_gunzip_data(to);
      }
      else if (config->compression_type == COMPRESSION_ZSTD)
      {
         pgmoneta_zstandardd_data(to);
      }
   }

   o = pgmoneta_append(o, directory);
   o = pgmoneta_append(o, "/");
   o = pgmoneta_append(o, config->servers[server].name);
   o = pgmoneta_append(o, "-");
   o = pgmoneta_append(o, id);
   o = pgmoneta_append(o, "/");

   ident = pgmoneta_append(ident, id);

   *output = o;
   *identifier = ident;

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   for (int i = 0; i < number_of_wal_files; i++)
   {
      free(wal_files[i]);
   }
   free(wal_files);

   free(from);
   free(to);
   free(d);
   free(origwal);
   free(waldir);
   free(waltarget);
   free(start);

   return 0;

error:

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   for (int i = 0; i < number_of_wal_files; i++)
   {
      free(wal_files[i]);
   }
   free(wal_files);

   free(from);
   free(to);
   free(d);
   free(origwal);
   free(waldir);
   free(waltarget);
   free(start);

   return 1;
}
