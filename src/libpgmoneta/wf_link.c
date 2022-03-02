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
#include <backup.h>
#include <info.h>
#include <link.h>
#include <logging.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <stdlib.h>

static int link_setup(int, char*);
static int link_execute(int, char*);
static int link_teardown(int, char*);

struct workflow*
pgmoneta_workflow_create_link(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->setup = &link_setup;
   wf->execute = &link_execute;
   wf->teardown = &link_teardown;
   wf->next = NULL;

   return wf;
}

static int
link_setup(int server, char* identifier)
{
   return 0;
}

static int
link_execute(int server, char* identifier)
{
   char* server_path = NULL;
   char* from = NULL;
   char* to = NULL;
   int next_newest = -1;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   time_t link_time;
   int total_seconds;
   int hours;
   int minutes;
   int seconds;
   char elapsed[128];
   struct configuration* config;

   config = (struct configuration*)shmem;

   link_time = time(NULL);

   server_path = pgmoneta_get_server_backup(server);

   pgmoneta_get_backups(server_path, &number_of_backups, &backups);

   if (number_of_backups >= 2)
   {
      for (int j = number_of_backups - 2; j >= 0 && next_newest == -1; j--)
      {
         if (backups[j]->valid == VALID_TRUE)
         {
            if (next_newest == -1)
            {
               next_newest = j;
            }
         }
      }

      if (next_newest != -1)
      {
         from = pgmoneta_get_server_backup_identifier_data(server, identifier);
         to = pgmoneta_get_server_backup_identifier_data(server, backups[next_newest]->label);

         pgmoneta_link(from, to);

         total_seconds = (int)difftime(time(NULL), link_time);
         hours = total_seconds / 3600;
         minutes = (total_seconds % 3600) / 60;
         seconds = total_seconds % 60;

         memset(&elapsed[0], 0, sizeof(elapsed));
         sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

         pgmoneta_log_debug("Link: %s/%s (Elapsed: %s)", config->servers[server].name, identifier, &elapsed[0]);
      }
   }

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(server_path);
   free(from);
   free(to);

   return 0;
}

static int
link_teardown(int server, char* identifier)
{
   return 0;
}
