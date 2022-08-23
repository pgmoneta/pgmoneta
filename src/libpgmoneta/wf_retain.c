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
#include <node.h>
#include <pgmoneta.h>
#include <info.h>
#include <link.h>
#include <logging.h>
#include <delete.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

static int retain_setup(int, char*, struct node*, struct node**);
static int retain_execute(int, char*, struct node*, struct node**);
static int retain_teardown(int, char*, struct node*, struct node**);

struct workflow*
pgmoneta_workflow_create_retention(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->setup = &retain_setup;
   wf->execute = &retain_execute;
   wf->teardown = &retain_teardown;
   wf->next = NULL;

   return wf;
}

static int
retain_setup(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   return 0;
}

static int
retain_execute(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* d;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   time_t t;
   char check_date[128];
   struct tm* time_info;
   struct configuration* config;

   config = (struct configuration*)shmem;

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

      d = pgmoneta_get_server_backup(i);

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

   return 0;
}

static int
retain_teardown(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   return 0;
}
