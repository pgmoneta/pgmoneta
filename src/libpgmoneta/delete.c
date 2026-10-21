/*
 * Copyright (C) 2026 The pgmoneta community
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
#include <delete.h>
#include <deque.h>
#include <job.h>
#include <logging.h>
#include <management.h>
#include <progress.h>
#include <storage.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * Delete wal files older than the given srv_wal file under the base directory
 * Base directory could be the wal/ or the wal_shipping directory
 * @param srv The server
 * @param srv_wal The oldest wal segment file we would like to keep
 * @param base The base directory holding the wal segments
 */
static void
delete_wal_older_than(int srv, char* srv_wal, char* base);

/**
 * Delete a backup that is marked as incomplete, locally and on the remote
 * storage engines. The marker stays until the remote cleanup succeeds
 * @param srv The server index
 * @param label The backup label
 * @return 0 upon success, otherwise 1
 */
static int
delete_incomplete_backup(int srv, char* label);

/**
 * Delete the backup directories of a server that have no backup.info
 * @param srv The server index
 * @return 0 upon success, otherwise 1
 */
static int
delete_backups_without_metadata(int srv);

int
pgmoneta_delete(int srv, char* label)
{
   bool active = false;
   bool locked = false;
   int ec = -1;
   char* en = NULL;
   struct workflow* workflow = NULL;
   struct art* nodes = NULL;
   struct backup* backup = NULL; /* owned by nodes */
   struct main_configuration* config = (struct main_configuration*)shmem;

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_DELETE_BACKUP, NULL);

   if (pgmoneta_art_create(&nodes))
   {
      goto error;
   }

   if (pgmoneta_workflow_nodes(srv, label, nodes, &backup))
   {
      goto error;
   }

   if (!atomic_compare_exchange_strong(&config->common.servers[srv].repository, &active, true))
   {
      ec = MANAGEMENT_ERROR_DELETE_BACKUP_ACTIVE;
      pgmoneta_log_info("Delete: Server %s is active", config->common.servers[srv].name);
      pgmoneta_log_debug("Backup=%s, Restore=%s, Archive=%s, Delete=%s, Retention=%s",
                         config->common.servers[srv].active_backup ? "Yes" : "No",
                         config->common.servers[srv].active_restore ? "Yes" : "No",
                         config->common.servers[srv].active_archive ? "Yes" : "No",
                         config->common.servers[srv].active_delete ? "Yes" : "No",
                         config->common.servers[srv].active_retention ? "Yes" : "No");
      goto error;
   }
   config->common.servers[srv].active_delete = true;
   locked = true;
   config->common.servers[srv].repository_pid = getpid();

   if (pgmoneta_workflow_execute(workflow, nodes, &en, &ec))
   {
      goto error;
   }

   config->common.servers[srv].active_delete = false;
   config->common.servers[srv].repository_pid = 0;
   atomic_store(&config->common.servers[srv].repository, false);

   pgmoneta_art_destroy(nodes);

   pgmoneta_workflow_destroy(workflow);

   return 0;

error:

   pgmoneta_log_error("Delete: %s (%d)", en, ec);

   if (locked)
   {
      config->common.servers[srv].active_delete = false;
      config->common.servers[srv].repository_pid = 0;
      atomic_store(&config->common.servers[srv].repository, false);
   }

   pgmoneta_art_destroy(nodes);

   pgmoneta_workflow_destroy(workflow);

   return 1;
}

int
pgmoneta_delete_wal(int srv)
{
   char* d = NULL;
   struct backup* backup = NULL;
   char* srv_wal = NULL;
   char* wal_shipping = NULL;
   struct deque* srv_wal_files = NULL;

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

      srv_wal_files = NULL;

      pgmoneta_get_wal_files(d, &srv_wal_files);

      if (pgmoneta_deque_size(srv_wal_files) > 0)
      {
         srv_wal = (char*)pgmoneta_deque_peek(srv_wal_files, NULL);
      }

      free(d);
      d = NULL;
   }

   /* Delete WAL if there are no backups, or the oldest one is valid */
   if (backup == NULL)
   {
      d = pgmoneta_get_server_wal(srv);
      delete_wal_older_than(srv, srv_wal, d);
      free(d);
      d = NULL;

      /* Also delete WAL under wal_shipping directory */
      wal_shipping = pgmoneta_get_server_wal_shipping_wal(srv);
      if (wal_shipping != NULL)
      {
         delete_wal_older_than(srv, srv_wal, wal_shipping);
      }

      free(wal_shipping);
      wal_shipping = NULL;
   }

   free(backup);

   pgmoneta_deque_destroy(srv_wal_files);

   return 0;

error:

   free(d);
   free(wal_shipping);

   free(backup);

   pgmoneta_deque_destroy(srv_wal_files);

   return 1;
}

int
pgmoneta_delete_incomplete_backups(int srv, bool startup)
{
   char* d = NULL;
   int ret = 0;
   struct deque* files = NULL;
   struct deque_iterator* iter = NULL;

   d = pgmoneta_get_server_backup(srv);

   if (d == NULL)
   {
      goto error;
   }

   if (!pgmoneta_exists(d))
   {
      free(d);
      return 0;
   }

   if (pgmoneta_get_files(PGMONETA_FILE_TYPE_ALL, d, false, &files) || pgmoneta_deque_iterator_create(files, &iter))
   {
      goto error;
   }

   while (pgmoneta_deque_iterator_next(iter))
   {
      char* name = (char*)pgmoneta_value_data(iter->value);
      char* label = NULL;

      if (!pgmoneta_ends_with(name, BACKUP_INCOMPLETE_SUFFIX))
      {
         continue;
      }

      label = pgmoneta_format_and_append(NULL, "%.*s", (int)(strlen(name) - strlen(BACKUP_INCOMPLETE_SUFFIX)), name);

      if (label == NULL || delete_incomplete_backup(srv, label))
      {
         ret = 1;
      }

      free(label);
   }

   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_deque_destroy(files);
   free(d);

   if (startup && delete_backups_without_metadata(srv))
   {
      ret = 1;
   }

   return ret;

error:

   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_deque_destroy(files);
   free(d);

   return 1;
}

static int
delete_backups_without_metadata(int srv)
{
   char* d = NULL;
   char* root = NULL;
   char* info = NULL;
   int number_of_directories = 0;
   char** dirs = NULL;
   int ret = 0;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   d = pgmoneta_get_server_backup(srv);

   if (d == NULL)
   {
      goto error;
   }

   pgmoneta_get_directories(d, &number_of_directories, &dirs);

   for (int i = 0; i < number_of_directories; i++)
   {
      root = pgmoneta_get_server_backup_identifier(srv, dirs[i]);

      if (root == NULL)
      {
         goto error;
      }

      info = pgmoneta_format_and_append(NULL, "%sbackup.info", root);

      if (info == NULL)
      {
         goto error;
      }

      if (!pgmoneta_exists(info))
      {
         pgmoneta_log_warn("Deleting backup directory %s/%s without backup.info", config->common.servers[srv].name, dirs[i]);

         if (pgmoneta_delete_directory(root))
         {
            pgmoneta_log_error("Could not delete %s", root);
            ret = 1;
         }
      }

      free(info);
      info = NULL;

      free(root);
      root = NULL;
   }

   for (int i = 0; i < number_of_directories; i++)
   {
      free(dirs[i]);
   }
   free(dirs);

   free(d);

   return ret;

error:

   free(info);
   free(root);

   for (int i = 0; i < number_of_directories; i++)
   {
      free(dirs[i]);
   }
   free(dirs);

   free(d);

   return 1;
}

void
pgmoneta_delete_failed_backup(char** argv, int srv, pid_t pid)
{
   int ret = 0;
   struct main_configuration* config;

   pgmoneta_start_logging();

   config = (struct main_configuration*)shmem;

   pgmoneta_set_proc_title(1, argv, "cleanup", config->common.servers[srv].name);

   /* An async backup that was killed never got to save its job */
   if (pgmoneta_job_fail_dead_owner(srv, pid, MANAGEMENT_ERROR_BACKUP_ERROR, WORKFLOW_NAME_BACKUP))
   {
      ret = 1;
   }

   if (pgmoneta_delete_incomplete_backups(srv, false))
   {
      ret = 1;
   }

   if (pgmoneta_is_progress_enabled(srv))
   {
      pgmoneta_progress_teardown(srv);
   }

   config->common.servers[srv].active_delete = false;
   config->common.servers[srv].repository_pid = 0;
   atomic_store(&config->common.servers[srv].repository, false);

   pgmoneta_stop_logging();

   exit(ret);
}

static int
delete_incomplete_backup(int srv, char* label)
{
   char* d = NULL;
   char* marker = NULL;
   char* root = NULL;
   char* info = NULL;
   FILE* file = NULL;
   int ret = 0;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (!pgmoneta_is_safe_label(label))
   {
      pgmoneta_log_error("Invalid incomplete backup %s", label);
      goto error;
   }

   d = pgmoneta_get_server_backup(srv);
   root = pgmoneta_get_server_backup_identifier(srv, label);

   if (d == NULL || root == NULL)
   {
      goto error;
   }

   marker = pgmoneta_format_and_append(NULL, "%s%s%s", d, label, BACKUP_INCOMPLETE_SUFFIX);
   info = pgmoneta_format_and_append(NULL, "%sbackup.info", root);

   if (marker == NULL || info == NULL)
   {
      goto error;
   }

   pgmoneta_log_warn("Deleting incomplete backup %s/%s", config->common.servers[srv].name, label);

   if (pgmoneta_exists(info))
   {
      /* The upload to the remote storage engines only starts after backup.info
       * is written, so the marker keeps that once backup.info is gone */
      if (pgmoneta_fopen_secure(marker, "w", &file) || fputs("remote\n", file) == EOF ||
          fflush(file) != 0 || fsync(fileno(file)) != 0)
      {
         pgmoneta_log_error("Could not write %s: %s", marker, strerror(errno));
         goto error;
      }
      fclose(file);
      file = NULL;

      /* backup.info goes first, so a partly deleted backup is never taken for a complete one */
      pgmoneta_delete_file(info, NULL);
   }

   if (pgmoneta_get_file_size(marker) > 0 && pgmoneta_storage_remote_cleanup(srv, label))
   {
      pgmoneta_log_warn("Keeping the marker of %s/%s to retry the remote cleanup", config->common.servers[srv].name, label);
      ret = 1;
   }

   if (pgmoneta_exists(root) && pgmoneta_delete_directory(root))
   {
      pgmoneta_log_error("Could not delete %s", root);
      goto error;
   }

   if (ret == 0)
   {
      pgmoneta_delete_file(marker, NULL);
   }

   free(info);
   free(marker);
   free(root);
   free(d);

   return ret;

error:

   if (file != NULL)
   {
      fclose(file);
   }

   free(info);
   free(marker);
   free(root);
   free(d);

   return 1;
}

static void
delete_wal_older_than(int srv, char* srv_wal, char* base)
{
   struct deque* wal_files = NULL;
   struct deque_iterator* iter = NULL;
   char wal_address[MAX_PATH] = {0};
   bool delete = false;
   bool active = false;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (atomic_compare_exchange_strong(&config->common.servers[srv].wal_repository, &active, true))
   {
      if (pgmoneta_get_wal_files(base, &wal_files))
      {
         pgmoneta_log_warn("Unable to get WAL segments under %s", base);
         atomic_store(&config->common.servers[srv].wal_repository, false);
         goto error;
      }

      pgmoneta_deque_iterator_create(wal_files, &iter);
      while (pgmoneta_deque_iterator_next(iter))
      {
         char* file = (char*)iter->value->data;
         delete = false;

         if (srv_wal != NULL)
         {
            if (strcmp(file, srv_wal) < 0)
            {
               delete = true;
            }
         }
         else
         {
            delete = true;
         }

         if (delete)
         {
            memset(wal_address, 0, MAX_PATH);
            if (pgmoneta_ends_with(base, "/"))
            {
               pgmoneta_snprintf(wal_address, MAX_PATH, "%s%s", base, file);
            }
            else
            {
               pgmoneta_snprintf(wal_address, MAX_PATH, "%s/%s", base, file);
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

      atomic_store(&config->common.servers[srv].wal_repository, false);
   }
   else
   {
      pgmoneta_log_debug("WAL: Did not get WAL repository lock for server %s", config->common.servers[srv].name);
      goto error;
   }

   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_deque_destroy(wal_files);

   return;

error:

   pgmoneta_deque_destroy(wal_files);
   pgmoneta_deque_iterator_destroy(iter);
}
