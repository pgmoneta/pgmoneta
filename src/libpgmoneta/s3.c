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
#include <aes.h>
#include <art.h>
#include <backup.h>
#include <progress.h>
#include <s3.h>
#include <compression.h>
#include <info.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <restore.h>
#include <security.h>
#include <storage.h>
#include <utils.h>
#include <wal.h>
#include <workflow.h>
#include <workflow_funcs.h>

#define NAME "s3"

/*
 * Check the local catalog for a live backup whose parent is @p label.
 *
 * S3 delete is a leaf-only mirror operation: removing a backup that has a
 * dependent incremental child would break the remaining S3 chain.
 * Chain-aware removal stays owned by normal 'delete'.
 *
 * This check reflects the LOCAL catalog only; it is not a comprehensive
 * S3-side chain check. Live incremental chains require local data, so the
 * child scan only applies when the local storage engine is enabled.
 *
 * A child that exists but is not valid (failed backup) does not protect
 * the parent: there is no restorable chain depending on it.
 *
 * @return 1 when a live (valid) child exists and @p child_label holds its
 * label (caller frees), 0 when deletion may proceed, 2 when the label is
 * unknown locally, 3 when the target is retained, -1 when the local
 * catalog could not be read at all.
 */
static int
s3_check_catalog(int server, char* label, char** child_label)
{
   char* d = NULL;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   struct backup* target = NULL;
   int rc = -1;

   *child_label = NULL;

   d = pgmoneta_get_server_backup(server);
   if (d == NULL)
   {
      goto done;
   }

   if (pgmoneta_load_infos(d, &number_of_backups, &backups))
   {
      goto done;
   }

   /* Single catalog load over the in-memory array: confirm the label,
    * then scan for children. No second load. */
   for (int i = 0; i < number_of_backups; i++)
   {
      if (pgmoneta_compare_string(backups[i]->label, label))
      {
         target = backups[i];
         break;
      }
   }

   if (target == NULL)
   {
      /* Unknown locally: also covers partial prefixes, which can never
       * match a full label. */
      rc = 2;
      goto done;
   }

   /* Retained backups are refused, mirroring normal delete. */
   if (target->keep)
   {
      rc = 3;
      goto done;
   }

   /* Live incremental chains require local data: without the local engine
    * there is no chain the catalog could protect. */
   if (!pgmoneta_is_storage_engine_enabled(STORAGE_ENGINE_LOCAL))
   {
      rc = 0;
      goto done;
   }

   rc = 0;
   for (int i = 0; i < number_of_backups; i++)
   {
      if (pgmoneta_compare_string(label, backups[i]->parent_label) &&
          pgmoneta_is_backup_struct_valid(server, backups[i]))
      {
         *child_label = pgmoneta_append(*child_label, backups[i]->label);
         rc = 1;
         break;
      }
   }

done:
   free(d);
   if (backups != NULL)
   {
      for (int i = 0; i < number_of_backups; i++)
      {
         free(backups[i]);
      }
      free(backups);
   }

   return rc;
}

void
pgmoneta_list_s3_objects(int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* elapsed = NULL;
   char* en = NULL;
   char* prefix = NULL;
   int ec = -1;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   struct json* response = NULL;
   struct json* objects_json = NULL;
   struct deque* objects = NULL;
   struct deque_iterator* diter = NULL;
   struct art* nodes = NULL;
   struct workflow* workflow = NULL;
   struct main_configuration* config;
   struct json* request = NULL;

   config = (struct main_configuration*)shmem;
   request = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   prefix = (char*)pgmoneta_json_get(request, MANAGEMENT_ARGUMENT_S3_PREFIX);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   if (pgmoneta_art_create(&nodes))
   {
      ec = MANAGEMENT_ERROR_LIST_S3_WORKFLOW;
      goto error;
   }

   if (pgmoneta_art_insert(nodes, NODE_SERVER_ID, (uintptr_t)server, ValueInt32))
   {
      ec = MANAGEMENT_ERROR_LIST_S3_WORKFLOW;
      goto error;
   }

   if (pgmoneta_art_insert(nodes, NODE_LABEL, (uintptr_t)(prefix != NULL ? prefix : ""), ValueString))
   {
      ec = MANAGEMENT_ERROR_LIST_S3_WORKFLOW;
      goto error;
   }

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_S3_LIST, NULL);

   if (workflow == NULL)
   {
      ec = MANAGEMENT_ERROR_LIST_S3_WORKFLOW;
      pgmoneta_log_error("List S3: S3 storage engine is not configured for %s", config->common.servers[server].name);
      goto error;
   }

   pgmoneta_progress_setup(server, workflow, nodes, WORKFLOW_TYPE_S3_LIST);

   if (pgmoneta_workflow_execute(workflow, nodes, &en, &ec))
   {
      pgmoneta_log_error("List S3: Workflow failed for %s", config->common.servers[server].name);
      goto error;
   }
   if (pgmoneta_is_progress_enabled(server))
   {
      pgmoneta_progress_teardown(server);
   }

   objects = (struct deque*)pgmoneta_art_search(nodes, NODE_S3_OBJECTS);

   if (pgmoneta_management_create_response(payload, server, &response))
   {
      ec = MANAGEMENT_ERROR_ALLOCATION;
      goto error;
   }

   if (pgmoneta_json_create(&objects_json))
   {
      ec = MANAGEMENT_ERROR_LIST_S3_JSON_VALUE;
      goto error;
   }

   if (objects != NULL)
   {
      if (pgmoneta_deque_iterator_create(objects, &diter))
      {
         ec = MANAGEMENT_ERROR_LIST_S3_JSON_VALUE;
         goto error;
      }

      while (pgmoneta_deque_iterator_next(diter))
      {
         struct json* obj = NULL;

         if (pgmoneta_json_create(&obj))
         {
            ec = MANAGEMENT_ERROR_LIST_S3_JSON_VALUE;
            goto error;
         }

         pgmoneta_json_put(obj, MANAGEMENT_ARGUMENT_S3_KEY, (uintptr_t)pgmoneta_value_data(diter->value), ValueString);
         pgmoneta_json_append(objects_json, (uintptr_t)obj, ValueJSON);
      }
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->common.servers[server].name, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_S3_OBJECTS, (uintptr_t)objects_json, ValueJSON);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      ec = MANAGEMENT_ERROR_LIST_S3_NETWORK;
      pgmoneta_log_error("List S3: Error sending response for %s", config->common.servers[server].name);
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);
   pgmoneta_log_info("List S3: %s (Elapsed: %s)", config->common.servers[server].name, elapsed);

   pgmoneta_json_destroy(payload);
   pgmoneta_art_destroy(nodes);
   pgmoneta_workflow_destroy(workflow);
   free(elapsed);

   pgmoneta_disconnect(client_fd);
   pgmoneta_stop_logging();
   exit(0);

error:

   if (pgmoneta_is_progress_enabled(server))
   {
      pgmoneta_progress_teardown(server);
   }
   pgmoneta_management_response_error_with_nodes(NULL, client_fd, config->common.servers[server].name,
                                                 ec != -1 ? ec : MANAGEMENT_ERROR_LIST_S3_ERROR, en != NULL ? en : NAME,
                                                 compression, encryption, payload, nodes);

   pgmoneta_json_destroy(payload);
   pgmoneta_art_destroy(nodes);
   pgmoneta_workflow_destroy(workflow);
   free(elapsed);

   pgmoneta_disconnect(client_fd);
   pgmoneta_stop_logging();
   exit(1);
}

void
pgmoneta_restore_s3_objects(int client_fd, int server, char* prefix, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* elapsed = NULL;
   char* en = NULL;
   int ec = -1;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   char* directory = NULL;
   char* position = NULL;
   char* local_data = NULL;
   struct art* nodes = NULL;
   struct backup* backup = NULL;
   struct json* req = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   position = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_POSITION);
   directory = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_DIRECTORY);

   if (!pgmoneta_is_safe_label(prefix))
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_ERROR;
      pgmoneta_log_error("S3 restore: invalid prefix for %s", config->common.servers[server].name);
      goto error;
   }

   if (directory == NULL || strlen(directory) == 0)
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_ERROR;
      pgmoneta_log_error("S3 restore: missing target directory for %s/%s", config->common.servers[server].name, prefix);
      goto error;
   }

   if (pgmoneta_art_create(&nodes))
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_WORKFLOW;
      goto error;
   }

   if (pgmoneta_art_insert(nodes, NODE_SERVER_ID, (uintptr_t)server, ValueInt32))
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_WORKFLOW;
      goto error;
   }

   if (pgmoneta_art_insert(nodes, NODE_LABEL, (uintptr_t)prefix, ValueString))
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_WORKFLOW;
      goto error;
   }

   if (pgmoneta_art_insert(nodes, USER_IDENTIFIER, (uintptr_t)prefix, ValueString))
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_WORKFLOW;
      goto error;
   }

   if (pgmoneta_storage_remote_download(server, prefix))
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_DOWNLOAD;
      pgmoneta_log_error("S3 restore: failed to stage %s/%s", config->common.servers[server].name, prefix);
      goto error;
   }

   if (pgmoneta_workflow_nodes(server, prefix, nodes, &backup))
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_WORKFLOW;
      pgmoneta_log_error("S3 restore: could not load workflow nodes for %s/%s", config->common.servers[server].name, prefix);
      goto error;
   }

   if (pgmoneta_art_insert(nodes, USER_POSITION, (uintptr_t)position, ValueString))
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_WORKFLOW;
      pgmoneta_log_error("S3 restore: could not add restore position to art");
      goto error;
   }

   if (pgmoneta_art_insert(nodes, USER_DIRECTORY, (uintptr_t)directory, ValueString))
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_WORKFLOW;
      pgmoneta_log_error("S3 restore: could not add target directory to art");
      goto error;
   }

   if (pgmoneta_restore_backup(nodes) != 0)
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_WORKFLOW;
      pgmoneta_log_error("S3 restore: restore workflow failed for %s", config->common.servers[server].name);
      goto error;
   }

   local_data = pgmoneta_get_server_backup_identifier_data(server, prefix);
   pgmoneta_delete_directory(local_data);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      ec = MANAGEMENT_ERROR_RESTORE_S3_NETWORK;
      pgmoneta_log_error("S3 resore: error sending response for %s", config->common.servers[server].name);
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);
   pgmoneta_log_info("S3 restore: %s/%s (Elapsed: %s)", config->common.servers[server].name, prefix, elapsed);

   pgmoneta_json_destroy(payload);
   pgmoneta_art_destroy(nodes);
   free(elapsed);
   free(local_data);

   pgmoneta_disconnect(client_fd);
   pgmoneta_stop_logging();
   exit(0);

error:

   pgmoneta_management_response_error_with_nodes(NULL, client_fd, config->common.servers[server].name,
                                                 ec != -1 ? ec : MANAGEMENT_ERROR_RESTORE_S3_ERROR, en != NULL ? en : NAME,
                                                 compression, encryption, payload, nodes);

   pgmoneta_json_destroy(payload);
   pgmoneta_art_destroy(nodes);
   free(elapsed);

   if (local_data != NULL)
   {
      pgmoneta_delete_directory(local_data);
   }

   pgmoneta_disconnect(client_fd);
   pgmoneta_stop_logging();
   free(local_data);
   exit(1);
}

void
pgmoneta_delete_s3_objects(int client_fd, int server, char* prefix, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* elapsed = NULL;
   char* en = NULL;
   char* child_label = NULL;
   int guard = -1;
   int ec = -1;
   bool locked = false;
   bool active = false;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   if (prefix == NULL || !pgmoneta_is_safe_label(prefix))
   {
      ec = MANAGEMENT_ERROR_DELETE_S3_ERROR;
      pgmoneta_log_error("S3 delete: invalid prefix for %s", config->common.servers[server].name);
      goto error;
   }

   /* Fail early when S3 is not configured, like s3 ls/restore do via a
    * NULL workflow: there is no bucket to talk to. */
   if (!pgmoneta_is_storage_engine_enabled(STORAGE_ENGINE_S3))
   {
      ec = MANAGEMENT_ERROR_DELETE_S3_ERROR;
      pgmoneta_log_error("S3 delete: S3 storage engine is not configured for %s",
                         config->common.servers[server].name);
      goto error;
   }

   /* Serialize against backup/restore/delete/retention on this server,
    * mirroring normal delete. */
   if (!atomic_compare_exchange_strong(&config->common.servers[server].repository, &active, true))
   {
      ec = MANAGEMENT_ERROR_DELETE_BACKUP_ACTIVE;
      pgmoneta_log_info("S3 delete: Server %s is active", config->common.servers[server].name);
      goto error;
   }

   locked = true;
   config->common.servers[server].active_delete = true;

   {
      /* Catalog verdict, verified against the local catalog only. Live
       * incremental chains require local data. An unknown label (also
       * covering partial prefixes, which can never match a full label)
       * is refused, as are retained backups, mirroring normal delete. */
      guard = s3_check_catalog(server, prefix, &child_label);
      if (guard == 1)
      {
         ec = MANAGEMENT_ERROR_DELETE_S3_HAS_CHILD;
         pgmoneta_log_error("S3 delete: refused for %s/%s: has dependent incremental child %s (use 'delete' for chain-aware removal)",
                            config->common.servers[server].name,
                            prefix,
                            child_label != NULL ? child_label : "unknown");
         free(child_label);
         goto error;
      }
      else if (guard == 2)
      {
         ec = MANAGEMENT_ERROR_DELETE_S3_NOBACKUP;
         pgmoneta_log_error("S3 delete: no such backup %s/%s",
                            config->common.servers[server].name,
                            prefix);
         goto error;
      }
      else if (guard == 3)
      {
         ec = MANAGEMENT_ERROR_DELETE_BACKUP_RETAINED;
         pgmoneta_log_error("S3 delete: backup is retained %s/%s",
                            config->common.servers[server].name,
                            prefix);
         goto error;
      }
      else if (guard < 0)
      {
         ec = MANAGEMENT_ERROR_DELETE_S3_UNVERIFIED;
         pgmoneta_log_error("S3 delete: refused for %s/%s: local catalog unreadable, cannot verify incremental chain",
                            config->common.servers[server].name,
                            prefix);
         goto error;
      }
      else if (pgmoneta_s3_cleanup(server, prefix))
      {
         ec = MANAGEMENT_ERROR_DELETE_S3_ERROR;
         pgmoneta_log_error("S3 delete: failed for %s/%s",
                            config->common.servers[server].name,
                            prefix);
         goto error;
      }
      free(child_label);
   }

   /* Exclusive S3 owns no local copy: drop the leftover local metadata too,
    * otherwise 'list' keeps showing the deleted backup and 'verify' fails on
    * the missing data. The equality check is deliberate: configurations such
    * as s3,azure still need the local metadata for the other backend.
    * A half-completed deletion (S3 gone, metadata left) is reported as an
    * error, not success. */
   if (config->storage_engine == STORAGE_ENGINE_S3)
   {
      char* local_id = pgmoneta_get_server_backup_identifier(server, prefix);

      if (local_id != NULL)
      {
         if (pgmoneta_exists(local_id) && pgmoneta_delete_directory(local_id))
         {
            ec = MANAGEMENT_ERROR_DELETE_S3_ERROR;
            pgmoneta_log_error("S3 delete: removed S3 objects for %s/%s but could not remove local metadata %s",
                               config->common.servers[server].name,
                               prefix,
                               local_id);
            free(local_id);
            goto error;
         }
         free(local_id);
      }
   }

   /* The operation is fully complete: release the lock before
    * answering. */
   config->common.servers[server].active_delete = false;
   atomic_store(&config->common.servers[server].repository, false);
   locked = false;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      ec = MANAGEMENT_ERROR_DELETE_S3_NETWORK;
      pgmoneta_log_error("S3 delete: error sending response for %s",
                         config->common.servers[server].name);
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("S3 delete: %s/%s (Elapsed: %s)",
                     config->common.servers[server].name,
                     prefix,
                     elapsed);

   pgmoneta_json_destroy(payload);
   free(elapsed);

   pgmoneta_disconnect(client_fd);
   pgmoneta_stop_logging();
   exit(0);

error:

   if (locked)
   {
      config->common.servers[server].active_delete = false;
      atomic_store(&config->common.servers[server].repository, false);
   }

   pgmoneta_management_response_error(
      NULL,
      client_fd,
      config->common.servers[server].name,
      ec != -1 ? ec : MANAGEMENT_ERROR_DELETE_S3_ERROR,
      en != NULL ? en : NAME,
      compression,
      encryption,
      payload);

   pgmoneta_json_destroy(payload);
   free(elapsed);

   pgmoneta_disconnect(client_fd);
   pgmoneta_stop_logging();
   exit(1);
}
