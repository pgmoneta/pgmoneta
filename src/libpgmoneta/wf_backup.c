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

/* pgmoneta */
#include <pgmoneta.h>
#include <info.h>
#include <logging.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <security.h>
#include <server.h>
#include <tablespace.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int basebackup_setup(int, char*, struct node*, struct node**);
static int basebackup_execute(int, char*, struct node*, struct node**);
static int basebackup_teardown(int, char*, struct node*, struct node**);

struct workflow*
pgmoneta_workflow_create_basebackup(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->setup = &basebackup_setup;
   wf->execute = &basebackup_execute;
   wf->teardown = &basebackup_teardown;
   wf->next = NULL;

   return wf;
}

static int
basebackup_setup(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   return 0;
}

static int
basebackup_execute(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   time_t start_time;
   int status;
   char* root = NULL;
   char* d = NULL;
   unsigned long size = 0;
   int usr;
   SSL* ssl = NULL;
   int socket = -1;
   int total_seconds;
   int hours;
   int minutes;
   int seconds;
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
   struct node* o_root = NULL;
   struct node* o_to = NULL;
   struct configuration* config;
   struct message* basebackup_msg = NULL;
   struct message* tablespace_msg = NULL;
   struct stream_buffer* buffer = NULL;
   struct query_response* response = NULL;
   struct tablespace* tablespaces = NULL;
   struct tablespace* current_tablespace = NULL;
   struct tuple* tup = NULL;
   struct token_bucket* bucket = NULL;

   start_time = time(NULL);

   pgmoneta_memory_init();

   config = (struct configuration*)shmem;

   // default is 0
   if (config->backup_max_rate)
   {
      bucket = (struct token_bucket*)malloc(sizeof(struct token_bucket));
      if (pgmoneta_token_bucket_init(bucket, config->backup_max_rate))
      {
         pgmoneta_log_error("failed to initialize the token bucket for backup.\n");
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

   if (config->servers[server].version == 0)
   {
      if (pgmoneta_server_get_version(ssl, socket, server))
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
   label = pgmoneta_append(label, "pgmoneta_base_backup_");
   label = pgmoneta_append(label, identifier);
   pgmoneta_create_base_backup_message(config->servers[server].version, label, true, "SHA256",
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
   root = pgmoneta_get_server_backup_identifier(server, identifier);

   pgmoneta_mkdir(root);
   if (config->servers[server].version < 15)
   {
      if (pgmoneta_receive_archive_files(ssl, socket, buffer, root, tablespaces, config->servers[server].version, bucket))
      {
         pgmoneta_log_error("Backup: Could not backup %s", config->servers[server].name);

         pgmoneta_create_info(root, identifier, 0);

         goto error;
      }
   }
   else
   {
      if (pgmoneta_receive_archive_stream(ssl, socket, buffer, root, tablespaces, bucket))
      {
         pgmoneta_log_error("Backup: Could not backup %s", config->servers[server].name);

         pgmoneta_create_info(root, identifier, 0);

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
   if (pgmoneta_ends_with(root, "/"))
   {
      snprintf(old_label_path, MAX_PATH, "%sdata/%s", root, "backup_label.old");
   }
   else
   {
      snprintf(old_label_path, MAX_PATH, "%s/data/%s", root, "backup_label.old");
   }

   if (pgmoneta_exists(old_label_path))
   {
      pgmoneta_delete_file(old_label_path);
   }

   // receive and ignore the last result set, it's just a summary
   pgmoneta_consume_data_row_messages(ssl, socket, buffer, &response);

   total_seconds = (int) difftime(time(NULL), start_time);
   hours = total_seconds / 3600;
   minutes = (total_seconds % 3600) / 60;
   seconds = total_seconds % 60;

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

   pgmoneta_log_debug("Base: %s/%s (Elapsed: %s)", config->servers[server].name, identifier, &elapsed[0]);

   d = pgmoneta_get_server_backup_identifier_data(server, identifier);

   size = pgmoneta_directory_size(d);
   pgmoneta_read_wal(d, &wal);
   pgmoneta_read_checkpoint_info(d, &chkptpos);

   if (pgmoneta_create_node_string(root, "root", &o_root))
   {
      goto error;
   }
   pgmoneta_append_node(o_nodes, o_root);

   if (pgmoneta_create_node_string(d, "to", &o_to))
   {
      goto error;
   }
   pgmoneta_append_node(o_nodes, o_to);

   pgmoneta_create_info(root, identifier, 1);
   pgmoneta_update_info_string(root, INFO_WAL, wal);
   pgmoneta_update_info_unsigned_long(root, INFO_RESTORE, size);
   pgmoneta_update_info_string(root, INFO_VERSION, version);
   pgmoneta_update_info_string(root, INFO_MINOR_VERSION, minor_version);
   pgmoneta_update_info_bool(root, INFO_KEEP, false);
   pgmoneta_update_info_string(root, INFO_START_WALPOS, startpos);
   pgmoneta_update_info_string(root, INFO_END_WALPOS, endpos);
   pgmoneta_update_info_unsigned_long(root, INFO_START_TIMELINE, start_timeline);
   pgmoneta_update_info_unsigned_long(root, INFO_END_TIMELINE, end_timeline);
   // in case of parsing error
   if (chkptpos != NULL)
   {
      pgmoneta_update_info_string(root, INFO_CHKPT_WALPOS, chkptpos);
   }

   current_tablespace = tablespaces;
   while (current_tablespace != NULL)
   {
      char key[MISC_LENGTH];

      number_of_tablespaces++;
      pgmoneta_update_info_unsigned_long(root, INFO_TABLESPACES, number_of_tablespaces);

      snprintf(key, sizeof(key) - 1, "TABLESPACE%d", number_of_tablespaces);
      pgmoneta_update_info_string(root, key, current_tablespace->name);

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
   pgmoneta_free_copy_message(basebackup_msg);
   pgmoneta_free_copy_message(tablespace_msg);
   pgmoneta_free_query_response(response);
   pgmoneta_token_bucket_destroy(bucket);
   free(root);
   free(label);
   free(d);
   free(wal);

   return 0;

error:
   pgmoneta_close_ssl(ssl);
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   pgmoneta_memory_destroy();
   pgmoneta_memory_stream_buffer_free(buffer);
   pgmoneta_free_tablespaces(tablespaces);
   pgmoneta_free_copy_message(basebackup_msg);
   pgmoneta_free_query_response(response);
   pgmoneta_token_bucket_destroy(bucket);
   free(root);
   free(label);
   free(d);
   free(wal);

   return 1;
}

static int
basebackup_teardown(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   return 0;
}
