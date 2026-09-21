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
#include <deque.h>
#include <files.h>
#include <logging.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <security.h>
#include <server.h>
#include <stream.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <stdlib.h>

#define CONFIGURATION_CHUNK_SIZE 16384

static char* configuration_files[] = {"postgresql.conf", "pg_hba.conf", "pg_ident.conf"};

static char* configuration_name(void);
static int configuration_execute(char*, struct art*);
static void fetch_file(int, SSL*, int, char*, char*, char*, char*, struct deque*);
static bool in_backup(char*, char*, char*);

struct workflow*
pgmoneta_create_configuration(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->name = &configuration_name;
   wf->setup = &pgmoneta_common_setup;
   wf->execute = &configuration_execute;
   wf->teardown = &pgmoneta_common_teardown;
   wf->next = NULL;

   return wf;
}

static char*
configuration_name(void)
{
   return "Configuration";
}

static int
configuration_execute(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   int usr = -1;
   char* backup_data = NULL;
   char* data_directory = NULL;
   char* label = NULL;
   char* suffix = NULL;
   SSL* ssl = NULL;
   int socket = -1;
   bool captured = false;
   int number_of_files = sizeof(configuration_files) / sizeof(configuration_files[0]);
   bool plain = false;
   struct deque* files = NULL;
   struct message* query_msg = NULL;
   struct query_response* response = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   pgmoneta_dump_art(nodes);
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP_DATA));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   backup_data = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_DATA);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);
   plain = pgmoneta_art_contains_key(nodes, NODE_INCREMENTAL_BASE) && config->common.servers[server].version < 17;

   pgmoneta_log_debug("Configuration (execute): %s/%s", config->common.servers[server].name, label);

   if (pgmoneta_extraction_get_suffix(config->compression_type, config->common.encryption, &suffix) == 0)
   {
      captured = true;

      for (int i = 0; captured && i < number_of_files; i++)
      {
         captured = in_backup(backup_data, configuration_files[i], suffix);
      }
   }

   free(suffix);

   if (captured)
   {
      pgmoneta_log_debug("Configuration: %s has its configuration files in the data directory",
                         config->common.servers[server].name);
      return 0;
   }

   pgmoneta_memory_init();

   for (int i = 0; usr == -1 && i < config->common.number_of_users; i++)
   {
      if (pgmoneta_compare_string(config->common.servers[server].username, config->common.users[i].username))
      {
         usr = i;
      }
   }

   if (usr == -1 ||
       pgmoneta_server_authenticate(server, "postgres", config->common.users[usr].username,
                                    config->common.users[usr].password, false, &ssl, &socket) != AUTH_SUCCESS)
   {
      pgmoneta_log_warn("Configuration: Unable to connect to %s", config->common.servers[server].name);
      goto done;
   }

   if (pgmoneta_create_query_message("SELECT current_setting('data_directory'), current_setting('config_file'), "
                                     "current_setting('hba_file'), current_setting('ident_file');",
                                     &query_msg) != MESSAGE_STATUS_OK ||
       pgmoneta_query_execute(ssl, socket, query_msg, &response) ||
       response == NULL || response->number_of_columns != 4 ||
       pgmoneta_query_response_get_data(response, 0) == NULL)
   {
      pgmoneta_log_warn("Configuration: Unable to get the configuration file locations for %s (needs pg_read_all_settings)",
                        config->common.servers[server].name);
      goto done;
   }

   data_directory = pgmoneta_append(data_directory, pgmoneta_query_response_get_data(response, 0));
   if (!pgmoneta_ends_with(data_directory, "/"))
   {
      data_directory = pgmoneta_append_char(data_directory, '/');
   }

   /* PG14-16 incremental writes plain files and generates the manifest from data/ */
   if (!plain && pgmoneta_deque_create(false, &files))
   {
      pgmoneta_log_warn("Configuration: Unable to track the configuration files for %s", config->common.servers[server].name);
      goto done;
   }

   for (int i = 0; i < number_of_files; i++)
   {
      fetch_file(server, ssl, socket, data_directory, pgmoneta_query_response_get_data(response, i + 1),
                 backup_data, configuration_files[i], files);
   }

   if (files != NULL)
   {
      pgmoneta_art_insert(nodes, NODE_CONFIGURATION_FILES, (uintptr_t)files, ValueDeque);
   }

done:

   pgmoneta_free_query_response(response);
   pgmoneta_free_message(query_msg);
   free(data_directory);

   if (ssl != NULL)
   {
      pgmoneta_close_ssl(ssl);
   }
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }

   pgmoneta_memory_destroy();

   return 0;
}

static void
fetch_file(int server, SSL* ssl, int socket, char* data_directory, char* path, char* backup_data, char* name, struct deque* files)
{
   int offset = 0;
   int length = 0;
   uint8_t end = 0;
   uint8_t* data = NULL;
   char* to = NULL;
   char* dest = NULL;
   struct vfile* writer = NULL;
   struct streamer* streamer = NULL;
   struct hasher* hasher = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (path == NULL || pgmoneta_starts_with(path, data_directory))
   {
      return;
   }

   to = pgmoneta_append(to, backup_data);
   to = pgmoneta_append(to, name);

   if (pgmoneta_streamer_create(STREAMER_MODE_BACKUP,
                                files != NULL ? config->common.encryption : ENCRYPTION_NONE,
                                files != NULL ? config->compression_type : COMPRESSION_NONE,
                                &streamer) ||
       (files != NULL && pgmoneta_hasher_create("SHA512", &hasher)) ||
       streamer->get_dest_file_name(streamer, to, &dest) ||
       pgmoneta_vfile_create_local(dest, "wb", &writer))
   {
      goto error;
   }

   pgmoneta_streamer_add_destination(streamer, writer);

   do
   {
      if (pgmoneta_server_read_binary_file(server, ssl, path, offset, CONFIGURATION_CHUNK_SIZE, socket, &data, &length))
      {
         goto error;
      }

      if (length > 0 &&
          (pgmoneta_streamer_write(streamer, data, length, false) ||
           (hasher != NULL && pgmoneta_hasher_update(hasher, data, length, false))))
      {
         goto error;
      }

      offset += length;
      free(data);
      data = NULL;
   }
   while (length == CONFIGURATION_CHUNK_SIZE);

   if (pgmoneta_streamer_write(streamer, &end, 0, true) ||
       (hasher != NULL && pgmoneta_hasher_update(hasher, &end, 0, true)))
   {
      goto error;
   }

   pgmoneta_streamer_destroy(streamer);
   streamer = NULL;

   if (hasher != NULL && pgmoneta_deque_add(files, name, (uintptr_t)hasher->hash, ValueString))
   {
      goto error;
   }

   pgmoneta_log_debug("Configuration: %s -> %s", path, dest);

   if (hasher != NULL)
   {
      pgmoneta_hasher_destroy(hasher);
   }
   free(to);
   free(dest);

   return;

error:

   pgmoneta_log_warn("Configuration: Unable to back up %s for %s", path, config->common.servers[server].name);

   pgmoneta_streamer_destroy(streamer);

   if (hasher != NULL)
   {
      pgmoneta_hasher_destroy(hasher);
   }

   if (dest != NULL)
   {
      pgmoneta_delete_file(dest, NULL);
   }

   free(data);
   free(to);
   free(dest);
}

static bool
in_backup(char* backup_data, char* name, char* suffix)
{
   bool found = false;
   char* path = NULL;

   path = pgmoneta_append(path, backup_data);
   path = pgmoneta_append(path, name);
   found = pgmoneta_exists(path);

   path = pgmoneta_append(path, suffix);
   found = found || pgmoneta_exists(path);

   free(path);

   return found;
}
