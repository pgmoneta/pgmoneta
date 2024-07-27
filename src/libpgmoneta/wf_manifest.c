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
#include <csv.h>
#include <json.h>
#include <logging.h>
#include <manifest.h>
#include <utils.h>
#include <workflow.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int manifest_setup(int, char*, struct deque*);
static int manifest_execute_build(int, char*, struct deque*);
static int manifest_teardown(int, char*, struct deque*);

struct workflow*
pgmoneta_workflow_create_manifest(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->setup = &manifest_setup;
   wf->execute = &manifest_execute_build;
   wf->teardown = &manifest_teardown;
   wf->next = NULL;

   return wf;
}

static int
manifest_setup(int server, char* identifier, struct deque* nodes)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Manifest (setup): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   return 0;
}

static int
manifest_execute_build(int server, char* identifier, struct deque* nodes)
{
   char* root = NULL;
   char* data = NULL;
   char* manifest_orig = NULL;
   char* manifest = NULL;
   char* key_path[1] = {"Files"};
   char* backup_dir = NULL;
   struct backup* backup = NULL;
   struct json_reader* reader = NULL;
   struct json* entry = NULL;
   struct csv_writer* writer = NULL;
   char* tblspc = NULL;
   char file_path[MAX_PATH];
   char* info[MANIFEST_COLUMN_COUNT];
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Manifest (execute): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   backup_dir = pgmoneta_get_server_backup(server);
   root = pgmoneta_get_server_backup_identifier(server, identifier);
   data = pgmoneta_get_server_backup_identifier_data(server, identifier);
   manifest = pgmoneta_append(manifest, root);
   manifest = pgmoneta_append(manifest, "backup.manifest");
   manifest_orig = pgmoneta_append(manifest_orig, data);
   manifest_orig = pgmoneta_append(manifest_orig, "backup_manifest");

   pgmoneta_get_backup(backup_dir, identifier, &backup);

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
      snprintf(file_path, MAX_PATH, "%s", pgmoneta_json_get_string(entry, "Path"));
      info[MANIFEST_PATH_INDEX] = file_path;
      info[MANIFEST_CHECKSUM_INDEX] = pgmoneta_json_get_string(entry, "Checksum");
      pgmoneta_csv_write(writer, MANIFEST_COLUMN_COUNT, info);
      pgmoneta_json_free(entry);
      entry = NULL;
   }

   pgmoneta_json_reader_close(reader);
   pgmoneta_csv_writer_destroy(writer);
   pgmoneta_json_free(entry);
   free(root);
   free(data);
   free(manifest);
   free(manifest_orig);
   free(backup_dir);
   free(backup);
   free(tblspc);
   return 0;

error:
   pgmoneta_json_reader_close(reader);
   pgmoneta_csv_writer_destroy(writer);
   pgmoneta_json_free(entry);
   free(root);
   free(data);
   free(manifest);
   free(manifest_orig);
   free(backup_dir);
   free(backup);
   free(tblspc);
   return 1;
}

static int
manifest_teardown(int server, char* identifier, struct deque* nodes)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Manifest (teardown): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   return 0;
}
