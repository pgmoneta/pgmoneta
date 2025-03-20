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
#include <pgmoneta.h>
#include <art.h>
#include <csv.h>
#include <json.h>
#include <logging.h>
#include <manifest.h>
#include <utils.h>
#include <workflow.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* manifest_name(void);
static int manifest_execute(char*, struct art*);

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
   return "Manifest";
}

static int
manifest_execute(char* name, struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double manifest_elapsed_time;
   char* backup_base = NULL;
   char* backup_data = NULL;
   char* manifest_orig = NULL;
   char* manifest = NULL;
   char* key_path[1] = {"Files"};
   struct backup* backup = NULL;
   struct json_reader* reader = NULL;
   struct json* entry = NULL;
   struct csv_writer* writer = NULL;
   char file_path[MAX_PATH];
   char* info[MANIFEST_COLUMN_COUNT];
   struct main_configuration* config;

   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);

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

   pgmoneta_log_debug("Manifest (execute): %s/%s", config->common.servers[server].name, label);

   if (pgmoneta_workflow_nodes(server, label, nodes, &backup))
   {
      goto error;
   }

   backup_base = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_BASE);
   backup_data = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_DATA);

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
      snprintf(file_path, MAX_PATH, "%s", (char*)pgmoneta_json_get(entry, "Path"));
      info[MANIFEST_PATH_INDEX] = file_path;
      info[MANIFEST_CHECKSUM_INDEX] = (char*)pgmoneta_json_get(entry, "Checksum");
      pgmoneta_csv_write(writer, MANIFEST_COLUMN_COUNT, info);
      pgmoneta_json_destroy(entry);
      entry = NULL;
   }

   pgmoneta_json_reader_close(reader);
   pgmoneta_csv_writer_destroy(writer);
   pgmoneta_json_destroy(entry);
   free(backup);
   free(manifest);
   free(manifest_orig);

   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
   manifest_elapsed_time = pgmoneta_compute_duration(start_t, end_t);

   pgmoneta_update_info_double(backup_base, INFO_MANIFEST_ELAPSED, manifest_elapsed_time);
   return 0;

error:
   pgmoneta_json_reader_close(reader);
   pgmoneta_csv_writer_destroy(writer);
   pgmoneta_json_destroy(entry);
   free(backup);
   free(manifest);
   free(manifest_orig);

   return 1;
}
