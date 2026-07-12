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
#include <pgmoneta.h>
#include <aes.h>
#include <art.h>
#include <compression.h>
#include <info.h>
#include <json.h>
#include <link.h>
#include <logging.h>
#include <manifest.h>
#include <migration.h>
#include <security.h>
#include <utils.h>
#include <value.h>

// system
#include <assert.h>
#include <csv.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <libgen.h>
#include <stdio.h>
#include <time.h>

#define PGBACKREST_BACKUP_INFO      "backup.info"
#define PGBACKREST_BACKUP_MANIFEST  "backup.manifest"
#define CBC_DIGEST_DEFAULT          "sha1"
#define BACKUP_ID_SIZE              15
#define PG_DATA_PREFIX_LEN          strlen("pg_data/")
#define PGBACKREST_FILE_REFERENCE   "reference"
#define PGBACKREST_PASSWORD_KEY     "cipher-pass"
#define PGBACKREST_WAL_CHECKSUM_LEN 40
#define PG_WAL_DIR                  "pg_wal/"
#define HISTORY_SUFFIX              ".history"

struct pgbackrest_backup_info
{
   char* lsn_start;
   char* lsn_stop;
   char* archive_start;
   char* archive_stop;
   char* backup_id;
   char* target_backup_id;
   char** backup_chain; // from oldest to newest
   int backup_chain_size;
   char* backup_parent;
   size_t restore_size;
   time_t start_time;
   int type;
};

struct pgbackrest_manifest
{
   int compression;
   char* backup_password;
   uint64_t db_system_id;
   uint64_t db_version;
   struct art* files;
};

struct timeline_history
{
   uint32_t tli;
   // max lsn is FFFFFFFF/FFFFFFFF which takes 17 bytes
   char lsn[18];
};

static int load_backup_info(char* source_dir, char* workspace, char** password, struct art** backups);
static int load_archive_password(char* source_wal_dir, char* workspace_dir, char** password);
static void build_target_dir(char* source_dir, char* server, char** target_data_dir, char** target_wal_dir);
static int migrate_pgbackrest(char* source_data_dir, char* source_wal_dir, char* target_base_dir, char* backup_id, char* server, char* workspace);

static int parse_pgbackrest_backup_info(char* path, char** password, struct art** backups);
static int insert_pgbackrest_backup_info(char* backup_id, struct pgbackrest_backup_info* backup, struct art* backups);
static int pgbackrest_backup_info_validate(struct pgbackrest_backup_info* bck);
static int pgbackrest_backup_info_create(char* backup_id, struct json* info, struct pgbackrest_backup_info** backup);
static void pgbackrest_backup_info_destroy(struct pgbackrest_backup_info* backup);
static void pgbackrest_backup_info_destroy_cb(uintptr_t data);
static void get_target_backup_id(time_t start_time, char** target_id);
static int migrate_pgbackrest_backup(struct art* manifests, struct pgbackrest_backup_info* prev_backup_info, struct pgbackrest_backup_info* backup_info, char* password, char* root_workspace, char* source_dir, char* target_data_dir, char* target_wal_dir);
static void pgbackrest_manifest_create(struct pgbackrest_manifest** manifest);
static void pgbackrest_manifest_destroy(struct pgbackrest_manifest* manifest);
static int pgbackrest_manifest_validate(struct pgbackrest_manifest* manifest);
static int load_pgbackrest_manifest(char* password, char* source_backup_path, char* workspace, struct pgbackrest_manifest** manifest);
static int parse_pgbackrest_manifest(char* path, struct pgbackrest_manifest** manifest);
static int migrate_pgbackrest_file(struct art* manifests, struct pgbackrest_backup_info* backup_info, struct pgbackrest_manifest* manifest, struct json* pg_manifest, char* target_data_dir, char* relative_path, char* source_root_dir, char* target_root_dir, char* workspace_root_dir, struct csv_writer* writer);
static int create_file_directory(char* target_root_dir, char* workspace_root_dir, char* relative_path);
static int create_file_placeholder(char* target_path);
static int migrate_wal_segments(char* workspace, char* source_dir, char* target_dir);
static int extract_wal_segments(char* password, char* source_dir, char* target_dir);
static int extract_wal_history(char* password, char* source_history, char* history_name, char* target_dir);
static bool is_wal_dir(char* dir);
static int extract_wal_file_info(char* file_name, char** seg_name, int* compression);
static int pgbackrest_handle_pg_wal(uint32_t start_tli, uint32_t stop_tli, char* wal_dir, char* target_backup_data_path, char* wal_start, char* wal_stop, char* lsn_start, char* lsn_stop, struct json* pg_manifest);
static int write_backup_manifest(char* target_backup_data_path, struct json* pg_manifest);
static int set_backup_info(char* target_backup_data_path, struct pgbackrest_backup_info* backup_info, struct pgbackrest_manifest* manifest, struct backup** backup);
static int get_current_timeline(char* wal_name, uint32_t* tli);
static int build_wal_ranges(char* lsn_start, char* lsn_stop, uint32_t start_tli, uint32_t stop_tli, char* history_path, struct json** wal_ranges);
static int read_history(char* history_path, struct deque** switch_points);
static int timeline_history_create(struct timeline_history** entry);
static int wal_range_create(uint32_t tli, char* start_lsn, char* stop_lsn, struct json** range);
static int add_pg_manifest_info(struct pgbackrest_manifest* manifest, struct json* pg_manifest);
static int get_and_set_file_manifest(struct art* manifests, struct pgbackrest_backup_info* backup, char* reference_backup_id, char* target_data_dir, char* relative_path, struct pgbackrest_manifest* manifest, struct json* pg_manifest, struct csv_writer* writer);
static int link_backups(char* data_dir, struct pgbackrest_backup_info* from_bck, struct pgbackrest_backup_info* to_bck);

int
pgmoneta_migrate(char* backup_id, char* server, char* workspace)
{
   struct muse_configuration* config = NULL;
   char* source_data_dir = NULL;
   char* source_wal_dir = NULL;
   char* target_base_dir = NULL;

   config = (struct muse_configuration*)shmem;

   source_data_dir = pgmoneta_append(source_data_dir, config->source_data_dir);
   if (!pgmoneta_ends_with(source_data_dir, "/"))
   {
      source_data_dir = pgmoneta_append(source_data_dir, "/");
   }

   source_wal_dir = pgmoneta_append(source_wal_dir, config->source_wal_dir);
   if (!pgmoneta_ends_with(source_wal_dir, "/"))
   {
      source_wal_dir = pgmoneta_append(source_wal_dir, "/");
   }

   target_base_dir = pgmoneta_append(target_base_dir, config->base_dir);
   if (!pgmoneta_ends_with(target_base_dir, "/"))
   {
      target_base_dir = pgmoneta_append(target_base_dir, "/");
   }

   if (config->source_tool == TOOL_PGBACKREST)
   {
      if (migrate_pgbackrest(source_data_dir, source_wal_dir, target_base_dir, backup_id, server, workspace))
      {
         pgmoneta_log_error("Failed to migrate pgbackrest backup and archive,"
                            "source data: %s, source wal: %s, target base: %s, backup id: %s, server %s, workspace %s",
                            source_data_dir, source_wal_dir, target_base_dir, backup_id, server, workspace);
         goto error;
      }
   }
   free(source_data_dir);
   free(source_wal_dir);
   free(target_base_dir);
   return 0;
error:
   free(source_data_dir);
   free(source_wal_dir);
   free(target_base_dir);
   return 1;
}

static int
migrate_pgbackrest(char* source_data_dir, char* source_wal_dir, char* target_base_dir, char* backup_id, char* server, char* workspace)
{
   char* target_data_dir = NULL;
   char* target_wal_dir = NULL;
   char* password = NULL;
   struct art* backups = NULL;
   struct art* manifests = NULL;
   struct pgbackrest_backup_info* bck = NULL;
   struct muse_configuration* conf = NULL;
   struct pgbackrest_backup_info* prev_backup = NULL;

   conf = (struct muse_configuration*)shmem;

   pgmoneta_art_create(&manifests);

   pgmoneta_log_info("Start migration from pgB_file_sha256_checksumackRest, workspace %s", workspace);
   build_target_dir(target_base_dir, server, &target_data_dir, &target_wal_dir);
   if (pgmoneta_exists(target_data_dir))
   {
      if (pgmoneta_delete_directory(target_data_dir))
      {
         pgmoneta_log_error("Failed to clean up target data directory %s", target_data_dir);
         goto error;
      }
   }

   pgmoneta_log_info("Creating backup directory %s and %s", target_data_dir, target_wal_dir);
   if (pgmoneta_mkdir(target_data_dir))
   {
      pgmoneta_log_error("Failed to create target data directory at %s", target_data_dir);
      goto error;
   }

   if (pgmoneta_mkdir(target_wal_dir))
   {
      pgmoneta_log_error("Failed to create target wal directory at %s", target_wal_dir);
      goto error;
   }

   if (load_backup_info(source_data_dir, workspace, &password, &backups))
   {
      pgmoneta_log_error("Failed to load backup info");
   }

   bck = (struct pgbackrest_backup_info*)pgmoneta_art_search(backups, backup_id);
   if (bck == NULL)
   {
      pgmoneta_log_error("Unable to find backup info of %s", backup_id);
      goto error;
   }
   else
   {
      pgmoneta_log_info("Found backup info of %s, backup start time %ld", backup_id, bck->start_time);
   }

   //Move WAL segments
   pgmoneta_log_info("Migrating WAL segments from %s to %s", source_wal_dir, target_wal_dir);
   if (migrate_wal_segments(workspace, source_wal_dir, target_wal_dir))
   {
      pgmoneta_log_error("Failed to migrate WAL segments from %s to %s", source_wal_dir, target_wal_dir);
      goto error;
   }

   for (int i = 0; i < bck->backup_chain_size; i++)
   {
      char* parent_backup_id = bck->backup_chain[i];
      pgmoneta_log_info("Start migrating parent backup %s", parent_backup_id);
      struct pgbackrest_backup_info* b = (struct pgbackrest_backup_info*)pgmoneta_art_search(backups, parent_backup_id);
      if (b == NULL)
      {
         pgmoneta_log_error("Failed to find parent backup %s", parent_backup_id);
         goto error;
      }
      if (migrate_pgbackrest_backup(manifests, prev_backup, b, password, workspace, source_data_dir, target_data_dir, target_wal_dir))
      {
         pgmoneta_log_error("Failed to migrate backup %s", b->backup_id);
         goto error;
      }
      prev_backup = b;
      pgmoneta_log_info("Successfully migrated parent backup %s", parent_backup_id);
   }

   pgmoneta_log_info("Start migrating backup %s", bck->backup_id);
   if (migrate_pgbackrest_backup(manifests, prev_backup, bck, password, workspace, source_data_dir, target_data_dir, target_wal_dir))
   {
      pgmoneta_log_error("Failed to migrate backup %s", bck->backup_id);
      goto error;
   }
   pgmoneta_log_info("Successfully migrated backup %s", bck->backup_id);

   //compress and encrypt wal segments
   if (conf->compression != COMPRESSION_NONE)
   {
      if (pgmoneta_compress_directory(-1, target_wal_dir, conf->compression, NULL, NULL))
      {
         pgmoneta_log_error("Failed to compress WAL archive directory %s", target_wal_dir);
         goto error;
      }
   }

   if (conf->encryption != ENCRYPTION_NONE)
   {
      if (pgmoneta_encrypt_directory(-1, target_wal_dir, NULL, NULL))
      {
         pgmoneta_log_error("Failed to encrypt WAL archive directory %s", target_wal_dir);
         goto error;
      }
   }

   free(password);
   free(target_data_dir);
   free(target_wal_dir);
   pgmoneta_art_destroy(backups);
   pgmoneta_art_destroy(manifests);
   return 0;

error:
   pgmoneta_delete_directory(target_data_dir);
   pgmoneta_delete_directory(target_wal_dir);
   free(target_data_dir);
   free(target_wal_dir);
   free(password);
   pgmoneta_art_destroy(backups);
   pgmoneta_art_destroy(manifests);
   return 1;
}

static int
load_backup_info(char* source_dir, char* workspace, char** password, struct art** backups)
{
   char* backup_info_path = NULL;
   char* dest = NULL;
   size_t password_len = 0;
   struct muse_configuration* conf = NULL;

   conf = (struct muse_configuration*)shmem;

   backup_info_path = pgmoneta_append(backup_info_path, source_dir);
   backup_info_path = pgmoneta_append(backup_info_path, PGBACKREST_BACKUP_INFO);
   dest = pgmoneta_append(dest, workspace);
   dest = pgmoneta_append(dest, PGBACKREST_BACKUP_INFO);

   password_len = strlen(conf->source_password);

   // TODO: handle the case where backup is not encrypted
   pgmoneta_log_info("Decrypting backup info from %s to %s", backup_info_path, dest);
   if (pgmoneta_cbc_decrypt_salted_file(CBC_DIGEST_DEFAULT,
                                        false,
                                        (unsigned char*)conf->source_password,
                                        password_len,
                                        backup_info_path,
                                        dest))
   {
      pgmoneta_log_error("Failed to decrypt backup info at %s", backup_info_path);
      goto error;
   }

   if (parse_pgbackrest_backup_info(dest, password, backups))
   {
      pgmoneta_log_error("Failed to parse backup info at %s", dest);
      goto error;
   }

   free(dest);
   free(backup_info_path);
   return 0;
error:
   free(dest);
   free(backup_info_path);
   return 1;
}

static int
load_archive_password(char* source_wal_dir, char* workspace_dir, char** password)
{
   char* archive_info_path = NULL;
   char* workspace_path = NULL;
   char* pass = NULL;
   FILE* file = NULL;
   char buffer[INFO_BUFFER_SIZE];
   struct muse_configuration* conf = NULL;

   *password = NULL;

   conf = (struct muse_configuration*)shmem;
   // I'm too tired to add another configuration so I'm going to assume archive.info
   // exists one level above the actual archive based on pgbackrest directory structure
   archive_info_path = pgmoneta_format_and_append(archive_info_path, "%s../archive.info", source_wal_dir);
   workspace_path = pgmoneta_format_and_append(workspace_path, "%sarchive.info", workspace_dir);
   // TODO: handle the case where backup is not encrypted
   if (pgmoneta_cbc_decrypt_salted_file(CBC_DIGEST_DEFAULT,
                                        false,
                                        (unsigned char*)conf->source_password,
                                        strlen(conf->source_password),
                                        archive_info_path,
                                        workspace_path))
   {
      pgmoneta_log_error("Failed to decrypt archive info at %s", archive_info_path);
      goto error;
   }

   if (pgmoneta_fopen_secure(workspace_path, "r", &file))
   {
      pgmoneta_log_error("Failed to open decrypted archive info at %s", workspace_path);
      goto error;
   }

   while ((fgets(&buffer[0], sizeof(buffer), file)) != NULL)
   {
      char key[INFO_BUFFER_SIZE];
      char value[INFO_BUFFER_SIZE];
      char* ptr = NULL;

      if (buffer[0] == '\n' || buffer[0] == '[')
      {
         continue;
      }
      memset(&key[0], 0, sizeof(key));
      memset(&value[0], 0, sizeof(value));

      ptr = strtok(&buffer[0], "=");

      if (ptr == NULL)
      {
         goto error;
      }

      memcpy(&key[0], ptr, strlen(ptr));

      ptr = strtok(NULL, "=");

      if (ptr == NULL)
      {
         goto error;
      }

      memcpy(&value[0], ptr, strlen(ptr) - 1);

      if (pgmoneta_compare_string(PGBACKREST_PASSWORD_KEY, &key[0]))
      {
         // remove the double quotes
         value[strlen(value) - 1] = 0;
         pass = pgmoneta_append(pass, &value[1]);
      }
   }

   *password = pass;

   free(archive_info_path);
   free(workspace_path);
   fclose(file);
   return 0;
error:
   free(archive_info_path);
   free(workspace_path);
   free(pass);
   if (file != NULL)
   {
      fclose(file);
   }
   return 1;
}

static void
build_target_dir(char* source_dir, char* server, char** target_data_dir, char** target_wal_dir)
{
   char* ddir = NULL;
   char* wdir = NULL;
   *target_data_dir = NULL;
   *target_wal_dir = NULL;

   ddir = pgmoneta_append(ddir, source_dir);
   ddir = pgmoneta_append(ddir, server);
   ddir = pgmoneta_append(ddir, "/backup/");

   wdir = pgmoneta_append(wdir, source_dir);
   wdir = pgmoneta_append(wdir, server);
   wdir = pgmoneta_append(wdir, "/wal/");

   *target_data_dir = ddir;
   *target_wal_dir = wdir;
}

static int
parse_pgbackrest_backup_info(char* path, char** password, struct art** backups)
{
   struct art* dict = NULL;
   struct json* backup_data = NULL;
   struct pgbackrest_backup_info* bck = NULL;
   char* pass = NULL;
   char buffer[INFO_BUFFER_SIZE];
   char section[128];
   FILE* file = NULL;

   *password = NULL;
   *backups = NULL;

   pgmoneta_art_create(&dict);

   if (pgmoneta_fopen_secure(path, "r", &file))
   {
      pgmoneta_log_error("Could not open file %s: %s", path, strerror(errno));
      errno = 0;
      goto error;
   }

   while ((fgets(&buffer[0], sizeof(buffer), file)) != NULL)
   {
      char key[INFO_BUFFER_SIZE];
      char value[INFO_BUFFER_SIZE];
      char* ptr = NULL;

      if (buffer[0] == '\n')
      {
         continue;
      }

      if (buffer[0] == '[')
      {
         memset(section, 0, sizeof(section));
         memcpy(section, buffer, strlen(buffer) - 1);
         continue;
      }

      memset(&key[0], 0, sizeof(key));
      memset(&value[0], 0, sizeof(value));

      ptr = strtok(&buffer[0], "=");

      if (ptr == NULL)
      {
         goto error;
      }

      memcpy(&key[0], ptr, strlen(ptr));

      ptr = strtok(NULL, "=");

      if (ptr == NULL)
      {
         goto error;
      }

      memcpy(&value[0], ptr, strlen(ptr) - 1);

      if (pgmoneta_compare_string(section, "[backup:current]"))
      {
         if (pgmoneta_json_parse_string(value, &backup_data))
         {
            pgmoneta_log_error("unable to parse backup info %s", value);
            goto error;
         }
         if (pgbackrest_backup_info_create(key, backup_data, &bck))
         {
            pgmoneta_log_error("unable to create backup info %s", key);
            goto error;
         }
         if (insert_pgbackrest_backup_info(key, bck, dict))
         {
            pgmoneta_log_error("unable to insert backup info");
            goto error;
         }
         bck = NULL;
         pgmoneta_json_destroy(backup_data);
         backup_data = NULL;
      }
      else if (pgmoneta_compare_string(PGBACKREST_PASSWORD_KEY, &key[0]))
      {
         // remove the double quotes
         value[strlen(value) - 1] = 0;
         pass = pgmoneta_append(pass, &value[1]);
      }
   }

   if (file != NULL)
   {
      fclose(file);
   }

   *password = pass;
   *backups = dict;
   return 0;

error:

   if (file != NULL)
   {
      fclose(file);
   }

   free(pass);
   pgmoneta_art_destroy(dict);
   pgmoneta_json_destroy(backup_data);
   pgbackrest_backup_info_destroy(bck);
   return 1;
}

static int
insert_pgbackrest_backup_info(char* backup_id, struct pgbackrest_backup_info* backup, struct art* backups)
{
   struct value_config vc = {.destroy_data = &pgbackrest_backup_info_destroy_cb,
                             .to_string = NULL};
   return pgmoneta_art_insert_with_config(backups, backup_id, (uintptr_t)backup, &vc);
}

static int
pgbackrest_backup_info_create(char* backup_id, struct json* info, struct pgbackrest_backup_info** backup)
{
   struct pgbackrest_backup_info* b = NULL;
   struct json_iterator* iter = NULL;
   struct json* backup_chain = NULL;
   char* type = NULL;
   int idx = 0;

   if (info == NULL || info->type != JSONItem)
   {
      pgmoneta_log_error("Incorrect backup info type");
      goto error;
   }
   *backup = NULL;

   b = malloc(sizeof(struct pgbackrest_backup_info));
   memset(b, 0, sizeof(struct pgbackrest_backup_info));
   b->archive_start = pgmoneta_append(b->archive_start, (char*)pgmoneta_json_get(info, "backup-archive-start"));
   b->archive_stop = pgmoneta_append(b->archive_stop, (char*)pgmoneta_json_get(info, "backup-archive-stop"));
   b->lsn_start = pgmoneta_append(b->lsn_start, (char*)pgmoneta_json_get(info, "backup-lsn-start"));
   b->lsn_stop = pgmoneta_append(b->lsn_stop, (char*)pgmoneta_json_get(info, "backup-lsn-stop"));
   b->restore_size = (size_t)pgmoneta_json_get(info, "backup-info-size");
   b->start_time = (time_t)pgmoneta_json_get(info, "backup-timestamp-start");
   assert(b->restore_size != 0 && b->start_time != 0);

   type = (char*)pgmoneta_json_get(info, "backup-type");
   if (pgmoneta_compare_string(type, "full"))
   {
      b->type = TYPE_FULL;
   }
   else if (pgmoneta_compare_string(type, "incr"))
   {
      b->type = TYPE_INCREMENTAL;
   }
   else if (pgmoneta_compare_string(type, "diff"))
   {
      b->type = TYPE_DIFFERENTIAL;
   }
   else
   {
      pgmoneta_log_error("unrecognized backup type %s, backup id %s", type, backup_id);
      goto error;
   }

   b->backup_id = pgmoneta_append(b->backup_id, backup_id);
   get_target_backup_id(b->start_time, &b->target_backup_id);
   if (pgmoneta_json_contains_key(info, "backup-prior"))
   {
      b->backup_parent = pgmoneta_append(b->backup_parent, (char*)pgmoneta_json_get(info, "backup-prior"));

      backup_chain = (struct json*)pgmoneta_json_get(info, "backup-reference");

      if (backup_chain == NULL || backup_chain->type != JSONArray)
      {
         pgmoneta_log_error("Incorrect backup reference type");
         goto error;
      }

      b->backup_chain_size = pgmoneta_json_array_length(backup_chain);
      b->backup_chain = malloc(sizeof(char*) * b->backup_chain_size);
      memset(b->backup_chain, 0, sizeof(char*) * b->backup_chain_size);
      pgmoneta_json_iterator_create(backup_chain, &iter);
      while (pgmoneta_json_iterator_next(iter))
      {
         b->backup_chain[idx] = pgmoneta_append(b->backup_chain[idx], (char*)iter->value->data);
         idx++;
      }
      assert(idx == b->backup_chain_size);
   }
   pgmoneta_json_iterator_destroy(iter);
   *backup = b;

   return 0;

error:
   pgbackrest_backup_info_destroy(b);
   pgmoneta_json_iterator_destroy(iter);
   return 1;
}

static void
pgbackrest_backup_info_destroy(struct pgbackrest_backup_info* backup)
{
   if (backup == NULL)
   {
      return;
   }
   free(backup->lsn_start);
   free(backup->lsn_stop);
   free(backup->archive_start);
   free(backup->archive_stop);
   for (int i = 0; i < backup->backup_chain_size; i++)
   {
      free(backup->backup_chain[i]);
   }
   free(backup->backup_chain);
   free(backup->backup_parent);
   free(backup->backup_id);
   free(backup->target_backup_id);
   free(backup);
}

static int
pgbackrest_backup_info_validate(struct pgbackrest_backup_info* bck)
{
   if (bck == NULL)
   {
      pgmoneta_log_error("Expecting non NULL backup info");
      goto error;
   }
   if (bck->archive_start == NULL || strlen(bck->archive_start) == 0)
   {
      pgmoneta_log_error("Expecting non empty backup archive start");
      goto error;
   }
   else if (!pgmoneta_is_wal_file(bck->archive_start))
   {
      pgmoneta_log_error("Invalid backup archive start");
      goto error;
   }
   if (bck->archive_stop == NULL || strlen(bck->archive_stop) == 0)
   {
      pgmoneta_log_error("Expecting non empty backup archive stop");
      goto error;
   }
   else if (!pgmoneta_is_wal_file(bck->archive_stop))
   {
      pgmoneta_log_error("Invalid backup archive stop");
      goto error;
   }
   if (bck->lsn_start == NULL || strlen(bck->lsn_start) == 0)
   {
      pgmoneta_log_error("Expecting non empty backup lsn start");
      goto error;
   }
   if (bck->lsn_stop == NULL || strlen(bck->lsn_stop) == 0)
   {
      pgmoneta_log_error("Expecting non empty backup lsn stop");
      goto error;
   }
   if (bck->backup_id == NULL || strlen(bck->backup_id) == 0)
   {
      pgmoneta_log_error("Expecting non empty backup ID");
      goto error;
   }
   if (bck->backup_parent != NULL && bck->backup_chain_size == 0)
   {
      pgmoneta_log_error("Expecting non empty backup chain when the backup has parent");
      goto error;
   }
   return 0;
error:
   return 1;
}

static void
pgbackrest_backup_info_destroy_cb(uintptr_t data)
{
   pgbackrest_backup_info_destroy((struct pgbackrest_backup_info*)data);
}

static void
get_target_backup_id(time_t start_time, char** target_id)
{
   char* id = NULL;
   struct tm* time_info = localtime(&start_time);
   *target_id = NULL;

   id = (char*)malloc(BACKUP_ID_SIZE);
   memset(id, 0, BACKUP_ID_SIZE);
   strftime(id, BACKUP_ID_SIZE, "%Y%m%d%H%M%S", time_info);
   *target_id = id;
}

static int
migrate_pgbackrest_backup(struct art* manifests, struct pgbackrest_backup_info* prev_backup_info, struct pgbackrest_backup_info* backup_info, char* password, char* root_workspace, char* source_dir, char* target_data_dir, char* target_wal_dir)
{
   char source_backup_path[MAX_PATH];
   char source_backup_data_path[MAX_PATH];
   char target_backup_path[MAX_PATH];
   char target_backup_data_path[MAX_PATH];
   char workspace[MAX_PATH];
   char data_workspace[MAX_PATH];
   uint32_t start_tli = 0;
   uint32_t stop_tli = 0;
   struct backup* backup = NULL;
   struct pgbackrest_manifest* manifest = NULL;
   struct art_iterator* iter = NULL;
   struct json* pg_manifest = NULL;
   struct csv_writer* writer = NULL;
   char* manifest_path = NULL;
   struct muse_configuration* conf = NULL;

   conf = (struct muse_configuration*)shmem;

   if (pgbackrest_backup_info_validate(backup_info))
   {
      pgmoneta_log_error("Failed to validate backup info");
      goto error;
   }

   if (pgmoneta_json_create(&pg_manifest))
   {
      pgmoneta_log_error("Failed to created pg manifest json");
      goto error;
   }

   memset(target_backup_path, 0, sizeof(target_backup_path));
   memset(source_backup_path, 0, sizeof(source_backup_path));
   memset(target_backup_data_path, 0, sizeof(target_backup_data_path));
   memset(source_backup_data_path, 0, sizeof(source_backup_data_path));
   memset(workspace, 0, sizeof(workspace));
   memset(data_workspace, 0, sizeof(data_workspace));

   snprintf(source_backup_path, sizeof(source_backup_path), "%s%s/", source_dir, backup_info->backup_id);
   snprintf(target_backup_path, sizeof(target_backup_path), "%s%s/", target_data_dir, backup_info->target_backup_id);
   snprintf(workspace, sizeof(workspace), "%s%s/", root_workspace, backup_info->backup_id);
   snprintf(source_backup_data_path, sizeof(source_backup_data_path), "%s%s/pg_data/", source_dir, backup_info->backup_id);
   snprintf(target_backup_data_path, sizeof(target_backup_data_path), "%s%s/data/", target_data_dir, backup_info->target_backup_id);
   snprintf(data_workspace, sizeof(data_workspace), "%s%s/data/", root_workspace, backup_info->backup_id);
   manifest_path = pgmoneta_format_and_append(manifest_path, "%s%s/%s", target_data_dir, backup_info->target_backup_id, "backup.manifest");

   pgmoneta_log_info("Start to migrate backup %s from %s to %s", backup_info->backup_id, source_backup_path, target_backup_path);
   if (get_current_timeline(backup_info->archive_start, &start_tli))
   {
      pgmoneta_log_error("Failed to extract current timeline from %s", backup_info->archive_start);
      goto error;
   }

   pgmoneta_log_info("Current backup start timeline: %" PRIx32, start_tli);

   if (get_current_timeline(backup_info->archive_stop, &stop_tli))
   {
      pgmoneta_log_error("Failed to extract current timeline from %s", backup_info->archive_stop);
      goto error;
   }

   pgmoneta_log_info("Current backup stop timeline: %" PRIx32, stop_tli);

   if (pgmoneta_mkdir(workspace))
   {
      pgmoneta_log_error("Failed to create workspace directory %s", workspace);
      goto error;
   }

   if (pgmoneta_exists(target_backup_path))
   {
      pgmoneta_delete_directory(target_backup_path);
   }

   if (pgmoneta_mkdir(target_backup_path))
   {
      pgmoneta_log_error("Failed to create backup directory %s", target_backup_path);
      goto error;
   }

   if (load_pgbackrest_manifest(password, source_backup_path, workspace, &manifest))
   {
      pgmoneta_log_error("Failed to load backup manifest %s%s", source_backup_path, PGBACKREST_BACKUP_MANIFEST);
      goto error;
   }

   if (pgmoneta_csv_writer_init(manifest_path, &writer))
   {
      pgmoneta_log_error("Could not create csv writer for %s", manifest_path);
      goto error;
   }

   pgmoneta_art_iterator_create(manifest->files, &iter);
   while (pgmoneta_art_iterator_next(iter))
   {
      if (pgmoneta_starts_with(iter->key, "pg_wal/summaries/"))
      {
         continue;
      }
      if (migrate_pgbackrest_file(manifests, backup_info, manifest,
                                  pg_manifest, target_data_dir, iter->key,
                                  source_backup_data_path, target_backup_data_path,
                                  data_workspace, writer))
      {
         pgmoneta_log_error("Failed to migrate pgbackrest file: %s from %s to %s", iter->key, source_backup_data_path, target_backup_data_path);
         goto error;
      }
   }

   if (pgmoneta_manifest_write_directory_entries(writer, target_backup_data_path, ""))
   {
      pgmoneta_log_error("Could not record directory layout in %s", target_backup_data_path);
      goto error;
   }

   if (add_pg_manifest_info(manifest, pg_manifest))
   {
      pgmoneta_log_error("Failed to add manifest info to pg manifest");
      goto error;
   }

   //copy WAL segments
   pgmoneta_log_info("Start copying WAL segments to pg_wal");

   if (pgbackrest_handle_pg_wal(start_tli, stop_tli, target_wal_dir,
                                target_backup_data_path, backup_info->archive_start,
                                backup_info->archive_stop, backup_info->lsn_start,
                                backup_info->lsn_stop, pg_manifest))
   {
      pgmoneta_log_error("Failed to copy WAL segments from %s", target_wal_dir);
      goto error;
   }

   if (write_backup_manifest(target_backup_data_path, pg_manifest))
   {
      pgmoneta_log_error("Failed to write manifest to %s", target_backup_data_path);
      goto error;
   }

   if (set_backup_info(target_backup_data_path, backup_info, manifest, &backup))
   {
      pgmoneta_log_error("Failed to set backup info %s", target_backup_path);
      goto error;
   }

   //compress, encrypt, link
   if (conf->compression != COMPRESSION_NONE)
   {
      if (pgmoneta_compress_directory(-1, target_backup_data_path, conf->compression, NULL, NULL))
      {
         pgmoneta_log_error("Failed to compress data directory %s", target_backup_data_path);
         goto error;
      }
   }
   if (conf->encryption != ENCRYPTION_NONE)
   {
      if (pgmoneta_encrypt_directory(-1, target_backup_data_path, NULL, NULL))
      {
         pgmoneta_log_error("Failed to encrypt data directory %s", target_backup_data_path);
         goto error;
      }
   }
   if (prev_backup_info != NULL)
   {
      if (link_backups(target_data_dir, backup_info, prev_backup_info))
      {
         pgmoneta_log_error("Failed to link backup %s to %s", backup_info->target_backup_id, prev_backup_info->target_backup_id);
         goto error;
      }
   }

   backup->backup_size = pgmoneta_directory_size(target_backup_path);
   pgmoneta_log_info("Writing backup info to %s%s/backup.info", target_data_dir, backup->label);
   if (pgmoneta_save_info(target_data_dir, backup))
   {
      pgmoneta_log_error("Failed to write backup info to %s%s/backup.info", target_data_dir, backup->label);
      goto error;
   }
   pgmoneta_art_iterator_destroy(iter);
   pgbackrest_manifest_destroy(manifest);
   pgmoneta_json_destroy(pg_manifest);
   pgmoneta_csv_writer_destroy(writer);
   free(manifest_path);
   free(backup);
   return 0;
error:
   pgmoneta_art_iterator_destroy(iter);
   pgbackrest_manifest_destroy(manifest);
   pgmoneta_json_destroy(pg_manifest);
   pgmoneta_csv_writer_destroy(writer);
   free(manifest_path);
   free(backup);
   return 1;
}

static void
pgbackrest_manifest_create(struct pgbackrest_manifest** manifest)
{
   struct pgbackrest_manifest* m = NULL;
   *manifest = NULL;
   m = malloc(sizeof(struct pgbackrest_backup_info));
   memset(m, 0, sizeof(struct pgbackrest_manifest));
   pgmoneta_art_create(&m->files);
   *manifest = m;
}

static void
pgbackrest_manifest_destroy(struct pgbackrest_manifest* manifest)
{
   if (manifest == NULL)
   {
      return;
   }
   free(manifest->backup_password);
   pgmoneta_art_destroy(manifest->files);
   free(manifest);
}

static int
pgbackrest_manifest_validate(struct pgbackrest_manifest* manifest)
{
   if (manifest == NULL)
   {
      pgmoneta_log_error("Expecting non-NULL manifest");
      goto error;
   }
   if (manifest->files == NULL || manifest->files->size == 0)
   {
      pgmoneta_log_error("Expecting manifest files");
      goto error;
   }
   if (manifest->db_system_id == 0)
   {
      pgmoneta_log_error("Expecting database system ID in the manifest");
      goto error;
   }
   if (manifest->db_version <= 14)
   {
      pgmoneta_log_error("Backup db version is below 15: %" PRIu64, manifest->db_version);
      goto error;
   }
   return 0;
error:
   return 1;
}

static int
load_pgbackrest_manifest(char* password, char* source_backup_path, char* workspace, struct pgbackrest_manifest** manifest)
{
   char manifest_path[MAX_PATH];
   char dest[MAX_PATH];
   struct pgbackrest_manifest* m = NULL;

   *manifest = NULL;

   memset(manifest_path, 0, sizeof(manifest_path));
   memset(dest, 0, sizeof(dest));
   snprintf(manifest_path, sizeof(manifest_path), "%s%s", source_backup_path, PGBACKREST_BACKUP_MANIFEST);
   snprintf(dest, sizeof(dest), "%s%s", workspace, PGBACKREST_BACKUP_MANIFEST);

   //TODO: handle the case where backup is not encrypted
   pgmoneta_log_info("decrypting %s to %s", manifest_path, dest);
   if (pgmoneta_cbc_decrypt_salted_file(CBC_DIGEST_DEFAULT,
                                        false,
                                        (unsigned char*)password,
                                        strlen(password),
                                        manifest_path,
                                        dest))
   {
      pgmoneta_log_error("Failed to decrypt manifest %s to %s", manifest_path, dest);
      goto error;
   }
   pgmoneta_log_info("parsing manifest %s", dest);
   if (parse_pgbackrest_manifest(dest, &m))
   {
      pgmoneta_log_error("Failed to parse manifest at %s", dest);
      goto error;
   }
   if (pgbackrest_manifest_validate(m))
   {
      pgmoneta_log_error("Failed to validate manifest at %s", dest);
      goto error;
   }
   *manifest = m;

   return 0;
error:
   return 1;
}

static int
parse_pgbackrest_manifest(char* path, struct pgbackrest_manifest** manifest)
{
   struct pgbackrest_manifest* m = NULL;
   FILE* file = NULL;
   char buffer[INFO_BUFFER_SIZE];
   char section[128];
   struct json* file_info = NULL;

   *manifest = NULL;
   pgbackrest_manifest_create(&m);
   if (pgmoneta_fopen_secure(path, "r", &file))
   {
      pgmoneta_log_error("Could not open file %s: %s", path, strerror(errno));
      errno = 0;
      goto error;
   }

   while ((fgets(&buffer[0], sizeof(buffer), file)) != NULL)
   {
      char key[INFO_BUFFER_SIZE];
      char value[INFO_BUFFER_SIZE];
      char* ptr = NULL;

      if (buffer[0] == '\n')
      {
         continue;
      }

      if (buffer[0] == '[')
      {
         memset(section, 0, sizeof(section));
         memcpy(section, buffer, strlen(buffer) - 1);
         continue;
      }

      memset(&key[0], 0, sizeof(key));
      memset(&value[0], 0, sizeof(value));

      ptr = strtok(&buffer[0], "=");

      if (ptr == NULL)
      {
         goto error;
      }

      memcpy(&key[0], ptr, strlen(ptr));

      ptr = strtok(NULL, "=");

      if (ptr == NULL)
      {
         goto error;
      }

      memcpy(&value[0], ptr, strlen(ptr) - 1);

      if (pgmoneta_compare_string(section, "[target:file]"))
      {
         if (pgmoneta_json_parse_string(value, &file_info))
         {
            pgmoneta_log_error("unable to parse backup info %s", value);
            goto error;
         }
         // strip the pg_data/ entry
         pgmoneta_art_insert(m->files, &key[PG_DATA_PREFIX_LEN], (uintptr_t)file_info, ValueJSON);
         file_info = NULL;
      }
      else if (pgmoneta_compare_string(PGBACKREST_PASSWORD_KEY, &key[0]))
      {
         // remove the double quotes
         value[strlen(value) - 1] = 0;
         m->backup_password = pgmoneta_append(m->backup_password, &value[1]);
      }
      else if (pgmoneta_compare_string("option-compress-type", &key[0]))
      {
         if (pgmoneta_compare_string(value, "\"none\""))
         {
            m->compression = COMPRESSION_NONE;
         }
         else if (pgmoneta_compare_string(value, "\"gz\""))
         {
            m->compression = COMPRESSION_CLIENT_GZIP;
         }
         else if (pgmoneta_compare_string(value, "\"bz2\""))
         {
            m->compression = COMPRESSION_CLIENT_BZIP2;
         }
         else if (pgmoneta_compare_string(value, "\"lz4\""))
         {
            m->compression = COMPRESSION_CLIENT_LZ4;
         }
         else if (pgmoneta_compare_string(value, "\"zst\""))
         {
            m->compression = COMPRESSION_CLIENT_ZSTD;
         }
         else
         {
            pgmoneta_log_error("Unrecognized compression method %s", value);
            goto error;
         }
      }
      else if (pgmoneta_compare_string("db-system-id", &key[0]))
      {
         if (pgmoneta_string_to_uint64(value, true, &m->db_system_id))
         {
            pgmoneta_log_error("Failed to get system ID from %s", value);
            goto error;
         }
      }
      else if (pgmoneta_compare_string("db-version", &key[0]))
      {
         // remove the double quotes
         value[strlen(value) - 1] = 0;
         if (pgmoneta_string_to_uint64(&value[1], false, &m->db_version))
         {
            pgmoneta_log_error("Failed to get db version from %s", value);
            goto error;
         }
      }
   }

   if (file != NULL)
   {
      fclose(file);
   }

   *manifest = m;
   return 0;
error:
   if (file != NULL)
   {
      fclose(file);
   }

   pgbackrest_manifest_destroy(m);
   return 1;
}

static int
migrate_pgbackrest_file(struct art* manifests, struct pgbackrest_backup_info* backup_info, struct pgbackrest_manifest* manifest, struct json* pg_manifest, char* target_data_dir, char* relative_path, char* source_root_dir, char* target_root_dir, char* workspace_root_dir, struct csv_writer* writer)
{
   char* source_file_path = NULL;
   char* target_file_path = NULL;
   char* workspace_file_path = NULL;
   struct json* file_info = NULL;
   char* reference_backup_id = NULL;

   source_file_path = pgmoneta_format_and_append(source_file_path, "%s%s", source_root_dir, relative_path);
   target_file_path = pgmoneta_format_and_append(target_file_path, "%s%s", target_root_dir, relative_path);
   workspace_file_path = pgmoneta_format_and_append(workspace_file_path, "%s%s", workspace_root_dir, relative_path);

   switch (manifest->compression)
   {
      case COMPRESSION_CLIENT_GZIP:
         source_file_path = pgmoneta_append(source_file_path, ".gz");
         workspace_file_path = pgmoneta_append(workspace_file_path, ".gz");
         break;
      case COMPRESSION_CLIENT_LZ4:
         source_file_path = pgmoneta_append(source_file_path, ".lz4");
         workspace_file_path = pgmoneta_append(workspace_file_path, ".lz4");
         break;
      case COMPRESSION_CLIENT_ZSTD:
         source_file_path = pgmoneta_append(source_file_path, ".zst");
         workspace_file_path = pgmoneta_append(workspace_file_path, ".zst");
         break;
      case COMPRESSION_CLIENT_BZIP2:
         source_file_path = pgmoneta_append(source_file_path, ".bz2");
         workspace_file_path = pgmoneta_append(workspace_file_path, ".bz2");
         break;
      default:
         break;
   }

   file_info = (struct json*)pgmoneta_art_search(manifest->files, relative_path);
   if (file_info == NULL)
   {
      pgmoneta_log_error("Unable to find %s in the manifest", relative_path);
      goto error;
   }

   if (create_file_directory(target_root_dir, workspace_root_dir, relative_path))
   {
      pgmoneta_log_error("Failed to create directory for file %s: %s", target_file_path, strerror(errno));
      errno = 0;
      goto error;
   }

   if (!pgmoneta_json_contains_key(file_info, PGBACKREST_FILE_REFERENCE))
   {
      // full file, decrypt and copy to workspace first
      // TODO: handle non encrypted case
      if (pgmoneta_cbc_decrypt_salted_file(CBC_DIGEST_DEFAULT,
                                           false,
                                           (unsigned char*)manifest->backup_password,
                                           strlen(manifest->backup_password),
                                           source_file_path,
                                           workspace_file_path))
      {
         pgmoneta_log_error("Failed to decrypt %s to %s", source_file_path, target_file_path);
         goto error;
      }
      if (pgmoneta_decompress_file(workspace_file_path, target_file_path, manifest->compression, NULL))
      {
         pgmoneta_log_error("Failed to decompress %s -> %s", workspace_file_path, target_file_path);
         goto error;
      }
   }
   else
   {
      reference_backup_id = (char*)pgmoneta_json_get(file_info, PGBACKREST_FILE_REFERENCE);
      if (reference_backup_id == NULL)
      {
         pgmoneta_log_error("Failed to get reference backup ID");
         goto error;
      }
      if (create_file_placeholder(target_file_path))
      {
         pgmoneta_log_error("Failed to create placeholder for file %s ", target_file_path);
         goto error;
      }
   }

   if (get_and_set_file_manifest(manifests, backup_info, reference_backup_id, target_data_dir, relative_path, manifest, pg_manifest, writer))
   {
      pgmoneta_log_error("Failed to get or set manifest of file %s, backup ID %s", relative_path, backup_info->backup_id);
      goto error;
   }

   free(source_file_path);
   free(target_file_path);
   free(workspace_file_path);
   return 0;
error:
   free(source_file_path);
   free(target_file_path);
   free(workspace_file_path);
   return 1;
}

static int
create_file_directory(char* target_root_dir, char* workspace_root_dir, char* relative_path)
{
   char* path = NULL;
   char* workspace_path = NULL;
   path = pgmoneta_format_and_append(path, "%s%s", target_root_dir, relative_path);
   workspace_path = pgmoneta_format_and_append(workspace_path, "%s%s", workspace_root_dir, relative_path);

   char* dir = dirname(path);
   char* wspc_dir = dirname(workspace_path);

   if (!pgmoneta_exists(dir))
   {
      pgmoneta_log_debug("Creating directory for %s", dir);
      if (pgmoneta_mkdir(dir))
      {
         pgmoneta_log_error("Failed to create dir %s", dir);
         goto error;
      }
   }

   if (!pgmoneta_exists(wspc_dir))
   {
      pgmoneta_log_debug("Creating workspace directory for %s", wspc_dir);
      if (pgmoneta_mkdir(wspc_dir))
      {
         pgmoneta_log_error("Failed to create dir %s", wspc_dir);
         goto error;
      }
   }

   free(path);
   free(workspace_path);
   return 0;
error:
   free(path);
   free(workspace_path);
   return 1;
}

static int
create_file_placeholder(char* target_path)
{
   FILE* file = NULL;
   if (pgmoneta_fopen_secure(target_path, "w", &file))
   {
      pgmoneta_log_error("Unable to create placeholder file %s: %s", target_path, strerror(errno));
      errno = 0;
      goto error;
   }
   fclose(file);
   return 0;
error:
   if (file != NULL)
   {
      fclose(file);
   }
   return 1;
}

static int
migrate_wal_segments(char* workspace, char* source_dir, char* target_dir)
{
   struct dirent* entry;
   struct stat statbuf;
   DIR* d = opendir(source_dir);
   char* from = NULL;
   char* password = NULL;

   // TODO: handle no encryption case (password may not be present)
   if (load_archive_password(source_dir, workspace, &password) || password == NULL)
   {
      pgmoneta_log_error("Failed to load archive password");
      goto error;
   }

   if (d == NULL)
   {
      pgmoneta_log_error("Failed to open directory %s", source_dir);
      goto error;
   }

   while ((entry = readdir(d)))
   {
      if (pgmoneta_compare_string(entry->d_name, ".") || pgmoneta_compare_string(entry->d_name, ".."))
      {
         continue;
      }
      from = pgmoneta_format_and_append(from, "%s%s", source_dir, entry->d_name);

      if (!stat(from, &statbuf))
      {
         if (S_ISDIR(statbuf.st_mode) && is_wal_dir(entry->d_name))
         {
            from = pgmoneta_append(from, "/");
            pgmoneta_log_info("Extracting WAL directory %s", from);
            if (extract_wal_segments(password, from, target_dir))
            {
               pgmoneta_log_error("Failed to extract WAL segments from %s", from);
               goto error;
            }
         }
         // handle history file, in pgbackrest archive only segments are compressed
         else if (S_ISREG(statbuf.st_mode) && pgmoneta_ends_with(entry->d_name, HISTORY_SUFFIX))
         {
            if (extract_wal_history(password, from, entry->d_name, target_dir))
            {
               pgmoneta_log_error("Failed to extract wal history from %s to %s", from, target_dir);
               goto error;
            }
         }
         else
         {
            pgmoneta_log_debug("Skipping directory/path %s", from);
         }
      }

      free(from);
      from = NULL;
   }

   if (d != NULL)
   {
      closedir(d);
   }
   free(from);
   free(password);
   return 0;
error:
   if (d != NULL)
   {
      closedir(d);
   }
   free(from);
   free(password);
   return 1;
}

static int
extract_wal_segments(char* password, char* source_dir, char* target_dir)
{
   struct dirent* entry;
   struct stat statbuf;
   DIR* d = opendir(source_dir);
   char* path = NULL;
   char* target_path = NULL;
   char* final_path = NULL;
   char* seg_name = NULL;
   int compression = COMPRESSION_NONE;

   while ((entry = readdir(d)))
   {
      if (pgmoneta_compare_string(entry->d_name, ".") ||
          pgmoneta_compare_string(entry->d_name, "..") ||
          pgmoneta_contains(entry->d_name, ".backup"))
      {
         continue;
      }

      path = pgmoneta_format_and_append(path, "%s%s", source_dir, entry->d_name);
      target_path = pgmoneta_format_and_append(target_path, "%s%s", target_dir, entry->d_name);

      if (!stat(path, &statbuf))
      {
         if (S_ISREG(statbuf.st_mode))
         {
            if (extract_wal_file_info(entry->d_name, &seg_name, &compression))
            {
               pgmoneta_log_error("Failed to extract WAL file info from %s", entry->d_name);
               goto error;
            }
         }
      }

      final_path = pgmoneta_format_and_append(final_path, "%s%s", target_dir, seg_name);

      if (pgmoneta_cbc_decrypt_salted_file(CBC_DIGEST_DEFAULT,
                                           false,
                                           (unsigned char*)password,
                                           strlen(password),
                                           path,
                                           target_path))
      {
         pgmoneta_log_error("Failed to decrypt WAL segments");
         goto error;
      }

      if (pgmoneta_decompress_file(target_path, final_path, compression, NULL))
      {
         pgmoneta_log_error("Failed to decompress %s to %s, compression mode %d", target_path, final_path, compression);
         goto error;
      }

      free(path);
      free(target_path);
      free(final_path);
      free(seg_name);
      target_path = NULL;
      final_path = NULL;
      path = NULL;
      seg_name = NULL;
   }

   if (d != NULL)
   {
      closedir(d);
   }
   free(path);
   free(target_path);
   free(final_path);
   free(seg_name);
   return 0;
error:

   if (d != NULL)
   {
      closedir(d);
   }
   free(path);
   free(seg_name);
   free(target_path);
   free(final_path);
   return 1;
}

static int
extract_wal_history(char* password, char* source_history, char* history_name, char* target_dir)
{
   char* target_history = NULL;

   target_history = pgmoneta_format_and_append(target_history, "%s%s", target_dir, history_name);
   // TODO: handle the case where backup is not encrypted
   pgmoneta_log_info("Decrypting archive history from %s to %s", source_history, target_history);
   if (pgmoneta_cbc_decrypt_salted_file(CBC_DIGEST_DEFAULT,
                                        false,
                                        (unsigned char*)password,
                                        strlen(password),
                                        source_history,
                                        target_history))
   {
      pgmoneta_log_error("Failed to decrypt archive history at %s", source_history);
      goto error;
   }

   free(target_history);
   return 0;
error:
   free(target_history);
   return 1;
}

static bool
is_wal_dir(char* dir)
{
   if (strlen(dir) != 16)
      return false;

   for (int i = 0; i < 16; i++)
   {
      if (!isxdigit((unsigned char)dir[i]))
      {
         return false;
      }
   }
   return true;
}

static int
extract_wal_file_info(char* file_name, char** seg_name, int* compression)
{
   // example wal file name: 00000001000000000000006A-6df4da8ba173143537d03dcc41e3e8bf72360cc9.gz
   char buffer[MAX_PATH];
   char* token = NULL;
   char* subtoken = NULL;
   char* seg = NULL;
   char* saveptr1 = NULL;
   char* saveptr2 = NULL;
   int c = COMPRESSION_NONE;
   memset(buffer, 0, sizeof(buffer));
   memcpy(buffer, file_name, strlen(file_name));

   // 00000001000000000000006A-6df4da8ba173143537d03dcc41e3e8bf72360cc9
   token = strtok_r(buffer, ".", &saveptr1);
   if (token == NULL)
   {
      pgmoneta_log_error("Failed to tokenize file name %s", file_name);
      goto error;
   }

   // 00000001000000000000006A
   subtoken = strtok_r(token, "-", &saveptr2);
   if (subtoken == NULL)
   {
      pgmoneta_log_error("Failed to tokenize file name token %s", token);
      goto error;
   }

   seg = pgmoneta_append(seg, subtoken);

   // gz
   token = strtok_r(NULL, ".", &saveptr1);
   if (token != NULL)
   {
      if (pgmoneta_compare_string(token, "gz"))
      {
         c = COMPRESSION_CLIENT_GZIP;
      }
      else if (pgmoneta_compare_string(token, "bz2"))
      {
         c = COMPRESSION_CLIENT_BZIP2;
      }
      else if (pgmoneta_compare_string(token, "lz4"))
      {
         c = COMPRESSION_CLIENT_LZ4;
      }
      else if (pgmoneta_compare_string(token, "zst"))
      {
         c = COMPRESSION_CLIENT_ZSTD;
      }
   }

   *seg_name = seg;
   *compression = c;
   return 0;
error:
   free(seg);
   return 1;
}

static int
pgbackrest_handle_pg_wal(uint32_t start_tli, uint32_t stop_tli, char* wal_dir, char* target_backup_data_path, char* wal_start, char* wal_stop, char* lsn_start, char* lsn_stop, struct json* pg_manifest)
{
   char achv_stat_path[MAX_PATH];
   char summaries_path[MAX_PATH];
   char wal_file_from[MAX_PATH];
   char wal_stat_file_to[MAX_PATH];
   char wal_file_to[MAX_PATH];
   char wal_history_from[MAX_PATH];
   char wal_history_to[MAX_PATH];
   DIR* dir = NULL;
   char* history_path = NULL;
   struct json* wal_ranges = NULL;
   struct dirent* entry;
   bool ranges_added = false;

   pgmoneta_log_info("Copying WAL segments ranging from %s to %s", wal_start, wal_stop);

   memset(achv_stat_path, 0, sizeof(achv_stat_path));
   memset(summaries_path, 0, sizeof(summaries_path));

   if (wal_dir == NULL || target_backup_data_path == NULL ||
       wal_start == NULL || wal_stop == NULL ||
       lsn_start == NULL || lsn_stop == NULL ||
       pg_manifest == NULL)
   {
      pgmoneta_log_error("Invalid input for pgbackrest_handle_pg_wal");
      goto error;
   }

   snprintf(achv_stat_path, sizeof(achv_stat_path), "%s" PG_WAL_DIR "archive_status/", target_backup_data_path);
   snprintf(summaries_path, sizeof(summaries_path), "%s" PG_WAL_DIR "summaries/", target_backup_data_path);

   // postgres doesn't actually backup summaries file, so an empty folder is enough
   if (pgmoneta_mkdir(summaries_path))
   {
      pgmoneta_log_error("Failed to create the pg_wal/summaries/ directory");
      goto error;
   }

   if (pgmoneta_mkdir(achv_stat_path))
   {
      pgmoneta_log_error("Failed to create the pg_wal/archive_status/ directory");
      goto error;
   }

   dir = opendir(wal_dir);
   if (dir == NULL)
   {
      pgmoneta_log_error("Failed to open the WAL directory at %s", wal_dir);
      goto error;
   }
   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type != DT_REG)
      {
         continue;
      }
      if (!pgmoneta_is_wal_file(entry->d_name) ||
          strcmp(entry->d_name, wal_start) < 0 ||
          strcmp(entry->d_name, wal_stop) > 0)
      {
         continue;
      }
      memset(wal_file_from, 0, sizeof(wal_file_from));
      memset(wal_file_to, 0, sizeof(wal_file_to));
      memset(wal_stat_file_to, 0, sizeof(wal_stat_file_to));
      snprintf(wal_file_from, sizeof(wal_file_from), "%s%s", wal_dir, entry->d_name);
      snprintf(wal_file_to, sizeof(wal_file_to), "%s" PG_WAL_DIR "%s", target_backup_data_path, entry->d_name);
      // since we are copying the segment, we can safely assume the segnment archive is done
      // if not, the stat file won't be used anyway because pgmoneta relies on streaming replication instead of archive command :)
      snprintf(wal_stat_file_to, sizeof(wal_stat_file_to), "%s" PG_WAL_DIR "%s.done", target_backup_data_path, entry->d_name);
      if (create_file_placeholder(wal_stat_file_to))
      {
         pgmoneta_log_error("Failed to create WAL archive status file %s", wal_stat_file_to);
         goto error;
      }
      if (pgmoneta_copy_file(wal_file_from, wal_file_to, NULL))
      {
         pgmoneta_log_error("Failed to copy WAL segment from %s to %s", wal_file_from, wal_file_to);
         goto error;
      }
      pgmoneta_log_info("Copied wal segment %s to %s" PG_WAL_DIR, entry->d_name, target_backup_data_path);
   }
   closedir(dir);

   if (stop_tli > 1)
   {
      memset(wal_history_to, 0, sizeof(wal_history_to));
      memset(wal_history_from, 0, sizeof(wal_history_from));
      snprintf(wal_history_to, sizeof(wal_history_to), "%s" PG_WAL_DIR "%08x.history", target_backup_data_path, stop_tli);
      snprintf(wal_history_from, sizeof(wal_history_from), "%s%08x.history", wal_dir, stop_tli);
      pgmoneta_log_info("Try copying history file from %s to %s", wal_history_from, wal_history_to);
      if (!pgmoneta_exists(wal_history_from))
      {
         pgmoneta_log_warn("Unable to find history file %s", wal_history_from);
      }
      else
      {
         if (pgmoneta_copy_file(wal_history_from, wal_history_to, NULL))
         {
            pgmoneta_log_error("Failed to copy history file from %s to %s", wal_history_from, wal_history_to);
            goto error;
         }
         history_path = wal_history_from;
      }
   }

   if (build_wal_ranges(lsn_start, lsn_stop, start_tli, stop_tli, history_path, &wal_ranges))
   {
      pgmoneta_log_error("Failed to build the WAL range");
      goto error;
   }

   if (pgmoneta_json_put(pg_manifest, MANIFEST_KEY_WAL_RANGES, (uintptr_t)wal_ranges, ValueJSON))
   {
      pgmoneta_json_destroy(wal_ranges);
      goto error;
   }
   pgmoneta_log_info("Successfully built and added wal ranges to postgres manifest");
   ranges_added = true;

   return 0;
error:
   if (dir != NULL)
   {
      closedir(dir);
   }
   if (!ranges_added)
   {
      pgmoneta_json_destroy(wal_ranges);
   }
   return 1;
}

static int
write_backup_manifest(char* target_backup_data_path, struct json* pg_manifest)
{
   char* manifest_path = NULL;
   manifest_path = pgmoneta_format_and_append(manifest_path, "%sbackup_manifest", target_backup_data_path);
   pgmoneta_log_info("Writing backup manifest to %s", manifest_path);
   if (pgmoneta_write_postgresql_manifest(pg_manifest, manifest_path))
   {
      pgmoneta_log_error("Failed to write PostgreSQL manifest at %s", manifest_path);
      goto error;
   }
   free(manifest_path);
   return 0;
error:
   free(manifest_path);
   return 1;
}

static int
set_backup_info(char* target_backup_data_path, struct pgbackrest_backup_info* backup_info, struct pgbackrest_manifest* manifest, struct backup** backup)
{
   struct backup* bck = NULL;
   char* wal = NULL;
   struct art_iterator* iter = NULL;
   struct muse_configuration* conf = NULL;

   conf = (struct muse_configuration*)shmem;
   bck = (struct backup*)malloc(sizeof(struct backup));
   memset(bck, 0, sizeof(struct backup));

   bck->valid = VALID_TRUE;
   //TODO: handle tablespaces
   bck->number_of_tablespaces = 0;
   bck->compression = conf->compression;
   bck->encryption = conf->encryption;
   memcpy(bck->label, backup_info->target_backup_id, strlen(backup_info->target_backup_id));
   if (pgmoneta_read_wal(target_backup_data_path, &wal))
   {
      pgmoneta_log_error("Failed to get wal from %spg_wal/", target_backup_data_path);
      goto error;
   }
   memcpy(bck->wal, wal, strlen(wal));

   bck->restore_size = backup_info->restore_size;

   if (pgmoneta_art_iterator_create(manifest->files, &iter))
   {
      pgmoneta_log_error("Failed to create files interator");
      goto error;
   }
   while (pgmoneta_art_iterator_next(iter))
   {
      struct json* file = (struct json*)pgmoneta_value_data(iter->value);
      size_t size = (size_t)pgmoneta_json_get(file, "size");
      if (size > bck->biggest_file_size)
      {
         bck->biggest_file_size = size;
      }
   }
   bck->major_version = manifest->db_version;
   //TODO: handle minor version
   bck->keep = false;
   sscanf(backup_info->lsn_start, "%X/%X", &bck->start_lsn_hi32, &bck->start_lsn_lo32);
   sscanf(backup_info->lsn_stop, "%X/%X", &bck->end_lsn_hi32, &bck->end_lsn_lo32);
   if (get_current_timeline(backup_info->archive_start, &bck->start_timeline))
   {
      pgmoneta_log_error("Failed to extract current timeline from %s", backup_info->archive_start);
      goto error;
   }

   if (get_current_timeline(backup_info->archive_stop, &bck->end_timeline))
   {
      pgmoneta_log_error("Failed to extract current timeline from %s", backup_info->archive_stop);
      goto error;
   }
   // TODO: all backups are considered full for now until block level incremental is introduced
   bck->type = TYPE_FULL;

   *backup = bck;

   pgmoneta_art_iterator_destroy(iter);
   free(wal);
   return 0;
error:
   pgmoneta_art_iterator_destroy(iter);
   free(wal);
   return 1;
}

static int
get_current_timeline(char* wal_name, uint32_t* tli)
{
   uint32_t t = 0;
   char wal[9];
   if (!pgmoneta_is_wal_file(wal_name))
   {
      pgmoneta_log_error("%s is not a valid wal segment name", wal_name);
      goto error;
   }
   memset(wal, 0, sizeof(wal));
   memcpy(wal, wal_name, 8);
   t = (uint32_t)strtoul(wal, NULL, 16);

   *tli = t;
   return 0;
error:
   return 1;
}

static int
build_wal_ranges(char* lsn_start, char* lsn_stop, uint32_t start_tli, uint32_t stop_tli, char* history_path, struct json** wal_ranges)
{
   struct json* ranges = NULL;
   struct json* range = NULL;
   char* cur_lsn = NULL;
   struct deque* switch_points = NULL;
   struct timeline_history* entry = NULL;
   struct deque_iterator* iter = NULL;

   pgmoneta_json_create(&ranges);

   if (start_tli != stop_tli)
   {
      if (history_path == NULL || !pgmoneta_exists(history_path))
      {
         pgmoneta_log_error("Failed to find the corresponding history to build WAL ranges at %s", history_path);
         goto error;
      }
      if (read_history(history_path, &switch_points))
      {
         pgmoneta_log_error("Failed to read history at %s", history_path);
         goto error;
      }

      if (pgmoneta_deque_iterator_create(switch_points, &iter))
      {
         pgmoneta_log_error("Failed to create deque iterator for switch points");
         goto error;
      }

      cur_lsn = lsn_start;
      while (pgmoneta_deque_iterator_next(iter))
      {
         entry = (struct timeline_history*)pgmoneta_value_data(iter->value);
         if (entry->tli < start_tli)
         {
            continue;
         }
         if (wal_range_create(entry->tli, cur_lsn, entry->lsn, &range))
         {
            pgmoneta_log_error("Failed to create WAL range");
            goto error;
         }
         pgmoneta_json_append(ranges, (uintptr_t)range, ValueJSON);
         range = NULL;
         cur_lsn = entry->lsn;
      }
      if (wal_range_create(stop_tli, cur_lsn, lsn_stop, &range))
      {
         pgmoneta_log_error("Failed to create WAL range");
         goto error;
      }
      pgmoneta_json_append(ranges, (uintptr_t)range, ValueJSON);
      range = NULL;
   }
   else
   {
      if (wal_range_create(start_tli, lsn_start, lsn_stop, &range))
      {
         pgmoneta_log_error("Failed to create WAL range");
         goto error;
      }
      pgmoneta_json_append(ranges, (uintptr_t)range, ValueJSON);
   }

   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_deque_destroy(switch_points);
   *wal_ranges = ranges;
   return 0;
error:
   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_deque_destroy(switch_points);
   pgmoneta_json_destroy(ranges);
   return 1;
}

static int
read_history(char* history_path, struct deque** switch_points)
{
   struct deque* sp = NULL;
   FILE* file = NULL;
   char line[1024];
   if (pgmoneta_fopen_secure(history_path, "r", &file))
   {
      pgmoneta_log_error("Failed to read history file %s", history_path);
      goto error;
   }

   while (fgets(line, sizeof(line), file) != NULL)
   {
      char* ptr = line;
      uint32_t timeline;
      uint32_t high;
      uint32_t low;
      struct timeline_history* entry = NULL;
      while (isspace((unsigned char)(*ptr)))
      {
         ptr++;
      }

      if (*ptr == '#' || *ptr == '\0')
      {
         continue;
      }
      if (sscanf(ptr, "%u\t%X/%08X", &timeline, &high, &low) != 3)
      {
         pgmoneta_log_error("Failed to parse line %s", ptr);
         goto error;
      }
      if (timeline_history_create(&entry))
      {
         pgmoneta_log_error("Failed to create timeline history entry");
         goto error;
      }
      entry->tli = timeline;
      snprintf(entry->lsn, sizeof(entry->lsn), "%X/%08X", high, low);
      pgmoneta_deque_add(sp, NULL, (uintptr_t)entry, ValueMem);
   }

   *switch_points = sp;
   return 0;
error:
   pgmoneta_deque_destroy(sp);
   return 1;
}

static int
timeline_history_create(struct timeline_history** entry)
{
   struct timeline_history* e = NULL;
   e = malloc(sizeof(struct timeline_history));
   if (e == NULL)
   {
      return 1;
   }
   memset(e, 0, sizeof(struct timeline_history));
   *entry = e;

   return 0;
}

static int
wal_range_create(uint32_t tli, char* start_lsn, char* stop_lsn, struct json** range)
{
   struct json* r = NULL;
   *range = NULL;
   if (pgmoneta_json_create(&r))
   {
      pgmoneta_log_error("Failed to create range json");
      goto error;
   }
   if (pgmoneta_json_put(r, "Timeline", (uintptr_t)tli, ValueUInt32))
   {
      pgmoneta_log_error("Failed to put timeline");
      goto error;
   }
   if (pgmoneta_json_put(r, "Start-LSN", (uintptr_t)start_lsn, ValueString))
   {
      pgmoneta_log_error("Failed to put start LSN");
      goto error;
   }
   if (pgmoneta_json_put(r, "End-LSN", (uintptr_t)stop_lsn, ValueString))
   {
      pgmoneta_log_error("Failed to put end LSN");
      goto error;
   }
   *range = r;
   return 0;
error:
   pgmoneta_json_destroy(r);
   return 1;
}

static int
add_pg_manifest_info(struct pgbackrest_manifest* manifest, struct json* pg_manifest)
{
   if (manifest == NULL || pg_manifest == NULL)
   {
      pgmoneta_log_error("Invalid input for add_pg_manifest_info");
      goto error;
   }
   if (manifest->db_version < 13)
   {
      pgmoneta_log_error("manifest is not supported for postgres version below 13");
      goto error;
   }
   if (manifest->db_version < 17)
   {
      if (pgmoneta_json_put(pg_manifest, MANIFEST_KEY_VERSION, 1, ValueUInt32))
      {
         pgmoneta_log_error("Failed to put manifest version");
         goto error;
      }
   }
   else
   {
      if (pgmoneta_json_put(pg_manifest, MANIFEST_KEY_VERSION, 2, ValueUInt32))
      {
         pgmoneta_log_error("Failed to put manifest version");
         goto error;
      }
      if (pgmoneta_json_put(pg_manifest, MANIFEST_KEY_SYS_IDENTIFIER, manifest->db_system_id, ValueUInt64))
      {
         pgmoneta_log_error("Failed to put system identifier");
         goto error;
      }
   }

   return 0;
error:
   return 1;
}

static int
get_and_set_file_manifest(struct art* manifests, struct pgbackrest_backup_info* backup, char* reference_backup_id, char* target_data_dir, char* relative_path, struct pgbackrest_manifest* manifest, struct json* pg_manifest, struct csv_writer* writer)
{
   char* file_path = NULL;
   struct json* file = NULL;
   struct json* pg_file = NULL;
   struct json* pg_files = NULL;
   time_t timestamp;
   struct json* ref_manifest = NULL;
   struct json_iterator* iter = NULL;
   char* info[MANIFEST_COLUMN_COUNT];

   if (backup == NULL)
   {
      goto error;
   }

   pg_files = (struct json*)pgmoneta_json_get(pg_manifest, MANIFEST_KEY_FILES);
   if (pg_files == NULL)
   {
      pgmoneta_json_create(&pg_files);
      if (pgmoneta_json_put(pg_manifest, MANIFEST_KEY_FILES, (uintptr_t)pg_files, ValueJSONRef))
      {
         pgmoneta_log_error("Failed to put files into manifest");
         goto error;
      }
      if (pgmoneta_art_insert(manifests, backup->backup_id, (uintptr_t)pg_files, ValueJSON))
      {
         pgmoneta_log_error("Failed to put files into manifest cache");
         goto error;
      }
   }

   if (reference_backup_id != NULL)
   {
      bool found = false;
      ref_manifest = (struct json*)pgmoneta_art_search(manifests, reference_backup_id);
      if (ref_manifest == NULL)
      {
         pgmoneta_log_error("failed to get backup %s", reference_backup_id);
         goto error;
      }
      if (pgmoneta_json_iterator_create(ref_manifest, &iter))
      {
         goto error;
      }
      while (pgmoneta_json_iterator_next(iter))
      {
         char* p = NULL;
         pg_file = (struct json*)pgmoneta_value_data(iter->value);
         p = (char*)pgmoneta_json_get(pg_file, MANIFEST_FILE_KEY_PATH);
         if (pgmoneta_compare_string(p, relative_path))
         {
            found = true;
            if (pgmoneta_json_append(pg_files, (uintptr_t)pg_file, ValueJSONRef))
            {
               pgmoneta_log_error("Failed to add file to file array: %s", relative_path);
               goto error;
            }
            break;
         }
      }
      if (!found)
      {
         pgmoneta_log_error("Failed to find referenced file manifest %s in backup %s", relative_path, reference_backup_id);
         goto error;
      }
   }
   else
   {
      file = (struct json*)pgmoneta_art_search(manifest->files, relative_path);
      if (file == NULL)
      {
         pgmoneta_log_error("Failed to find %s inside manifest", relative_path);
         goto error;
      }
      timestamp = (time_t)pgmoneta_json_get(file, "timestamp");
      if (timestamp == 0)
      {
         pgmoneta_log_error("Failed to find timestamp inside file manifest %s", relative_path);
         goto error;
      }

      file_path = pgmoneta_format_and_append(file_path, "%s%s/data/%s", target_data_dir, backup->target_backup_id, relative_path);

      //TODO: relative path might not be the manifest path for table spaces, need to handle differently
      if (pgmoneta_get_file_manifest(file_path, relative_path, timestamp, &pg_file))
      {
         pgmoneta_log_error("Failed to get manifest of file %s", file_path);
         goto error;
      }
      if (pgmoneta_json_append(pg_files, (uintptr_t)pg_file, ValueJSON))
      {
         pgmoneta_log_error("Failed to add file to file array: %s", relative_path);
         goto error;
      }
   }

   //TODO: relative path might not be the manifest path for table spaces, need to handle differently
   info[MANIFEST_PATH_INDEX] = relative_path;
   info[MANIFEST_CHECKSUM_INDEX] = (char*)pgmoneta_json_get(pg_file, MANIFEST_FILE_KEY_CHECKSUM);
   pgmoneta_csv_write(writer, MANIFEST_COLUMN_COUNT, info);

   free(file_path);
   pgmoneta_json_iterator_destroy(iter);
   return 0;
error:
   free(file_path);
   pgmoneta_json_iterator_destroy(iter);
   return 1;
}

static int
link_backups(char* data_dir, struct pgbackrest_backup_info* from_bck, struct pgbackrest_backup_info* to_bck)
{
   char* from = NULL;
   char* to = NULL;
   char* from_manifest = NULL;
   char* to_manifest = NULL;
   struct art* deleted_files = NULL;
   struct art* changed_files = NULL;
   struct art* added_files = NULL;

   from = pgmoneta_format_and_append(from, "%s%s/data/", data_dir, from_bck->target_backup_id);
   to = pgmoneta_format_and_append(to, "%s%s/data/", data_dir, to_bck->target_backup_id);

   from_manifest = pgmoneta_format_and_append(from_manifest, "%s%s/backup.manifest", data_dir, from_bck->target_backup_id);
   to_manifest = pgmoneta_format_and_append(to_manifest, "%s%s/backup.manifest", data_dir, to_bck->target_backup_id);

   if (pgmoneta_compare_manifests(to_manifest, from_manifest, &deleted_files, &changed_files, &added_files))
   {
      pgmoneta_log_error("Failed to compare manifest from %s to %s", from_manifest, to_manifest);
      goto error;
   }
   if (pgmoneta_link_manifest(from, to, from, changed_files, added_files, NULL))
   {
      pgmoneta_log_error("Failed to link manifest from %s to %s", from, to);
      goto error;
   }

   free(from);
   free(to);
   free(from_manifest);
   free(to_manifest);
   pgmoneta_art_destroy(changed_files);
   pgmoneta_art_destroy(added_files);
   pgmoneta_art_destroy(deleted_files);

   return 0;

error:
   free(from);
   free(to);
   free(from_manifest);
   free(to_manifest);
   pgmoneta_art_destroy(changed_files);
   pgmoneta_art_destroy(added_files);
   pgmoneta_art_destroy(deleted_files);
   return 1;
}