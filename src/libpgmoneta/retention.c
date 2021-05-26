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
#include <delete.h>
#include <logging.h>
#include <retention.h>
#include <utils.h>

/* system */
#include <stdlib.h>
#include <unistd.h>

void
pgmoneta_retention(char** argv)
{
   char* d;
   int number_of_directories;
   char** dirs;
   time_t t;
   char check_date[128];
   struct tm* time_info;
   struct configuration* config;

   pgmoneta_start_logging();

   config = (struct configuration*)shmem;

   pgmoneta_set_proc_title(1, argv, "retention", NULL);

   t = time(NULL);

   memset(&check_date[0], 0, sizeof(check_date));
   t = t - (config->retention * 24 * 60 * 60);
   time_info = localtime(&t);
   strftime(&check_date[0], sizeof(check_date), "%Y%m%d%H%M%S", time_info);

   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = NULL;

      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[i].name);
      d = pgmoneta_append(d, "/backup/");

      number_of_directories = 0;
      dirs = NULL;

      pgmoneta_get_directories(d, &number_of_directories, &dirs);

      if (number_of_directories > 0)
      {
         if (strcmp(dirs[0], &check_date[0]) < 0)
         {
            pgmoneta_delete(i, dirs[0]);
            pgmoneta_delete_wal(i);
            pgmoneta_log_info("Retention: %s/%s", config->servers[i].name, dirs[0]);
         }
      }
      else if (number_of_directories == 0)
      {
         pgmoneta_delete_wal(i);
      }

      for (int i = 0; i < number_of_directories; i++)
      {
         free(dirs[i]);
      }
      free(dirs);

      free(d);
   }

   pgmoneta_stop_logging();

   exit(0);
}
