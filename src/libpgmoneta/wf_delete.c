/*
 * Copyright (C) 2024 The pgmoneta community
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
#include <deque.h>
#include <info.h>
#include <link.h>
#include <logging.h>
#include <utils.h>
#include <workers.h>
#include <workflow.h>

/* system */
#include <stdatomic.h>
#include <stdlib.h>

static int delete_backup_setup(int, char*, struct deque*);
static int delete_backup_execute(int, char*, struct deque*);
static int delete_backup_teardown(int, char*, struct deque*);

struct workflow*
pgmoneta_workflow_delete_backup(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->setup = &delete_backup_setup;
   wf->execute = &delete_backup_execute;
   wf->teardown = &delete_backup_teardown;
   wf->next = NULL;

   return wf;
}

static int
delete_backup_setup(int server, char* identifier, struct deque* nodes)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Delete (setup): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   return 0;
}

static int
delete_backup_execute(int server, char* identifier, struct deque* nodes)
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
   int number_of_workers = 0;
   struct backup** backups = NULL;
   struct workers* workers = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Delete (execute): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   active = false;

   if (!atomic_compare_exchange_strong(&config->servers[server].delete, &active, true))
   {
      goto error;
   }

   d = pgmoneta_get_server_backup(server);

   if (pgmoneta_get_backups(d, &number_of_backups, &backups))
   {
      goto error;
   }

   free(d);
   d = NULL;

   if (!strcmp(identifier, "oldest"))
   {
      for (int i = 0; backup_index == -1 && i < number_of_backups; i++)
      {
         if (backups[i] != NULL)
         {
            backup_index = i;
         }
      }
   }
   else if (!strcmp(identifier, "latest") || !strcmp(identifier, "newest"))
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
         if (backups[i] != NULL && !strcmp(backups[i]->label, identifier))
         {
            backup_index = i;
         }
      }
   }

   if (backup_index == -1)
   {
      pgmoneta_log_error("Delete: No identifier for %s/%s", config->servers[server].name, identifier);
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

   d = pgmoneta_get_server_backup_identifier(server, backups[backup_index]->label);

   number_of_workers = pgmoneta_get_number_of_workers(server);
   if (number_of_workers > 0)
   {
      pgmoneta_workers_initialize(number_of_workers, &workers);
   }

   if (backups[backup_index]->valid == VALID_TRUE)
   {
      if (prev_index != -1 && next_index != -1)
      {
         /* In-between valid backup */
         from = pgmoneta_get_server_backup_identifier_data(server, backups[backup_index]->label);
         to = pgmoneta_get_server_backup_identifier_data(server, backups[next_index]->label);

         pgmoneta_relink(from, to, workers);

         /* Delete from */
         pgmoneta_delete_directory(d);
         free(d);
         d = NULL;

         /* Recalculate to */
         d = pgmoneta_get_server_backup_identifier(server, backups[next_index]->label);

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
         from = pgmoneta_get_server_backup_identifier_data(server, backups[backup_index]->label);
         to = pgmoneta_get_server_backup_identifier_data(server, backups[next_index]->label);

         pgmoneta_relink(from, to, workers);

         /* Delete from */
         pgmoneta_delete_directory(d);
         free(d);
         d = NULL;

         /* Recalculate to */
         d = pgmoneta_get_server_backup_identifier(server, backups[next_index]->label);

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

   if (number_of_workers > 0)
   {
      pgmoneta_workers_wait(workers);
      pgmoneta_workers_destroy(workers);
   }

   pgmoneta_log_info("Delete: %s/%s", config->servers[server].name, backups[backup_index]->label);

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(d);

   atomic_store(&config->servers[server].delete, false);

   return 0;

error:

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(d);

   atomic_store(&config->servers[server].delete, false);

   return 1;
}

static int
delete_backup_teardown(int server, char* identifier, struct deque* nodes)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Delete (teardown): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   return 0;
}
