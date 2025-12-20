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
#include <logging.h>
#include <management.h>
#include <manifest.h>
#include <network.h>
#include <restore.h>
#include <security.h>
#include <utils.h>
#include <workers.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define NAME                  "restore"
#define RESTORE_OK            0
#define RESTORE_MISSING_LABEL 1
#define RESTORE_NO_DISK_SPACE 2
#define RESTORE_TYPE_UNKNOWN  3
#define RESTORE_ERROR         4
#define MAX_PATH_CONCAT       (MAX_PATH * 2)
#define TMP_SUFFIX            ".tmp"

struct build_backup_file_input
{
   struct worker_common common;
   int server;
   char label[MISC_LENGTH];
   char output_dir[MAX_PATH];
   char relative_dir[MAX_PATH];
   char file_name[MAX_PATH];
   struct deque* prior_labels;
   struct art* backups;
   struct json* files;
   bool incremental;
   bool exclude;
};

static char* restore_last_files_names[] = {"/global/pg_control", "/postgresql.conf", "/pg_hba.conf"};

static int restore_backup_full(struct art* nodes);

static int restore_backup_incremental(struct art* nodes);

static int carry_out_workflow(struct workflow* workflow, struct art* nodes);

static void clear_manifest_incremental_entries(struct json* manifest);
/**
 * Combine the provided backups or each of the user defined table-spaces
 * The function will be called for two rounds, the first round would construct the data directory
 * including pg_tblspc/ and the tsoid directory underneath. And the second round
 * will combine each user defined tablespaces
 * @param tsoid The table space oid, if we are reconstructing a tablespace
 * @param server The server
 * @param label The label of the current backup to combine
 * @param input_dir The base directory of the current input incremental backup
 * @param output_dir The base directory of the output incremental backup
 * @param relative_dir The internal directory relative to input_dir (either data directory or tsoid dir under pg_tblspc)
 * (the last level of directory should not be followed by back slash)
 * @param prior_labels The labels of prior incremental/full backups, from newest to oldest
 * @param files The file array inside manifest of the backup
 * @param incremental Whether to combine the backups into incremental backup
 * @param exclude Whether to exclude some of the files
 * @param workers The workers
 * @return 0 on success, 1 if otherwise
 */
static int combine_backups_recursive(uint32_t tsoid,
                                     int server,
                                     char* label,
                                     char* input_dir,
                                     char* output_dir,
                                     char* relative_dir,
                                     struct deque* prior_labels,
                                     struct art* backups,
                                     struct json* files,
                                     bool incremental,
                                     bool exclude,
                                     struct workers* workers);

/**
 * Reconstruct an incremental backup file from itself and its prior incremental/full backup files to a full backup file
 * @param server The server
 * @param label The label of the current backup to reconstruct
 * @param output_dir The absolute directory containing the reconstructed full backup file
 * @param relative_dir The directory containing the incremental file relative to the root dir, should be the same across all backups
 * @param bare_file_name The name of the file without "INCREMENTAL." prefix
 * @param prior_labels The labels of prior incremental/full backups, from newest to oldest
 * @param backups The backups, including the current one
 * @param incremental Whether to reconstruct into an incremental file
 * @param algorithm The checksum algorithm used in the backup manifest
 * @param files The file entries in manifest
 * @return 0 on success, 1 if otherwise
 */
static int
reconstruct_backup_file(int server,
                        char* label,
                        char* output_dir,
                        char* relative_dir,
                        char* bare_file_name,
                        struct deque* prior_labels,
                        struct art* backups,
                        bool incremental,
                        struct json* files);

static void
do_reconstruct_backup_file(struct worker_common* wc);

static void
create_reconstruct_backup_file_input(int server,
                                     char* label,
                                     char* output_dir,
                                     char* relative_dir,
                                     char* bare_file_name,
                                     struct deque* prior_labels,
                                     struct art* backups,
                                     bool incremental,
                                     struct json* files,
                                     struct workers* workers,
                                     struct build_backup_file_input** wi);

/**
 *
 * Extract and copy a full backup file to the output directory, will add .tmp prefix to file if it's to be excluded
 * @param server The server
 * @param label The label of the current backup to reconstruct
 * @param output_dir The absolute directory containing the full backup file
 * @param relative_dir The directory containing the file relative to the root dir, should be the same across all backups
 * @param file_name The name of the file
 * @param exclude Whether to exclude some of the files
 * @return 0 on success, 1 if otherwise
 */
static int
copy_backup_file(int server,
                 char* label,
                 char* output_dir,
                 char* relative_dir,
                 char* file_name,
                 bool exclude);

static void
do_copy_backup_file(struct worker_common* wc);

static void
create_copy_backup_file_input(
   int server,
   char* label,
   char* output_dir,
   char* relative_dir,
   char* file_name,
   bool exclude,
   struct workers* workers,
   struct build_backup_file_input** wi);

/**
 * Get the number of blocks that the final reconstructed full backup file should have.
 * Normally it is the same as truncation_block_length.
 * But the table could be going through truncation during the backup process. In that case
 * the reconstructed file could have more blocks than truncation_block_length.
 * So anyway extend the file length to include those blocks.
 * @param s The rfile of the incremental file
 * @return The block length
 */
static uint32_t
find_reconstructed_block_length(struct rfile* s);

static void
rfile_destroy_cb(uintptr_t data);

static bool
is_full_file(struct rfile* rf);

static int
read_block(struct rfile* rf, off_t offset, uint32_t blocksz, uint8_t* buffer);

static int
write_reconstructed_file_full(char* output_file_path,
                              uint32_t block_length,
                              struct rfile** source_map,
                              off_t* offset_map,
                              uint32_t blocksz);

static int
write_reconstructed_file_incremental(char* output_file_path,
                                     uint32_t block_length,
                                     struct rfile** source_map,
                                     struct rfile* latest_source,
                                     off_t* offset_map,
                                     uint32_t blocksz);

static int
write_backup_label(char* from_dir, char* to_dir, char* lsn_entry, char* tli_entry);

static int
write_backup_label_incremental(int server, char* oldest_label, char* from_dir, char* to_dir);

static uint32_t
parse_oid(char* name);

static void
cleanup_workspaces(int server, struct deque* labels);

static void
create_workspace_directory(int server, char* label, char* relative_prefix);

static void
create_workspace_directories(int server, struct deque* labels, char* relative_prefix);

/**
 * Construct the backup label chain starting from the newest backup
 * @param server The server
 * @param newest_label The newest backup label
 * @param oldest_label [optional] The oldest backup label, if not set, chain stops at full backup
 * @param inclusive Whether to include the newest backup label in the chain
 * @param labels [out] The resulting label deque
 * @return 0 on success, 1 if otherwise
 */
static int
construct_backup_label_chain(int server, char* newest_label, char* oldest_label, bool inclusive, struct deque** labels);

static int
file_base_name(char* file, char** basename);

static int copy_tablespaces_restore(char* from, char* to, char* base,
                                    char* server, char* id,
                                    struct backup* backup,
                                    struct workers* workers);
static int copy_tablespaces_hotstandby(int server,
                                       char* from, char* to,
                                       char* tblspc_mappings,
                                       struct backup* backup,
                                       struct workers* workers);

int
pgmoneta_get_restore_last_files_names(char*** output)
{
   int number_of_elements = 0;
   number_of_elements = sizeof(restore_last_files_names) / sizeof(restore_last_files_names[0]);

   *output = (char**)malloc((number_of_elements + 1) * sizeof(char*));
   if (*output == NULL)
   {
      return 1;
   }

   for (int i = 0; i < number_of_elements; i++)
   {
      (*output)[i] = strdup(restore_last_files_names[i]);
      if ((*output)[i] == NULL)
      {
         return 1;
      }
   }
   (*output)[number_of_elements] = NULL;

   return 0;
}

bool
pgmoneta_is_restore_last_name(char* file_name)
{
   int number_of_elements = sizeof(restore_last_files_names) / sizeof(restore_last_files_names[0]);

   for (int i = 0; i < number_of_elements; i++)
   {
      if (strstr(restore_last_files_names[i], file_name) != NULL)
      {
         return true;
      }
   }

   return false;
}

void
pgmoneta_restore(SSL* ssl, int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* payload)
{
   bool active = false;
   bool locked = false;
   int ret = RESTORE_OK;
   char* identifier = NULL;
   char* position = NULL;
   char* directory = NULL;
   char* elapsed = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds = 0;
   char* output = NULL;
   char* en = NULL;
   int ec = -1;
   struct backup* backup = NULL;
   struct art* nodes = NULL;
   struct json* req = NULL;
   struct json* response = NULL;
   struct main_configuration* config;

   pgmoneta_start_logging();

   config = (struct main_configuration*)shmem;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   if (!atomic_compare_exchange_strong(&config->common.servers[server].repository, &active, true))
   {
      ec = MANAGEMENT_ERROR_RESTORE_ACTIVE;
      pgmoneta_log_info("Restore: Server %s is active", config->common.servers[server].name);
      goto error;
   }

   config->common.servers[server].active_restore = true;
   locked = true;

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   identifier = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_BACKUP);
   position = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_POSITION);
   directory = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_DIRECTORY);

   if (identifier == NULL || strlen(identifier) == 0)
   {
      ec = MANAGEMENT_ERROR_RESTORE_NOBACKUP;
      goto error;
   }

   if (pgmoneta_art_create(&nodes))
   {
      goto error;
   }

   if (pgmoneta_workflow_nodes(server, identifier, nodes, &backup))
   {
      ec = MANAGEMENT_ERROR_RESTORE_NOBACKUP;
      goto error;
   }

   if (pgmoneta_art_insert(nodes, USER_POSITION, (uintptr_t)position, ValueString))
   {
      goto error;
   }

   if (pgmoneta_art_insert(nodes, USER_DIRECTORY, (uintptr_t)directory, ValueString))
   {
      goto error;
   }

   ret = pgmoneta_restore_backup(nodes);
   if (ret == RESTORE_OK)
   {
      if (pgmoneta_management_create_response(payload, server, &response))
      {
         ec = MANAGEMENT_ERROR_ALLOCATION;
         goto error;
      }

      backup = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);

      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->common.servers[server].name, ValueString);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)backup->label, ValueString);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP_SIZE, (uintptr_t)backup->backup_size, ValueUInt64);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_RESTORE_SIZE, (uintptr_t)backup->restore_size, ValueUInt64);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BIGGEST_FILE_SIZE, (uintptr_t)backup->biggest_file_size, ValueUInt64);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_COMMENTS, (uintptr_t)backup->comments, ValueString);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_COMPRESSION, (uintptr_t)backup->compression, ValueInt32);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_ENCRYPTION, (uintptr_t)backup->encryption, ValueInt32);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_INCREMENTAL, (uintptr_t)backup->type, ValueBool);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_INCREMENTAL_PARENT, (uintptr_t)backup->parent_label, ValueString);

#ifdef HAVE_FREEBSD
      clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
      clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

      if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
      {
         ec = MANAGEMENT_ERROR_RESTORE_NETWORK;
         pgmoneta_log_error("Restore: Error sending response for %s", config->common.servers[server].name);
         goto error;
      }

      elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);
      pgmoneta_log_info("Restore: %s/%s (Elapsed: %s)", config->common.servers[server].name, backup->label, elapsed);
   }
   else if (ret == RESTORE_MISSING_LABEL)
   {
      ec = MANAGEMENT_ERROR_RESTORE_NOBACKUP;
      pgmoneta_log_warn("Restore: No identifier for %s/%s", config->common.servers[server].name, identifier);
      goto error;
   }
   else
   {
      ec = MANAGEMENT_ERROR_RESTORE_NODISK;
      goto error;
   }

   config->common.servers[server].active_restore = false;
   atomic_store(&config->common.servers[server].repository, false);

   pgmoneta_json_destroy(payload);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   free(backup);
   free(elapsed);
   free(output);

   exit(0);

error:

   pgmoneta_management_response_error(ssl, client_fd, config->common.servers[server].name,
                                      ec != -1 ? ec : MANAGEMENT_ERROR_ANNOTATE_ERROR, en != NULL ? en : NAME,
                                      compression, encryption, payload);

   pgmoneta_json_destroy(payload);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   if (locked)
   {
      config->common.servers[server].active_restore = false;
      atomic_store(&config->common.servers[server].repository, false);
   }

   free(backup);
   free(elapsed);
   free(output);

   exit(1);
}

int
pgmoneta_restore_backup(struct art* nodes)
{
   struct backup* backup = NULL;
   char* position = NULL;
   struct deque* labels = NULL;
   int server = 0;
   char* label = NULL;

#ifdef DEBUG
   pgmoneta_dump_art(nodes);

   assert(pgmoneta_art_contains_key(nodes, USER_DIRECTORY));
   assert(pgmoneta_art_contains_key(nodes, USER_IDENTIFIER));
   assert(pgmoneta_art_contains_key(nodes, USER_SERVER));

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   position = (char*)pgmoneta_art_search(nodes, USER_POSITION);
   backup = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);
   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   if (position != NULL && strlen(position) > 0)
   {
      char tokens[512];
      bool primary = true;
      bool copy_wal = false;
      char* ptr = NULL;

      memset(&tokens[0], 0, sizeof(tokens));
      memcpy(&tokens[0], position, strlen(position));

      ptr = strtok(&tokens[0], ",");

      while (ptr != NULL)
      {
         char key[256];
         char value[256];
         char* equal = NULL;

         memset(&key[0], 0, sizeof(key));
         memset(&value[0], 0, sizeof(value));

         equal = strchr(ptr, '=');

         if (equal == NULL)
         {
            memcpy(&key[0], ptr, strlen(ptr));
         }
         else
         {
            memcpy(&key[0], ptr, strlen(ptr) - strlen(equal));
            memcpy(&value[0], equal + 1, strlen(equal) - 1);
         }

         if (!strcmp(&key[0], "current") ||
             !strcmp(&key[0], "immediate") ||
             !strcmp(&key[0], "name") ||
             !strcmp(&key[0], "xid") ||
             !strcmp(&key[0], "lsn") ||
             !strcmp(&key[0], "time"))
         {
            copy_wal = true;
         }
         else if (!strcmp(&key[0], "primary"))
         {
            primary = true;
         }
         else if (!strcmp(&key[0], "replica"))
         {
            primary = false;
         }
         else if (!strcmp(&key[0], "inclusive") || !strcmp(&key[0], "timeline") || !strcmp(&key[0], "action"))
         {
            /* Ok */
         }

         ptr = strtok(NULL, ",");
      }

      pgmoneta_art_insert(nodes, NODE_PRIMARY, primary, ValueBool);

      pgmoneta_art_insert(nodes, NODE_RECOVERY_INFO, true, ValueBool);

      pgmoneta_art_insert(nodes, NODE_COPY_WAL, copy_wal, ValueBool);
   }
   else
   {
      pgmoneta_art_insert(nodes, NODE_RECOVERY_INFO, false, ValueBool);
   }

   if (backup->type == TYPE_FULL)
   {
      return restore_backup_full(nodes);
   }
   else if (backup->type == TYPE_INCREMENTAL)
   {
      if (construct_backup_label_chain(server, label, NULL, false, &labels))
      {
         return RESTORE_MISSING_LABEL;
      }
      pgmoneta_art_insert(nodes, NODE_LABELS, (uintptr_t)labels, ValueDeque);
      pgmoneta_art_insert(nodes, NODE_INCREMENTAL_COMBINE, (uintptr_t)false, ValueBool);
      pgmoneta_art_insert(nodes, NODE_COMBINE_AS_IS, (uintptr_t)false, ValueBool);
      return restore_backup_incremental(nodes);
   }
   else
   {
      return RESTORE_TYPE_UNKNOWN;
   }
}

int
pgmoneta_combine_backups(int server, char* label, char* base, char* input_dir, char* output_dir, struct deque* prior_labels, struct backup* bck, struct json* manifest, bool incremental, bool combine_as_is)
{
   uint32_t tsoid = 0;
   char relative_tablespace_path[MAX_PATH];
   char relative_tablespace_prefix[MAX_PATH];
   char full_tablespace_path[MAX_PATH];
   char itblspc_dir[MAX_PATH];
   char otblspc_dir[MAX_PATH];
   char manifest_path[MAX_PATH];
   char* oldest_label = NULL;
   char* server_dir = NULL;
   int number_of_workers = 0;
   struct workers* workers = NULL;
   struct art* backups = NULL;
   struct deque_iterator* iter = NULL;
   struct json* files = NULL;
   struct main_configuration* config;

   if (manifest == NULL || prior_labels == NULL || base == NULL || input_dir == NULL || output_dir == NULL)
   {
      goto error;
   }

   config = (struct main_configuration*)shmem;

   number_of_workers = pgmoneta_get_number_of_workers(server);
   if (number_of_workers > 0)
   {
      pgmoneta_workers_initialize(number_of_workers, &workers);
   }

   pgmoneta_deque_iterator_create(prior_labels, &iter);
   pgmoneta_art_create(&backups);
   server_dir = pgmoneta_get_server_backup(server);
   while (pgmoneta_deque_iterator_next(iter))
   {
      struct backup* b = NULL;
      char* l = NULL;
      l = (char*)pgmoneta_value_data(iter->value);
      pgmoneta_load_info(server_dir, l, &b);
      if (b == NULL)
      {
         pgmoneta_log_error("Unable to find backup %s", l);
         goto error;
      }
      pgmoneta_art_insert(backups, l, (uintptr_t)b, ValueMem);
   }
   pgmoneta_art_insert(backups, label, (uintptr_t)bck, ValueRef);

   memset(manifest_path, 0, MAX_PATH);
   snprintf(manifest_path, MAX_PATH, "%s/backup_manifest", output_dir);

   clear_manifest_incremental_entries(manifest);
   files = (struct json*)pgmoneta_json_get(manifest, MANIFEST_FILES);
   if (files == NULL)
   {
      goto error;
   }

   if (number_of_workers > 0)
   {
      pgmoneta_deque_set_thread_safe((struct deque*)files->elements);
   }

   // It is actually ok even if we don't explicitly create the top level directory
   // since pgmoneta_mkdir creates parent directory if it doesn't exist.
   // We do this to make the code clearer and safer
   if (pgmoneta_mkdir(output_dir))
   {
      pgmoneta_log_error("Combine incremental: Unable to create directory %s", output_dir);
      goto error;
   }
   create_workspace_directory(server, label, NULL);
   create_workspace_directories(server, prior_labels, NULL);

   // round 1 for base data directory
   if (combine_backups_recursive(0, server, label, input_dir, output_dir, NULL, prior_labels, backups, files, incremental, !combine_as_is, workers))
   {
      goto error;
   }

   pgmoneta_workers_wait(workers);

   if (workers != NULL && !workers->outcome)
   {
      goto error;
   }

   // round 2 for each tablespaces
   for (uint64_t i = 0; i < bck->number_of_tablespaces; i++)
   {
      tsoid = parse_oid(bck->tablespaces_oids[i]);

      memset(relative_tablespace_path, 0, MAX_PATH);
      memset(full_tablespace_path, 0, MAX_PATH);
      memset(relative_tablespace_prefix, 0, MAX_PATH);
      memset(otblspc_dir, 0, MAX_PATH);
      memset(itblspc_dir, 0, MAX_PATH);

      snprintf(otblspc_dir, MAX_PATH, "%s/%s/%u", output_dir, "pg_tblspc", tsoid);
      snprintf(itblspc_dir, MAX_PATH, "%s/%s/%u", input_dir, "pg_tblspc", tsoid);
      snprintf(relative_tablespace_prefix, MAX_PATH, "%s/%u/", "pg_tblspc", tsoid);

      if (!combine_as_is)
      {
         snprintf(relative_tablespace_path, MAX_PATH, "../../%s-%s-%s",
                  config->common.servers[server].name, bck->label, bck->tablespaces[i]);
         snprintf(full_tablespace_path, MAX_PATH, "%s/%s-%s-%s", base, config->common.servers[server].name, bck->label, bck->tablespaces[i]);
      }
      else
      {
         snprintf(relative_tablespace_path, MAX_PATH, "../../%s", bck->tablespaces[i]);
         snprintf(full_tablespace_path, MAX_PATH, "%s/%s", base, bck->tablespaces[i]);
      }

      create_workspace_directory(server, label, relative_tablespace_prefix);
      create_workspace_directories(server, prior_labels, relative_tablespace_prefix);

      // create and link the actual tablespace directory
      if (pgmoneta_mkdir(full_tablespace_path))
      {
         pgmoneta_log_error("Combine backups: unable to create directory %s", full_tablespace_path);
         goto error;
      }

      if (pgmoneta_symlink_at_file(otblspc_dir, relative_tablespace_path))
      {
         pgmoneta_log_error("Combine backups: unable to create symlink %s->%s", otblspc_dir, relative_tablespace_path);
         goto error;
      }

      if (combine_backups_recursive(tsoid, server, label, itblspc_dir, full_tablespace_path, NULL, prior_labels, backups, files, incremental, !combine_as_is, workers))
      {
         goto error;
      }
   }

   pgmoneta_workers_wait(workers);

   if (workers != NULL && !workers->outcome)
   {
      goto error;
   }

   if (incremental)
   {
      oldest_label = (char*)pgmoneta_deque_peek_last(prior_labels, NULL);
      if (write_backup_label_incremental(server, oldest_label, input_dir, output_dir))
      {
         goto error;
      }
   }
   else
   {
      if (write_backup_label(input_dir, output_dir, NULL, NULL))
      {
         goto error;
      }
   }

   if (pgmoneta_write_postgresql_manifest(manifest, manifest_path))
   {
      pgmoneta_log_error("Fail to write manifest to %s", manifest_path);
      goto error;
   }

   pgmoneta_workers_destroy(workers);
   pgmoneta_art_destroy(backups);
   pgmoneta_deque_iterator_destroy(iter);
   free(server_dir);
   return 0;

error:
   pgmoneta_workers_destroy(workers);
   pgmoneta_art_destroy(backups);
   pgmoneta_deque_iterator_destroy(iter);
   free(server_dir);
   return 1;
}

int
pgmoneta_rollup_backups(int server, char* newest_label, char* oldest_label)
{
   struct art* nodes = NULL;
   struct backup* newest_backup = NULL;
   struct backup* oldest_backup = NULL;
   struct backup* tmp_backup = NULL;
   bool incremental = false;
   struct deque* labels = NULL;
   char* tmp_backup_dir = NULL;
   char* tmp_backup_root = NULL;
   char* tmp_backup_label = NULL;
   char* backup_dir = NULL;
   char backup_info_path[MAX_PATH];
   char tmp_backup_info_path[MAX_PATH];
   struct workflow* workflow = NULL;
   pgmoneta_log_trace("Rollup: %s", newest_label);
   memset(backup_info_path, 0, MAX_PATH);
   memset(tmp_backup_info_path, 0, MAX_PATH);

   pgmoneta_art_create(&nodes);
   if (pgmoneta_workflow_nodes(server, newest_label, nodes, &newest_backup))
   {
      goto error;
   }

   tmp_backup_dir = pgmoneta_get_server_backup(server);
   if (pgmoneta_load_info(tmp_backup_dir, oldest_label, &oldest_backup))
   {
      pgmoneta_log_error("Unable to find the oldest backup %s", oldest_label);
      goto error;
   }
   if (oldest_backup == NULL)
   {
      pgmoneta_log_error("Unable to find the oldest backup %s", oldest_label);
   }
   incremental = (oldest_backup->type == TYPE_INCREMENTAL);

   if (newest_backup->type == TYPE_FULL)
   {
      pgmoneta_log_error("Cannot rollup a full backup %s", newest_label);
      goto error;
   }

   if (construct_backup_label_chain(server, newest_label, oldest_label, false, &labels))
   {
      pgmoneta_log_error("Unable to construct chain from backup %s to backup %s", newest_label, oldest_label);
      goto error;
   }
   pgmoneta_art_insert(nodes, NODE_LABELS, (uintptr_t)labels, ValueDeque);

   // USER DIRECTORY
   tmp_backup_label = pgmoneta_append(tmp_backup_label, TMP_SUFFIX);
   tmp_backup_label = pgmoneta_append(tmp_backup_label, "_");
   tmp_backup_label = pgmoneta_append(tmp_backup_label, newest_label);
   tmp_backup_root = pgmoneta_append(tmp_backup_root, tmp_backup_dir);
   tmp_backup_root = pgmoneta_append(tmp_backup_root, tmp_backup_label);
   backup_dir = pgmoneta_get_server_backup_identifier(server, newest_label);

   pgmoneta_art_insert(nodes, USER_DIRECTORY, (uintptr_t)tmp_backup_root, ValueString);
   pgmoneta_art_insert(nodes, NODE_INCREMENTAL_COMBINE, (uintptr_t)incremental, ValueBool);
   pgmoneta_art_insert(nodes, NODE_COMBINE_AS_IS, (uintptr_t)true, ValueBool);
   if (restore_backup_incremental(nodes))
   {
      pgmoneta_log_error("Unable to roll up backups from %s to %s", oldest_label, newest_label);
      goto error;
   }

   // rebuild backup.info
   snprintf(backup_info_path, sizeof(backup_info_path), "%s%s", backup_dir, "backup.info");
   snprintf(tmp_backup_info_path, sizeof(tmp_backup_info_path), "%s/%s", tmp_backup_root, "backup.info");
   if (pgmoneta_copy_file(backup_info_path, tmp_backup_info_path, NULL))
   {
      pgmoneta_log_error("Unable to copy %s to %s", backup_info_path, tmp_backup_info_path);
      goto error;
   }
   if (pgmoneta_load_info(tmp_backup_dir, tmp_backup_label, &tmp_backup))
   {
      pgmoneta_log_error("Unable to get backup for directory %s", tmp_backup_root);
      goto error;
   }
   if (!incremental)
   {
      tmp_backup->type = TYPE_FULL;
      memset(tmp_backup->parent_label, 0, sizeof(tmp_backup->parent_label));
   }
   else
   {
      snprintf(tmp_backup->parent_label, sizeof(tmp_backup->parent_label), "%s", oldest_backup->parent_label);
   }
   if (pgmoneta_save_info(tmp_backup_dir, tmp_backup))
   {
      pgmoneta_log_error("Unable to save backup info for directory %s", tmp_backup_root);
      goto error;
   }
   //  Now that tmp_backup is the new new_backup, replace it inside the nodes
   pgmoneta_art_insert(nodes, NODE_BACKUP, (uintptr_t)tmp_backup, ValueMem);
   pgmoneta_delete_directory(backup_dir);
   if (rename(tmp_backup_root, backup_dir) != 0)
   {
      pgmoneta_log_error("rollup: could not rename directory %s to %s", tmp_backup_root, backup_dir);
      goto error;
   }

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_POST_ROLLUP, tmp_backup);
   if (carry_out_workflow(workflow, nodes) != RESTORE_OK)
   {
      goto error;
   }

   pgmoneta_workflow_destroy(workflow);
   pgmoneta_art_destroy(nodes);
   free(oldest_backup);
   free(tmp_backup_dir);
   free(tmp_backup_label);
   free(tmp_backup_root);
   free(backup_dir);
   return 0;

error:
   if (pgmoneta_exists(tmp_backup_root))
   {
      pgmoneta_delete_directory(tmp_backup_root);
   }
   pgmoneta_workflow_destroy(workflow);
   pgmoneta_art_destroy(nodes);
   free(oldest_backup);
   free(tmp_backup_dir);
   free(tmp_backup_label);
   free(tmp_backup_root);
   free(backup_dir);
   return 1;
}

int
pgmoneta_extract_incremental_backup(int server, char* label, char** root, char** base)
{
   struct art* nodes = NULL;
   struct backup* backup = NULL;
   struct deque* labels = NULL;
   char* backup_root = NULL;
   char* backup_base = NULL;
   char backup_info_path[MAX_PATH];
   char tmp_backup_info_path[MAX_PATH];

   memset(backup_info_path, 0, MAX_PATH);
   memset(tmp_backup_info_path, 0, MAX_PATH);

   pgmoneta_art_create(&nodes);
   if (pgmoneta_workflow_nodes(server, label, nodes, &backup))
   {
      goto error;
   }

   if (backup->type != TYPE_INCREMENTAL)
   {
      pgmoneta_log_error("Backup %s is not incremental backup", label);
      goto error;
   }

   if (construct_backup_label_chain(server, label, NULL, false, &labels))
   {
      pgmoneta_log_error("Unable to construct chain from backup %s", label);
      goto error;
   }
   pgmoneta_art_insert(nodes, NODE_LABELS, (uintptr_t)labels, ValueDeque);

   // USER DIRECTORY
   backup_root = pgmoneta_get_server_workspace(server);
   backup_root = pgmoneta_append(backup_root, TMP_SUFFIX);
   backup_root = pgmoneta_append(backup_root, "_");
   backup_root = pgmoneta_append(backup_root, label);

   pgmoneta_art_insert(nodes, USER_DIRECTORY, (uintptr_t)backup_root, ValueString);
   pgmoneta_art_insert(nodes, NODE_INCREMENTAL_COMBINE, (uintptr_t)false, ValueBool);
   pgmoneta_art_insert(nodes, NODE_COMBINE_AS_IS, (uintptr_t)false, ValueBool);
   if (restore_backup_incremental(nodes))
   {
      pgmoneta_log_error("Unable to extract backup %s", label);
      goto error;
   }

#ifdef DEBUG
   assert(pgmoneta_art_contains_key(nodes, NODE_TARGET_BASE));
#endif
   backup_base = pgmoneta_append(backup_base, (char*)pgmoneta_art_search(nodes, NODE_TARGET_BASE));
   *base = backup_base;
   *root = backup_root;

   pgmoneta_art_destroy(nodes);
   return 0;

error:
   if (backup_root != NULL && pgmoneta_exists(backup_root))
   {
      pgmoneta_delete_directory(backup_root);
   }
   free(backup_base);
   free(backup_root);
   pgmoneta_art_destroy(nodes);
   return 1;
}

int
pgmoneta_copy_postgresql_restore(char* from, char* to, char* base, char* server, char* id, struct backup* backup, struct workers* workers)
{
   DIR* d = opendir(from);
   char* from_buffer = NULL;
   char* to_buffer = NULL;
   struct dirent* entry;
   struct stat statbuf;
   char** restore_last_files_names = NULL;

   if (pgmoneta_get_restore_last_files_names(&restore_last_files_names))
   {
      goto error;
   }

   if (restore_last_files_names != NULL)
   {
      for (int i = 0; restore_last_files_names[i] != NULL; i++)
      {
         char* temp = NULL;
         temp = (char*)malloc((strlen(restore_last_files_names[i]) + strlen(from)) * sizeof(char) + 1);

         if (temp == NULL)
         {
            goto error;
         }
         snprintf(temp, strlen(from) + strlen(restore_last_files_names[i]) + 1, "%s%s", from, restore_last_files_names[i]);
         free(restore_last_files_names[i]);

         restore_last_files_names[i] = temp;
      }
   }

   pgmoneta_mkdir(to);

   if (d)
   {
      while ((entry = readdir(d)))
      {
         if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
         {
            continue;
         }

         from_buffer = pgmoneta_append(from_buffer, from);
         if (!pgmoneta_ends_with(from_buffer, "/"))
         {
            from_buffer = pgmoneta_append(from_buffer, "/");
         }
         from_buffer = pgmoneta_append(from_buffer, entry->d_name);

         to_buffer = pgmoneta_append(to_buffer, to);
         if (!pgmoneta_ends_with(to_buffer, "/"))
         {
            to_buffer = pgmoneta_append(to_buffer, "/");
         }
         to_buffer = pgmoneta_append(to_buffer, entry->d_name);

         if (!stat(from_buffer, &statbuf))
         {
            if (S_ISDIR(statbuf.st_mode))
            {
               if (!strcmp(entry->d_name, "pg_tblspc"))
               {
                  copy_tablespaces_restore(from, to, base, server, id, backup, workers);
               }
               else
               {
                  pgmoneta_copy_directory(from_buffer, to_buffer, restore_last_files_names, workers);
               }
            }
            else
            {
               bool file_is_excluded = false;
               if (restore_last_files_names != NULL)
               {
                  for (int i = 0; restore_last_files_names[i] != NULL; i++)
                  {
                     file_is_excluded = !strcmp(from_buffer, restore_last_files_names[i]);
                  }
                  if (!file_is_excluded)
                  {
                     pgmoneta_copy_file(from_buffer, to_buffer, workers);
                  }
               }
               else
               {
                  pgmoneta_copy_file(from_buffer, to_buffer, workers);
               }
            }
         }

         free(from_buffer);
         free(to_buffer);

         from_buffer = NULL;
         to_buffer = NULL;
      }
      closedir(d);
   }
   else
   {
      goto error;
   }

   pgmoneta_workers_wait(workers);

   if (restore_last_files_names != NULL)
   {
      for (int i = 0; restore_last_files_names[i] != NULL; i++)
      {
         free(restore_last_files_names[i]);
      }
      free(restore_last_files_names);
   }

   return 0;

error:

   pgmoneta_workers_wait(workers);

   if (restore_last_files_names != NULL)
   {
      for (int i = 0; restore_last_files_names[i] != NULL; i++)
      {
         free(restore_last_files_names[i]);
      }
      free(restore_last_files_names);
   }

   return 1;
}

int
pgmoneta_copy_postgresql_hotstandby(int server, char* from, char* to, char* tblspc_mappings, struct backup* backup, struct workers* workers)
{
   DIR* d = opendir(from);
   char* from_buffer = NULL;
   char* to_buffer = NULL;
   struct dirent* entry;
   struct stat statbuf;

   pgmoneta_mkdir(to);

   if (d)
   {
      while ((entry = readdir(d)))
      {
         if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
         {
            continue;
         }

         from_buffer = pgmoneta_append(from_buffer, from);
         from_buffer = pgmoneta_append(from_buffer, "/");
         from_buffer = pgmoneta_append(from_buffer, entry->d_name);

         to_buffer = pgmoneta_append(to_buffer, to);
         to_buffer = pgmoneta_append(to_buffer, "/");
         to_buffer = pgmoneta_append(to_buffer, entry->d_name);

         if (!stat(from_buffer, &statbuf))
         {
            if (S_ISDIR(statbuf.st_mode))
            {
               if (!strcmp(entry->d_name, "pg_tblspc"))
               {
                  copy_tablespaces_hotstandby(server, from, to, tblspc_mappings, backup, workers);
               }
               else
               {
                  pgmoneta_copy_directory(from_buffer, to_buffer, NULL, workers);
               }
            }
            else
            {
               pgmoneta_copy_file(from_buffer, to_buffer, workers);
            }
         }

         free(from_buffer);
         free(to_buffer);

         from_buffer = NULL;
         to_buffer = NULL;
      }
      closedir(d);
   }
   else
   {
      goto error;
   }

   pgmoneta_workers_wait(workers);

   return 0;

error:

   return 1;
}

static int
combine_backups_recursive(uint32_t tsoid,
                          int server,
                          char* label,
                          char* input_dir,
                          char* output_dir,
                          char* relative_dir,
                          struct deque* prior_labels,
                          struct art* backups,
                          struct json* files,
                          bool incremental,
                          bool exclude,
                          struct workers* workers)
{
   bool is_pg_tblspc = false;
   bool is_incremental_dir = false;
   char ifulldir[MAX_PATH];
   char ofulldir[MAX_PATH];
   // Current directory of the file to be reconstructed relative to backup base directory.
   // In normal cases it's the same as relative_dir, except for having an ending backup slash
   // For table spaces the relative_dir is relative to the table space oid directory,
   // so the relative_prefix should be pg_tblspc/oid/relative_dir/ instead.
   // In short, this is the "absolute" relative directory to data directory, this always ends with `/`
   char relative_prefix[MAX_PATH];
   DIR* dir = NULL;
   struct dirent* entry;

   memset(ifulldir, 0, MAX_PATH);
   memset(ofulldir, 0, MAX_PATH);
   memset(relative_prefix, 0, MAX_PATH);

   // categorize current directory
   is_pg_tblspc = pgmoneta_compare_string(relative_dir, "pg_tblspc");
   // incremental directories are subdirectories of base/ (files directly under base/ itself doesn't count),
   // the pg_global directory itself (subdirectories doesn't count, only files directly under global),
   // and subdirectories of pg_tblspc/
   is_incremental_dir = pgmoneta_starts_with(relative_dir, "base/") ||
                        pgmoneta_compare_string(relative_dir, "global") ||
                        pgmoneta_starts_with(relative_dir, "pg_tblspc/") ||
                        tsoid != 0;
   if (relative_dir == NULL)
   {
      memcpy(ifulldir, input_dir, strlen(input_dir));
      memcpy(ofulldir, output_dir, strlen(output_dir));

      // Since relative_dir is either relative to data directory or the pg_tblspc/oid dir,
      // when relative directory is NULL, either it's the data root directory,
      // or it's the tablespace directory(pg_tblspc), only the latter case
      // we'll need to deal with incremental files within, the former case is invalid
      if (tsoid != 0)
      {
         snprintf(relative_prefix, MAX_PATH, "%s/%u/", "pg_tblspc", tsoid);
      }
   }
   else
   {
      snprintf(ifulldir, MAX_PATH, "%s/%s", input_dir, relative_dir);
      snprintf(ofulldir, MAX_PATH, "%s/%s", output_dir, relative_dir);
      if (tsoid == 0)
      {
         snprintf(relative_prefix, MAX_PATH, "%s/", relative_dir);
      }
      else
      {
         snprintf(relative_prefix, MAX_PATH, "%s/%u/%s/", "pg_tblspc", tsoid, relative_dir);
      }
   }

   // top level output directories should have been created
   if (relative_dir != NULL)
   {
      if (pgmoneta_mkdir(ofulldir))
      {
         pgmoneta_log_error("combine backup: could not create directory %s", ofulldir);
         goto error;
      }
   }

   if (!(dir = opendir(ifulldir)))
   {
      pgmoneta_log_error("combine backup: could not open directory %s", ofulldir);
      goto error;
   }
   while ((entry = readdir(dir)) != NULL)
   {
      if (pgmoneta_compare_string(entry->d_name, ".") || pgmoneta_compare_string(entry->d_name, ".."))
      {
         continue;
      }

      // Right now we only care about copying everything directly underneath pg_tblspc dir that's not a symlink
      if (is_pg_tblspc &&
          (entry->d_type == DT_DIR || entry->d_type == DT_LNK) &&
          parse_oid(entry->d_name) != 0)
      {
         continue;
      }

      if (entry->d_type == DT_DIR)
      {
         // go into the next level directory
         char new_relative_dir[MAX_PATH];
         char new_relative_prefix[MAX_PATH_CONCAT];
         memset(new_relative_dir, 0, MAX_PATH);
         memset(new_relative_prefix, 0, MAX_PATH_CONCAT);
         if (relative_dir == NULL)
         {
            memcpy(new_relative_dir, entry->d_name, strlen(entry->d_name));
         }
         else
         {
            snprintf(new_relative_dir, MAX_PATH, "%s/%s", relative_dir, entry->d_name);
         }

         snprintf(new_relative_prefix, MAX_PATH_CONCAT, "%s%s/", relative_prefix, entry->d_name);
         create_workspace_directory(server, label, new_relative_prefix);
         create_workspace_directories(server, prior_labels, new_relative_prefix);

         if (combine_backups_recursive(tsoid, server, label, input_dir, output_dir, new_relative_dir, prior_labels, backups, files, incremental, exclude, workers))
         {
            goto error;
         }
         continue;
      }

      if (entry->d_type != DT_REG)
      {
         if (entry->d_type == DT_LNK)
         {
#ifdef DEBUG
            pgmoneta_log_trace("restoring symbolic link \"%s%s\"", relative_prefix, entry->d_name);
#endif
         }
         else
         {
            pgmoneta_log_warn("skipping special file %s%s", relative_prefix, entry->d_name);
            continue;
         }
      }

      // skip these, backup_label requires special handling
      if (relative_dir == NULL &&
          (pgmoneta_compare_string(entry->d_name, "backup_label") ||
           pgmoneta_compare_string(entry->d_name, "backup_manifest")))
      {
         continue;
      }
      if (is_incremental_dir && pgmoneta_starts_with(entry->d_name, INCREMENTAL_PREFIX))
      {
         // finally found an incremental file
         if (workers != NULL)
         {
            struct build_backup_file_input* wi = NULL;
            if (workers->outcome)
            {
               create_reconstruct_backup_file_input(server,
                                                    label,
                                                    ofulldir,
                                                    relative_prefix,
                                                    entry->d_name + INCREMENTAL_PREFIX_LENGTH,
                                                    prior_labels,
                                                    backups,
                                                    incremental,
                                                    files,
                                                    workers,
                                                    &wi);
               pgmoneta_workers_add(workers, do_reconstruct_backup_file, (struct worker_common*)wi);
            }
            else
            {
               pgmoneta_log_error("Combine backups: workers returned error");
               goto error;
            }
         }
         else
         {
            if (reconstruct_backup_file(server,
                                        label,
                                        ofulldir,
                                        relative_prefix,
                                        entry->d_name + INCREMENTAL_PREFIX_LENGTH,
                                        prior_labels,
                                        backups,
                                        incremental,
                                        files))
            {
               pgmoneta_log_error("unable to reconstruct file %s%s", relative_prefix, entry->d_name + INCREMENTAL_PREFIX_LENGTH);
               goto error;
            }
         }
      }
      else
      {
         if (workers != NULL)
         {
            struct build_backup_file_input* wi = NULL;
            if (workers->outcome)
            {
               create_copy_backup_file_input(server, label, ofulldir, relative_prefix, entry->d_name, exclude, workers, &wi);
               pgmoneta_workers_add(workers, do_copy_backup_file, (struct worker_common*)wi);
            }
            else
            {
               pgmoneta_log_error("Combine backups: workers returned error");
               goto error;
            }
         }
         else
         {
            if (copy_backup_file(server, label, ofulldir, relative_prefix, entry->d_name, exclude))
            {
               pgmoneta_log_error("unable to copy file %s%s", relative_prefix, entry->d_name);
               goto error;
            }
         }
      }
   }

   if (dir != NULL)
   {
      closedir(dir);
   }

   return 0;
error:
   if (dir != NULL)
   {
      closedir(dir);
   }
   return 1;
}

static int
reconstruct_backup_file(int server,
                        char* label,
                        char* output_dir,
                        char* relative_dir,
                        char* bare_file_name,
                        struct deque* prior_labels,
                        struct art* backups,
                        bool incremental,
                        struct json* files)
{
   struct deque* sources = NULL;             // bookkeeping of each incr/full backup rfile, so that we can free them conveniently
   struct deque_iterator* label_iter = NULL; // the iterator for backup directories
   struct rfile* latest_source = NULL;       // the metadata of current incr backup file
   struct rfile** source_map = NULL;         // source to find each block
   off_t* offset_map = NULL;                 // offsets to find each block in corresponding file
   uint32_t block_length = 0;                // total number of blocks in the reconstructed file
   bool full_copy_possible = true;           // whether we could just copy over directly instead of block by block
   uint32_t b = 0;                           // temp variable for block numbers
   struct main_configuration* config;
   size_t blocksz = 0;
   char incr_file_name[MAX_PATH];
   char ofullpath[MAX_PATH_CONCAT];
   char manifest_path[MAX_PATH_CONCAT]; // This is actually the relative file path to the data directory, it's named because manifest uses the relative path internally
   char* prior_label = NULL;
   struct backup* bck = NULL;
   char* base_file_name = NULL;
   uint32_t nblocks = 0;
   size_t file_size = 0;
   struct rfile* copy_source = NULL;
   struct value_config rfile_config = {.destroy_data = rfile_destroy_cb, .to_string = NULL};
   struct json* file = NULL;
   bool full_file_found = false;

   config = (struct main_configuration*)shmem;

   // since we are working with backup archives, these path will have compression and encryption suffix
   // ofullpath and manifest_path shouldn't have the decryption or decompression suffixes
   memset(ofullpath, 0, MAX_PATH_CONCAT);
   memset(manifest_path, 0, MAX_PATH_CONCAT);

   blocksz = config->common.servers[server].block_size;

   pgmoneta_deque_create(false, &sources);

   // either bare_file_name nor base_file_name contains the incremental prefix
   file_base_name(bare_file_name, &base_file_name);

   // Note that we are working directly on backup archive, so bare file name could include compression/encryption suffix
   // and bare file name is alway stripped from the INCREMENTAL. prefix
   memset(incr_file_name, 0, MAX_PATH);
   snprintf(incr_file_name, MAX_PATH, "%s%s", INCREMENTAL_PREFIX, base_file_name);
   // handle the latest file specially, it is the only file that can only be incremental
   bck = (struct backup*)pgmoneta_art_search(backups, label);
   if (pgmoneta_incremental_rfile_initialize(server, label, relative_dir, incr_file_name, bck->encryption, bck->compression, &latest_source))
   {
      goto error;
   }

   // The key insight is that the blocks are always consecutive.
   // Blocks deleted but not vacuumed are treated as modified.
   // Vacuum will move data around, rearrange free spaces
   // so that there's no void in the middle (also leading
   // to some blocks getting modified), and then
   // if a block is the new limit block will be updated
   block_length = find_reconstructed_block_length(latest_source);
   pgmoneta_deque_add_with_config(sources, NULL, (uintptr_t)latest_source, &rfile_config);

   source_map = malloc(sizeof(struct rfile*) * block_length);
   offset_map = malloc(sizeof(off_t) * block_length);

   memset(source_map, 0, sizeof(struct rfile*) * block_length);
   memset(offset_map, 0, sizeof(off_t) * block_length);

   // A block is always sourced from its latest appearance,
   // it could be in an incremental file, or a full file.
   // Blocks included in the latest incremental backup can of course
   // be sourced from there directly.
   for (uint32_t i = 0; i < latest_source->num_blocks; i++)
   {
      // the block number of blocks inside latest incr file
      b = latest_source->relative_block_numbers[i];
      if (b >= block_length)
      {
         pgmoneta_log_error("find block number %d exceeding reconstructed file size %d at file path %s%s", b, block_length, relative_dir, bare_file_name);
         goto error;
      }
      source_map[b] = latest_source;
      offset_map[b] = latest_source->header_length + (i * blocksz);

      // some blocks have been modified,
      // so cannot just copy the file from the prior full backup over
      full_copy_possible = false;
   }

   // Go over all source files and try finding the source block for each block number,
   // starting from the latest. Any block can date back to as far as the latest full file.
   // There could be blocks that cannot be sourced. This is probably because the block gets truncated
   // during the backup process before it gets backed up. In this case just zero fill the block later,
   // the WAL replay will fix the inconsistency since it's getting truncated in the first place.
   pgmoneta_deque_iterator_create(prior_labels, &label_iter);
   while (pgmoneta_deque_iterator_next(label_iter))
   {
      struct rfile* rf = NULL;

      prior_label = (char*)pgmoneta_value_data(label_iter->value);
      bck = (struct backup*)pgmoneta_art_search(backups, prior_label);
      // try finding the full or incremental file, we need to try
      // 1. base name (without compression/encryption suffix, nor incremental prefix)
      // 2. final base name (with compression/encryption suffix, no incremental prefix)
      // 3. base incr name (without compression/encryption suffix, with incremental prefix)
      // 4. final incr name (with compression/encryption suffix, and incremental prefix)
      if (pgmoneta_rfile_create(server, prior_label, relative_dir, base_file_name, bck->encryption, bck->compression, &rf))
      {
         if (pgmoneta_incremental_rfile_initialize(server, prior_label, relative_dir, incr_file_name, bck->encryption, bck->compression, &rf))
         {
            goto error;
         }
      }
      pgmoneta_deque_add_with_config(sources, NULL, (uintptr_t)rf, &rfile_config);

      // If it's a full file, all blocks not sourced yet can be sourced from it.
      // And then we are done, no need to go further back.
      if (is_full_file(rf))
      {
         full_file_found = true;
         // would be nice if we could check if stat fails
         file_size = pgmoneta_get_file_size(rf->filepath);
         nblocks = file_size / blocksz;

         // no need to check for blocks beyond truncation_block_length
         // since those blocks should have been truncated away anyway,
         // we just need to zero fill them later.
         for (b = 0; b < latest_source->truncation_block_length; b++)
         {
            if (source_map[b] == NULL && b < nblocks)
            {
               source_map[b] = rf;
               offset_map[b] = b * blocksz;
            }
         }

         // full_copy_possible only remains true when there are no modified blocks in later incremental files,
         // which means the file has probably never been modified since last full backup.
         // But it still could've gotten truncated, so check the file size.
         if (full_copy_possible && file_size == block_length * blocksz)
         {
            copy_source = rf;
         }

         break;
      }
      // as for an incremental file, source blocks we don't have yet from it
      for (uint32_t i = 0; i < rf->num_blocks; i++)
      {
         b = rf->relative_block_numbers[i];
         // only the latest source may contain blocks exceeding the latest truncation block length
         // as for the rest...
         if (b >= latest_source->truncation_block_length || source_map[b] != NULL)
         {
            continue;
         }
         source_map[b] = rf;
         offset_map[b] = rf->header_length + (i * blocksz);
         full_copy_possible = false;
      }
   }

   // non-incremental combine must have a full file
   if (!full_file_found && !incremental)
   {
      pgmoneta_log_error("reconstruct: unable to find full file %s for backup %s", ofullpath, label);
      goto error;
   }

   if (full_file_found)
   {
      snprintf(ofullpath, MAX_PATH_CONCAT, "%s/%s", output_dir, base_file_name);
      snprintf(manifest_path, MAX_PATH_CONCAT, "%s%s", relative_dir, base_file_name);
   }
   else
   {
      snprintf(ofullpath, MAX_PATH_CONCAT, "%s/%s%s", output_dir, INCREMENTAL_PREFIX, base_file_name);
      snprintf(manifest_path, MAX_PATH_CONCAT, "%s%s%s", relative_dir, INCREMENTAL_PREFIX, base_file_name);
   }

   if (copy_source != NULL)
   {
      if (pgmoneta_copy_file(copy_source->filepath, ofullpath, NULL))
      {
         pgmoneta_log_error("reconstruct: fail to copy file from %s to %s", copy_source->filepath, ofullpath);
         goto error;
      }
   }
   else
   {
      if (full_file_found)
      {
         if (write_reconstructed_file_full(ofullpath, block_length, source_map, offset_map, blocksz))
         {
            pgmoneta_log_error("reconstruct: fail to write reconstructed full file at %s", ofullpath);
            goto error;
         }
      }
      else
      {
         if (write_reconstructed_file_incremental(ofullpath, block_length, source_map, latest_source, offset_map, blocksz))
         {
            pgmoneta_log_error("reconstruct: fail to write reconstructed incremental file at %s", ofullpath);
            goto error;
         }
      }
   }

   // Update file entry in manifest
   if (pgmoneta_get_file_manifest(ofullpath, manifest_path, &file))
   {
      pgmoneta_log_error("Unable to get manifest for file %s", ofullpath);
      goto error;
   }
   else
   {
      pgmoneta_json_append(files, (uintptr_t)file, ValueJSON);
   }

   pgmoneta_deque_destroy(sources);
   pgmoneta_deque_iterator_destroy(label_iter);
   free(source_map);
   free(offset_map);
   free(base_file_name);
   return 0;
error:
   pgmoneta_deque_destroy(sources);
   pgmoneta_deque_iterator_destroy(label_iter);
   free(source_map);
   free(offset_map);
   free(base_file_name);
   return 1;
}

static int
copy_backup_file(int server,
                 char* label,
                 char* output_dir,
                 char* relative_dir,
                 char* file_name,
                 bool exclude)
{
   bool excluded = false;
   char ofullpath[MAX_PATH_CONCAT];
   char manifest_path[MAX_PATH_CONCAT];
   char* extracted_file_path = NULL;
   char* base_file_name = NULL;
   int excluded_files = 0;

#ifdef DEBUG
   assert(file_name != NULL);
   assert(!pgmoneta_starts_with(file_name, INCREMENTAL_PREFIX));
#endif

   excluded_files = sizeof(restore_last_files_names) / sizeof(restore_last_files_names[0]);

   memset(ofullpath, 0, MAX_PATH_CONCAT);
   memset(manifest_path, 0, MAX_PATH_CONCAT);
   // copy the full file from input dir to output dir
   // extract before copy
   snprintf(manifest_path, MAX_PATH_CONCAT, "%s%s", relative_dir, file_name);
   if (pgmoneta_extract_backup_file(server, label, manifest_path, NULL, &extracted_file_path))
   {
      goto error;
   }

   for (int i = 0; i < excluded_files; i++)
   {
      if (pgmoneta_ends_with(extracted_file_path, restore_last_files_names[i]))
      {
         pgmoneta_log_debug("combine_backup_recursive: exclude %s", manifest_path);
         excluded = true;
      }
   }

   file_base_name(file_name, &base_file_name);

   if (excluded && exclude)
   {
      snprintf(ofullpath, MAX_PATH_CONCAT, "%s/%s%s", output_dir, base_file_name, TMP_SUFFIX);
   }
   else
   {
      snprintf(ofullpath, MAX_PATH_CONCAT, "%s/%s", output_dir, base_file_name);
   }

   pgmoneta_copy_file(extracted_file_path, ofullpath, NULL);

   pgmoneta_delete_file(extracted_file_path, NULL);
   free(extracted_file_path);
   extracted_file_path = NULL;

   free(base_file_name);
   free(extracted_file_path);
   return 0;

error:
   if (extracted_file_path != NULL)
   {
      pgmoneta_delete_file(extracted_file_path, NULL);
   }
   free(extracted_file_path);
   free(base_file_name);
   return 1;
}

static uint32_t
find_reconstructed_block_length(struct rfile* s)
{
   uint32_t block_length = 0;
   if (s == NULL)
   {
      return 0;
   }
   block_length = s->truncation_block_length;
   for (uint32_t i = 0; i < s->num_blocks; i++)
   {
      if (s->relative_block_numbers[i] >= block_length)
      {
         block_length = s->relative_block_numbers[i] + 1;
      }
   }

   return block_length;
}

static void
rfile_destroy_cb(uintptr_t data)
{
   pgmoneta_rfile_destroy((struct rfile*)data);
}

static bool
is_full_file(struct rfile* rf)
{
   if (rf == NULL)
   {
      return false;
   }
   return rf->header_length == 0;
}

static int
read_block(struct rfile* rf, off_t offset, uint32_t blocksz, uint8_t* buffer)
{
   uint32_t nread = 0;
   if (fseek(rf->fp, offset, SEEK_SET))
   {
      pgmoneta_log_error("unable to locate file pointer to offset %llu in file %s", offset, rf->filepath);
      goto error;
   }

   nread = fread(buffer, 1, blocksz, rf->fp);
   if (nread != blocksz)
   {
      pgmoneta_log_error("unable to read block at offset %llu from file %s", offset, rf->filepath);
      goto error;
   }

   return 0;
error:
   return 1;
}

static int
write_reconstructed_file_full(char* output_file_path,
                              uint32_t block_length,
                              struct rfile** source_map,
                              off_t* offset_map,
                              uint32_t blocksz)
{
   FILE* wfp = NULL;
   uint8_t buffer[blocksz];
   struct rfile* s = NULL;

   wfp = fopen(output_file_path, "wb+");
   if (wfp == NULL)
   {
      pgmoneta_log_error("reconstruct: unable to open file for reconstruction at %s", output_file_path);
      goto error;
   }
   for (uint32_t i = 0; i < block_length; i++)
   {
      memset(buffer, 0, blocksz);
      s = source_map[i];
      if (s == NULL)
      {
         // zero fill the block since source doesn't exist
         memset(buffer, 0, blocksz);
         if (fwrite(buffer, 1, blocksz, wfp) != blocksz)
         {
            pgmoneta_log_error("reconstruct: fail to write to file %s", output_file_path);
            goto error;
         }
      }
      else
      {
         // we might be able to use copy_file_range to have faster copy,
         // but for now let's stay in user space
         if (read_block(s, offset_map[i], blocksz, buffer))
         {
            goto error;
         }
         if (fwrite(buffer, 1, blocksz, wfp) != blocksz)
         {
            pgmoneta_log_error("reconstruct: fail to write to file %s", output_file_path);
            goto error;
         }
      }
   }
   if (wfp != NULL)
   {
      fclose(wfp);
   }
   return 0;
error:
   if (wfp != NULL)
   {
      fclose(wfp);
   }
   return 1;
}

static int
write_reconstructed_file_incremental(char* output_file_path,
                                     uint32_t block_length,
                                     struct rfile** source_map,
                                     struct rfile* latest_source,
                                     off_t* offset_map,
                                     uint32_t blocksz)
{
   FILE* wfp = NULL;
   size_t hdrlen = 0;
   size_t hdrptr = 0;
   uint8_t buffer[blocksz];
   uint32_t num_blocks = 0;
   uint32_t idx = 0;
   void* header = NULL;
   uint32_t magic = INCREMENTAL_MAGIC;
   struct rfile* s = NULL;

   pgmoneta_log_debug("reconstruct incremental file %s", output_file_path);

   // build header
   for (uint32_t i = 0; i < block_length; i++)
   {
      if (source_map[i] != NULL)
      {
         num_blocks++;
      }
   }
   hdrlen = sizeof(uint32_t) * (1 + 1 + 1 + num_blocks);
   if (num_blocks > 0 && hdrlen % blocksz != 0)
   {
      hdrlen += (blocksz - (hdrlen % blocksz));
   }

   header = malloc(hdrlen);
   memset(header, 0, hdrlen);

   memcpy(header + hdrptr, &magic, sizeof(uint32_t));
   hdrptr += sizeof(uint32_t);

   memcpy(header + hdrptr, &num_blocks, sizeof(uint32_t));
   hdrptr += sizeof(uint32_t);

   memcpy(header + hdrptr, &latest_source->truncation_block_length, sizeof(uint32_t));
   hdrptr += sizeof(uint32_t);

   for (idx = 0; idx < block_length; idx++)
   {
      if (source_map[idx] != NULL)
      {
         memcpy(header + hdrptr, &idx, sizeof(idx));
         hdrptr += sizeof(idx);
      }
   }

   wfp = fopen(output_file_path, "wb+");
   if (wfp == NULL)
   {
      pgmoneta_log_error("reconstruct: unable to open file for reconstruction at %s", output_file_path);
      goto error;
   }

   if (fwrite(header, 1, hdrlen, wfp) != hdrlen)
   {
      pgmoneta_log_error("reconstruct: fail to write header to file %s", output_file_path);
      goto error;
   }

   for (uint32_t i = 0; i < block_length; i++)
   {
      memset(buffer, 0, blocksz);
      s = source_map[i];
      if (s != NULL)
      {
         // we might be able to use copy_file_range to have faster copy,
         // but for now let's stay in user space
         if (read_block(s, offset_map[i], blocksz, buffer))
         {
            goto error;
         }
         if (fwrite(buffer, 1, blocksz, wfp) != blocksz)
         {
            pgmoneta_log_error("reconstruct: fail to write to file %s", output_file_path);
            goto error;
         }
      }
   }

   free(header);
   if (wfp != NULL)
   {
      fclose(wfp);
   }
   return 0;

error:
   free(header);
   if (wfp != NULL)
   {
      fclose(wfp);
   }
   return 1;
}

static int
write_backup_label(char* from_dir, char* to_dir, char* lsn_entry, char* tli_entry)
{
   char from_path[MAX_PATH];
   char to_path[MAX_PATH];
   char row[MISC_LENGTH];
   FILE* from = NULL;
   FILE* to = NULL;

   memset(from_path, 0, MAX_PATH);
   memset(to_path, 0, MAX_PATH);
   memset(row, 0, MISC_LENGTH);

   snprintf(from_path, MAX_PATH, "%s/backup_label", from_dir);
   snprintf(to_path, MAX_PATH, "%s/backup_label", to_dir);

   from = fopen(from_path, "r");
   to = fopen(to_path, "w");

   if (from == NULL)
   {
      pgmoneta_log_error("Write backup label, could not open %s", from_path);
      goto error;
   }

   if (to == NULL)
   {
      pgmoneta_log_error("Write backup label, could not open %s", to_path);
      goto error;
   }

   while (fgets(row, MISC_LENGTH, from) != NULL)
   {
      if (!pgmoneta_starts_with(row, "INCREMENTAL FROM LSN: ") &&
          !pgmoneta_starts_with(row, "INCREMENTAL FROM TLI: "))
      {
         if (fputs(row, to) == EOF)
         {
            pgmoneta_log_error("Write backup label, could not write to file %s", to_path);
            goto error;
         }
      }
      memset(row, 0, MISC_LENGTH);
   }
   //update lsn and tli to the ones of the earliest backup if provided (because the backup is still incremental)
   if (lsn_entry != NULL && tli_entry != NULL)
   {
      if (fputs(lsn_entry, to) == EOF)
      {
         pgmoneta_log_error("Write backup label, could not write to file %s", to_path);
         goto error;
      }
      if (fputs(tli_entry, to) == EOF)
      {
         pgmoneta_log_error("Write backup label, could not write to file %s", to_path);
         goto error;
      }
   }

   fclose(from);
   fclose(to);
   return 0;
error:
   if (from != NULL)
   {
      fclose(from);
   }
   if (to != NULL)
   {
      fclose(to);
   }
   return 1;
}

static int
write_backup_label_incremental(int server, char* oldest_label, char* from_dir, char* to_dir)
{
   char* path = NULL;
   char buf[MISC_LENGTH];
   char lsn_entry[MISC_LENGTH];
   char tli_entry[MISC_LENGTH];
   FILE* fp = NULL;
   path = pgmoneta_get_server_backup_identifier_data(server, oldest_label);
   path = pgmoneta_append(path, "backup_label");
   fp = fopen(path, "r");
   if (fp == NULL)
   {
      pgmoneta_log_error("Unable to open backup label %s", path);
      goto error;
   }

#ifdef DEBUG
   assert(sizeof(lsn_entry) == sizeof(buf));
   assert(sizeof(tli_entry) == sizeof(buf));
#endif

   memset(buf, 0, sizeof(buf));
   memset(lsn_entry, 0, sizeof(lsn_entry));
   memset(tli_entry, 0, sizeof(tli_entry));

   while (fgets(buf, MISC_LENGTH, fp) != NULL)
   {
      if (pgmoneta_starts_with(buf, "INCREMENTAL FROM LSN: "))
      {
         memcpy(lsn_entry, buf, sizeof(buf));
      }
      else if (pgmoneta_starts_with(buf, "INCREMENTAL FROM TLI: "))
      {
         memcpy(tli_entry, buf, sizeof(buf));
      }
      memset(buf, 0, MISC_LENGTH);
   }

   if (strlen(lsn_entry) == 0 || strlen(tli_entry) == 0)
   {
      pgmoneta_log_error("Unable to find FROM LSN or FROM TLI entry inside %s", path);
      goto error;
   }

   if (write_backup_label(from_dir, to_dir, lsn_entry, tli_entry))
   {
      goto error;
   }

   fclose(fp);
   free(path);
   return 0;

error:
   if (fp != NULL)
   {
      fclose(fp);
   }
   free(path);
   return 1;
}

static uint32_t
parse_oid(char* name)
{
   uint64_t oid = 0;
   char* ep = NULL; //pointer to the ending char
   errno = 0;
   if (name == NULL)
   {
      goto error;
   }
   oid = strtoul(name, &ep, 10);
   // check for overflow or premature return from parsing
   if (errno != 0 || *ep != '\0' || oid == 0 || oid > UINT32_MAX)
   {
      pgmoneta_log_error("Unable to parse oid %s", name);
      goto error;
   }
   return oid;
error:
   errno = 0;
   return 0;
}

static void
clear_manifest_incremental_entries(struct json* manifest)
{
   struct json* files = NULL;
   struct json* f = NULL;
   char* path = NULL;
   struct json_iterator* iter = NULL;
   struct deque_iterator* diter = NULL;
   if (manifest == NULL)
   {
      return;
   }
   files = (struct json*)pgmoneta_json_get(manifest, MANIFEST_FILES);
   if (files == NULL)
   {
      return;
   }
   pgmoneta_json_iterator_create(files, &iter);
   // a tiny hack to get the internal deque iterator of the json iterator
   diter = (struct deque_iterator*)iter->iter;
   while (pgmoneta_deque_iterator_next(diter))
   {
      f = (struct json*)pgmoneta_value_data(diter->value);
      path = (char*)pgmoneta_json_get(f, "Path");
      if (pgmoneta_is_incremental_path(path))
      {
         pgmoneta_deque_iterator_remove(diter);
      }
   }
   pgmoneta_json_iterator_destroy(iter);
}

static int
restore_backup_full(struct art* nodes)
{
   int ret = RESTORE_OK;
   int server = -1;
   char* directory = NULL;
   struct backup* backup = NULL;
   char* target_root = NULL;
   char* target_base = NULL;
   uint64_t free_space = 0;
   uint64_t required_space = 0;
   struct main_configuration* config;

   struct workflow* workflow = NULL;

   config = (struct main_configuration*)shmem;

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   directory = (char*)pgmoneta_art_search(nodes, USER_DIRECTORY);
   backup = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);

   target_root = pgmoneta_append(target_root, directory);
   target_base = pgmoneta_append(target_base, directory);
   if (!pgmoneta_ends_with(target_base, "/"))
   {
      target_base = pgmoneta_append(target_base, "/");
   }
   target_base = pgmoneta_append(target_base, config->common.servers[server].name);
   target_base = pgmoneta_append(target_base, "-");
   target_base = pgmoneta_append(target_base, backup->label);
   target_base = pgmoneta_append(target_base, "/");

   if (!pgmoneta_exists(target_root))
   {
      if (pgmoneta_mkdir(target_root))
      {
         pgmoneta_log_error("Unable to create target root directory %s", target_root);
         goto error;
      }
   }

   if (!pgmoneta_exists(target_base))
   {
      if (pgmoneta_mkdir(target_base))
      {
         pgmoneta_log_error("Unable to create target base directory %s", target_base);
         goto error;
      }
   }

   pgmoneta_art_insert(nodes, NODE_TARGET_ROOT, (uintptr_t)target_root, ValueString);
   pgmoneta_art_insert(nodes, NODE_TARGET_BASE, (uintptr_t)target_base, ValueString);

   if (pgmoneta_log_is_enabled(PGMONETA_LOGGING_LEVEL_DEBUG5))
   {
      pgmoneta_log_trace("Restore: Used space is %lld for %s", pgmoneta_directory_size(target_root), target_root);
      pgmoneta_log_trace("Restore: Free space is %lld for %s", pgmoneta_free_space(target_root), target_root);
      pgmoneta_log_trace("Restore: Total space is %lld for %s", pgmoneta_total_space(target_root), target_root);
   }

   free_space = pgmoneta_free_space(target_root);
   required_space =
      backup->restore_size + (pgmoneta_get_number_of_workers(server) * backup->biggest_file_size);

   if (free_space < required_space)
   {
      char* f = NULL;
      char* r = NULL;

      f = pgmoneta_translate_file_size(free_space);
      r = pgmoneta_translate_file_size(required_space);

      pgmoneta_log_error("Restore: Not enough disk space for %s/%s on %s (Available: %s, Required: %s)",
                         config->common.servers[server].name, backup->label, target_root, f, r);

      free(f);
      free(r);

      ret = RESTORE_NO_DISK_SPACE;
      goto error;
   }

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_RESTORE, backup);
   if ((ret = carry_out_workflow(workflow, nodes) != RESTORE_OK))
   {
      goto error;
   }

   free(target_root);
   free(target_base);

   pgmoneta_workflow_destroy(workflow);
   return RESTORE_OK;

error:
   free(target_root);
   free(target_base);

   pgmoneta_workflow_destroy(workflow);
   return ret;
}

static int
restore_backup_incremental(struct art* nodes)
{
   int ret = RESTORE_OK;
   int server = -1;
   bool combine_as_is = false;
   char* directory = NULL;
   struct backup* backup = NULL;
   struct deque* labels = NULL;
   char target_root_combine[MAX_PATH];
   char target_base_combine[MAX_PATH_CONCAT];
   char excluded_file_path[MAX_PATH_CONCAT];
   char tmp_excluded_file_path[MAX_PATH_CONCAT + sizeof(TMP_SUFFIX)];
   int excluded_files = 0;
   char* manifest_path = NULL;
   struct json* manifest = NULL;
   struct workflow* workflow = NULL;
   struct main_configuration* config;
   uint64_t free_space = 0;
   uint64_t required_space = 0;

   config = (struct main_configuration*)shmem;

   memset(target_root_combine, 0, MAX_PATH);
   memset(target_base_combine, 0, MAX_PATH_CONCAT);

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   directory = (char*)pgmoneta_art_search(nodes, USER_DIRECTORY);
   backup = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);
   combine_as_is = (bool)pgmoneta_art_search(nodes, NODE_COMBINE_AS_IS);
   labels = (struct deque*)pgmoneta_art_search(nodes, NODE_LABELS);

   if (labels == NULL)
   {
      pgmoneta_log_error("restore_backup_incremental: missing backup labels");
      goto error;
   }

   snprintf(target_root_combine, MAX_PATH, "%s", directory);
   if (!combine_as_is)
   {
      snprintf(target_base_combine, MAX_PATH_CONCAT, "%s/%s-%s", directory, config->common.servers[server].name, backup->label);
   }
   else
   {
      snprintf(target_base_combine, MAX_PATH_CONCAT, "%s/data", directory);
   }

   manifest_path = pgmoneta_get_server_backup_identifier_data(server, backup->label);
   manifest_path = pgmoneta_append(manifest_path, "backup_manifest");
   // read the manifest for later usage
   if (pgmoneta_json_read_file(manifest_path, &manifest))
   {
      goto error;
   }
   pgmoneta_art_insert(nodes, NODE_MANIFEST, (uintptr_t)manifest, ValueJSON);

   if (!pgmoneta_exists(target_root_combine))
   {
      if (pgmoneta_mkdir(target_root_combine))
      {
         pgmoneta_log_error("Unable to create target root directory %s", target_root_combine);
         goto error;
      }
   }

   free_space = pgmoneta_free_space(target_root_combine);
   required_space =
      backup->restore_size + (pgmoneta_get_number_of_workers(server) * backup->biggest_file_size);

   if (free_space < required_space)
   {
      char* f = NULL;
      char* r = NULL;

      f = pgmoneta_translate_file_size(free_space);
      r = pgmoneta_translate_file_size(required_space);

      pgmoneta_log_error("Restore: Not enough disk space for %s/%s on %s (Available: %s, Required: %s)",
                         config->common.servers[server].name, backup->label, target_root_combine, f, r);

      free(f);
      free(r);

      ret = RESTORE_NO_DISK_SPACE;
      goto error;
   }

   pgmoneta_art_insert(nodes, NODE_TARGET_ROOT, (uintptr_t)target_root_combine, ValueString);
   pgmoneta_art_insert(nodes, NODE_TARGET_BASE, (uintptr_t)target_base_combine, ValueString);

   if (!combine_as_is)
   {
      workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_COMBINE, backup);
   }
   else
   {
      workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_COMBINE_AS_IS, backup);
   }

   if ((ret = carry_out_workflow(workflow, nodes) != RESTORE_OK))
   {
      goto error;
   }

   // rename the excluded files
   if (!combine_as_is)
   {
      excluded_files = sizeof(restore_last_files_names) / sizeof(restore_last_files_names[0]);
      for (int i = 0; i < excluded_files; i++)
      {
         memset(excluded_file_path, 0, MAX_PATH_CONCAT);
         memset(tmp_excluded_file_path, 0, MAX_PATH_CONCAT);
         snprintf(tmp_excluded_file_path, sizeof(tmp_excluded_file_path), "%s%s%s", target_base_combine, restore_last_files_names[i], TMP_SUFFIX);
         snprintf(excluded_file_path, MAX_PATH_CONCAT, "%s%s", target_base_combine, restore_last_files_names[i]);

         if (rename(tmp_excluded_file_path, excluded_file_path) != 0)
         {
            pgmoneta_log_error("restore_backup_incremental: could not rename file %s to %s",
                               tmp_excluded_file_path, excluded_file_path);
            goto error;
         }
         pgmoneta_log_debug("restore_backup_incremental: rename file %s to %s", tmp_excluded_file_path, excluded_file_path);
      }
   }

   pgmoneta_delete_server_workspace(server, (char*)pgmoneta_art_search(nodes, NODE_LABEL));
   cleanup_workspaces(server, labels);

   pgmoneta_workflow_destroy(workflow);
   workflow = NULL;

   free(manifest_path);
   return RESTORE_OK;

error:
   pgmoneta_delete_server_workspace(server, (char*)pgmoneta_art_search(nodes, NODE_LABEL));
   cleanup_workspaces(server, labels);

   pgmoneta_delete_directory(target_base_combine);
   // purge each table space
   for (uint32_t i = 0; i < backup->number_of_tablespaces; i++)
   {
      char tblspc[MAX_PATH];
      memset(tblspc, 0, MAX_PATH);
      if (combine_as_is)
      {
         snprintf(tblspc, MAX_PATH, "%s/%s", directory, backup->tablespaces[i]);
      }
      else
      {
         snprintf(tblspc, MAX_PATH, "%s/%s-%s-%s", directory,
                  config->common.servers[server].name, backup->label,
                  backup->tablespaces[i]);
      }
      if (pgmoneta_exists(tblspc))
      {
         pgmoneta_delete_directory(tblspc);
      }
   }

   free(manifest_path);
   pgmoneta_workflow_destroy(workflow);
   // set ret error code by default to RESTORE_ERROR if it's not set
   if (ret == RESTORE_OK)
   {
      ret = RESTORE_ERROR;
   }
   return ret;
}

static int
carry_out_workflow(struct workflow* workflow, struct art* nodes)
{
   struct workflow* current = NULL;
   int ret = RESTORE_OK;
   current = workflow;
   while (current != NULL)
   {
      if (current->setup(current->name(), nodes))
      {
         ret = RESTORE_MISSING_LABEL;
         pgmoneta_log_error("setup/%s", current->name());
         goto error;
      }
      current = current->next;
   }

   current = workflow;
   while (current != NULL)
   {
      if (current->execute(current->name(), nodes))
      {
         ret = RESTORE_MISSING_LABEL;
         pgmoneta_log_error("execute/%s", current->name());
         goto error;
      }
      current = current->next;
   }

   current = workflow;
   while (current != NULL)
   {
      if (current->teardown(current->name(), nodes))
      {
         ret = RESTORE_MISSING_LABEL;
         pgmoneta_log_error("teardown/%s", current->name());
         goto error;
      }
      current = current->next;
   }

   return ret;
error:
   return ret;
}

static void
cleanup_workspaces(int server, struct deque* labels)
{
   struct deque_iterator* iter = NULL;

   if (labels == NULL)
   {
      return;
   }

   pgmoneta_deque_iterator_create(labels, &iter);
   while (pgmoneta_deque_iterator_next(iter))
   {
      pgmoneta_delete_server_workspace(server, (char*)pgmoneta_value_data(iter->value));
   }
   pgmoneta_deque_iterator_destroy(iter);
}

static int
construct_backup_label_chain(int server, char* newest_label, char* oldest_label, bool inclusive, struct deque** labels)
{
   struct backup* bck = NULL;
   char* server_dir = NULL;
   char label[MISC_LENGTH];
   struct deque* l = NULL;

   if (pgmoneta_compare_string(newest_label, oldest_label))
   {
      pgmoneta_log_error("newest label cannot be the same as oldest_label %s", newest_label);
      goto error;
   }

   server_dir = pgmoneta_get_server_backup(server);
   pgmoneta_deque_create(false, &l);

   pgmoneta_load_info(server_dir, newest_label, &bck);
   if (bck == NULL)
   {
      pgmoneta_log_error("Unable to find backup %s", newest_label);
      goto error;
   }

   if (inclusive)
   {
      pgmoneta_deque_add(l, NULL, (uintptr_t)bck->label, ValueString);
   }

   /*
    * 4 cases: 1.oldest label isn't specified, full backup is not found, then the chain is breaking due to missing full backup
    * 2. oldest label isn't specified, full backup is found, then we are restoring into full backup
    * 3. oldest label is specified, oldest backup is not found, then the oldest backup is not in the chain
    * 4. oldest label is specified, oldest backup is found, then the oldest backup's type determines what we are restoring into
    */
   if (oldest_label == NULL)
   {
      while (bck->type != TYPE_FULL)
      {
         memset(label, 0, MISC_LENGTH);
         memcpy(label, bck->parent_label, sizeof(bck->parent_label));
         free(bck);
         bck = NULL;

         pgmoneta_load_info(server_dir, label, &bck);

         if (bck == NULL)
         {
            pgmoneta_log_error("Unable to find backup %s", label);
            goto error;
         }

         pgmoneta_deque_add(l, NULL, (uintptr_t)label, ValueString);
      }
   }
   else
   {
      while (!pgmoneta_compare_string(bck->label, oldest_label))
      {
         memset(label, 0, MISC_LENGTH);
         memcpy(label, bck->parent_label, sizeof(bck->parent_label));
         free(bck);
         bck = NULL;

         pgmoneta_load_info(server_dir, label, &bck);

         if (bck == NULL)
         {
            pgmoneta_log_error("Unable to find backup %s", label);
            goto error;
         }

         pgmoneta_deque_add(l, NULL, (uintptr_t)label, ValueString);
      }
   }

   *labels = l;

   free(bck);
   free(server_dir);
   return 0;

error:
   free(bck);
   free(server_dir);
   pgmoneta_deque_destroy(l);
   return 1;
}

static void
create_workspace_directory(int server, char* label, char* relative_prefix)
{
   char* dir = NULL;
   dir = pgmoneta_get_server_workspace(server);
   dir = pgmoneta_append(dir, label);
   dir = pgmoneta_append(dir, "/");
   if (relative_prefix != NULL)
   {
      dir = pgmoneta_append(dir, relative_prefix);
   }

   if (!pgmoneta_exists(dir))
   {
      pgmoneta_mkdir(dir);
   }
   free(dir);
}

static void
create_workspace_directories(int server, struct deque* labels, char* relative_prefix)
{
   struct deque_iterator* iter = NULL;
   pgmoneta_deque_iterator_create(labels, &iter);
   while (pgmoneta_deque_iterator_next(iter))
   {
      create_workspace_directory(server, (char*)pgmoneta_value_data(iter->value), relative_prefix);
   }
   pgmoneta_deque_iterator_destroy(iter);
}

static void
do_reconstruct_backup_file(struct worker_common* wc)
{
   struct build_backup_file_input* input = (struct build_backup_file_input*)wc;
   if (reconstruct_backup_file(input->server,
                               input->label,
                               input->output_dir,
                               input->relative_dir,
                               input->file_name,
                               input->prior_labels,
                               input->backups,
                               input->incremental,
                               input->files))
   {
      goto error;
   }
   input->common.workers->outcome = true;
   free(input);
   return;

error:
   pgmoneta_log_error("Unable to construct file %s/%s", input->label, input->relative_dir, input->file_name);
   input->common.workers->outcome = false;
   free(input);
   return;
}

static void
do_copy_backup_file(struct worker_common* wc)
{
   struct build_backup_file_input* input = (struct build_backup_file_input*)wc;
   if (copy_backup_file(input->server,
                        input->label,
                        input->output_dir,
                        input->relative_dir,
                        input->file_name,
                        input->exclude))
   {
      goto error;
   }
   free(input);
   return;

error:
   pgmoneta_log_error("Unable to construct file %s/%s", input->label, input->relative_dir, input->file_name);
   input->common.workers->outcome = false;
   free(input);
   return;
}

static void
create_reconstruct_backup_file_input(int server,
                                     char* label,
                                     char* output_dir,
                                     char* relative_dir,
                                     char* bare_file_name,
                                     struct deque* prior_labels,
                                     struct art* backups,
                                     bool incremental,
                                     struct json* files,
                                     struct workers* workers,
                                     struct build_backup_file_input** wi)
{
   struct build_backup_file_input* input = NULL;
   input = (struct build_backup_file_input*)malloc(sizeof(struct build_backup_file_input));
   memset(input, 0, sizeof(struct build_backup_file_input));
   input->common.workers = workers;
   input->server = server;
   memcpy(input->label, label, strlen(label));
   memcpy(input->output_dir, output_dir, strlen(output_dir));
   memcpy(input->relative_dir, relative_dir, strlen(relative_dir));
   memcpy(input->file_name, bare_file_name, strlen(bare_file_name));
   input->prior_labels = prior_labels;
   input->backups = backups;
   input->incremental = incremental;
   input->files = files;
   *wi = input;
}

static void
create_copy_backup_file_input(
   int server,
   char* label,
   char* output_dir,
   char* relative_dir,
   char* file_name,
   bool exclude,
   struct workers* workers,
   struct build_backup_file_input** wi)
{
   struct build_backup_file_input* input = NULL;
   input = (struct build_backup_file_input*)malloc(sizeof(struct build_backup_file_input));
   memset(input, 0, sizeof(struct build_backup_file_input));
   input->common.workers = workers;
   input->server = server;
   memcpy(input->label, label, strlen(label));
   memcpy(input->output_dir, output_dir, strlen(output_dir));
   memcpy(input->relative_dir, relative_dir, strlen(relative_dir));
   memcpy(input->file_name, file_name, strlen(file_name));
   input->exclude = exclude;
   *wi = input;
}

static int
file_base_name(char* file, char** basename)
{
   char* b = NULL;

   *basename = NULL;
   if (file == NULL)
   {
      goto error;
   }

   b = pgmoneta_append(b, file);
   if (pgmoneta_is_encrypted(b))
   {
      char* new_b = NULL;

      if (pgmoneta_strip_extension(b, &new_b))
      {
         goto error;
      }

      free(b);
      b = new_b;
   }

   if (pgmoneta_is_compressed(b))
   {
      char* new_b = NULL;

      if (pgmoneta_strip_extension(b, &new_b))
      {
         goto error;
      }

      free(b);
      b = new_b;
   }

   *basename = b;
   return 0;

error:
   free(b);
   return 1;
}

static int
copy_tablespaces_restore(char* from, char* to, char* base, char* server, char* id, struct backup* backup, struct workers* workers)
{
   char* from_tblspc = NULL;
   char* to_tblspc = NULL;
   int idx = -1;
   DIR* d = NULL;
   ssize_t size;
   struct dirent* entry;

   from_tblspc = pgmoneta_append(from_tblspc, from);
   if (!pgmoneta_ends_with(from_tblspc, "/"))
   {
      from_tblspc = pgmoneta_append(from_tblspc, "/");
   }
   from_tblspc = pgmoneta_append(from_tblspc, "pg_tblspc/");

   to_tblspc = pgmoneta_append(to_tblspc, to);
   if (!pgmoneta_ends_with(to_tblspc, "/"))
   {
      to_tblspc = pgmoneta_append(to_tblspc, "/");
   }
   to_tblspc = pgmoneta_append(to_tblspc, "pg_tblspc/");

   pgmoneta_mkdir(to_tblspc);

   if (backup->number_of_tablespaces > 0)
   {
      d = opendir(from_tblspc);

      if (d == NULL)
      {
         pgmoneta_log_error("Could not open the %s directory", from_tblspc);
         goto error;
      }

      while ((entry = readdir(d)))
      {
         char tmp_tblspc_name[MISC_LENGTH];
         char* link = NULL;
         char path[MAX_PATH];
         char* tblspc_name = NULL;

         if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
         {
            continue;
         }

         link = pgmoneta_append(link, from_tblspc);
         link = pgmoneta_append(link, entry->d_name);

         memset(&path[0], 0, sizeof(path));
         size = readlink(link, &path[0], sizeof(path));
         if (size == -1)
         {
            goto error;
         }

         if (pgmoneta_ends_with(&path[0], "/"))
         {
            memset(&tmp_tblspc_name[0], 0, sizeof(tmp_tblspc_name));
            memcpy(&tmp_tblspc_name[0], &path[0], strlen(&path[0]) - 1);

            tblspc_name = strrchr(&tmp_tblspc_name[0], '/') + 1;
         }
         else
         {
            tblspc_name = strrchr(&path[0], '/') + 1;
         }

         for (uint64_t i = 0; idx == -1 && i < backup->number_of_tablespaces; i++)
         {
            if (!strcmp(tblspc_name, backup->tablespaces[i]))
            {
               idx = i;
            }
         }

         if (idx >= 0)
         {
            char* to_oid = NULL;
            char* to_directory = NULL;
            char* relative_directory = NULL;

            pgmoneta_log_trace("Tablespace %s -> %s was found in the backup", entry->d_name, &path[0]);

            to_oid = pgmoneta_append(to_oid, to_tblspc);
            to_oid = pgmoneta_append(to_oid, entry->d_name);

            to_directory = pgmoneta_append(to_directory, base);
            to_directory = pgmoneta_append(to_directory, "/");
            to_directory = pgmoneta_append(to_directory, server);
            to_directory = pgmoneta_append(to_directory, "-");
            to_directory = pgmoneta_append(to_directory, id);
            to_directory = pgmoneta_append(to_directory, "-");
            to_directory = pgmoneta_append(to_directory, tblspc_name);
            to_directory = pgmoneta_append(to_directory, "/");

            relative_directory = pgmoneta_append(relative_directory, "../../");
            relative_directory = pgmoneta_append(relative_directory, server);
            relative_directory = pgmoneta_append(relative_directory, "-");
            relative_directory = pgmoneta_append(relative_directory, id);
            relative_directory = pgmoneta_append(relative_directory, "-");
            relative_directory = pgmoneta_append(relative_directory, tblspc_name);
            relative_directory = pgmoneta_append(relative_directory, "/");

            pgmoneta_delete_directory(to_directory);
            pgmoneta_mkdir(to_directory);
            pgmoneta_symlink_at_file(to_oid, relative_directory);

            pgmoneta_copy_directory(link, to_directory, NULL, workers);

            free(to_oid);
            free(to_directory);
            free(relative_directory);

            to_oid = NULL;
            to_directory = NULL;
         }
         else
         {
            pgmoneta_log_trace("Tablespace %s -> %s was not found in the backup", entry->d_name, &path[0]);
         }

         free(link);
         link = NULL;
      }

      closedir(d);
   }

   free(from_tblspc);
   free(to_tblspc);

   return 0;

error:

   free(from_tblspc);
   free(to_tblspc);

   return 1;
}

static int
copy_tablespaces_hotstandby(int server, char* from, char* to, char* tblspc_mappings, struct backup* backup, struct workers* workers)
{
   char* from_tblspc = NULL;
   char* to_tblspc = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   from_tblspc = pgmoneta_append(from_tblspc, from);
   if (!pgmoneta_ends_with(from_tblspc, "/"))
   {
      from_tblspc = pgmoneta_append(from_tblspc, "/");
   }
   from_tblspc = pgmoneta_append(from_tblspc, "pg_tblspc/");

   to_tblspc = pgmoneta_append(to_tblspc, to);
   if (!pgmoneta_ends_with(to_tblspc, "/"))
   {
      to_tblspc = pgmoneta_append(to_tblspc, "/");
   }
   to_tblspc = pgmoneta_append(to_tblspc, "pg_tblspc/");

   pgmoneta_mkdir(to_tblspc);

   if (backup->number_of_tablespaces > 0)
   {
      for (unsigned long i = 0; i < backup->number_of_tablespaces; i++)
      {
         char* src = NULL;
         char* dst = NULL;
         char* link = NULL;
         bool found = false;
         char* copied_tblspc_mappings = NULL;
         char* token = NULL;

         src = pgmoneta_append(src, from_tblspc);
         src = pgmoneta_append(src, backup->tablespaces_oids[i]);

         link = pgmoneta_append(link, to_tblspc);
         link = pgmoneta_append(link, backup->tablespaces_oids[i]);

         if (strcmp(tblspc_mappings, ""))
         {
            copied_tblspc_mappings = (char*)malloc(strlen(tblspc_mappings) + 1);

            if (copied_tblspc_mappings == NULL)
            {
               goto error;
            }

            memset(copied_tblspc_mappings, 0, strlen(tblspc_mappings) + 1);
            memcpy(copied_tblspc_mappings, tblspc_mappings, strlen(tblspc_mappings));

            token = strtok(copied_tblspc_mappings, ",");

            if (token == NULL)
            {
               free(copied_tblspc_mappings);
               goto error;
            }

            while (token != NULL)
            {
               char* k = NULL;
               char* v = NULL;

               k = strtok(token, "->");
               k = pgmoneta_remove_whitespace(k);
               v = strtok(NULL, "->");
               v = pgmoneta_remove_whitespace(v);

               if (!strcmp(k, backup->tablespaces_oids[i]) || !strcmp(k, backup->tablespaces_paths[i]))
               {
                  dst = pgmoneta_append(dst, v);
                  found = true;
               }

               token = strtok(NULL, ",");
               free(k);
               free(v);
            }

            free(copied_tblspc_mappings);
         }

         if (!found)
         {
            if (config->common.servers[server].number_of_hot_standbys == 1)
            {
               pgmoneta_log_info("Using default tablespace mapping for %s/%s",
                                 config->common.servers[server].name, src);
            }
            else
            {
               pgmoneta_log_warn("Using default tablespace mapping for %s/%s",
                                 config->common.servers[server].name, src);
            }

            dst = pgmoneta_append(dst, backup->tablespaces_paths[i]);
            dst = pgmoneta_append(dst, "hs");
         }

         if (!pgmoneta_exists(dst))
         {
            if (pgmoneta_mkdir(dst))
            {
               goto error;
            }
         }

         if (!pgmoneta_exists(link))
         {
            if (pgmoneta_symlink_file(link, dst))
            {
               goto error;
            }
         }

         pgmoneta_copy_directory(src, dst, NULL, workers);

         free(src);
         free(dst);
         free(link);
      }
   }

   free(from_tblspc);
   free(to_tblspc);

   return 0;

error:

   free(from_tblspc);
   free(to_tblspc);

   return 1;
}
