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
#include <delete.h>
#include <workflow.h>
#include <info.h>
#include <link.h>
#include <logging.h>
#include <utils.h>

/* system */
#include <stdatomic.h>
#include <stdlib.h>

int
pgmoneta_delete(int srv, char* backup_id)
{
   struct workflow* workflow = NULL;
   struct workflow* current = NULL;
   struct node* i_nodes = NULL;
   struct node* o_nodes = NULL;

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_DELETE_BACKUP);

   current = workflow;
   while (current != NULL)
   {
      if (current->setup(srv, backup_id, i_nodes, &o_nodes))
      {
         goto error;
      }
      current = current->next;
   }

   current = workflow;
   while (current != NULL)
   {
      if (current->execute(srv, backup_id, i_nodes, &o_nodes))
      {
         goto error;
      }
      current = current->next;
   }

   current = workflow;
   while (current != NULL)
   {
      if (current->teardown(srv, backup_id, i_nodes, &o_nodes))
      {
         goto error;
      }
      current = current->next;
   }

   pgmoneta_workflow_delete(workflow);

   return 0;

error:

   pgmoneta_workflow_delete(workflow);

   return 1;
}

int
pgmoneta_delete_wal(int srv)
{
   int backup_index = -1;
   char* d = NULL;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   char* srv_wal = NULL;
   int number_of_srv_wal_files = 0;
   char** srv_wal_files = NULL;
   int number_of_wal_files = 0;
   char** wal_files = NULL;
   bool delete;

   /* Find the oldest backup */
   d = pgmoneta_get_server_backup(srv);

   if (pgmoneta_get_backups(d, &number_of_backups, &backups))
   {
      goto error;
   }

   for (int i = 0; backup_index == -1 && i < number_of_backups; i++)
   {
      if (!backups[i]->keep && backups[i]->valid == VALID_TRUE)
      {
         backup_index = i;
      }
   }

   free(d);
   d = NULL;

   /* Find the oldest WAL file */
   if (backup_index == 0)
   {
      d = pgmoneta_get_server_backup_identifier_data_wal(srv, backups[backup_index]->label);

      number_of_srv_wal_files = 0;
      srv_wal_files = NULL;

      pgmoneta_get_wal_files(d, &number_of_srv_wal_files, &srv_wal_files);

      if (number_of_srv_wal_files > 0)
      {
         srv_wal = srv_wal_files[0];
      }

      free(d);
      d = NULL;
   }

   /* Delete WAL if there are no backups, or the oldest one is valid */
   if (number_of_backups == 0 || backup_index == 0)
   {
      /* Find WAL files */
      d = pgmoneta_get_server_wal(srv);

      number_of_wal_files = 0;
      wal_files = NULL;

      pgmoneta_get_wal_files(d, &number_of_wal_files, &wal_files);

      free(d);
      d = NULL;

      /* Delete outdated WAL files */
      for (int i = 0; i < number_of_wal_files; i++)
      {
         delete = false;

         if (backup_index == -1)
         {
            delete = true;
         }
         else if (srv_wal != NULL)
         {
            if (strcmp(wal_files[i], srv_wal) < 0)
            {
               delete = true;
            }
         }

         if (delete)
         {
            d = pgmoneta_get_server_wal(srv);
            d = pgmoneta_append(d, wal_files[i]);

            pgmoneta_log_trace("WAL: Deleting %s", d);
            pgmoneta_delete_file(d);

            free(d);
            d = NULL;
         }
         else
         {
            break;
         }
      }
   }

   for (int i = 0; i < number_of_srv_wal_files; i++)
   {
      free(srv_wal_files[i]);
   }
   free(srv_wal_files);

   for (int i = 0; i < number_of_wal_files; i++)
   {
      free(wal_files[i]);
   }
   free(wal_files);

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   return 0;

error:

   free(d);

   for (int i = 0; i < number_of_srv_wal_files; i++)
   {
      free(srv_wal_files[i]);
   }
   free(srv_wal_files);

   for (int i = 0; i < number_of_wal_files; i++)
   {
      free(wal_files[i]);
   }
   free(wal_files);

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   return 1;
}
