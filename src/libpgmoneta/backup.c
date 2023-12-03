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
#include <backup.h>
#include <info.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>

void
pgmoneta_backup(int client_fd, int server, char** argv)
{
   bool active = false;
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
   unsigned long size;
   struct workflow* workflow = NULL;
   struct workflow* current = NULL;
   struct node* i_nodes = NULL;
   struct node* o_nodes = NULL;
   struct configuration* config;

   pgmoneta_start_logging();

   config = (struct configuration*)shmem;

   pgmoneta_set_proc_title(1, argv, "backup", config->servers[server].name);

   if (!config->servers[server].valid)
   {
      pgmoneta_log_error("Backup: Server %s is not in a valid configuration", config->servers[server].name);
      goto error;
   }

   if (!atomic_compare_exchange_strong(&config->servers[server].backup, &active, true))
   {
      goto done;
   }

   start_time = time(NULL);

   memset(&date[0], 0, sizeof(date));
   time(&current_time);
   time_info = localtime(&current_time);
   strftime(&date[0], sizeof(date), "%Y%m%d%H%M%S", time_info);

   root = pgmoneta_get_server_backup_identifier(server, &date[0]);

   pgmoneta_mkdir(root);

   d = pgmoneta_get_server_backup_identifier_data(server, &date[0]);

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_BACKUP);

   current = workflow;
   while (current != NULL)
   {
      if (current->setup(server, &date[0], i_nodes, &o_nodes))
      {
         goto error;
      }
      current = current->next;
   }

   current = workflow;
   while (current != NULL)
   {
      if (current->execute(server, &date[0], i_nodes, &o_nodes))
      {
         goto error;
      }
      current = current->next;
   }

   current = workflow;
   while (current != NULL)
   {
      if (current->teardown(server, &date[0], i_nodes, &o_nodes))
      {
         goto error;
      }
      current = current->next;
   }

   size = pgmoneta_directory_size(d);
   pgmoneta_update_info_unsigned_long(root, INFO_BACKUP, size);

   total_seconds = (int)difftime(time(NULL), start_time);
   hours = total_seconds / 3600;
   minutes = (total_seconds % 3600) / 60;
   seconds = total_seconds % 60;

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

   pgmoneta_log_info("Backup: %s/%s (Elapsed: %s)", config->servers[server].name, &date[0], &elapsed[0]);

   pgmoneta_update_info_unsigned_long(root, INFO_ELAPSED, total_seconds);

   atomic_store(&config->servers[server].backup, false);

done:

   pgmoneta_workflow_delete(workflow);

   pgmoneta_free_nodes(i_nodes);

   pgmoneta_free_nodes(o_nodes);

   free(root);
   free(d);

   pgmoneta_management_process_result(client_fd, server, NULL, 0, true);
   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(0);

error:

   pgmoneta_workflow_delete(workflow);

   pgmoneta_free_nodes(i_nodes);

   pgmoneta_free_nodes(o_nodes);

   free(root);
   free(d);

   pgmoneta_management_process_result(client_fd, server, NULL, 1, true);
   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(1);
}
