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
   char* server_backup = NULL;
   unsigned long size = 0;
   int usr;
   SSL* ssl = NULL;
   int socket = -1;
   double basebackup_elapsed_time;
   int hours;
   int minutes;
   double seconds;
   char elapsed[128];
   char* tag = NULL;
   char* manifest_path = NULL;
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
   struct backup* backup = NULL;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   pgmoneta_dump_art(nodes);

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP_BASE));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP_DATA));
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_BACKUP));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);
   backup = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);
   backup_base = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_BASE);
   backup_data = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_DATA);
   server_backup = (char*)pgmoneta_art_search(nodes, NODE_SERVER_BACKUP);

   pgmoneta_log_debug("Basebackup (execute): %s", config->common.servers[server].name, label);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

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
      pgmoneta_server_info(server, ssl, socket);

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

   tag = pgmoneta_append(tag, "pgmoneta_");
   tag = pgmoneta_append(tag, label);

   pgmoneta_create_base_backup_message(config->common.servers[server].version, false, tag, true,
                                       config->compression_type, config->compression_level,
                                       &basebackup_msg);

   status = pgmoneta_write_message(ssl, socket, basebackup_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   // Receive the first result set, which contains the WAL starting point
   if (pgmoneta_consume_data_row_messages(server, ssl, socket, buffer, &response))
   {
      goto error;
   }
   memset(startpos, 0, sizeof(startpos));
   memcpy(startpos, response->tuples[0].data[0], strlen(response->tuples[0].data[0]));
   start_timeline = atoi(response->tuples[0].data[1]);
   pgmoneta_free_query_response(response);
   response = NULL;

   pgmoneta_mkdir(backup_base);

   if (config->common.servers[server].version < 15)
   {
      if (pgmoneta_receive_archive_files(server, ssl, socket, buffer, backup_base, tablespaces, bucket, network_bucket))
      {
         pgmoneta_log_error("Backup: Could not backup %s", config->common.servers[server].name);

         backup->valid = VALID_FALSE;
         snprintf(backup->label, sizeof(backup->label), "%s", label);
         if (pgmoneta_save_info(server_backup, backup))
         {
            pgmoneta_log_error("Backup: Could not save backup %s", label);
            goto error;
         }

         goto error;
      }
   }
   else
   {
      if (pgmoneta_receive_archive_stream(server, ssl, socket, buffer, backup_base, tablespaces, bucket, network_bucket))
      {
         pgmoneta_log_error("Backup: Could not backup %s", config->common.servers[server].name);

         backup->valid = VALID_FALSE;
         snprintf(backup->label, sizeof(backup->label), "%s", label);
         if (pgmoneta_save_info(server_backup, backup))
         {
            pgmoneta_log_error("Backup: Could not save backup %s", label);
            goto error;
         }

         goto error;
      }
   }

   // Receive the final result set, which contains the WAL ending point
   if (pgmoneta_consume_data_row_messages(server, ssl, socket, buffer, &response))
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
   pgmoneta_consume_data_row_messages(server, ssl, socket, buffer, &response);

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

   size = pgmoneta_directory_size(backup_data);
   biggest_file_size = pgmoneta_biggest_file(backup_data);

   pgmoneta_read_wal(backup_data, &wal);
   pgmoneta_read_checkpoint_info(backup_data, &chkptpos);

   backup->valid = VALID_TRUE;
   snprintf(backup->label, sizeof(backup->label), "%s", label);
   backup->number_of_tablespaces = 0;
   backup->compression = config->compression_type;
   backup->encryption = config->encryption;
   snprintf(backup->wal, sizeof(backup->wal), "%s", wal);
   backup->restore_size = size;
   backup->biggest_file_size = biggest_file_size;
   backup->major_version = atoi(version);
   backup->minor_version = atoi(minor_version);
   backup->keep = false;
   sscanf(startpos, "%X/%X", &backup->start_lsn_hi32, &backup->start_lsn_lo32);
   sscanf(endpos, "%X/%X", &backup->end_lsn_hi32, &backup->end_lsn_lo32);
   backup->start_timeline = start_timeline;
   backup->end_timeline = end_timeline;
   backup->basebackup_elapsed_time = basebackup_elapsed_time;

   backup->type = TYPE_FULL;
   // in case of parsing error
   if (chkptpos != NULL)
   {
      sscanf(chkptpos, "%X/%X", &backup->checkpoint_lsn_hi32, &backup->checkpoint_lsn_lo32);
   }

   current_tablespace = tablespaces;
   backup->number_of_tablespaces = 0;

   while (current_tablespace != NULL && backup->number_of_tablespaces < MAX_NUMBER_OF_TABLESPACES)
   {
      int i = backup->number_of_tablespaces;

      snprintf(backup->tablespaces[i], sizeof(backup->tablespaces[i]), "tblspc_%s", current_tablespace->name);
      snprintf(backup->tablespaces_oids[i], sizeof(backup->tablespaces_oids[i]), "%u", current_tablespace->oid);
      snprintf(backup->tablespaces_paths[i], sizeof(backup->tablespaces_paths[i]), "%s", current_tablespace->path);

      backup->number_of_tablespaces++;
      current_tablespace = current_tablespace->next;
   }

   if (pgmoneta_save_info(server_backup, backup))
   {
      pgmoneta_log_error("Backup: Could not save backup %s", label);
      goto error;
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
   free(manifest_path);
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
   pgmoneta_free_query_response(response);
   pgmoneta_token_bucket_destroy(bucket);
   pgmoneta_token_bucket_destroy(network_bucket);
   free(manifest_path);
   free(chkptpos);
   free(tag);
   free(wal);

   return 1;
}
