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
#include <csv.h>
#include <info.h>
#include <logging.h>
#include <manifest.h>
#include <security.h>
#include <utils.h>
#include <workflow.h>

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char* manifest_name(void);
static int manifest_execute(char*, struct art*);
static int write_directory_entries(struct csv_writer* writer, char* base, char* relative);

/*
 * A directory holding no files has no entry of its own in the PostgreSQL
 * manifest, so it would be lost by anything that reconstructs a backup from the
 * file list alone. Record every directory with a trailing slash so those
 * consumers can tell the two apart; the checksum column is not meaningful for a
 * directory and is written as "-".
 */
static int
write_directory_entries(struct csv_writer* writer, char* base, char* relative)
{
   DIR* dir = NULL;
   struct dirent* entry = NULL;
   char path[MAX_PATH];
   char child[MAX_PATH];
   char row[MAX_PATH];
   char probe[MAX_PATH];
   struct stat sb;
   bool is_dir = false;
   char* info[MANIFEST_COLUMN_COUNT];

   if (pgmoneta_snprintf(path, sizeof(path), "%s%s%s", base,
                         strlen(relative) > 0 ? "/" : "", relative) <= 0)
   {
      return 1;
   }

   if (!(dir = opendir(path)))
   {
      return 1;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      is_dir = (entry->d_type == DT_DIR);

      if (pgmoneta_compare_string(entry->d_name, ".") || pgmoneta_compare_string(entry->d_name, ".."))
      {
         continue;
      }

      /* some filesystems do not populate d_type */
      if (entry->d_type == DT_UNKNOWN &&
          pgmoneta_snprintf(probe, sizeof(probe), "%s/%s", path, entry->d_name) > 0 &&
          stat(probe, &sb) == 0)
      {
         is_dir = S_ISDIR(sb.st_mode);
      }

      if (!is_dir)
      {
         continue;
      }

      if (pgmoneta_snprintf(child, sizeof(child), "%s%s%s",
                            relative, strlen(relative) > 0 ? "/" : "", entry->d_name) <= 0 ||
          pgmoneta_snprintf(row, sizeof(row), "%s/", child) <= 0)
      {
         closedir(dir);
         return 1;
      }

      info[MANIFEST_PATH_INDEX] = row;
      info[MANIFEST_CHECKSUM_INDEX] = "-";
      pgmoneta_csv_write(writer, MANIFEST_COLUMN_COUNT, info);

      if (write_directory_entries(writer, base, child))
      {
         closedir(dir);
         return 1;
      }
   }

   closedir(dir);

   return 0;
}

struct workflow*
pgmoneta_create_manifest(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->name = &manifest_name;
   wf->setup = &pgmoneta_common_setup;
   wf->execute = &manifest_execute;
   wf->teardown = &pgmoneta_common_teardown;
   wf->next = NULL;

   return wf;
}

static char*
manifest_name(void)
{
   return PHASE_NAME_MANIFEST;
}

static int
manifest_execute(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   struct timespec start_t;
   struct timespec end_t;
   char* server_backup = NULL;
   char* backup_base = NULL;
   char* backup_data = NULL;
   char* manifest_orig = NULL;
   char* manifest = NULL;
   char* incremental = NULL;
   char* key_path[1] = {"Files"};
   struct backup* backup = NULL;
   struct json_reader* reader = NULL;
   struct json* entry = NULL;
   struct csv_writer* writer = NULL;
   char file_path[MAX_PATH];
   char* info[MANIFEST_COLUMN_COUNT];
   struct main_configuration* config;

   struct json* m = NULL;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   pgmoneta_dump_art(nodes);

   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP_BASE));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP_DATA));
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_BACKUP));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("Manifest (execute): %s/%s", config->common.servers[server].name, label);

   backup = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);
   backup_base = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_BASE);
   backup_data = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_DATA);
   server_backup = (char*)pgmoneta_art_search(nodes, NODE_SERVER_BACKUP);

   incremental = (char*)pgmoneta_art_search(nodes, NODE_INCREMENTAL_BASE);

   manifest = pgmoneta_append(manifest, backup_base);
   if (!pgmoneta_ends_with(manifest, "/"))
   {
      manifest = pgmoneta_append(manifest, "/");
   }
   manifest = pgmoneta_append(manifest, "backup.manifest");

   manifest_orig = pgmoneta_append(manifest_orig, backup_data);
   if (!pgmoneta_ends_with(manifest_orig, "/"))
   {
      manifest_orig = pgmoneta_append(manifest_orig, "/");
   }
   manifest_orig = pgmoneta_append(manifest_orig, "backup_manifest");

   /* create manifest file manually for incremental backups for PostgreSQL version 14-16 */
   if (incremental && config->common.servers[server].version < 17)
   {
      if (pgmoneta_generate_manifest(1, 0, backup_data, backup, &m, server, nodes))
      {
         pgmoneta_log_error("Could not generate the manifest");
         goto error;
      }

      if (pgmoneta_write_postgresql_manifest(m, manifest_orig))
      {
         pgmoneta_log_error("Could not write file %s to disk", manifest_orig);
         goto error;
      }

      pgmoneta_json_destroy(m);
   }

   if (pgmoneta_csv_writer_init(manifest, &writer))
   {
      pgmoneta_log_error("Could not create csv writer for %s", manifest);
      goto error;
   }

   if (pgmoneta_json_reader_init(manifest_orig, &reader))
   {
      goto error;
   }
   if (pgmoneta_json_locate(reader, key_path, 1))
   {
      pgmoneta_log_error("Could not locate files array in manifest %s", manifest_orig);
      goto error;
   }

   // convert original manifest file
   while (pgmoneta_json_next_array_item(reader, &entry))
   {
      memset(file_path, 0, MAX_PATH);
      pgmoneta_snprintf(file_path, MAX_PATH, "%s", (char*)pgmoneta_json_get(entry, "Path"));
      info[MANIFEST_PATH_INDEX] = file_path;
      info[MANIFEST_CHECKSUM_INDEX] = (char*)pgmoneta_json_get(entry, "Checksum");
      pgmoneta_csv_write(writer, MANIFEST_COLUMN_COUNT, info);
      pgmoneta_json_destroy(entry);
      entry = NULL;
   }

   if (write_directory_entries(writer, backup_data, ""))
   {
      pgmoneta_log_error("Could not record directory layout in %s", manifest);
      goto error;
   }

   pgmoneta_permission(manifest, 6, 0, 0);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   backup->manifest_elapsed_time = pgmoneta_compute_duration(start_t, end_t);

   if (pgmoneta_save_info(server_backup, backup))
   {
      goto error;
   }

   pgmoneta_json_reader_close(reader);
   pgmoneta_csv_writer_destroy(writer);
   pgmoneta_json_destroy(entry);
   free(manifest);
   free(manifest_orig);

   return 0;
error:
   pgmoneta_json_destroy(m);
   pgmoneta_json_reader_close(reader);
   pgmoneta_csv_writer_destroy(writer);
   pgmoneta_json_destroy(entry);
   free(manifest);
   free(manifest_orig);

   return 1;
}
