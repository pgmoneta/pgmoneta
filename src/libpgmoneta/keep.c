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
#include <info.h>
#include <keep.h>
#include <logging.h>
#include <utils.h>

/* system */
#include <stdatomic.h>
#include <stdlib.h>

static int keep(char* prefix, int srv, char* backup_id, bool k);

int
pgmoneta_retain_backup(int srv, char* backup_id)
{
   return keep("Retain", srv, backup_id, true);
}

int
pgmoneta_expunge_backup(int srv, char* backup_id)
{
   return keep("Expunge", srv, backup_id, false);
}

static int
keep(char* prefix, int srv, char* backup_id, bool k)
{
   char* d = NULL;
   int backup_index = -1;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   d = NULL;
   d = pgmoneta_append(d, config->base_dir);
   d = pgmoneta_append(d, "/");
   d = pgmoneta_append(d, config->servers[srv].name);
   d = pgmoneta_append(d, "/backup/");

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
      pgmoneta_log_error("%s: No identifier for %s/%s", prefix, config->servers[srv].name, backup_id);
      goto error;
   }

   if (backups[backup_index]->valid)
   {
      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[srv].name);
      d = pgmoneta_append(d, "/backup/");
      d = pgmoneta_append(d, backups[backup_index]->label);

      pgmoneta_update_keep_info(d, k);

      free(d);
      d = NULL;
   }

   pgmoneta_log_info("%s: %s/%s", prefix, config->servers[srv].name, backups[backup_index]->label);

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(d);

   return 0;

error:

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(d);

   return 1;
}
