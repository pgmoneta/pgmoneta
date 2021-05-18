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
   char* b;
   char* d;
   char* f;
   char* w;
   int number_of_directories;
   char** dirs;
   int number_of_files;
   char** files;
   int number_of_wals;
   char** wals;
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
            w = NULL;
            f = NULL;

            w = pgmoneta_append(w, config->base_dir);
            w = pgmoneta_append(w, "/");
            w = pgmoneta_append(w, config->servers[i].name);
            w = pgmoneta_append(w, "/backup/");
            w = pgmoneta_append(w, dirs[0]);
            w = pgmoneta_append(w, "/data/pg_wal/");

            f = pgmoneta_append(f, config->base_dir);
            f = pgmoneta_append(f, "/");
            f = pgmoneta_append(f, config->servers[i].name);
            f = pgmoneta_append(f, "/wal/");

            number_of_wals = 0;
            wals = NULL;

            number_of_files = 0;
            files = NULL;

            pgmoneta_get_files(w, &number_of_wals, &wals);
            pgmoneta_get_files(f, &number_of_files, &files);

            for (int j = 0; j < number_of_files; j++)
            {
               if (strcmp(files[j], wals[0]) < 0)
               {
                  b = NULL;

                  b = pgmoneta_append(b, config->base_dir);
                  b = pgmoneta_append(b, "/");
                  b = pgmoneta_append(b, config->servers[i].name);
                  b = pgmoneta_append(b, "/wal/");
                  b = pgmoneta_append(b, files[j]);

                  pgmoneta_delete_file(b);

                  free(b);
               }
            }

            pgmoneta_delete(i, dirs[0]);
            pgmoneta_log_info("Retention: %s/%s", config->servers[i].name, dirs[0]);

            for (int i = 0; i < number_of_wals; i++)
            {
               free(wals[i]);
            }
            free(wals);

            for (int i = 0; i < number_of_files; i++)
            {
               free(files[i]);
            }
            free(files);

            free(w);
            free(f);
         }
      }
      else if (number_of_directories == 0)
      {
         f = NULL;

         f = pgmoneta_append(f, config->base_dir);
         f = pgmoneta_append(f, "/");
         f = pgmoneta_append(f, config->servers[i].name);
         f = pgmoneta_append(f, "/wal/");

         number_of_files = 0;
         files = NULL;

         pgmoneta_get_files(f, &number_of_files, &files);

         for (int j = 0; j < number_of_files; j++)
         {
            if (pgmoneta_ends_with(files[j], ".partial"))
            {
               continue;
            }

            b = NULL;

            b = pgmoneta_append(b, config->base_dir);
            b = pgmoneta_append(b, "/");
            b = pgmoneta_append(b, config->servers[i].name);
            b = pgmoneta_append(b, "/wal/");
            b = pgmoneta_append(b, files[j]);

            pgmoneta_delete_file(b);

            free(b);
         }

         for (int i = 0; i < number_of_files; i++)
         {
            free(files[i]);
         }
         free(files);

         free(f);
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
