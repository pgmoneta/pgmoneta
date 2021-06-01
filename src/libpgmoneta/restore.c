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
#include <restore.h>
#include <utils.h>

/* system */
#include <stdlib.h>
#include <unistd.h>

void
pgmoneta_restore(int server, char* backup_id, char* directory, char** argv)
{
   char elapsed[128];
   time_t start_time;
   int total_seconds;
   int hours;
   int minutes;
   int seconds;
   char* from = NULL;
   char* to = NULL;
   struct configuration* config;

   pgmoneta_start_logging();

   config = (struct configuration*)shmem;

   pgmoneta_set_proc_title(1, argv, "restore", config->servers[server].name);

   start_time = time(NULL);

   from = pgmoneta_append(from, config->base_dir);
   from = pgmoneta_append(from, "/");
   from = pgmoneta_append(from, config->servers[server].name);
   from = pgmoneta_append(from, "/backup/");
   from = pgmoneta_append(from, backup_id);
   from = pgmoneta_append(from, "/data");

   to = pgmoneta_append(to, directory);
   to = pgmoneta_append(to, "/");
   to = pgmoneta_append(to, backup_id);

   pgmoneta_delete_directory(to);

   if (pgmoneta_copy_directory(from, to))
   {
      pgmoneta_log_error("Restore: Could not restore %s/%s", config->servers[server].name, backup_id);
   }
   else
   {
      if (config->compression_type == COMPRESSION_GZIP)
      {
         pgmoneta_gunzip_data(to);
      }

      total_seconds = (int)difftime(time(NULL), start_time);
      hours = total_seconds / 3600;
      minutes = (total_seconds % 3600) / 60;
      seconds = total_seconds % 60;

      memset(&elapsed[0], 0, sizeof(elapsed));
      sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

      pgmoneta_log_info("Restore: %s/%s (Elapsed: %s)", config->servers[server].name, backup_id, &elapsed[0]);
   }

   pgmoneta_stop_logging();

   free(from);
   free(to);

   free(backup_id);
   free(directory);

   exit(0);
}
