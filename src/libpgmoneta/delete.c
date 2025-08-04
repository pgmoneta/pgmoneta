/*
 * Copyright (C) 2025 The pgmoneta community
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
#include <logging.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Delete wal files older than the given srv_wal file under the base directory
 * Base directory could be the wal/ or the wal_shipping directory
 * @param srv_wal The oldest wal segment file we would like to keep
 * @param base The base directory holding the wal segments
 * @param backup_index The index of the oldest backup
 */
static void
delete_wal_older_than(char* srv_wal, char* base, int backup_index);

int
pgmoneta_delete(int srv, char* label)
{
   int ec = -1;
   char* en = NULL;
   struct workflow* workflow = NULL;
   struct art* nodes = NULL;
   struct backup* backup = NULL;

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_DELETE_BACKUP, NULL);

   if (pgmoneta_art_create(&nodes))
   {
      goto error;
   }

   if (pgmoneta_workflow_nodes(srv, label, nodes, &backup))
   {
      goto error;
   }

   if (pgmoneta_workflow_execute(workflow, nodes, &en, &ec))
   {
      goto error;
   }

   free(backup);
   pgmoneta_art_destroy(nodes);

   pgmoneta_workflow_destroy(workflow);

   return 0;

error:

   pgmoneta_log_error("Delete: %s (%d)", en, ec);

   free(backup);
   pgmoneta_art_destroy(nodes);

   pgmoneta_workflow_destroy(workflow);

   return 1;
}

int
pgmoneta_delete_wal(int srv)
{
   int backup_index = -1;
   char* d = NULL;
   struct backup* backup = NULL;
   char* srv_wal = NULL;
   char* wal_shipping = NULL;
   int number_of_srv_wal_files = 0;
   char** srv_wal_files = NULL;

   /* Find the oldest backup */
   d = pgmoneta_get_server_backup(srv);

   if (pgmoneta_load_info(d, "oldest", &backup))
   {
      goto error;
   }

   free(d);
   d = NULL;

   if (backup != NULL)
   {
      d = pgmoneta_get_server_backup_identifier_data_wal(srv, backup->label);

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
   if (backup == NULL)
   {
      d = pgmoneta_get_server_wal(srv);
      delete_wal_older_than(srv_wal, d, backup_index);
      free(d);
      d = NULL;

      /* Also delete WAL under wal_shipping directory */
      wal_shipping = pgmoneta_get_server_wal_shipping_wal(srv);
      if (wal_shipping != NULL)
      {
         delete_wal_older_than(srv_wal, wal_shipping, backup_index);
      }

      free(wal_shipping);
      wal_shipping = NULL;
   }

   for (int i = 0; i < number_of_srv_wal_files; i++)
   {
      free(srv_wal_files[i]);
   }
   free(srv_wal_files);

   return 0;

error:

   free(d);
   free(wal_shipping);

   for (int i = 0; i < number_of_srv_wal_files; i++)
   {
      free(srv_wal_files[i]);
   }
   free(srv_wal_files);

   return 1;
}

static void
delete_wal_older_than(char* srv_wal, char* base, int backup_index)
{
   int number_of_wal_files = 0;
   char** wal_files = NULL;
   char wal_address[MAX_PATH];
   bool delete;

   if (pgmoneta_get_wal_files(base, &number_of_wal_files, &wal_files))
   {
      pgmoneta_log_warn("Unable to get WAL segments under %s", base);
      goto error;
   }
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
         memset(wal_address, 0, MAX_PATH);
         if (pgmoneta_ends_with(base, "/"))
         {
            snprintf(wal_address, MAX_PATH, "%s%s", base, wal_files[i]);
         }
         else
         {
            snprintf(wal_address, MAX_PATH, "%s/%s", base, wal_files[i]);
         }

         pgmoneta_log_trace("WAL: Deleting %s", wal_address);
         if (pgmoneta_exists(wal_address))
         {
            pgmoneta_delete_file(wal_address, NULL);
         }
         else
         {
            pgmoneta_log_debug("%s doesn't exists", wal_address);
         }
      }
      else
      {
         break;
      }
   }

   for (int i = 0; i < number_of_wal_files; i++)
   {
      free(wal_files[i]);
   }
   free(wal_files);

   return;

error:
   for (int i = 0; i < number_of_wal_files; i++)
   {
      free(wal_files[i]);
   }
   free(wal_files);
}
