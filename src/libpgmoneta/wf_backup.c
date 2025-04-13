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
#include <achv.h>
#include <backup.h>
#include <logging.h>
#include <network.h>
#include <security.h>
#include <server.h>
#include <tablespace.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* basebackup_name(void);
static int basebackup_execute(char*, struct art*);

static int send_upload_manifest(SSL* ssl, int socket);
static int upload_manifest(SSL* ssl, int socket, char* path);

struct workflow*
pgmoneta_create_basebackup(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->name = &basebackup_name;
   wf->setup = &pgmoneta_common_setup;
   wf->execute = &basebackup_execute;
   wf->teardown = &pgmoneta_common_teardown;
   wf->next = NULL;

   return wf;
}

static char*
basebackup_name(void)
{
   return "Base backup";
}

static int
basebackup_execute(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   struct timespec start_t;
   struct timespec end_t;
   int status;
   char* backup_base = NULL;
   char* backup_data = NULL;
   unsigned long size = 0;
   int usr;
   SSL* ssl = NULL;
   int socket = -1;
   double basebackup_elapsed_time;
   int hours;
   int minutes;
   double seconds;
   char elapsed[128];
   int number_of_tablespaces = 0;
   char* tag = NULL;
   char* incremental = NULL;
   char* incremental_label = NULL;
   char* manifest_path = NULL;
   char* old_manifest_path = NULL;
   char version[10];
   char minor_version[10];
   char* wal = NULL;
   char startpos[20];
   char endpos[20];
   char* chkptpos = NULL;
   uint32_t start_timeline = 0;
   uint32_t end_timeline = 0;
   char old_label_path[MAX_PATH];
   int backup_max_rate;
   int network_max_rate;
   int hash;
   uint64_t biggest_file_size;
   struct main_configuration* config;
   struct message* basebackup_msg = NULL;
   struct message* tablespace_msg = NULL;
   struct stream_buffer* buffer = NULL;
   struct query_response* response = NULL;
   struct tablespace* tablespaces = NULL;
   struct tablespace* current_tablespace = NULL;
   struct tuple* tup = NULL;
   struct token_bucket* bucket = NULL;
   struct token_bucket* network_bucket = NULL;

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

   pgmoneta_log_debug("Basebackup (execute): %s", config->common.servers[server].name, label);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   incremental = (char*)pgmoneta_art_search(nodes, NODE_INCREMENTAL_BASE);
   incremental_label = (char*)pgmoneta_art_search(nodes, NODE_INCREMENTAL_LABEL);

   if ((incremental != NULL && incremental_label == NULL) ||
       (incremental == NULL && incremental_label != NULL))
   {
      pgmoneta_log_error("base and label for incremental should either be both NULL or both non-NULL");
      goto error;
   }

   pgmoneta_memory_init();

   backup_max_rate = pgmoneta_get_backup_max_rate(server);
   if (backup_max_rate)
   {
      bucket = (struct token_bucket*)malloc(sizeof(struct token_bucket));
      if (pgmoneta_token_bucket_init(bucket, backup_max_rate))
      {
         pgmoneta_log_error("failed to initialize the token bucket for backup.\n");
         goto error;
      }
   }

   network_max_rate = pgmoneta_get_network_max_rate(server);
   if (network_max_rate)
   {
      network_bucket = (struct token_bucket*)malloc(sizeof(struct token_bucket));
      if (pgmoneta_token_bucket_init(network_bucket, network_max_rate))
      {
         pgmoneta_log_error("failed to initialize the network token bucket for backup.\n");
         goto error;
      }
   }
   usr = -1;
   // find the corresponding user's index of the given server
   for (int i = 0; usr == -1 && i < config->common.number_of_users; i++)
   {
      if (!strcmp(config->common.servers[server].username, config->common.users[i].username))
      {
         usr = i;
      }
   }
   // establish a connection, with replication flag set
   if (pgmoneta_server_authenticate(server, "postgres", config->common.users[usr].username, config->common.users[usr].password, false, &ssl, &socket) != AUTH_SUCCESS)
   {
      pgmoneta_log_info("Invalid credentials for %s", config->common.users[usr].username);
      goto error;
   }

   if (!pgmoneta_server_valid(server))
   {
      pgmoneta_server_info(server);

      if (!pgmoneta_server_valid(server))
      {
         goto error;
      }
   }
   memset(version, 0, sizeof(version));
   snprintf(version, sizeof(version), "%d", config->common.servers[server].version);
   memset(minor_version, 0, sizeof(minor_version));
   snprintf(minor_version, sizeof(minor_version), "%d", config->common.servers[server].minor_version);

   pgmoneta_create_query_message("SELECT spcname, pg_tablespace_location(oid) FROM pg_tablespace;", &tablespace_msg);
   if (pgmoneta_query_execute(ssl, socket, tablespace_msg, &response) || response == NULL)
   {
      goto error;
   }

   tup = response->tuples;
   while (tup != NULL)
   {
      char* tablespace_name = tup->data[0];
      char* tablespace_path = tup->data[1];

      if (tablespace_name != NULL && tablespace_path != NULL)
      {
         pgmoneta_log_debug("tablespace_name: %s", tablespace_name);
         pgmoneta_log_debug("tablespace_path: %s", tablespace_path);

         if (tablespaces == NULL)
         {
            pgmoneta_create_tablespace(tablespace_name, tablespace_path, &tablespaces);
         }
         else
         {
            struct tablespace* append = NULL;

            pgmoneta_create_tablespace(tablespace_name, tablespace_path, &append);
            pgmoneta_append_tablespace(&tablespaces, append);
         }
      }
      tup = tup->next;
   }
   pgmoneta_free_query_response(response);
   response = NULL;
   pgmoneta_close_ssl(ssl);
   pgmoneta_disconnect(socket);

   if (pgmoneta_server_authenticate(server, "postgres", config->common.users[usr].username, config->common.users[usr].password, true, &ssl, &socket) != AUTH_SUCCESS)
   {
      pgmoneta_log_info("Invalid credentials for %s", config->common.users[usr].username);
      goto error;
   }

   pgmoneta_memory_stream_buffer_init(&buffer);

   if (incremental != NULL)
   {
      // send UPLOAD_MANIFEST
      if (send_upload_manifest(ssl, socket))
      {
         pgmoneta_log_error("Fail to send UPLOAD_MANIFEST to server %s", config->common.servers[server].name);
         goto error;
      }
      manifest_path = pgmoneta_append(NULL, incremental);
      manifest_path = pgmoneta_append(manifest_path, "data/backup_manifest");
      old_manifest_path = pgmoneta_append(NULL, incremental);
      old_manifest_path = pgmoneta_append(old_manifest_path, "backup_manifest.old");

      // use the old manifest because postgres doesn't recognize our own manifest,
      // we can remove this when we have a format converter
      if (pgmoneta_exists(old_manifest_path))
      {
         if (upload_manifest(ssl, socket, old_manifest_path))
         {
            pgmoneta_log_error("Fail to upload manifest to server %s", config->common.servers[server].name);
            goto error;
         }
      }
      else
      {
         if (upload_manifest(ssl, socket, manifest_path))
         {
            pgmoneta_log_error("Fail to upload manifest to server %s", config->common.servers[server].name);
            goto error;
         }
      }

      // receive and ignore the result set for UPLOAD_MANIFEST
      if (pgmoneta_consume_data_row_messages(ssl, socket, buffer, &response))
      {
         goto error;
      }
      pgmoneta_free_query_response(response);
      response = NULL;
   }

   tag = pgmoneta_append(tag, "pgmoneta_");
   tag = pgmoneta_append(tag, label);

   hash = config->common.servers[server].manifest;
   if (hash == HASH_ALGORITHM_DEFAULT)
   {
      hash = config->manifest;
   }

   pgmoneta_create_base_backup_message(config->common.servers[server].version, incremental != NULL, tag, true, hash,
                                       config->compression_type, config->compression_level,
                                       &basebackup_msg);

   status = pgmoneta_write_message(ssl, socket, basebackup_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   // Receive the first result set, which contains the WAL starting point
   if (pgmoneta_consume_data_row_messages(ssl, socket, buffer, &response))
   {
      goto error;
   }
   memset(startpos, 0, sizeof(startpos));
   memcpy(startpos, response->tuples[0].data[0], strlen(response->tuples[0].data[0]));
   start_timeline = atoi(response->tuples[0].data[1]);
   pgmoneta_free_query_response(response);
   response = NULL;

   // create the root dir
   backup_base = pgmoneta_get_server_backup_identifier(server, label);

   pgmoneta_mkdir(backup_base);
   if (config->common.servers[server].version < 15)
   {
      if (pgmoneta_receive_archive_files(ssl, socket, buffer, backup_base, tablespaces, bucket, network_bucket))
      {
         pgmoneta_log_error("Backup: Could not backup %s", config->common.servers[server].name);

         pgmoneta_create_info(backup_base, label, 0);

         goto error;
      }
   }
   else
   {
      if (pgmoneta_receive_archive_stream(ssl, socket, buffer, backup_base, tablespaces, bucket, network_bucket))
      {
         pgmoneta_log_error("Backup: Could not backup %s", config->common.servers[server].name);

         pgmoneta_create_info(backup_base, label, 0);

         goto error;
      }
   }

   // Receive the final result set, which contains the WAL ending point
   if (pgmoneta_consume_data_row_messages(ssl, socket, buffer, &response))
   {
      goto error;
   }
   memset(endpos, 0, sizeof(endpos));
   memcpy(endpos, response->tuples[0].data[0], strlen(response->tuples[0].data[0]));
   end_timeline = atoi(response->tuples[0].data[1]);
   pgmoneta_free_query_response(response);
   response = NULL;

   // remove backup_label.old if it exists
   memset(old_label_path, 0, MAX_PATH);
   if (pgmoneta_ends_with(backup_base, "/"))
   {
      snprintf(old_label_path, MAX_PATH, "%sdata/%s", backup_base, "backup_label.old");
   }
   else
   {
      snprintf(old_label_path, MAX_PATH, "%s/data/%s", backup_base, "backup_label.old");
   }

   if (pgmoneta_exists(old_label_path))
   {
      if (pgmoneta_exists(old_label_path))
      {
         pgmoneta_delete_file(old_label_path, NULL);
      }
      else
      {
         pgmoneta_log_debug("%s doesn't exists", old_label_path);
      }
   }

   // receive and ignore the last result set, it's just a summary
   pgmoneta_consume_data_row_messages(ssl, socket, buffer, &response);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   basebackup_elapsed_time = pgmoneta_compute_duration(start_t, end_t);
   hours = (int)basebackup_elapsed_time / 3600;
   minutes = ((int)basebackup_elapsed_time % 3600) / 60;
   seconds = (int)basebackup_elapsed_time % 60 + (basebackup_elapsed_time - ((long)basebackup_elapsed_time));

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%.4f", hours, minutes, seconds);

   pgmoneta_log_debug("Base: %s/%s (Elapsed: %s)", config->common.servers[server].name, label, &elapsed[0]);

   backup_data = pgmoneta_get_server_backup_identifier_data(server, label);

   size = pgmoneta_directory_size(backup_data);
   pgmoneta_read_wal(backup_data, &wal);
   pgmoneta_read_checkpoint_info(backup_data, &chkptpos);
   biggest_file_size = pgmoneta_biggest_file(backup_data);

   if (pgmoneta_art_insert(nodes, NODE_BACKUP_BASE, (uintptr_t)backup_base, ValueString))
   {
      goto error;
   }

   if (pgmoneta_art_insert(nodes, NODE_BACKUP_DATA, (uintptr_t)backup_data, ValueString))
   {
      goto error;
   }

   pgmoneta_create_info(backup_base, label, 1);
   pgmoneta_update_info_string(backup_base, INFO_WAL, wal);
   pgmoneta_update_info_unsigned_long(backup_base, INFO_RESTORE, size);
   pgmoneta_update_info_unsigned_long(backup_base, INFO_BIGGEST_FILE, biggest_file_size);
   pgmoneta_update_info_string(backup_base, INFO_MAJOR_VERSION, version);
   pgmoneta_update_info_string(backup_base, INFO_MINOR_VERSION, minor_version);
   pgmoneta_update_info_bool(backup_base, INFO_KEEP, false);
   pgmoneta_update_info_string(backup_base, INFO_START_WALPOS, startpos);
   pgmoneta_update_info_string(backup_base, INFO_END_WALPOS, endpos);
   pgmoneta_update_info_unsigned_long(backup_base, INFO_START_TIMELINE, start_timeline);
   pgmoneta_update_info_unsigned_long(backup_base, INFO_END_TIMELINE, end_timeline);
   pgmoneta_update_info_unsigned_long(backup_base, INFO_HASH_ALGORITHM, hash);
   pgmoneta_update_info_double(backup_base, INFO_BASEBACKUP_ELAPSED, basebackup_elapsed_time);

   if (incremental != NULL)
   {
      pgmoneta_update_info_unsigned_long(backup_base, INFO_TYPE, TYPE_INCREMENTAL);
      pgmoneta_update_info_string(backup_base, INFO_PARENT, incremental_label);
   }
   else
   {
      pgmoneta_update_info_unsigned_long(backup_base, INFO_TYPE, TYPE_FULL);
   }
   // in case of parsing error
   if (chkptpos != NULL)
   {
      pgmoneta_update_info_string(backup_base, INFO_CHKPT_WALPOS, chkptpos);
   }

   current_tablespace = tablespaces;
   while (current_tablespace != NULL)
   {
      char key[MISC_LENGTH];
      char tblname[MAX_PATH];

      snprintf(&tblname[0], MAX_PATH, "tblspc_%s", current_tablespace->name);

      number_of_tablespaces++;
      pgmoneta_update_info_unsigned_long(backup_base, INFO_TABLESPACES, number_of_tablespaces);

      snprintf(key, sizeof(key) - 1, "TABLESPACE%d", number_of_tablespaces);
      pgmoneta_update_info_string(backup_base, key, tblname);

      snprintf(key, sizeof(key) - 1, "TABLESPACE_OID%d", number_of_tablespaces);
      pgmoneta_update_info_unsigned_long(backup_base, key, current_tablespace->oid);

      snprintf(key, sizeof(key) - 1, "TABLESPACE_PATH%d", number_of_tablespaces);
      pgmoneta_update_info_string(backup_base, key, current_tablespace->path);

      current_tablespace = current_tablespace->next;
   }
   pgmoneta_close_ssl(ssl);
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   pgmoneta_memory_destroy();
   pgmoneta_memory_stream_buffer_free(buffer);
   pgmoneta_free_tablespaces(tablespaces);
   pgmoneta_free_message(basebackup_msg);
   pgmoneta_free_message(tablespace_msg);
   pgmoneta_free_query_response(response);
   pgmoneta_token_bucket_destroy(bucket);
   pgmoneta_token_bucket_destroy(network_bucket);
   free(backup_base);
   free(backup_data);
   free(manifest_path);
   free(old_manifest_path);
   free(chkptpos);
   free(tag);
   free(wal);

   return 0;

error:

   if (backup_base == NULL)
   {
      backup_base = pgmoneta_get_server_backup_identifier(server, label);
   }

   if (pgmoneta_exists(backup_base))
   {
      pgmoneta_delete_directory(backup_base);
   }

   pgmoneta_close_ssl(ssl);
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   pgmoneta_memory_destroy();
   pgmoneta_memory_stream_buffer_free(buffer);
   pgmoneta_free_message(tablespace_msg);
   pgmoneta_free_tablespaces(tablespaces);
   pgmoneta_free_message(basebackup_msg);
   pgmoneta_free_message(tablespace_msg);
   pgmoneta_free_query_response(response);
   pgmoneta_token_bucket_destroy(bucket);
   pgmoneta_token_bucket_destroy(network_bucket);
   free(backup_base);
   free(backup_data);
   free(manifest_path);
   free(old_manifest_path);
   free(chkptpos);
   free(tag);
   free(wal);

   return 1;
}

static int
send_upload_manifest(SSL* ssl, int socket)
{
   struct message* msg = NULL;
   int status;
   pgmoneta_create_query_message("UPLOAD_MANIFEST", &msg);
   status = pgmoneta_write_message(ssl, socket, msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   pgmoneta_free_message(msg);
   return 0;

error:
   pgmoneta_free_message(msg);
   return 1;
}

static int
upload_manifest(SSL* ssl, int socket, char* path)
{
   FILE* manifest = NULL;
   size_t nbytes = 0;
   char buffer[65536];

   manifest = fopen(path, "r");
   if (manifest == NULL)
   {
      pgmoneta_log_error("Upload manifest: failed to open manifest file at %s", path);
      goto error;
   }
   while ((nbytes = fread(buffer, 1, sizeof(buffer), manifest)) > 0)
   {
      if (pgmoneta_send_copy_data(ssl, socket, buffer, nbytes))
      {
         pgmoneta_log_error("Upload manifest: failed to send copy data");
         goto error;
      }
   }
   if (pgmoneta_send_copy_done_message(ssl, socket))
   {
      goto error;
   }

   if (manifest != NULL)
   {
      fclose(manifest);
   }
   return 0;

error:
   if (manifest != NULL)
   {
      fclose(manifest);
   }
   return 1;
}
