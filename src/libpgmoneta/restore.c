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
#include <deque.h>
#include <info.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <restore.h>
#include <security.h>
#include <string.h>
#include <utils.h>
#include <value.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define NAME "restore"
#define RESTORE_OK            0
#define RESTORE_MISSING_LABEL 1
#define RESTORE_NO_DISK_SPACE 2
#define RESTORE_TYPE_UNKNOWN  3
#define INCREMENTAL_MAGIC 0xd3ae1f0d
#define INCREMENTAL_PREFIX_LENGTH (sizeof(INCREMENTAL_PREFIX) - 1)
#define MANIFEST_FILES "Files"
#define MAX_PATH_CONCAT (MAX_PATH * 2)

/**
 * An rfile stores the metadata we need to use a file on disk for reconstruction.
 * For full backup file in the chain, only file name and file pointer are initialized.
 *
 * num_blocks is the number of blocks present inside an incremental file.
 * These are the blocks that have changed since the last checkpoint.
 * truncation_block_length is basically the shortest length this file has been between this and last checkpoint.
 * Note that truncation_block_length could be even greater than the number of blocks the original file has.
 * Because the tables are not locked during the backup, so blocks could be truncated during the process,
 * while truncation_block_length only reflects length until the checkpoint before backup starts.
 * relative_block_numbers are the relative BlockNumber of each block in the file. Relative here means relative to
 * the starting BlockNumber of this file.
 */
struct rfile
{
   char* filepath;
   FILE* fp;
   size_t header_length;
   uint32_t num_blocks;
   uint32_t* relative_block_numbers;
   uint32_t truncation_block_length;
};

static char* restore_last_files_names[] = {"/global/pg_control", "/postgresql.conf", "/pg_hba.conf"};

static int restore_backup_full(struct art* nodes);

static int restore_backup_incremental(struct art* nodes);

static int carry_out_workflow(struct workflow* workflow, struct art* nodes);

static void clear_manifest_incremental_entries(struct json* manifest);
static int get_file_manifest(char* path, char* manifest_path, int algorithm, struct json** file);
/**
 * Combine the provided backups or each of the user defined table-spaces
 * The function will be called for two rounds, the first round would construct the data directory
 * including pg_tblspc/ and the tsoid directory underneath. And the second round
 * will combine each user defined tablespaces
 * @param tsoid The table space oid, if we are reconstructing a tablespace
 * @param server The server
 * @param input_dir The base directory of the current input incremental backup
 * @param output_dir The base directory of the output incremental backup
 * @param relative_dir The internal directory relative to base directory
 * (the last level of directory should not be followed by back slash)
 * @param algorithm The manifest hash algorithm used for the backup
 * @param prior_backup_dirs The root directory of prior incremental/full backups, from newest to oldest
 * @param files The file array inside manifest of the backup
 * @return 0 on success, 1 if otherwise
 */
static int combine_backups_recursive(uint32_t tsoid,
                                     int server,
                                     char* input_dir,
                                     char* output_dir,
                                     char* relative_dir,
                                     int algorithm,
                                     struct deque* prior_backup_dirs,
                                     struct json* files);

/**
 * Reconstruct an incremental backup file from itself and its prior incremental/full backup files to a full backup file
 * @param server The server
 * @param input_file_path The absolute path to the incremental backup file
 * @param output_file_path The absolute path to the reconstructed full backup file
 * @param relative_dir The directory containing the incremental file relative to the root dir, should be the same across all backups
 * @param bare_file_name The name of the file without "INCREMENTAL." prefix
 * @param prior_backup_dirs The root directory of prior incremental/full backups, from newest to oldest
 * @return 0 on success, 1 if otherwise
 */
static int
reconstruct_backup_file(int server,
                        char* input_file_path,
                        char* output_file_path,
                        char* relative_dir,
                        char* bare_file_name,
                        struct deque* prior_backup_dirs);

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

static int
rfile_create(char* file_path, struct rfile** rfile);

static void
rfile_destroy(struct rfile* rf);

static void
rfile_destroy_cb(uintptr_t data);

static int
incremental_rfile_initialize(int server, char* file_path, struct rfile** rf);

static bool
is_full_file(struct rfile* rf);

static int
read_block(struct rfile* rf, off_t offset, uint32_t blocksz, uint8_t* buffer);

static int
write_reconstructed_file(char* output_file_path,
                         uint32_t block_length,
                         struct rfile** source_map,
                         off_t* offset_map,
                         uint32_t blocksz);

static int
write_backup_label(char* from_dir, char* to_dir);

static uint32_t
parse_oid(char* name);

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
   int ret = RESTORE_OK;
   char* identifier = NULL;
   char* position = NULL;
   char* directory = NULL;
   char* elapsed = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds = 0;
   char* output = NULL;
   struct backup* backup = NULL;
   struct art* nodes = NULL;
   struct json* req = NULL;
   struct json* response = NULL;
   struct configuration* config;

   pgmoneta_start_logging();

   config = (struct configuration*)shmem;

   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);

   atomic_fetch_add(&config->active_restores, 1);
   atomic_fetch_add(&config->servers[server].restore, 1);

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   identifier = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_BACKUP);
   position = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_POSITION);
   directory = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_DIRECTORY);

   if (identifier == NULL || strlen(identifier) == 0)
   {
      pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name,
                                         MANAGEMENT_ERROR_RESTORE_NOBACKUP, NAME, compression, encryption, payload);
      goto error;
   }

   if (pgmoneta_art_create(&nodes))
   {
      goto error;
   }

   if (pgmoneta_workflow_nodes(server, identifier, nodes, &backup))
   {
      pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name,
                                         MANAGEMENT_ERROR_RESTORE_NOBACKUP, NAME, compression, encryption, payload);
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
         pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_ALLOCATION, NAME, compression, encryption, payload);

         goto error;
      }

      backup = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);

      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->servers[server].name, ValueString);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)backup->label, ValueString);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP_SIZE, (uintptr_t)backup->backup_size, ValueUInt64);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_RESTORE_SIZE, (uintptr_t)backup->restore_size, ValueUInt64);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BIGGEST_FILE_SIZE, (uintptr_t)backup->biggest_file_size, ValueUInt64);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_COMMENTS, (uintptr_t)backup->comments, ValueString);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_COMPRESSION, (uintptr_t)backup->compression, ValueInt32);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_ENCRYPTION, (uintptr_t)backup->encryption, ValueInt32);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_INCREMENTAL, (uintptr_t)backup->type, ValueBool);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_INCREMENTAL_PARENT, (uintptr_t)backup->parent_label, ValueString);

      clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);

      if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
      {
         pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_RESTORE_NETWORK, NAME, compression, encryption, payload);
         pgmoneta_log_error("Restore: Error sending response for %s", config->servers[server].name);

         goto error;
      }

      elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);
      pgmoneta_log_info("Restore: %s/%s (Elapsed: %s)", config->servers[server].name, backup->label, elapsed);
   }
   else if (ret == RESTORE_MISSING_LABEL)
   {
      pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_RESTORE_NOBACKUP, NAME, compression, encryption, payload);
      pgmoneta_log_warn("Restore: No identifier for %s/%s", config->servers[server].name, identifier);
      goto error;
   }
   else
   {
      pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_RESTORE_NODISK, NAME,
                                         compression, encryption, payload);
      goto error;
   }

   pgmoneta_json_destroy(payload);

   pgmoneta_disconnect(client_fd);

   atomic_fetch_sub(&config->servers[server].restore, 1);
   atomic_fetch_sub(&config->active_restores, 1);

   pgmoneta_stop_logging();

   free(backup);
   free(elapsed);
   free(output);

   exit(0);

error:

   pgmoneta_json_destroy(payload);

   pgmoneta_disconnect(client_fd);

   atomic_fetch_sub(&config->servers[server].restore, 1);
   atomic_fetch_sub(&config->active_restores, 1);

   pgmoneta_stop_logging();

   free(backup);
   free(elapsed);
   free(output);

   exit(1);
}

int
pgmoneta_restore_backup(struct art* nodes)
{
   struct backup* backup = NULL;

#ifdef DEBUG
   if (pgmoneta_log_is_enabled(PGMONETA_LOGGING_LEVEL_DEBUG1))
   {
      char *a = NULL;
      a = pgmoneta_art_to_string(nodes, FORMAT_TEXT, NULL, 0);
      pgmoneta_log_debug("(Tree)\n%s", a);
      free(a);
   }
   assert(nodes != NULL);

   assert(pgmoneta_art_contains_key(nodes, USER_DIRECTORY));
   assert(pgmoneta_art_contains_key(nodes, USER_IDENTIFIER));
   assert(pgmoneta_art_contains_key(nodes, USER_POSITION));
   assert(pgmoneta_art_contains_key(nodes, USER_SERVER));

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   backup = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);
   if (backup->type == TYPE_FULL)
   {
      return restore_backup_full(nodes);
   }
   else if (backup->type == TYPE_INCREMENTAL)
   {
      return restore_backup_incremental(nodes);
   }
   else
   {
      return RESTORE_TYPE_UNKNOWN;
   }
}

int
pgmoneta_combine_backups(int server, char* base, char* input_dir, char* output_dir, struct deque* prior_backup_dirs, struct backup* bck, struct json* manifest)
{
   uint32_t tsoid = 0;
   char relative_tablespace_path[MAX_PATH];
   char full_tablespace_path[MAX_PATH];
   char itblspc_dir[MAX_PATH];
   char otblspc_dir[MAX_PATH];
   char manifest_path[MAX_PATH];
   struct json* files = NULL;
   struct configuration* config;

   if (manifest == NULL || prior_backup_dirs == NULL || base == NULL || input_dir == NULL || output_dir == NULL)
   {
      goto error;
   }

   config = (struct configuration*)shmem;

   memset(manifest_path, 0, MAX_PATH);
   snprintf(manifest_path, MAX_PATH, "%s/backup_manifest", output_dir);

   clear_manifest_incremental_entries(manifest);
   files = (struct json*)pgmoneta_json_get(manifest, MANIFEST_FILES);
   if (files == NULL)
   {
      goto error;
   }

   // It is actually ok even if we don't explicitly create the top level directory
   // since pgmoneta_mkdir creates parent directory if it doesn't exist.
   // We do this to make the code clearer and safer
   if (pgmoneta_mkdir(output_dir))
   {
      pgmoneta_log_error("Combine incremental: Unable to create directory %s", output_dir);
      goto error;
   }

   // round 1 for base data directory
   if (combine_backups_recursive(0, server, input_dir, output_dir, NULL, bck->hash_algorithm, prior_backup_dirs, files))
   {
      goto error;
   }

   // round 2 for each tablespaces
   for (int i = 0; i < bck->number_of_tablespaces; i++)
   {
      tsoid = parse_oid(bck->tablespaces_oids[i]);

      memset(relative_tablespace_path, 0, MAX_PATH);
      memset(full_tablespace_path, 0, MAX_PATH);
      memset(otblspc_dir, 0, MAX_PATH);
      memset(itblspc_dir, 0, MAX_PATH);

      snprintf(otblspc_dir, MAX_PATH, "%s/%s/%u", output_dir, "pg_tblspc", tsoid);
      snprintf(itblspc_dir, MAX_PATH, "%s/%s/%u", input_dir, "pg_tblspc", tsoid);
      snprintf(relative_tablespace_path, MAX_PATH, "../../%s-%s-%s",
               config->servers[server].name, bck->label, bck->tablespaces[i]);
      snprintf(full_tablespace_path, MAX_PATH, "%s/%s-%s-%s", base, config->servers[server].name, bck->label, bck->tablespaces[i]);

      // create and link the actual tablespace directory
      if (pgmoneta_mkdir(full_tablespace_path))
      {
         pgmoneta_log_error("Combine backups: unable to create directory %s", full_tablespace_path);
         goto error;
      }

      if (pgmoneta_symlink_at_file(otblspc_dir, relative_tablespace_path))
      {
         pgmoneta_log_error("Combine backups: unable to create symlink %s->%s", otblspc_dir, relative_tablespace_path);
      }

      if (combine_backups_recursive(tsoid, server, itblspc_dir, full_tablespace_path, NULL, bck->hash_algorithm, prior_backup_dirs, files))
      {
         goto error;
      }
   }

   if (write_backup_label(input_dir, output_dir))
   {
      goto error;
   }

   if (pgmoneta_json_write_file(manifest_path, manifest))
   {
      pgmoneta_log_error("Fail to write manifest to %s", manifest_path);
      goto error;
   }

   return 0;
error:
   return 1;
}

static int
combine_backups_recursive(uint32_t tsoid,
                          int server,
                          char* input_dir,
                          char* output_dir,
                          char* relative_dir,
                          int algorithm,
                          struct deque* prior_backup_dirs,
                          struct json* files)
{
   bool is_pg_tblspc = false;
   bool is_incremental_dir = false;
   char ifulldir[MAX_PATH];
   char ofulldir[MAX_PATH];
   // Current directory of the file to be reconstructed relative to backup base directory.
   // In normal cases it's the same as relative_dir, except for having an ending backup slash
   // For table spaces the relative_dir is relative to the table space oid directory,
   // so the relative_prefix should be pg_tblspc/oid/relative_dir/ instead
   char relative_prefix[MAX_PATH];
   DIR* dir = NULL;
   struct dirent* entry;
   struct json* file = NULL;

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
      char ifullpath[MAX_PATH_CONCAT];
      char ofullpath[MAX_PATH_CONCAT];
      char manifest_path[MAX_PATH_CONCAT];

      if (pgmoneta_compare_string(entry->d_name, ".") || pgmoneta_compare_string(entry->d_name, ".."))
      {
         continue;
      }

      memset(ifullpath, 0, MAX_PATH_CONCAT);
      memset(ofullpath, 0, MAX_PATH_CONCAT);
      memset(manifest_path, 0, MAX_PATH_CONCAT);

      snprintf(ifullpath, MAX_PATH_CONCAT, "%s/%s", ifulldir, entry->d_name);

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
         memset(new_relative_dir, 0, MAX_PATH);
         if (relative_dir == NULL)
         {
            memcpy(new_relative_dir, entry->d_name, strlen(entry->d_name));
         }
         else
         {
            snprintf(new_relative_dir, MAX_PATH, "%s/%s", relative_dir, entry->d_name);
         }
         combine_backups_recursive(tsoid, server, input_dir, output_dir, new_relative_dir, algorithm, prior_backup_dirs, files);
         continue;
      }

      if (entry->d_type != DT_REG)
      {
         if (entry->d_type == DT_LNK)
         {
            pgmoneta_log_warn("skipping symbolic link \"%s\"", ifullpath);
         }
         else
         {
            pgmoneta_log_warn("skipping special file \"%s\"", ifullpath);
         }
         continue;
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
         snprintf(ofullpath, MAX_PATH_CONCAT, "%s/%s", ofulldir, entry->d_name + INCREMENTAL_PREFIX_LENGTH);
         snprintf(manifest_path, MAX_PATH_CONCAT, "%s%s", relative_prefix, entry->d_name + INCREMENTAL_PREFIX_LENGTH);
         if (reconstruct_backup_file(server,
                                     ifullpath,
                                     ofullpath,
                                     relative_prefix,
                                     entry->d_name + INCREMENTAL_PREFIX_LENGTH,
                                     prior_backup_dirs))
         {
            pgmoneta_log_error("unable to reconstruct file %s", ifullpath);
            goto error;
         }
         // Update file entry in manifest
         if (get_file_manifest(ofullpath, manifest_path, algorithm, &file))
         {
            pgmoneta_log_error("Unable to get manifest for file %s", ofullpath);
         }
         else
         {
            pgmoneta_json_append(files, (uintptr_t)file, ValueJSON);
         }
      }
      else
      {
         // copy the full file from input dir to output dir
         snprintf(ofullpath, MAX_PATH_CONCAT, "%s/%s", ofulldir, entry->d_name);
         pgmoneta_copy_file(ifullpath, ofullpath, NULL);
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
                        char* input_file_path,
                        char* output_file_path,
                        char* relative_dir,
                        char* bare_file_name,
                        struct deque* prior_backup_dirs)
{
   struct deque* sources = NULL; // bookkeeping of each incr/full backup rfile, so that we can free them conveniently
   struct deque_iterator* bck_iter = NULL; // the iterator for backup directories
   struct rfile* latest_source = NULL; // the metadata of current incr backup file
   struct rfile** source_map = NULL; // source to find each block
   off_t* offset_map = NULL; // offsets to find each block in corresponding file
   uint32_t block_length = 0; // total number of blocks in the reconstructed file
   bool full_copy_possible = true; // whether we could just copy over directly instead of block by block
   uint32_t b = 0; // temp variable for block numbers
   struct configuration* config;
   size_t blocksz = 0;
   char path[MAX_PATH];
   uint32_t nblocks = 0;
   size_t file_size = 0;
   struct rfile* copy_source = NULL;
   struct value_config rfile_config = {.destroy_data = rfile_destroy_cb, .to_string = NULL};

   config = (struct configuration*)shmem;

   blocksz = config->servers[server].block_size;

   pgmoneta_deque_create(false, &sources);

   // handle the latest file specially, it is the only file that can only be incremental
   if (incremental_rfile_initialize(server, input_file_path, &latest_source))
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
   for (int i = 0; i < latest_source->num_blocks; i++)
   {
      // the block number of blocks inside latest incr file
      b = latest_source->relative_block_numbers[i];
      if (b >= block_length)
      {
         pgmoneta_log_error("find block number %d exceeding reconstructed file size %d at file path %s", b, block_length, input_file_path);
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
   pgmoneta_deque_iterator_create(prior_backup_dirs, &bck_iter);
   while (pgmoneta_deque_iterator_next(bck_iter))
   {
      struct rfile* rf = NULL;
      char* dir = (char*)pgmoneta_value_data(bck_iter->value);
      // try finding the full file
      memset(path, 0, MAX_PATH);
      // relative directory always ends with '/'
      snprintf(path, MAX_PATH, "%s/%s%s", dir, relative_dir, bare_file_name);
      if (rfile_create(path, &rf))
      {
         memset(path, 0, MAX_PATH);
         snprintf(path, MAX_PATH, "%s/%s/INCREMENTAL.%s", dir, relative_dir, bare_file_name);
         if (incremental_rfile_initialize(server, path, &rf))
         {
            goto error;
         }
      }
      pgmoneta_deque_add_with_config(sources, NULL, (uintptr_t)rf, &rfile_config);

      // If it's a full file, all blocks not sourced yet can be sourced from it.
      // And then we are done, no need to go further back.
      if (is_full_file(rf))
      {
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
      for (int i = 0; i < rf->num_blocks; i++)
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
   // let's skip manifest for now
   if (copy_source != NULL)
   {
      if (pgmoneta_copy_file(copy_source->filepath, output_file_path, NULL))
      {
         pgmoneta_log_error("reconstruct: fail to copy file from %s to %s", copy_source->filepath, output_file_path);
         goto error;
      }
   }
   else
   {
      if (write_reconstructed_file(output_file_path, block_length, source_map, offset_map, blocksz))
      {
         pgmoneta_log_error("reconstruct: fail to write reconstructed file at %s", output_file_path);
         goto error;
      }
   }
   pgmoneta_deque_destroy(sources);
   pgmoneta_deque_iterator_destroy(bck_iter);
   free(source_map);
   free(offset_map);
   return 0;
error:
   pgmoneta_deque_destroy(sources);
   pgmoneta_deque_iterator_destroy(bck_iter);
   free(source_map);
   free(offset_map);
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
   for (int i = 0; i < s->num_blocks; i++)
   {
      if (s->relative_block_numbers[i] >= block_length)
      {
         block_length = s->relative_block_numbers[i] + 1;
      }
   }

   return block_length;
}

static int
rfile_create(char* file_path, struct rfile** rfile)
{
   struct rfile* rf = NULL;
   FILE* fp = NULL;
   fp = fopen(file_path, "r");

   if (fp == NULL)
   {
      goto error;
   }
   rf = (struct rfile*) malloc(sizeof(struct rfile));
   memset(rf, 0, sizeof(struct rfile));
   rf->filepath = pgmoneta_append(NULL, file_path);
   rf->fp = fp;
   *rfile = rf;
   return 0;

error:
   rfile_destroy(rf);
   return 1;
}

static void
rfile_destroy(struct rfile* rf)
{
   if (rf == NULL)
   {
      return;
   }
   if (rf->fp != NULL)
   {
      fclose(rf->fp);
   }
   free(rf->filepath);
   free(rf->relative_block_numbers);
   free(rf);
}

static void
rfile_destroy_cb(uintptr_t data)
{
   rfile_destroy((struct rfile*) data);
}

static int
incremental_rfile_initialize(int server, char* file_path, struct rfile** rfile)
{
   uint32_t magic = 0;
   int nread = 0;
   struct rfile* rf = NULL;
   struct configuration* config;
   size_t relsegsz = 0;
   size_t blocksz = 0;

   config = (struct configuration*)shmem;

   relsegsz = config->servers[server].relseg_size;
   blocksz = config->servers[server].block_size;

   // create rfile after file is opened successfully
   if (rfile_create(file_path, &rf))
   {
      pgmoneta_log_error("rfile initialize: failed to open incremental backup file at %s", file_path);
      goto error;
   }

   // read magic number from header
   nread = fread(&magic, 1, sizeof(uint32_t), rf->fp);
   if (nread != sizeof(uint32_t))
   {
      pgmoneta_log_error("rfile initialize: incomplete file header at %s, cannot read magic number", file_path);
      goto error;
   }

   if (magic != INCREMENTAL_MAGIC)
   {
      pgmoneta_log_error("rfile initialize: incorrect magic number, getting %X, expecting %X", magic, INCREMENTAL_MAGIC);
      goto error;
   }

   // read number of blocks
   nread = fread(&rf->num_blocks, 1, sizeof(uint32_t), rf->fp);
   if (nread != sizeof(uint32_t))
   {
      pgmoneta_log_error("rfile initialize: incomplete file header at %s, cannot read block count", file_path);
      goto error;
   }
   if (rf->num_blocks > relsegsz)
   {
      pgmoneta_log_error("rfile initialize: file has %d blocks which is more than server's segment size", rf->num_blocks);
      goto error;
   }

   // read truncation block length
   nread = fread(&rf->truncation_block_length, 1, sizeof(uint32_t), rf->fp);
   if (nread != sizeof(uint32_t))
   {
      pgmoneta_log_error("rfile initialize: incomplete file header at %s, cannot read truncation block length", file_path);
      goto error;
   }
   if (rf->truncation_block_length > relsegsz)
   {
      pgmoneta_log_error("rfile initialize: file has truncation block length of %d which is more than server's segment size", rf->truncation_block_length);
      goto error;
   }

   if (rf->num_blocks > 0)
   {
      rf->relative_block_numbers = malloc(sizeof(uint32_t) * rf->num_blocks);
      nread = fread(rf->relative_block_numbers, sizeof(uint32_t), rf->num_blocks, rf->fp);
      if (nread != rf->num_blocks)
      {
         pgmoneta_log_error("rfile initialize: incomplete file header at %s, cannot read relative block numbers", file_path);
         goto error;
      }
   }

   // magic + block num + truncation block length + relative block numbers
   rf->header_length = sizeof(uint32_t) * (1 + 1 + 1 + rf->num_blocks);
   // round header length to multiple of block size, since the actual file data are aligned
   // only needed when the file actually has data
   if (rf->num_blocks > 0 && rf->header_length % blocksz != 0)
   {
      rf->header_length += (blocksz - (rf->header_length % blocksz));
   }

   *rfile = rf;

   return 0;
error:
   // contains fp closing logic
   rfile_destroy(rf);
   return 1;
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
   int nread = 0;
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
write_reconstructed_file(char* output_file_path,
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
   for (int i = 0; i < block_length; i++)
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
write_backup_label(char* from_dir, char* to_dir)
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
   diter = (struct deque_iterator*) iter->iter;
   while (pgmoneta_deque_iterator_next(diter))
   {
      f = (struct json*)pgmoneta_value_data(diter->value);
      path = (char*) pgmoneta_json_get(f, "Path");
      if (pgmoneta_is_incremental_path(path))
      {
         pgmoneta_deque_iterator_remove(diter);
      }
   }
   pgmoneta_json_iterator_destroy(iter);
}

static int
get_file_manifest(char* path, char* manifest_path, int algorithm, struct json** file)
{
   struct json* f = NULL;
   size_t size = 0;
   time_t t;
   struct tm* tinfo;
   char now[MISC_LENGTH];
   char* checksum = NULL;

   *file = NULL;
   pgmoneta_json_create(&f);

   size = pgmoneta_get_file_size(path);

   time(&t);
   tinfo = gmtime(&t);
   memset(now, 0, sizeof(now));
   strftime(now, sizeof(now), "%Y-%m-%d %H:%M:%S GMT", tinfo);

   if (pgmoneta_create_file_hash(algorithm, path, &checksum))
   {
      goto error;
   }

   switch (algorithm)
   {
      case HASH_ALGORITHM_SHA224:
         pgmoneta_json_put(f, "Checksum-Algorithm", (uintptr_t)"SHA224", ValueString);
         break;
      case HASH_ALGORITHM_SHA256:
         pgmoneta_json_put(f, "Checksum-Algorithm", (uintptr_t)"SHA256", ValueString);
         break;
      case HASH_ALGORITHM_SHA384:
         pgmoneta_json_put(f, "Checksum-Algorithm", (uintptr_t)"SHA384", ValueString);
         break;
      case HASH_ALGORITHM_SHA512:
         pgmoneta_json_put(f, "Checksum-Algorithm", (uintptr_t)"SHA512", ValueString);
         break;
      case HASH_ALGORITHM_CRC32C:
         pgmoneta_json_put(f, "Checksum-Algorithm", (uintptr_t)"CRC32C", ValueString);
         break;
      default:
         pgmoneta_json_put(f, "Checksum-Algorithm", (uintptr_t)"NONE", ValueString);
         break;
   }
   pgmoneta_json_put(f, "Path", (uintptr_t)manifest_path, ValueString);
   pgmoneta_json_put(f, "Size", size, ValueUInt64);
   pgmoneta_json_put(f, "Last-Modified", (uintptr_t)now, ValueString);
   pgmoneta_json_put(f, "Checksum", (uintptr_t)checksum, ValueString);
   *file = f;

   free(checksum);
   return 0;

error:
   free(checksum);
   pgmoneta_json_destroy(f);
   return 1;
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
   struct configuration* config;

   struct workflow* workflow = NULL;

   config = (struct configuration*)shmem;

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   directory = (char*)pgmoneta_art_search(nodes, USER_DIRECTORY);
   backup = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);

   target_root = pgmoneta_append(target_root, directory);
   target_base = pgmoneta_append(target_base, directory);
   if (!pgmoneta_ends_with(target_base, "/"))
   {
      target_base = pgmoneta_append(target_base, "/");
   }
   target_base = pgmoneta_append(target_base, config->servers[server].name);
   target_base = pgmoneta_append(target_base, "-");
   target_base = pgmoneta_append(target_base, backup->label);
   target_base = pgmoneta_append(target_base, "/");

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
                         config->servers[server].name, backup->label, target_root, f, r);

      free(f);
      free(r);

      ret = RESTORE_NO_DISK_SPACE;
      goto error;
   }

   pgmoneta_art_insert(nodes, NODE_TARGET_ROOT, (uintptr_t)target_root, ValueString);
   pgmoneta_art_insert(nodes, NODE_TARGET_BASE, (uintptr_t)target_base, ValueString);
   pgmoneta_log_trace("Full backup restore: %s", backup->label);
   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_RESTORE, server, backup);
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
   char* directory = NULL;
   struct backup* backup = NULL;
   struct backup* bck = NULL;
   struct deque* prior_backups = NULL;
   char target_root_restore[MAX_PATH];
   char target_base_restore[MAX_PATH_CONCAT];
   char target_root_combine[MAX_PATH];
   char target_base_combine[MAX_PATH_CONCAT];
   char label[MISC_LENGTH];
   char* manifest_path = NULL;
   char* server_dir = NULL;
   struct json* manifest = NULL;
   struct configuration* config;

   struct workflow* workflow = NULL;
   config = (struct configuration*)shmem;

   memset(target_root_restore, 0, MAX_PATH);
   memset(target_base_restore, 0, MAX_PATH_CONCAT);
   memset(target_root_combine, 0, MAX_PATH);
   memset(target_base_combine, 0, MAX_PATH_CONCAT);

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   directory = (char*)pgmoneta_art_search(nodes, USER_DIRECTORY);
   backup = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);
   server_dir = pgmoneta_get_server_backup(server);
   pgmoneta_deque_create(false, &prior_backups);
   pgmoneta_art_insert(nodes, NODE_BACKUPS, (uintptr_t)prior_backups, ValueDeque);

   // initialize label to be the parent label of current backup
   memset(label, 0, MISC_LENGTH);
   memcpy(label, backup->parent_label, sizeof(backup->parent_label));

   snprintf(target_root_restore, MAX_PATH, "%s/tmp_%s_incremental_%s", directory, config->servers[server].name, &backup->label[0]);
   snprintf(target_root_combine, MAX_PATH, "%s", directory);
   snprintf(target_base_combine, MAX_PATH_CONCAT, "%s/%s-%s", directory, config->servers[server].name, backup->label);
   manifest_path = pgmoneta_get_server_backup_identifier_data(server, backup->label);
   manifest_path = pgmoneta_append(manifest_path, "backup_manifest");
   // read the manifest for later usage
   if (pgmoneta_json_read_file(manifest_path, &manifest))
   {
      goto error;
   }
   pgmoneta_art_insert(nodes, NODE_MANIFEST, (uintptr_t)manifest, ValueJSON);

   //TODO: free space check during incr restore should be handled specially

   // restore current incremental backup
   snprintf(target_base_restore, MAX_PATH_CONCAT, "%s/%s-%s", target_root_restore, config->servers[server].name, backup->label);
   pgmoneta_art_insert(nodes, NODE_TARGET_ROOT, (uintptr_t)target_root_restore, ValueString);
   pgmoneta_art_insert(nodes, NODE_TARGET_BASE, (uintptr_t)target_base_restore, ValueString);

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_RESTORE, server, backup);
   if ((ret = carry_out_workflow(workflow, nodes) != RESTORE_OK))
   {
      goto error;
   }
   pgmoneta_workflow_destroy(workflow);
   workflow = NULL;
   pgmoneta_deque_add(prior_backups, NULL, (uintptr_t)target_base_restore, ValueString);

   // restore the chain of prior backups
   while (bck == NULL || bck->type != TYPE_FULL)
   {
      free(bck);
      bck = NULL;
      pgmoneta_get_backup(server_dir, label, &bck);
      if (bck == NULL)
      {
         ret = RESTORE_MISSING_LABEL;
         pgmoneta_log_error("Unable to find backup %s", label);
         goto error;
      }

      memset(target_base_restore, 0, MAX_PATH_CONCAT);
      snprintf(target_base_restore, MAX_PATH_CONCAT, "%s/%s-%s", target_root_restore, config->servers[server].name, bck->label);
      pgmoneta_art_insert(nodes, NODE_TARGET_BASE, (uintptr_t)target_base_restore, ValueString);
      pgmoneta_art_insert(nodes, NODE_LABEL, (uintptr_t)label, ValueString);
      pgmoneta_art_insert(nodes, NODE_BACKUP, (uintptr_t)bck, ValueRef);

      workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_RESTORE, server, backup);
      if ((ret = carry_out_workflow(workflow, nodes) != RESTORE_OK))
      {
         goto error;
      }
      pgmoneta_workflow_destroy(workflow);
      workflow = NULL;

      // get a copy of current backup's parent before we free it in the next round
      memset(label, 0, MISC_LENGTH);
      memcpy(label, bck->parent_label, sizeof(bck->parent_label));
      pgmoneta_deque_add(prior_backups, NULL, (uintptr_t)target_base_restore, ValueString);
   }

   //combine the backups, first reset some input keys
   pgmoneta_art_insert(nodes, NODE_TARGET_ROOT, (uintptr_t)target_root_combine, ValueString);
   pgmoneta_art_insert(nodes, NODE_TARGET_BASE, (uintptr_t)target_base_combine, ValueString);
   pgmoneta_art_insert(nodes, NODE_BACKUP, (uintptr_t)backup, ValueRef);
   pgmoneta_art_insert(nodes, NODE_LABEL, (uintptr_t)backup->label, ValueString);

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_COMBINE, server, backup);
   if ((ret = carry_out_workflow(workflow, nodes) != RESTORE_OK))
   {
      goto error;
   }
   pgmoneta_workflow_destroy(workflow);
   workflow = NULL;

   free(manifest_path);
   free(server_dir);
   free(bck);
   return RESTORE_OK;

error:
   pgmoneta_delete_directory(target_root_restore);
   pgmoneta_delete_directory(target_base_combine);
   // purge each table space
   for (int i = 0; i < backup->number_of_tablespaces; i++)
   {
      char tblspc[MAX_PATH];
      memset(tblspc, 0, MAX_PATH);
      snprintf(tblspc, MAX_PATH, "%s/%s-%s-%s", directory,
               config->servers[server].name, backup->label,
               backup->tablespaces[i]);
      if (pgmoneta_exists(tblspc))
      {
         pgmoneta_delete_directory(tblspc);
      }
   }

   free(manifest_path);
   free(bck);
   free(server_dir);

   pgmoneta_workflow_destroy(workflow);

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
         goto error;
      }
      current = current->next;
   }

   return ret;
error:
   return ret;
}
