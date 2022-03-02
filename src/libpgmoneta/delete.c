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
#include <link.h>
#include <logging.h>
#include <utils.h>

/* system */
#include <stdatomic.h>
#include <stdlib.h>

int
pgmoneta_delete(int srv, char* backup_id)
{
   bool active;
   int backup_index = -1;
   int prev_index = -1;
   int next_index = -1;
   char* d = NULL;
   char* from = NULL;
   char* to = NULL;
   unsigned long size;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   active = false;

   if (!atomic_compare_exchange_strong(&config->servers[srv].delete, &active, true))
   {
      goto error;
   }

   d = pgmoneta_get_server_backup(srv);

   if (pgmoneta_get_backups(d, &number_of_backups, &backups))
   {
      goto error;
   }

   free(d);
   d = NULL;

   if (!strcmp(backup_id, "oldest"))
   {
      for (int i = 0; backup_index == -1 && i < number_of_backups; i++)
      {
         if (backups[i] != NULL)
         {
            backup_index = i;
         }
      }
   }
   else if (!strcmp(backup_id, "latest") || !strcmp(backup_id, "newest"))
   {
      for (int i = number_of_backups - 1; backup_index == -1 && i >= 0; i--)
      {
         if (backups[i] != NULL)
         {
            backup_index = i;
         }
      }
   }
   else
   {
      for (int i = 0; backup_index == -1 && i < number_of_backups; i++)
      {
         if (backups[i] != NULL && !strcmp(backups[i]->label, backup_id))
         {
            backup_index = i;
         }
      }
   }

   if (backup_index == -1)
   {
      pgmoneta_log_error("Delete: No identifier for %s/%s", config->servers[srv].name, backup_id);
      goto error;
   }

   /* Find previous valid backup */
   for (int i = backup_index - 1; prev_index == -1 && i >= 0; i--)
   {
      if (backups[i]->valid == VALID_TRUE)
      {
         prev_index = i;
      }
   }

   /* Find next valid backup */
   for (int i = backup_index + 1; next_index == -1 && i < number_of_backups; i++)
   {
      if (backups[i]->valid == VALID_TRUE)
      {
         next_index = i;
      }
   }

   d = pgmoneta_get_server_backup_identifier(srv, backups[backup_index]->label);

   if (backups[backup_index]->valid == VALID_TRUE)
   {
      if (prev_index != -1 && next_index != -1)
      {
         /* In-between valid backup */
         from = pgmoneta_get_server_backup_identifier_data(srv, backups[backup_index]->label);
         to = pgmoneta_get_server_backup_identifier_data(srv, backups[next_index]->label);

         pgmoneta_relink(from, to);

         /* Delete from */
         pgmoneta_delete_directory(d);
         free(d);
         d = NULL;

         /* Recalculate to */
         d = pgmoneta_get_server_backup_identifier(srv, backups[next_index]->label);

         size = pgmoneta_directory_size(d);
         pgmoneta_update_info_unsigned_long(d, INFO_BACKUP, size);

         free(from);
         free(to);
         from = NULL;
         to = NULL;
      }
      else if (prev_index != -1)
      {
         /* Latest valid backup */
         pgmoneta_delete_directory(d);
      }
      else if (next_index != -1)
      {
         /* Oldest valid backup */
         from = pgmoneta_get_server_backup_identifier_data(srv, backups[backup_index]->label);
         to = pgmoneta_get_server_backup_identifier_data(srv, backups[next_index]->label);

         pgmoneta_relink(from, to);

         /* Delete from */
         pgmoneta_delete_directory(d);
         free(d);
         d = NULL;

         /* Recalculate to */
         d = pgmoneta_get_server_backup_identifier(srv, backups[next_index]->label);

         size = pgmoneta_directory_size(d);
         pgmoneta_update_info_unsigned_long(d, INFO_BACKUP, size);

         free(from);
         free(to);
         from = NULL;
         to = NULL;
      }
      else
      {
         /* Only valid backup */
         pgmoneta_delete_directory(d);
      }
   }
   else
   {
      /* Just delete */
      pgmoneta_delete_directory(d);
   }

   pgmoneta_log_info("Delete: %s/%s", config->servers[srv].name, backups[backup_index]->label);

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(d);

   atomic_store(&config->servers[srv].delete, false);

   return 0;

error:

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(d);

   atomic_store(&config->servers[srv].delete, false);

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
