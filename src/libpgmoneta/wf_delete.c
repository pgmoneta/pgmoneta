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
#include <art.h>
#include <backup.h>
#include <link.h>
#include <logging.h>
#include <management.h>
#include <restore.h>
#include <utils.h>
#include <value.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <strings.h>

static char* delete_name(void);
static int delete_backup_execute(char*, struct art*);

static int delete_backup(int server, int index, struct backup* backup, int number_of_backups, struct backup** backups);

struct workflow*
pgmoneta_create_delete_backup(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->name = &delete_name;
   wf->setup = &pgmoneta_common_setup;
   wf->execute = &delete_backup_execute;
   wf->teardown = &pgmoneta_common_teardown;
   wf->next = NULL;

   return wf;
}

static char*
delete_name(void)
{
   return "Delete";
}

static int
delete_backup_execute(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   bool active = false;
   int backup_index = -1;
   char* label = NULL;
   char* d = NULL;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   struct backup* backup = NULL;
   struct backup* child = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   if (pgmoneta_log_is_enabled(PGMONETA_LOGGING_LEVEL_DEBUG1))
   {
      char* a = NULL;
      a = pgmoneta_art_to_string(nodes, FORMAT_TEXT, NULL, 0);
      pgmoneta_log_debug("(Tree)\n%s", a);
      free(a);
   }
   assert(nodes != NULL);
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("Delete (execute): %s/%s", config->common.servers[server].name, label);

   active = false;

   if (!atomic_compare_exchange_strong(&config->common.servers[server].repository, &active, true))
   {
      pgmoneta_log_info("Delete: Server %s is active", config->common.servers[server].name);

      goto done;
   }

   config->common.servers[server].active_delete = true;

   d = pgmoneta_get_server_backup(server);

   if (pgmoneta_get_backups(d, &number_of_backups, &backups))
   {
      pgmoneta_art_insert(nodes, NODE_ERROR_CODE, (uintptr_t)MANAGEMENT_ERROR_DELETE_BACKUP_NOBACKUPS, ValueInt32);
      goto error;
   }

   free(d);
   d = NULL;

   /* Find backup index */
   for (int i = 0; backup_index == -1 && i < number_of_backups; i++)
   {
      if (!strcmp(backups[i]->label, label))
      {
         backup_index = i;
      }
   }

   if (backup_index == -1)
   {
      pgmoneta_art_insert(nodes, NODE_ERROR_CODE, (uintptr_t)MANAGEMENT_ERROR_DELETE_BACKUP_NOBACKUP, ValueInt32);
      pgmoneta_log_error("Delete: No identifier for %s/%s", config->common.servers[server].name, label);
      goto error;
   }

   if (backups[backup_index]->keep)
   {
      pgmoneta_art_insert(nodes, NODE_ERROR_CODE, (uintptr_t)MANAGEMENT_ERROR_DELETE_BACKUP_RETAINED, ValueInt32);
      pgmoneta_log_warn("Delete: Backup is retained for %s/%s", config->common.servers[server].name, label);
      goto error;
   }

   pgmoneta_get_backup_child(server, backups[backup_index], &child);
   if (child != NULL)
   {
      if (pgmoneta_rollup_backups(server, child->label, label))
      {
         pgmoneta_art_insert(nodes, NODE_ERROR_CODE, (uintptr_t)MANAGEMENT_ERROR_DELETE_BACKUP_ROLLUP, ValueInt32);
         pgmoneta_log_error("Delete: Unable to roll up backup %s to %s", label, child->label);
         goto error;
      }
   }

   if (delete_backup(server, backup_index, backups[backup_index], number_of_backups, backups))
   {
      pgmoneta_art_insert(nodes, NODE_ERROR_CODE, (uintptr_t)MANAGEMENT_ERROR_DELETE_BACKUP_FULL, ValueInt32);
      pgmoneta_log_error("Delete: Full backup error for %s/%s", config->common.servers[server].name, label);
      goto error;
   }

done:

   pgmoneta_log_debug("Delete: %s/%s", config->common.servers[server].name, backups[backup_index]->label);

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);
   free(backup);

   if (config->common.servers[server].number_of_hot_standbys > 0)
   {
      d = pgmoneta_get_server_backup(server);

      if (pgmoneta_get_backups(d, &number_of_backups, &backups))
      {
         goto error;
      }

      if (number_of_backups == 0)
      {
         for (int j = 0; j < config->common.servers[server].number_of_hot_standbys; j++)
         {
            char* hs = NULL;
            hs = pgmoneta_append(hs, config->common.servers[server].hot_standby[j]);
            if (!pgmoneta_ends_with(hs, "/"))
            {
               hs = pgmoneta_append_char(hs, '/');
            }
            if (pgmoneta_exists(hs))
            {
               pgmoneta_delete_directory(hs);

               pgmoneta_log_info("Hot standby deleted: %s (directory %d: %s)",
                                 config->common.servers[server].name,
                                 j + 1,
                                 config->common.servers[server].hot_standby[j]);
            }
            free(hs);
         }
      }

      for (int i = 0; i < number_of_backups; i++)
      {
         free(backups[i]);
      }
      free(backups);
   }

   free(d);

   free(child);

   config->common.servers[server].active_delete = false;
   atomic_store(&config->common.servers[server].repository, false);
   pgmoneta_log_trace("Delete is ready for %s", config->common.servers[server].name);

   return 0;

error:
   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);
   free(backup);

   free(d);

   free(child);

   config->common.servers[server].active_delete = false;
   atomic_store(&config->common.servers[server].repository, false);
   pgmoneta_log_trace("Delete is ready for %s", config->common.servers[server].name);

   return 1;
}

static int
delete_backup(int server, int index, struct backup* backup __attribute__((unused)), int number_of_backups, struct backup** backups)
{
   int prev_index = -1;
   int next_index = -1;
   char* from = NULL;
   char* to = NULL;
   char* d = NULL;
   unsigned long size;
   int number_of_workers = 0;
   struct workers* workers = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   /* Find previous valid backup */
   for (int i = index - 1; prev_index == -1 && i >= 0; i--)
   {
      if (pgmoneta_is_backup_struct_valid(server, backups[i]))
      {
         prev_index = i;
      }
   }

   /* Find next valid backup */
   for (int i = index + 1; next_index == -1 && i < number_of_backups; i++)
   {
      if (pgmoneta_is_backup_struct_valid(server, backups[i]))
      {
         next_index = i;
      }
   }

   if (prev_index != -1)
   {
      pgmoneta_log_trace("Prev label: %s/%s", config->common.servers[server].name, backups[prev_index]->label);
   }

   pgmoneta_log_trace("Delt label: %s/%s", config->common.servers[server].name, backups[index]->label);

   if (next_index != -1)
   {
      pgmoneta_log_trace("Next label: %s/%s", config->common.servers[server].name, backups[next_index]->label);
   }

   d = pgmoneta_get_server_backup_identifier(server, backups[index]->label);

   number_of_workers = pgmoneta_get_number_of_workers(server);
   if (number_of_workers > 0)
   {
      pgmoneta_workers_initialize(number_of_workers, &workers);
   }

   if (pgmoneta_is_backup_struct_valid(server, backups[index]))
   {
      if (prev_index != -1 && next_index != -1)
      {
         /* In-between valid backup */
         from = pgmoneta_get_server_backup_identifier_data(server, backups[index]->label);
         to = pgmoneta_get_server_backup_identifier_data(server, backups[next_index]->label);

         pgmoneta_relink(from, to, workers);

         pgmoneta_workers_wait(workers);
         if (workers != NULL && !workers->outcome)
         {
            goto error;
         }
         pgmoneta_workers_destroy(workers);

         /* Delete from */
         pgmoneta_delete_directory(d);
         free(d);
         d = NULL;

         /* Recalculate to */
         d = pgmoneta_get_server_backup_identifier(
            server, backups[next_index]->label);

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
         from = pgmoneta_get_server_backup_identifier_data(
            server, backups[index]->label);
         to = pgmoneta_get_server_backup_identifier_data(
            server, backups[next_index]->label);

         pgmoneta_relink(from, to, workers);

         pgmoneta_workers_wait(workers);
         if (workers != NULL && !workers->outcome)
         {
            goto error;
         }
         pgmoneta_workers_destroy(workers);

         /* Delete from */
         pgmoneta_delete_directory(d);
         free(d);
         d = NULL;

         /* Recalculate to */
         d = pgmoneta_get_server_backup_identifier(
            server, backups[next_index]->label);

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

   free(d);
   free(from);
   free(to);

   return 0;

error:
   if (number_of_workers > 0)
   {
      pgmoneta_workers_destroy(workers);
   }

   free(d);
   free(from);
   free(to);
   return 1;
}
