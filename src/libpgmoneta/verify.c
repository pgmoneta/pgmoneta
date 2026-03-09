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
#include <extension.h>
#include <extraction.h>
#include <info.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <restore.h>
#include <security.h>
#include <utils.h>
#include <wal.h>
#include <workflow.h>

/* system */
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#define NAME "verify"

/**
 * Structure to hold parsed recovery target information
 */
struct recovery_target
{
   char* target_lsn;    /**< Recovery target LSN (if specified) */
   char* target_xid;    /**< Recovery target XID (if specified) */
   char* target_time;   /**< Recovery target timestamp (if specified) */
   char* target_name;   /**< Recovery target name (if specified) */
   uint32_t target_tli; /**< Recovery target timeline (if specified) */
   bool inclusive;      /**< Whether recovery target is inclusive */
};

/**
 * Validation functions for restore integrity checks
 */
static int validate_backup_chain_integrity(int server, struct backup* backup);

static int validate_wal_continuity(int server, struct backup* backup, struct art* nodes);

static int validate_timeline_consistency(int server, struct deque* labels, struct art* nodes);

static int validate_postgresql_version(int server, struct backup* backup);

static int validate_backup_file_accessibility(int server, struct backup* backup);

/**
 * Helper functions for WAL validation
 */
static int parse_recovery_target(char* position, struct recovery_target* target);

static void free_recovery_target(struct recovery_target* target);

static int collect_available_wal_files(int server, struct backup* backup, struct deque** wal_files);

static int validate_wal_sequence_continuity(int server, struct deque* wal_files, char* start_wal);

void
pgmoneta_verify(SSL* ssl, int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* identifier = NULL;
   char* directory = NULL;
   char* real_directory = NULL;
   char* files = NULL;
   char* elapsed = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   char* label = NULL;
   struct backup* backup = NULL;
   struct workflow* workflow = NULL;
   struct workflow* current = NULL;
   struct art* nodes = NULL;
   struct deque* f = NULL;
   struct deque* a = NULL;
   struct deque_iterator* fiter = NULL;
   struct deque_iterator* aiter = NULL;
   struct json* failed = NULL;
   struct json* all = NULL;
   struct json* req = NULL;
   struct json* response = NULL;
   struct json* filesj = NULL;
   struct main_configuration* config;

   pgmoneta_start_logging();

   config = (struct main_configuration*)shmem;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   identifier = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_BACKUP);
   directory = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_DIRECTORY);
   files = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_FILES);

   if (pgmoneta_art_create(&nodes))
   {
      goto error;
   }

   if (pgmoneta_art_insert(nodes, USER_POSITION, (uintptr_t)"", ValueString))
   {
      goto error;
   }

   if (pgmoneta_art_insert(nodes, USER_DIRECTORY, (uintptr_t)directory, ValueString))
   {
      goto error;
   }

   if (files == NULL || strlen(files) == 0)
   {
      files = MANAGEMENT_ARGUMENT_FAILED;
   }

   if (pgmoneta_art_insert(nodes, USER_FILES, (uintptr_t)files, ValueString))
   {
      goto error;
   }

   if (pgmoneta_art_insert(nodes, NODE_TARGET_ROOT, (uintptr_t)directory, ValueString))
   {
      goto error;
   }

   if (pgmoneta_workflow_nodes(server, identifier, nodes, &backup))
   {
      goto error;
   }

   real_directory = pgmoneta_append(real_directory, directory);
   if (!pgmoneta_ends_with(real_directory, "/"))
   {
      real_directory = pgmoneta_append_char(real_directory, '/');
   }
   real_directory = pgmoneta_append(real_directory, config->common.servers[server].name);
   real_directory = pgmoneta_append_char(real_directory, '-');
   real_directory = pgmoneta_append(real_directory, backup->label);

   if (pgmoneta_exists(real_directory))
   {
      pgmoneta_delete_directory(real_directory);
   }

   pgmoneta_mkdir(real_directory);

   if (pgmoneta_art_insert(nodes, NODE_TARGET_BASE, (uintptr_t)real_directory, ValueString))
   {
      goto error;
   }

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_VERIFY, backup);

   current = workflow;
   while (current != NULL)
   {
      if (current->setup(current->name(), nodes))
      {
         goto error;
      }
      current = current->next;
   }

   current = workflow;
   while (current != NULL)
   {
      if (current->execute(current->name(), nodes))
      {
         goto error;
      }
      current = current->next;
   }

   current = workflow;
   while (current != NULL)
   {
      if (current->teardown(current->name(), nodes))
      {
         goto error;
      }
      current = current->next;
   }

   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   f = (struct deque*)pgmoneta_art_search(nodes, NODE_FAILED);
   a = (struct deque*)pgmoneta_art_search(nodes, NODE_ALL);

   if (pgmoneta_json_create(&failed))
   {
      goto error;
   }

   if (pgmoneta_deque_iterator_create(f, &fiter))
   {
      goto error;
   }

   while (pgmoneta_deque_iterator_next(fiter))
   {
      struct json* j = NULL;

      if (pgmoneta_json_clone((struct json*)pgmoneta_value_data(fiter->value), &j))
      {
         goto error;
      }

      pgmoneta_json_append(failed, (uintptr_t)j, ValueJSON);
   }

   if (files != NULL && !strcasecmp(files, "all"))
   {
      pgmoneta_json_create(&all);

      if (pgmoneta_deque_iterator_create(a, &aiter))
      {
         goto error;
      }

      while (pgmoneta_deque_iterator_next(aiter))
      {
         struct json* j = NULL;

         if (pgmoneta_json_clone((struct json*)pgmoneta_value_data(aiter->value), &j))
         {
            goto error;
         }

         pgmoneta_json_append(all, (uintptr_t)j, ValueJSON);
      }
   }

   if (pgmoneta_management_create_response(payload, server, &response))
   {
      pgmoneta_management_response_error(ssl, client_fd, config->common.servers[server].name, MANAGEMENT_ERROR_ALLOCATION, NAME, compression, encryption, payload);

      goto error;
   }

   if (pgmoneta_json_create(&filesj))
   {
      pgmoneta_management_response_error(ssl, client_fd, config->common.servers[server].name, MANAGEMENT_ERROR_ALLOCATION, NAME, compression, encryption, payload);

      goto error;
   }

   pgmoneta_json_put(filesj, MANAGEMENT_ARGUMENT_FAILED, (uintptr_t)failed, ValueJSON);
   pgmoneta_json_put(filesj, MANAGEMENT_ARGUMENT_ALL, (uintptr_t)all, ValueJSON);

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)label, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->common.servers[server].name, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_FILES, (uintptr_t)filesj, ValueJSON);

   pgmoneta_delete_directory((char*)pgmoneta_art_search(nodes, NODE_TARGET_BASE));

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(ssl, client_fd, start_t, end_t, compression, encryption, payload))
   {
      pgmoneta_management_response_error(ssl, client_fd, config->common.servers[server].name, MANAGEMENT_ERROR_VERIFY_NETWORK, NAME, compression, encryption, payload);
      pgmoneta_log_error("Verify: Error sending response for %s/%s", config->common.servers[server].name, identifier);

      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("Verify: %s/%s (Elapsed: %s)", config->common.servers[server].name, label, elapsed);

   pgmoneta_deque_iterator_destroy(fiter);
   pgmoneta_deque_iterator_destroy(aiter);

   pgmoneta_art_destroy(nodes);

   pgmoneta_json_destroy(payload);

   pgmoneta_workflow_destroy(workflow);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   free(real_directory);
   free(elapsed);

   exit(0);

error:

   pgmoneta_delete_directory((char*)pgmoneta_art_search(nodes, NODE_TARGET_BASE));

   pgmoneta_deque_iterator_destroy(fiter);
   pgmoneta_deque_iterator_destroy(aiter);

   pgmoneta_art_destroy(nodes);

   pgmoneta_json_destroy(payload);

   pgmoneta_workflow_destroy(workflow);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   free(real_directory);
   free(elapsed);

   exit(1);
}

void
pgmoneta_sha512_verification(char** argv)
{
   int server = 0;
   struct main_configuration* config;
   char* backup_dir = NULL;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   char* sha512_path = NULL;
   FILE* sha512_file = NULL;
   char buffer[4096];
   char* root = NULL;
   char* filename = NULL;
   char* hash = NULL;
   char* calculated_hash = NULL;
   char* absolute_file_path = NULL;
   bool active = false;
   bool locked = false;
   int line = 0;
   int err = 0;
   char* elapsed = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;

   pgmoneta_start_logging();

   config = (struct main_configuration*)shmem;

   pgmoneta_set_proc_title(1, argv, "verification", NULL);

   for (server = 0; server < config->common.number_of_servers; server++)
   {
      if (!config->common.servers[server].online)
      {
         pgmoneta_log_debug("Verification: Server %s is offline", config->common.servers[server].name);
         continue;
      }

      active = false;
      if (!atomic_compare_exchange_strong(&config->common.servers[server].repository, &active, true))
      {
         pgmoneta_log_info("Verification: Server %s is already active, skipping verification", config->common.servers[server].name);
         continue;
      }

      pgmoneta_log_debug("Verification: Starting for server %s", config->common.servers[server].name);

      locked = true;

      backup_dir = pgmoneta_get_server_backup(server);

      if (pgmoneta_load_infos(backup_dir, &number_of_backups, &backups))
      {
         pgmoneta_log_error("Verification: %s: Unable to get backups", config->common.servers[server].name);
         err = 1;
         goto server_cleanup;
      }

      for (int i = 0; i < number_of_backups; i++)
      {
         bool success = true;

#ifdef HAVE_FREEBSD
         clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
         clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

         if (!pgmoneta_is_backup_struct_valid(server, backups[i]))
         {
            err = 1;
            success = false;
            continue;
         }
         root = pgmoneta_get_server_backup_identifier(server, backups[i]->label);

         sha512_path = pgmoneta_append(sha512_path, root);
         if (!pgmoneta_ends_with(sha512_path, "/"))
         {
            sha512_path = pgmoneta_append_char(sha512_path, '/');
         }
         sha512_path = pgmoneta_append(sha512_path, "backup.sha512");

         sha512_file = fopen(sha512_path, "r");
         if (sha512_file == NULL)
         {
            pgmoneta_log_error("Verification: %s / Could not open file %s: %s",
                               config->common.servers[server].name, sha512_path,
                               strerror(errno));
            err = 1;
            success = false;
            goto backup_cleanup;
         }

         line = 0;
         while (fgets(&buffer[0], sizeof(buffer), sha512_file) != NULL)
         {
            char* entry = NULL;

            line++;
            entry = strtok(&buffer[0], " ");
            if (entry == NULL)
            {
               pgmoneta_log_error("Verification: %s / %s: formatting error at line %d",
                                  config->common.servers[server].name, sha512_path, sha512_path,
                                  line);
               err = 1;
               success = false;
               goto cleanup;
            }

            hash = strdup(entry);
            if (hash == NULL)
            {
               pgmoneta_log_error("Verification: %s / Memory allocation error for hash",
                                  config->common.servers[server].name);
               err = 1;
               success = false;
               goto cleanup;
            }

            entry = strtok(NULL, "\n");
            if (entry == NULL || strlen(entry) < 3)
            {
               pgmoneta_log_error("Verification: %s/%s %s formatting error at line %d",
                                  config->common.servers[server].name,
                                  backups[i]->label, sha512_path, line);
               err = 1;
               success = false;
               goto cleanup;
            }

            // skip the " *." or " */"
            filename = entry + 3;

            absolute_file_path = pgmoneta_append(absolute_file_path, root);
            if (!pgmoneta_ends_with(absolute_file_path, "/"))
            {
               absolute_file_path = pgmoneta_append(absolute_file_path, "/");
            }

            absolute_file_path = pgmoneta_append(absolute_file_path, filename);

            if (pgmoneta_create_sha512_file(absolute_file_path, &calculated_hash))
            {
               pgmoneta_log_error("Verification: %s / Could not create hash for %s",
                                  config->common.servers[server].name, absolute_file_path);
               err = 1;
               success = false;
               goto cleanup;
            }

            if (strcmp(hash, calculated_hash) != 0)
            {
               pgmoneta_log_error("Verification: %s / Hash mismatch for %s | Expected: %s | Got: %s",
                                  config->common.servers[server].name,
                                  absolute_file_path, hash, calculated_hash);
               err = 1;
            }

cleanup:
            free(hash);
            hash = NULL;

            free(absolute_file_path);
            absolute_file_path = NULL;

            free(calculated_hash);
            calculated_hash = NULL;
         }

#ifdef HAVE_FREEBSD
         clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
         clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

         elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);
         if (success)
         {
            pgmoneta_log_info("Verification: %s/%s (Elapsed: %s)", config->common.servers[server].name, backups[i]->label, elapsed);
         }
         else
         {
            char* bck_dir = NULL;

            pgmoneta_log_info("Update .info");

            backups[i]->valid = VALID_FALSE;

            bck_dir = pgmoneta_get_server_backup(server);

            pgmoneta_save_info(bck_dir, backups[i]);

            free(bck_dir);
         }
         free(elapsed);

backup_cleanup:
         if (sha512_file != NULL)
         {
            fclose(sha512_file);
            sha512_file = NULL;
         }
         free(sha512_path);
         sha512_path = NULL;

         free(root);
         root = NULL;
      }

server_cleanup:
      for (int i = 0; i < number_of_backups; i++)
      {
         if (backups[i])
         {
            free(backups[i]);
         }
      }
      free(backups);
      backups = NULL;

      free(backup_dir);
      backup_dir = NULL;

      if (locked)
      {
         atomic_store(&config->common.servers[server].repository, false);
         locked = false;
      }
   }

   pgmoneta_stop_logging();
   exit(err);
}

/**
 * Main restore validation function that orchestrates all integrity checks
 * @param server The server
 * @param backup The backup
 * @param labels The backup labels (for incremental chain)
 * @param nodes The nodes containing restore context
 * @return 0 on success, restore error code if validation fails
 */
int
pgmoneta_validate_restore(int server, struct backup* backup, struct deque* labels, struct art* nodes)
{
   int ret = RESTORE_OK;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (backup == NULL)
   {
      pgmoneta_log_error("Validate restore: backup is NULL");
      return RESTORE_ERROR;
   }

   pgmoneta_log_debug("Validate restore: Starting integrity checks for %s/%s",
                      config->common.servers[server].name, backup->label);

   ret = validate_backup_chain_integrity(server, backup);
   if (ret != RESTORE_OK)
   {
      pgmoneta_log_error("Validate restore: Backup chain integrity check failed");
      goto done;
   }

   ret = validate_wal_continuity(server, backup, nodes);
   if (ret != RESTORE_OK)
   {
      pgmoneta_log_error("Validate restore: WAL continuity check failed");
      goto done;
   }

   ret = validate_timeline_consistency(server, labels, nodes);
   if (ret != RESTORE_OK)
   {
      pgmoneta_log_error("Validate restore: Timeline consistency check failed");
      goto done;
   }

   ret = validate_postgresql_version(server, backup);
   if (ret != RESTORE_OK)
   {
      pgmoneta_log_error("Validate restore: PostgreSQL version compatibility check failed");
      goto done;
   }

   ret = validate_backup_file_accessibility(server, backup);
   if (ret != RESTORE_OK)
   {
      pgmoneta_log_error("Validate restore: Backup file accessibility check failed");
      goto done;
   }

   pgmoneta_log_debug("Validate restore: All integrity checks passed for %s/%s",
                      config->common.servers[server].name, backup->label);

   goto done;
done:
   return ret;
}

/**
 * Validate backup chain integrity (full + incrementals exist and are linked)
 * 
 * Checks:
 * - For incremental backups: verify all parent backups exist and are marked valid
 * - Verify backup chain is not broken
 * - Check that all backups in chain have valid SHA512 checksums
 * 
 * @param server The server
 * @param backup The backup to validate
 * @return 0 on success, RESTORE_CHAIN_INVALID if validation fails
 */
static int
validate_backup_chain_integrity(int server, struct backup* backup)
{
   struct backup* current = NULL;
   struct backup* parent = NULL;
   char* backup_dir = NULL;
   bool current_is_input = true;
   int depth = 0;

   if (backup == NULL)
   {
      return RESTORE_CHAIN_INVALID;
   }

   current = backup;

   while (current != NULL)
   {
      if (!pgmoneta_is_backup_struct_valid(server, current))
      {
         pgmoneta_log_error("Validate backup chain: invalid backup %s", current->label);
         goto error;
      }

      backup_dir = pgmoneta_get_server_backup_identifier(server, current->label);
      if (backup_dir == NULL || !pgmoneta_exists(backup_dir))
      {
         pgmoneta_log_error("Validate backup chain: missing backup directory for %s", current->label);
         goto error;
      }
      free(backup_dir);
      backup_dir = NULL;

      if (current->type == TYPE_FULL)
      {
         break;
      }

      if (strlen(current->parent_label) == 0)
      {
         pgmoneta_log_error("Validate backup chain: missing parent for %s", current->label);
         goto error;
      }

      depth++;
      if (depth > 1024)
      {
         pgmoneta_log_error("Validate backup chain: excessive chain depth for %s", current->label);
         goto error;
      }

      if (pgmoneta_get_backup_parent(server, current, &parent) || parent == NULL)
      {
         pgmoneta_log_error("Validate backup chain: unable to load parent for %s", current->label);
         goto error;
      }

      if (!strcmp(parent->label, current->label))
      {
         pgmoneta_log_error("Validate backup chain: parent label loops on %s", current->label);
         goto error;
      }

      if (!current_is_input)
      {
         free(current);
      }

      current = parent;
      parent = NULL;
      current_is_input = false;
   }

   if (!current_is_input && current != NULL)
   {
      free(current);
   }

   return RESTORE_OK;

error:
   if (!current_is_input && current != NULL)
   {
      free(current);
   }
   if (parent != NULL)
   {
      free(parent);
   }
   free(backup_dir);
   return RESTORE_CHAIN_INVALID;
}

/**
 * Validate WAL availability and continuity for requested recovery target
 * 
 * Checks:
 * - Verify WAL files are available from backup start LSN
 * - Check WAL continuity (no gaps in WAL segments)
 * - Verify timeline matches backup timeline
 * 
 * @param server The server
 * @param backup The backup
 * @param nodes The nodes containing recovery target info
 * @return 0 on success, RESTORE_WAL_INCOMPLETE if validation fails
 */
static int
validate_wal_continuity(int server, struct backup* backup, struct art* nodes)
{
   bool copy_wal = false;
   struct deque* wal_files = NULL;
   char* position = NULL;
   struct recovery_target target = {0};
   int ret = RESTORE_OK;

   if (backup == NULL)
   {
      return RESTORE_WAL_INCOMPLETE;
   }

   // Check if WAL copying is requested
   if (pgmoneta_art_contains_key(nodes, NODE_COPY_WAL))
   {
      copy_wal = (bool)pgmoneta_art_search(nodes, NODE_COPY_WAL);
   }

   if (!copy_wal)
   {
      pgmoneta_log_debug("Validate WAL continuity: WAL copying not requested, skipping validation");
      return RESTORE_OK;
   }

   if (strlen(backup->wal) == 0)
   {
      pgmoneta_log_error("Validate WAL continuity: backup %s missing start WAL segment", backup->label);
      return RESTORE_WAL_INCOMPLETE;
   }

   pgmoneta_log_debug("Validate WAL continuity: starting validation for backup %s, start WAL: %s",
                      backup->label, backup->wal);

   // Parse recovery target parameters if specified
   position = (char*)pgmoneta_art_search(nodes, USER_POSITION);
   if (position != NULL && strlen(position) > 0)
   {
      if (parse_recovery_target(position, &target) == 0)
      {
         if (target.target_lsn != NULL)
         {
            pgmoneta_log_debug("Validate WAL continuity: recovery target LSN = %s", target.target_lsn);
         }
         if (target.target_xid != NULL)
         {
            pgmoneta_log_debug("Validate WAL continuity: recovery target XID = %s", target.target_xid);
         }
         if (target.target_time != NULL)
         {
            pgmoneta_log_debug("Validate WAL continuity: recovery target time = %s", target.target_time);
         }
         if (target.target_name != NULL)
         {
            pgmoneta_log_debug("Validate WAL continuity: recovery target name = %s", target.target_name);
         }
         if (target.target_tli > 0)
         {
            pgmoneta_log_debug("Validate WAL continuity: recovery target timeline = %u", target.target_tli);
         }
      }
      else
      {
         pgmoneta_log_warn("Validate WAL continuity: failed to parse recovery target position");
      }
   }

   // Collect all available WAL files from backup and archive directories
   if (collect_available_wal_files(server, backup, &wal_files))
   {
      pgmoneta_log_error("Validate WAL continuity: failed to collect WAL files");
      ret = RESTORE_WAL_INCOMPLETE;
      goto cleanup;
   }

   if (wal_files == NULL || pgmoneta_deque_size(wal_files) == 0)
   {
      pgmoneta_log_error("Validate WAL continuity: no WAL files found for backup %s", backup->label);
      ret = RESTORE_WAL_INCOMPLETE;
      goto cleanup;
   }

   pgmoneta_log_debug("Validate WAL continuity: collected %d WAL files", pgmoneta_deque_size(wal_files));

   if (validate_wal_sequence_continuity(server, wal_files, backup->wal))
   {
      pgmoneta_log_error("Validate WAL continuity: WAL sequence validation failed for backup %s", backup->label);
      ret = RESTORE_WAL_INCOMPLETE;
      goto cleanup;
   }

   pgmoneta_log_debug("Validate WAL continuity: validation passed for backup %s", backup->label);

cleanup:
   free_recovery_target(&target);
   if (wal_files != NULL)
   {
      pgmoneta_deque_destroy(wal_files);
   }

   return ret;
}

/**
 * Validate timeline consistency across backup chain
 * 
 * Checks backup chain metadata for timeline consistency:
 * - Verify timeline never decreases in backup chain (full -> incremental)
 * - Validate recovery_target_timeline doesn't exceed chain maximum
 * - Ensure each backup has valid timeline range (start_timeline <= end_timeline)
 * 
 * @param server The server
 * @param labels The backup labels in chain
 * @param nodes The nodes containing recovery target timeline info
 * @return 0 on success, RESTORE_TIMELINE_MISMATCH if validation fails
 */
static int
validate_timeline_consistency(int server, struct deque* labels, struct art* nodes)
{
   if (labels == NULL || pgmoneta_deque_size(labels) == 0)
   {
      return RESTORE_OK;
   }

   struct deque_iterator* iter = NULL;
   struct backup* bck = NULL;
   char** label_list = NULL;
   int label_count = 0;
   int idx = 0;
   uint32_t prev_end_tli = 0;
   bool has_prev = false;
   uint32_t max_tli = 0;
   uint32_t target_tli = 0;
   char* position = NULL;
   char* server_backup = NULL;

   label_count = pgmoneta_deque_size(labels);
   label_list = (char**)calloc(label_count, sizeof(char*));
   if (label_list == NULL)
   {
      return RESTORE_TIMELINE_MISMATCH;
   }

   server_backup = pgmoneta_get_server_backup(server);
   if (server_backup == NULL)
   {
      free(label_list);
      return RESTORE_TIMELINE_MISMATCH;
   }

   pgmoneta_deque_iterator_create(labels, &iter);
   while (pgmoneta_deque_iterator_next(iter))
   {
      if (idx >= label_count)
      {
         break;
      }
      label_list[idx++] = (char*)pgmoneta_value_data(iter->value);
   }
   pgmoneta_deque_iterator_destroy(iter);

   for (int i = label_count - 1; i >= 0; i--)
   {
      if (pgmoneta_load_info(server_backup, label_list[i], &bck))
      {
         pgmoneta_log_error("Validate timeline consistency: unable to load backup metadata %s", label_list[i]);
         free(server_backup);
         free(label_list);
         return RESTORE_TIMELINE_MISMATCH;
      }

      if (bck == NULL)
      {
         free(server_backup);
         free(label_list);
         return RESTORE_TIMELINE_MISMATCH;
      }

      if (bck->start_timeline > bck->end_timeline)
      {
         pgmoneta_log_error("Validate timeline consistency: invalid timeline range for %s (start=%u > end=%u)",
                            bck->label, bck->start_timeline, bck->end_timeline);
         free(bck);
         free(server_backup);
         free(label_list);
         return RESTORE_TIMELINE_MISMATCH;
      }

      if (has_prev)
      {
         if (bck->start_timeline < prev_end_tli)
         {
            pgmoneta_log_error("Validate timeline consistency: timeline regression in backup chain at %s (prev=%u, current=%u)",
                               bck->label, prev_end_tli, bck->start_timeline);
            free(bck);
            free(server_backup);
            free(label_list);
            return RESTORE_TIMELINE_MISMATCH;
         }
      }

      if (bck->end_timeline > max_tli)
      {
         max_tli = bck->end_timeline;
      }

      prev_end_tli = bck->end_timeline;
      has_prev = true;
      free(bck);
      bck = NULL;
   }

   free(server_backup);
   free(label_list);

   position = (char*)pgmoneta_art_search(nodes, USER_POSITION);
   if (position != NULL && strlen(position) > 0)
   {
      struct recovery_target target = {0};

      if (parse_recovery_target(position, &target) == 0)
      {
         target_tli = target.target_tli;
         free_recovery_target(&target);
      }
      else
      {
         pgmoneta_log_warn("Validate timeline consistency: failed to parse recovery target position");
      }
   }

   if (target_tli > 0)
   {
      if (target_tli > max_tli)
      {
         pgmoneta_log_error("Validate timeline consistency: recovery_target_timeline=%u exceeds backup chain maximum timeline=%u",
                            target_tli, max_tli);
         return RESTORE_TIMELINE_MISMATCH;
      }

      pgmoneta_log_debug("Validate timeline consistency: recovery_target_timeline=%u is within backup chain range", target_tli);
   }

   pgmoneta_log_debug("Validate timeline consistency: backup chain metadata validation passed");

   return RESTORE_OK;
}

/**
 * Validate PostgreSQL version compatibility
 * 
 * Checks:
 * - Verify backup's PostgreSQL version is compatible with target server version
 * - Check for major version mismatches
 * - For incremental backups: verify all backups have same PostgreSQL major version
 * - Validate pgmoneta version compatibility between backup and current version
 * 
 * @param server The server
 * @param backup The backup
 * @return 0 on success, RESTORE_VERSION_INCOMPATIBLE if validation fails
 */
static int
validate_postgresql_version(int server, struct backup* backup)
{
   struct main_configuration* config;
   struct backup* current = NULL;
   struct backup* parent = NULL;
   bool current_is_input = true;
   int target_major = 0;
   struct version backup_pgmoneta_version = {0};
   struct version current_pgmoneta_version = {0};

   if (backup == NULL)
   {
      return RESTORE_VERSION_INCOMPATIBLE;
   }

   config = (struct main_configuration*)shmem;
   target_major = config->common.servers[server].version;

   if (strlen(backup->version) > 0)
   {
      if (pgmoneta_extension_parse_version(backup->version, &backup_pgmoneta_version) == 0)
      {
         if (pgmoneta_extension_parse_version(VERSION, &current_pgmoneta_version) == 0)
         {
            if (backup_pgmoneta_version.major > current_pgmoneta_version.major)
            {
               pgmoneta_log_error("Validate PostgreSQL version: backup %s created with pgmoneta %s, "
                                  "current is %s. Backup format may be incompatible.",
                                  backup->label, backup->version, VERSION);
               return RESTORE_VERSION_INCOMPATIBLE;
            }

            // Warn about significant version differences
            if (backup_pgmoneta_version.major < current_pgmoneta_version.major - 1)
            {
               pgmoneta_log_warn("Validate PostgreSQL version: backup %s created with older pgmoneta %s, "
                                 "current is %s. Please verify compatibility.",
                                 backup->label, backup->version, VERSION);
            }
         }
      }
      else
      {
         pgmoneta_log_debug("Validate PostgreSQL version: unable to parse backup pgmoneta version '%s'",
                            backup->version);
      }
   }

   if (backup->major_version > 0 && target_major > 0 && backup->major_version != target_major)
   {
      if (backup->major_version < target_major)
      {
         pgmoneta_log_error("Validate PostgreSQL version: backup %s is PostgreSQL %d, target server is PostgreSQL %d. "
                            "Restoring older major version to newer requires pg_upgrade after restore.",
                            backup->label, backup->major_version, target_major);
      }
      else
      {
         pgmoneta_log_error("Validate PostgreSQL version: backup %s is PostgreSQL %d, target server is PostgreSQL %d. "
                            "Cannot restore newer major version to older PostgreSQL.",
                            backup->label, backup->major_version, target_major);
      }
      return RESTORE_VERSION_INCOMPATIBLE;
   }

   // Check minor version compatibility - warning only
   if (backup->minor_version > 0 && config->common.servers[server].minor_version > 0 &&
       backup->minor_version != config->common.servers[server].minor_version)
   {
      if (backup->minor_version < config->common.servers[server].minor_version)
      {
         pgmoneta_log_info("Validate PostgreSQL version: backup minor version %d.%d, server is %d.%d. "
                           "Restoring older minor version to newer is generally safe.",
                           backup->major_version, backup->minor_version,
                           target_major, config->common.servers[server].minor_version);
      }
      else
      {
         pgmoneta_log_warn("Validate PostgreSQL version: backup minor version %d.%d, server is %d.%d. "
                           "Verify compatibility before proceeding.",
                           backup->major_version, backup->minor_version,
                           target_major, config->common.servers[server].minor_version);
      }
   }

   current = backup;
   while (current != NULL && current->type == TYPE_INCREMENTAL)
   {
      if (pgmoneta_get_backup_parent(server, current, &parent) || parent == NULL)
      {
         pgmoneta_log_error("Validate PostgreSQL version: unable to load parent for %s", current->label);
         goto error;
      }

      if (parent->major_version > 0 && backup->major_version > 0 &&
          parent->major_version != backup->major_version)
      {
         pgmoneta_log_error("Validate PostgreSQL version: PostgreSQL major version mismatch in backup chain. "
                            "Backup %s is PostgreSQL %d, parent %s is PostgreSQL %d.",
                            current->label, backup->major_version,
                            parent->label, parent->major_version);
         goto error;
      }

      if (!current_is_input)
      {
         free(current);
      }
      current = parent;
      parent = NULL;
      current_is_input = false;
   }

   if (!current_is_input && current != NULL)
   {
      free(current);
   }

   return RESTORE_OK;

error:
   if (!current_is_input && current != NULL)
   {
      free(current);
   }
   if (parent != NULL)
   {
      free(parent);
   }
   return RESTORE_VERSION_INCOMPATIBLE;
}

/**
 * Validate backup file accessibility
 * 
 * Checks:
 * - Verify backup directory exists and is readable
 * - Check for required files (backup_label, pg_control)
 * - Handles compressed/encrypted files automatically
 * 
 * @param server The server
 * @param backup The backup
 * @return 0 on success, RESTORE_FILE_INACCESSIBLE if validation fails
 */
static int
validate_backup_file_accessibility(int server, struct backup* backup)
{
   char* backup_root = NULL;
   char* backup_data = NULL;
   char* backup_label_path = NULL;
   char* pg_control_path = NULL;
   DIR* dir = NULL;

   if (backup == NULL)
   {
      return RESTORE_FILE_INACCESSIBLE;
   }

   backup_root = pgmoneta_get_server_backup_identifier(server, backup->label);
   backup_data = pgmoneta_get_server_backup_identifier_data(server, backup->label);

   if (backup_root == NULL || backup_data == NULL)
   {
      goto error;
   }

   if (!pgmoneta_exists(backup_root) || !pgmoneta_exists(backup_data))
   {
      pgmoneta_log_error("Validate backup files: missing backup directory for %s", backup->label);
      goto error;
   }

   dir = opendir(backup_data);
   if (dir == NULL)
   {
      pgmoneta_log_error("Validate backup files: unable to read %s", backup_data);
      goto error;
   }
   closedir(dir);
   dir = NULL;

   backup_label_path = pgmoneta_append(backup_label_path, backup_data);
   if (!pgmoneta_ends_with(backup_label_path, "/"))
   {
      backup_label_path = pgmoneta_append(backup_label_path, "/");
   }
   backup_label_path = pgmoneta_append(backup_label_path, "backup_label");

   pg_control_path = pgmoneta_append(pg_control_path, backup_data);
   if (!pgmoneta_ends_with(pg_control_path, "/"))
   {
      pg_control_path = pgmoneta_append(pg_control_path, "/");
   }
   pg_control_path = pgmoneta_append(pg_control_path, "global/pg_control");

   // Check for backup_label file using backup compression/encryption metadata
   char* actual_backup_label = pgmoneta_get_backup_file_path(backup_label_path, backup->compression, backup->encryption);
   if (actual_backup_label == NULL)
   {
      pgmoneta_log_error("Validate backup files: missing backup_label for %s", backup->label);
      goto error;
   }
   free(actual_backup_label);

   // Check for pg_control file using backup compression/encryption metadata
   char* actual_pg_control = pgmoneta_get_backup_file_path(pg_control_path, backup->compression, backup->encryption);
   if (actual_pg_control == NULL)
   {
      pgmoneta_log_error("Validate backup files: missing pg_control for %s", backup->label);
      goto error;
   }
   free(actual_pg_control);

   free(backup_root);
   free(backup_data);
   free(backup_label_path);
   free(pg_control_path);
   return RESTORE_OK;

error:
   if (dir != NULL)
   {
      closedir(dir);
   }
   free(backup_root);
   free(backup_data);
   free(backup_label_path);
   free(pg_control_path);
   return RESTORE_FILE_INACCESSIBLE;
}

/**
 * Parse recovery target information from position string
 * 
 * Parses the USER_POSITION string to extract recovery target parameters like
 * recovery_target_lsn, recovery_target_xid, recovery_target_time, etc.
 * 
 * @param position The position string (comma-separated key=value pairs)
 * @param target The recovery target structure to populate
 * @return 0 on success, 1 on failure
 */
static int
parse_recovery_target(char* position, struct recovery_target* target)
{
   char tokens[1024];
   char* ptr = NULL;

   if (target == NULL)
   {
      return 1;
   }

   memset(target, 0, sizeof(struct recovery_target));

   if (position == NULL || strlen(position) == 0)
   {
      return 0;
   }

   memset(tokens, 0, sizeof(tokens));
   if (strlen(position) >= sizeof(tokens))
   {
      pgmoneta_log_warn("Parse recovery target: position string too long, truncating");
      memcpy(tokens, position, sizeof(tokens) - 1);
   }
   else
   {
      memcpy(tokens, position, strlen(position));
   }

   ptr = strtok(tokens, ",");
   while (ptr != NULL)
   {
      char* equal = strchr(ptr, '=');
      if (equal != NULL)
      {
         char key[256];
         char value[512];
         size_t key_len = (size_t)(equal - ptr);

         memset(key, 0, sizeof(key));
         memset(value, 0, sizeof(value));

         if (key_len >= sizeof(key))
         {
            key_len = sizeof(key) - 1;
         }
         memcpy(key, ptr, key_len);

         if (strlen(equal + 1) >= sizeof(value))
         {
            memcpy(value, equal + 1, sizeof(value) - 1);
         }
         else
         {
            memcpy(value, equal + 1, strlen(equal + 1));
         }

         if (!strcmp(key, "lsn") || !strcmp(key, "recovery_target_lsn"))
         {
            target->target_lsn = strdup(value);
         }
         else if (!strcmp(key, "xid") || !strcmp(key, "recovery_target_xid"))
         {
            target->target_xid = strdup(value);
         }
         else if (!strcmp(key, "time") || !strcmp(key, "recovery_target_time"))
         {
            target->target_time = strdup(value);
         }
         else if (!strcmp(key, "name") || !strcmp(key, "recovery_target_name"))
         {
            target->target_name = strdup(value);
         }
         else if (!strcmp(key, "timeline") || !strcmp(key, "recovery_target_timeline"))
         {
            target->target_tli = (uint32_t)strtoul(value, NULL, 10);
         }
         else if (!strcmp(key, "inclusive"))
         {
            target->inclusive = (!strcmp(value, "true") || !strcmp(value, "1"));
         }
      }

      ptr = strtok(NULL, ",");
   }

   return 0;
}

/**
 * Free recovery target structure
 * 
 * @param target The recovery target to free
 */
static void
free_recovery_target(struct recovery_target* target)
{
   if (target == NULL)
   {
      return;
   }

   free(target->target_lsn);
   free(target->target_xid);
   free(target->target_time);
   free(target->target_name);

   memset(target, 0, sizeof(struct recovery_target));
}

/**
 * Collect all available WAL files from backup and archive directories
 * 
 * Collects WAL files from both the backup's WAL directory and the server's
 * archive WAL directory, normalizing filenames and storing them in a deque.
 * 
 * @param server The server index
 * @param backup The backup structure
 * @param wal_files Output deque of normalized WAL filenames (caller must destroy)
 * @return 0 on success, 1 on failure
 */
static int
collect_available_wal_files(int server, struct backup* backup, struct deque** wal_files)
{
   char* wal_dir_backup = NULL;
   char* wal_dir_archive = NULL;
   struct deque* backup_wals = NULL;
   struct deque* archive_wals = NULL;
   struct deque_iterator* iter = NULL;
   struct deque* collected = NULL;

   if (backup == NULL || wal_files == NULL)
   {
      return 1;
   }

   if (pgmoneta_deque_create(false, &collected))
   {
      return 1;
   }

   wal_dir_backup = pgmoneta_get_server_backup_identifier_data_wal(server, backup->label);
   wal_dir_archive = pgmoneta_get_server_wal(server);

   if (wal_dir_backup != NULL && pgmoneta_exists(wal_dir_backup))
   {
      if (pgmoneta_get_wal_files(wal_dir_backup, &backup_wals) == 0 && backup_wals != NULL)
      {
         pgmoneta_deque_iterator_create(backup_wals, &iter);
         while (pgmoneta_deque_iterator_next(iter))
         {
            char* filename = (char*)pgmoneta_value_data(iter->value);
            char* normalized = NULL;

            if (pgmoneta_extraction_strip_suffix(filename, 0, &normalized) == 0 && normalized != NULL)
            {
               pgmoneta_deque_add(collected, NULL, (uintptr_t)normalized, ValueString);
            }
         }
         pgmoneta_deque_iterator_destroy(iter);
         pgmoneta_deque_destroy(backup_wals);
         iter = NULL;
         backup_wals = NULL;
      }
   }

   if (wal_dir_archive != NULL && pgmoneta_exists(wal_dir_archive))
   {
      if (pgmoneta_get_wal_files(wal_dir_archive, &archive_wals) == 0 && archive_wals != NULL)
      {
         pgmoneta_deque_iterator_create(archive_wals, &iter);
         while (pgmoneta_deque_iterator_next(iter))
         {
            char* filename = (char*)pgmoneta_value_data(iter->value);
            char* normalized = NULL;

            if (pgmoneta_extraction_strip_suffix(filename, 0, &normalized) == 0 && normalized != NULL)
            {
               // Check if already in collected to avoid duplicates
               bool found = false;
               struct deque_iterator* check_iter = NULL;
               pgmoneta_deque_iterator_create(collected, &check_iter);
               while (pgmoneta_deque_iterator_next(check_iter))
               {
                  char* existing = (char*)pgmoneta_value_data(check_iter->value);
                  if (!strcmp(existing, normalized))
                  {
                     found = true;
                     free(normalized);
                     normalized = NULL;
                     break;
                  }
               }
               pgmoneta_deque_iterator_destroy(check_iter);

               if (!found && normalized != NULL)
               {
                  pgmoneta_deque_add(collected, NULL, (uintptr_t)normalized, ValueString);
               }
            }
         }
         pgmoneta_deque_iterator_destroy(iter);
         pgmoneta_deque_destroy(archive_wals);
         iter = NULL;
         archive_wals = NULL;
      }
   }

   free(wal_dir_backup);
   free(wal_dir_archive);

   *wal_files = collected;
   return 0;
}

/**
 * Validate WAL sequence continuity starting from a specific WAL file
 * 
 * Parses all WAL filenames, sorts them by timeline and segment number,
 * then checks for gaps in the sequence starting from start_wal.
 * Also validates that .history files exist for any timeline switches.
 * 
 * @param server The server index
 * @param wal_files Deque of available WAL filenames
 * @param start_wal The starting WAL filename
 * @return 0 if continuous, 1 if gaps detected or start not found
 */
static int
validate_wal_sequence_continuity(int server, struct deque* wal_files, char* start_wal)
{
   struct deque_iterator* iter = NULL;
   struct wal_segment* segments = NULL;
   struct wal_segment start_segment = {0};
   int segment_count = 0;
   int valid_count = 0;
   int start_index = -1;
   int ret = 1;

   if (wal_files == NULL || start_wal == NULL || strlen(start_wal) == 0)
   {
      return 1;
   }

   segment_count = pgmoneta_deque_size(wal_files);
   if (segment_count == 0)
   {
      pgmoneta_log_error("WAL sequence validation: no WAL files provided");
      return 1;
   }

   if (pgmoneta_parse_wal_filename(start_wal, &start_segment))
   {
      pgmoneta_log_error("WAL sequence validation: failed to parse start WAL %s", start_wal);
      return 1;
   }

   segments = (struct wal_segment*)calloc(segment_count, sizeof(struct wal_segment));
   if (segments == NULL)
   {
      pgmoneta_log_error("WAL sequence validation: memory allocation failed");
      return 1;
   }

   pgmoneta_deque_iterator_create(wal_files, &iter);
   while (pgmoneta_deque_iterator_next(iter))
   {
      char* wal_name = (char*)pgmoneta_value_data(iter->value);
      if (pgmoneta_parse_wal_filename(wal_name, &segments[valid_count]) == 0)
      {
         valid_count++;
      }
      else
      {
         pgmoneta_log_debug("WAL sequence validation: skipping unparseable file: %s", wal_name);
      }
   }
   pgmoneta_deque_iterator_destroy(iter);

   if (valid_count == 0)
   {
      pgmoneta_log_error("WAL sequence validation: no valid WAL files found");
      goto cleanup;
   }

   pgmoneta_log_debug("WAL sequence validation: parsed %d valid WAL files out of %d",
                      valid_count, segment_count);

   qsort(segments, valid_count, sizeof(struct wal_segment), pgmoneta_compare_wal_segments);

   for (int i = 0; i < valid_count; i++)
   {
      if (segments[i].timeline == start_segment.timeline &&
          segments[i].segment_no == start_segment.segment_no)
      {
         start_index = i;
         break;
      }
   }

   if (start_index == -1)
   {
      pgmoneta_log_error("WAL sequence validation: start WAL %s (TLI=%u, seg_no=%lu) not found in available files",
                         start_wal, start_segment.timeline, start_segment.segment_no);
      goto cleanup;
   }

   pgmoneta_log_debug("WAL sequence validation: found start WAL at index %d", start_index);

   // We check files with the same timeline as the start, and validate .history files for timeline switches
   uint32_t current_timeline = segments[start_index].timeline;
   uint64_t expected_segment_no = segments[start_index].segment_no;
   int gap_count = 0;
   struct timeline_history* history = NULL;

   for (int i = start_index; i < valid_count; i++)
   {
      if (segments[i].timeline != current_timeline)
      {
         pgmoneta_log_info("WAL sequence validation: timeline switch detected from %u to %u at segment %lu",
                           current_timeline, segments[i].timeline, segments[i].segment_no);

         // Validate that .history file exists for the new timeline (if > 1)
         if (segments[i].timeline > 1)
         {
            if (pgmoneta_get_timeline_history(server, segments[i].timeline, &history))
            {
               pgmoneta_log_error("WAL sequence validation: timeline switch to %u detected, but .history file missing or invalid",
                                  segments[i].timeline);
               pgmoneta_log_error("WAL sequence validation: recovery will fail without timeline history file %08X.history",
                                  segments[i].timeline);
               ret = 1;
               pgmoneta_free_timeline_history(history);
               goto cleanup;
            }
            else
            {
               pgmoneta_log_debug("WAL sequence validation: verified .history file exists for timeline %u",
                                  segments[i].timeline);
               pgmoneta_free_timeline_history(history);
               history = NULL;
            }
         }

         current_timeline = segments[i].timeline;
         expected_segment_no = segments[i].segment_no + 1;
         continue;
      }

      if (segments[i].segment_no != expected_segment_no)
      {
         gap_count++;
         if (gap_count <= 5) // Only log the first few gaps to avoid log spam
         {
            pgmoneta_log_warn("WAL sequence gap detected: expected segment %lu, found %lu (file: %s)",
                              expected_segment_no, segments[i].segment_no, segments[i].filename);
         }
         expected_segment_no = segments[i].segment_no + 1;
      }
      else
      {
         expected_segment_no++;
      }
   }

   if (gap_count > 0)
   {
      pgmoneta_log_error("WAL sequence validation: detected %d gap(s) in WAL sequence starting from %s",
                         gap_count, start_wal);
      pgmoneta_log_error("WAL sequence validation: restore may fail if recovery needs missing WAL segments");
      ret = 1;
   }
   else
   {
      pgmoneta_log_info("WAL sequence validation: no gaps detected, %d consecutive segments from %s",
                        valid_count - start_index, start_wal);
      ret = 0;
   }

cleanup:
   free(segments);
   return ret;
}
