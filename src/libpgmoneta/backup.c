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
#include <backup.h>
#include <gzip.h>
#include <info.h>
#include <link.h>
#include <logging.h>
#include <utils.h>
#include <zstandard.h>

/* system */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void
pgmoneta_backup(int server, char** argv)
{
   bool active;
   int usr;
   char date[128];
   char elapsed[128];
   time_t current_time;
   struct tm* time_info;
   time_t start_time;
   int total_seconds;
   int hours;
   int minutes;
   int seconds;
   char* root = NULL;
   char* d = NULL;
   char* cmd = NULL;
   int status;
   char* server_path = NULL;
   unsigned long size;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   char* from = NULL;
   char* to = NULL;
   int newest;
   int next_newest;
   struct configuration* config;

   pgmoneta_start_logging();

   config = (struct configuration*)shmem;

   pgmoneta_set_proc_title(1, argv, "backup", config->servers[server].name);

   if (!config->servers[server].valid)
   {
      pgmoneta_log_error("Backup: Server %s is not in a valid configuration", config->servers[server].name);
      goto done;
   }

   active = false;

   if (!atomic_compare_exchange_strong(&config->servers[server].backup, &active, true))
   {
      goto done;
   }

   start_time = time(NULL);
   total_seconds = 0;

   usr = -1;
   for (int i = 0; usr == -1 && i < config->number_of_users; i++)
   {
      if (!strcmp(config->servers[server].username, config->users[i].username))
      {
         usr = i;
      }  
   }

   memset(&date[0], 0, sizeof(date));
   time(&current_time);
   time_info = localtime(&current_time);
   strftime(&date[0], sizeof(date), "%Y%m%d%H%M%S", time_info);

   root = pgmoneta_append(root, config->base_dir);
   root = pgmoneta_append(root, "/");
   root = pgmoneta_append(root, config->servers[server].name);
   root = pgmoneta_append(root, "/backup/");
   root = pgmoneta_append(root, date);
   root = pgmoneta_append(root, "/");

   pgmoneta_mkdir(root);

   d = pgmoneta_append(d, config->base_dir);
   d = pgmoneta_append(d, "/");
   d = pgmoneta_append(d, config->servers[server].name);
   d = pgmoneta_append(d, "/backup/");
   d = pgmoneta_append(d, date);
   d = pgmoneta_append(d, "/data/");

   cmd = pgmoneta_append(cmd, "PGPASSWORD=\"");
   cmd = pgmoneta_append(cmd, config->users[usr].password);
   cmd = pgmoneta_append(cmd, "\" ");

   cmd = pgmoneta_append(cmd, config->pgsql_dir);
   cmd = pgmoneta_append(cmd, "/pg_basebackup ");

   cmd = pgmoneta_append(cmd, "-h ");
   cmd = pgmoneta_append(cmd, config->servers[server].host);
   cmd = pgmoneta_append(cmd, " ");

   cmd = pgmoneta_append(cmd, "-p ");
   cmd = pgmoneta_append_int(cmd, config->servers[server].port);
   cmd = pgmoneta_append(cmd, " ");

   cmd = pgmoneta_append(cmd, "-U ");
   cmd = pgmoneta_append(cmd, config->servers[server].username);
   cmd = pgmoneta_append(cmd, " ");

   if (strlen(config->servers[server].backup_slot) > 0)
   {
      cmd = pgmoneta_append(cmd, "-S ");
      cmd = pgmoneta_append(cmd, config->servers[server].backup_slot);
      cmd = pgmoneta_append(cmd, " ");
   }

   cmd = pgmoneta_append(cmd, "-l ");
   cmd = pgmoneta_append(cmd, date);
   cmd = pgmoneta_append(cmd, " ");

   cmd = pgmoneta_append(cmd, "-X stream ");
   cmd = pgmoneta_append(cmd, "--no-password ");
   cmd = pgmoneta_append(cmd, "-c fast ");

   cmd = pgmoneta_append(cmd, "-D ");
   cmd = pgmoneta_append(cmd, d);
   
   status = system(cmd);
   
   if (status != 0)
   {
      pgmoneta_log_error("Backup: Could not backup %s", config->servers[server].name);
      pgmoneta_create_info(root, 0, &date[0], 0, 0);
   }
   else
   {
      size = pgmoneta_directory_size(d);

      if (config->compression_type == COMPRESSION_GZIP)
      {
         pgmoneta_gzip_data(d);
      }
      else if (config->compression_type == COMPRESSION_ZSTD)
      {
         pgmoneta_zstandardc_data(d);
      }

      total_seconds = (int)difftime(time(NULL), start_time);
      hours = total_seconds / 3600;
      minutes = (total_seconds % 3600) / 60;
      seconds = total_seconds % 60;

      memset(&elapsed[0], 0, sizeof(elapsed));
      sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

      pgmoneta_log_info("Backup: %s/%s (Elapsed: %s)", config->servers[server].name, &date[0], &elapsed[0]);

      pgmoneta_create_info(root, 1, &date[0], size, total_seconds);

      if (config->link)
      {
         start_time = time(NULL);
         total_seconds = 0;

         server_path = NULL;

         server_path = pgmoneta_append(server_path, config->base_dir);
         if (!pgmoneta_ends_with(config->base_dir, "/"))
         {
            server_path = pgmoneta_append(server_path, "/");
         }
         server_path = pgmoneta_append(server_path, config->servers[server].name);
         server_path = pgmoneta_append(server_path, "/backup/");

         pgmoneta_get_backups(server_path, &number_of_backups, &backups);

         if (number_of_backups > 1)
         {
            newest = -1;
            next_newest = -1;

            for (int j = number_of_backups - 1; j >= 0; j--)
            {
               if (backups[j] != NULL && backups[j]->valid)
               {
                  if (newest == -1)
                  {
                     newest = j;
                  }
                  else if (next_newest == -1)
                  {
                     next_newest = j;
                  }
               }
            }

            if (newest != -1 && next_newest != -1)
            {
               from = NULL;

               from = pgmoneta_append(from, config->base_dir);
               if (!pgmoneta_ends_with(config->base_dir, "/"))
               {
                  from = pgmoneta_append(from, "/");
               }
               from = pgmoneta_append(from, config->servers[server].name);
               from = pgmoneta_append(from, "/backup/");
               from = pgmoneta_append(from, backups[newest]->label);
               from = pgmoneta_append(from, "/data");

               to = NULL;

               to = pgmoneta_append(to, config->base_dir);
               if (!pgmoneta_ends_with(config->base_dir, "/"))
               {
                  to = pgmoneta_append(to, "/");
               }
               to = pgmoneta_append(to, config->servers[server].name);
               to = pgmoneta_append(to, "/backup/");
               to = pgmoneta_append(to, backups[next_newest]->label);
               to = pgmoneta_append(to, "/data");

               pgmoneta_link(from, to);

               total_seconds = (int)difftime(time(NULL), start_time);
               hours = total_seconds / 3600;
               minutes = (total_seconds % 3600) / 60;
               seconds = total_seconds % 60;

               memset(&elapsed[0], 0, sizeof(elapsed));
               sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

               pgmoneta_log_debug("Link: %s/%s (Elapsed: %s)", config->servers[server].name, &date[0], &elapsed[0]);
            }
         }
      }

      size = pgmoneta_directory_size(d);
      pgmoneta_add_backup_info(root, size);
   }

   atomic_store(&config->servers[server].backup, false);

done:

   pgmoneta_stop_logging();

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(root);
   free(d);
   free(cmd);
   free(server_path);
   free(from);
   free(to);

   exit(0);
}
