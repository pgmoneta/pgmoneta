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
#include <backup.h>
#include <info.h>
#include <logging.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <security.h>
#include <server.h>
#include <stdint.h>
#include <tablespace.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int basebackup_setup(int, char*, struct deque*);
static int basebackup_execute(int, char*, struct deque*);
static int basebackup_teardown(int, char*, struct deque*);

struct workflow*
pgmoneta_create_basebackup(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->setup = &basebackup_setup;
   wf->execute = &basebackup_execute;
   wf->teardown = &basebackup_teardown;
   wf->next = NULL;

   return wf;
}

static int
basebackup_setup(int server, char* identifier, struct deque* nodes)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Basebackup (setup): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   return 0;
}

static int
basebackup_execute(int server, char* identifier, struct deque* nodes)
{
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
   char* label = NULL;
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
   struct configuration* config;
   struct message* basebackup_msg = NULL;
   struct message* tablespace_msg = NULL;
   struct stream_buffer* buffer = NULL;
   struct query_response* response = NULL;
   struct tablespace* tablespaces = NULL;
   struct tablespace* current_tablespace = NULL;
   struct tuple* tup = NULL;
   struct token_bucket* bucket = NULL;
   struct token_bucket* network_bucket = NULL;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Basebackup (execute): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);

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
      bucket = (struct token_bucket*)malloc(sizeof(struct token_bucket));
      if (pgmoneta_token_bucket_init(bucket, network_max_rate))
      {
         pgmoneta_log_error("failed to initialize the network token bucket for backup.\n");
         goto error;
      }
   }
   usr = -1;
   // find the corresponding user's index of the given server
   for (int i = 0; usr == -1 && i < config->number_of_users; i++)
   {
      if (!strcmp(config->servers[server].username, config->users[i].username))
      {
         usr = i;
      }
   }
   // establish a connection, with replication flag set
   if (pgmoneta_server_authenticate(server, "postgres", config->users[usr].username, config->users[usr].password, false, &ssl, &socket) != AUTH_SUCCESS)
   {
      pgmoneta_log_info("Invalid credentials for %s", config->users[usr].username);
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
   snprintf(version, sizeof(version), "%d", config->servers[server].version);
   memset(minor_version, 0, sizeof(minor_version));
   snprintf(minor_version, sizeof(minor_version), "%d", config->servers[server].minor_version);

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

   if (pgmoneta_server_authenticate(server, "postgres", config->users[usr].username, config->users[usr].password, true, &ssl, &socket) != AUTH_SUCCESS)
   {
      pgmoneta_log_info("Invalid credentials for %s", config->users[usr].username);
      goto error;
   }

   label = pgmoneta_append(label, "pgmoneta_");
   label = pgmoneta_append(label, identifier);

   hash = config->servers[server].manifest;
   if (hash == HASH_ALGORITHM_DEFAULT)
   {
      hash = config->manifest;
   }

   pgmoneta_create_base_backup_message(config->servers[server].version, label, true, hash,
                                       config->compression_type, config->compression_level,
                                       &basebackup_msg);

   status = pgmoneta_write_message(ssl, socket, basebackup_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   pgmoneta_memory_stream_buffer_init(&buffer);
   // Receive the first result set, which contains the WAL starting point
   if (pgmoneta_consume_data_row_messages(ssl, socket, buffer, &response))
   {
      goto error;
   }
   else
   {
      memset(startpos, 0, sizeof(startpos));
      memcpy(startpos, response->tuples[0].data[0], strlen(response->tuples[0].data[0]));
      start_timeline = atoi(response->tuples[0].data[1]);
      pgmoneta_free_query_response(response);
      response = NULL;
   }

   // create the root dir
   backup_base = pgmoneta_get_server_backup_identifier(server, identifier);

   pgmoneta_mkdir(backup_base);
   if (config->servers[server].version < 15)
   {
      if (pgmoneta_receive_archive_files(ssl, socket, buffer, backup_base, tablespaces, bucket, network_bucket))
      {
         pgmoneta_log_error("Backup: Could not backup %s", config->servers[server].name);

         pgmoneta_create_info(backup_base, identifier, 0);

         goto error;
      }
   }
   else
   {
      if (pgmoneta_receive_archive_stream(ssl, socket, buffer, backup_base, tablespaces, bucket, network_bucket))
      {
         pgmoneta_log_error("Backup: Could not backup %s", config->servers[server].name);

         pgmoneta_create_info(backup_base, identifier, 0);

         goto error;
      }
   }

   // Receive the final result set, which contains the WAL ending point
   if (pgmoneta_consume_data_row_messages(ssl, socket, buffer, &response))
   {
      goto error;
   }
   else
   {
      memset(endpos, 0, sizeof(endpos));
      memcpy(endpos, response->tuples[0].data[0], strlen(response->tuples[0].data[0]));
      end_timeline = atoi(response->tuples[0].data[1]);
      pgmoneta_free_query_response(response);
      response = NULL;
   }

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

   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);

   basebackup_elapsed_time = pgmoneta_compute_duration(start_t, end_t);
   hours = (int)basebackup_elapsed_time / 3600;
   minutes = ((int)basebackup_elapsed_time % 3600) / 60;
   seconds = (int)basebackup_elapsed_time % 60 + (basebackup_elapsed_time - ((long)basebackup_elapsed_time));

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%.4f", hours, minutes, seconds);

   pgmoneta_log_debug("Base: %s/%s (Elapsed: %s)", config->servers[server].name, identifier, &elapsed[0]);

   backup_data = pgmoneta_get_server_backup_identifier_data(server, identifier);

   size = pgmoneta_directory_size(backup_data);
   pgmoneta_read_wal(backup_data, &wal);
   pgmoneta_read_checkpoint_info(backup_data, &chkptpos);
   biggest_file_size = pgmoneta_biggest_file(backup_data);

   if (pgmoneta_deque_add(nodes, NODE_BACKUP_BASE, (uintptr_t)backup_base, ValueString))
   {
      goto error;
   }

   if (pgmoneta_deque_add(nodes, NODE_BACKUP_DATA, (uintptr_t)backup_data, ValueString))
   {
      goto error;
   }

   pgmoneta_create_info(backup_base, identifier, 1);
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
   free(chkptpos);
   free(label);
   free(wal);

   return 0;

error:

   if (backup_base == NULL)
   {
      backup_base = pgmoneta_get_server_backup_identifier(server, identifier);
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
   pgmoneta_free_tablespaces(tablespaces);
   pgmoneta_free_message(basebackup_msg);
   pgmoneta_free_message(tablespace_msg);
   pgmoneta_free_query_response(response);
   pgmoneta_token_bucket_destroy(bucket);
   pgmoneta_token_bucket_destroy(network_bucket);
   free(backup_base);
   free(backup_data);
   free(chkptpos);
   free(label);
   free(wal);

   return 1;
}

static int
basebackup_teardown(int server, char* identifier, struct deque* nodes)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Basebackup (teardown): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   return 0;
}
