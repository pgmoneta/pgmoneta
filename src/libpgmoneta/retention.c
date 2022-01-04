/*
 * Copyright (C) 2022 Red Hat
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
#include <info.h>
#include <logging.h>
#include <retention.h>
#include <utils.h>

/* system */
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

void
pgmoneta_retention(char** argv)
{
   char* d;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   time_t t;
   char check_date[128];
   struct tm* time_info;
   struct configuration* config;

   pgmoneta_start_logging();

   config = (struct configuration*)shmem;

   pgmoneta_set_proc_title(1, argv, "retention", NULL);

   for (int i = 0; i < config->number_of_servers; i++)
   {
      int retention;

      t = time(NULL);

      retention = config->servers[i].retention;
      if (retention <= 0)
      {
         retention = config->retention;
      }

      memset(&check_date[0], 0, sizeof(check_date));
      t = t - (retention * 24 * 60 * 60);
      time_info = localtime(&t);
      strftime(&check_date[0], sizeof(check_date), "%Y%m%d%H%M%S", time_info);

      number_of_backups = 0;
      backups = NULL;

      d = NULL;

      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[i].name);
      d = pgmoneta_append(d, "/backup/");

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      if (number_of_backups > 0)
      {
         for (int j = 0; j < number_of_backups; j++)
         {
            if (strcmp(backups[j]->label, &check_date[0]) < 0)
            {
               if (!backups[j]->keep)
               {
                  if (!atomic_load(&config->servers[i].delete))
                  {
                     pgmoneta_delete(i, backups[j]->label);
                     pgmoneta_log_info("Retention: %s/%s", config->servers[i].name, backups[j]->label);
                  }
               }
            }
            else
            {
               break;
            }
         }
      }

      pgmoneta_delete_wal(i);

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);

      free(d);
   }

   pgmoneta_stop_logging();

   exit(0);
}
