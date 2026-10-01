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
#include <configuration.h>
#include <logging.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>
#include <walfile.h>
#include <walbridge/walbridge.h>
#include <walbridge/lsn_map.h>
#include <walbridge/migration_engine.h>
#include <walbridge/wal_receiver.h>
#include <walbridge/wal_sender.h>

/* system */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern bool enable_translation;

static int
walbridge_read_configuration(char* configuration_path);

static int
walbridge_run_show(char* name, char* out, size_t outsz, SSL* ssl, int socket)
{
   char query[64];
   struct message* query_msg = NULL;
   struct query_response* response = NULL;

   pgmoneta_snprintf(query, sizeof(query), "SHOW %s;", name);
   if (pgmoneta_create_query_message(query, &query_msg) != MESSAGE_STATUS_OK)
   {
      return 1;
   }
   if (pgmoneta_query_execute(ssl, socket, query_msg, &response) ||
       response == NULL || response->tuples == NULL ||
       response->tuples->data == NULL || response->tuples->data[0] == NULL)
   {
      pgmoneta_free_query_response(response);
      pgmoneta_free_message(query_msg);
      return 1;
   }
   pgmoneta_snprintf(out, outsz, "%s", response->tuples->data[0]);
   pgmoneta_free_query_response(response);
   pgmoneta_free_message(query_msg);
   return 0;
}

/* Cache the primary's parameters (wal_level, data_checksums, wal_segment_size)
 * in shared memory so the receiver and sender answer handshake queries and
 * build the translated stream consistently. A failure is not fatal: the old
 * always-recompute behaviour is kept as the safe fallback.
 */
static void
walbridge_fetch_server_info(int srv)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   SSL* ssl = NULL;
   int socket = -1;
   int usr = -1;
   char value[MISC_LENGTH];

   for (int i = 0; i < config->common.number_of_users; i++)
   {
      if (pgmoneta_compare_string(config->common.servers[srv].username,
                                  config->common.users[i].username))
      {
         usr = i;
         break;
      }
   }
   if (usr == -1)
   {
      pgmoneta_log_warn("walbridge: no matching user for server %s; keeping default parameters",
                        config->common.servers[srv].name);
      return;
   }

   /* the message layer (used by the handshake below) needs its buffers */
   pgmoneta_memory_init();

   if (pgmoneta_server_authenticate(srv, "postgres", config->common.users[usr].username,
                                    config->common.users[usr].password, false, &ssl, &socket) != AUTH_SUCCESS)
   {
      pgmoneta_log_warn("walbridge: could not connect to %s to cache server parameters; "
                        "keeping default parameters",
                        config->common.servers[srv].name);
      return;
   }

   config->common.servers[srv].checksums = true;
   if (walbridge_run_show("data_checksums", value, sizeof(value), ssl, socket))
   {
      pgmoneta_log_warn("walbridge: SHOW data_checksums failed; keeping default parameters");
   }
   else
   {
      config->common.servers[srv].checksums = pgmoneta_compare_string("on", value);
      pgmoneta_log_info("walbridge: primary data_checksums %s",
                        config->common.servers[srv].checksums ? "on" : "off");
   }

   config->common.servers[srv].valid = false;
   if (walbridge_run_show("wal_level", value, sizeof(value), ssl, socket))
   {
      pgmoneta_log_warn("walbridge: SHOW wal_level failed; keeping default parameters");
   }
   else
   {
      if (pgmoneta_compare_string("replica", value) || pgmoneta_compare_string("logical", value))
      {
         config->common.servers[srv].valid = true;
      }
      pgmoneta_log_info("walbridge: primary wal_level %s",
                        config->common.servers[srv].valid ? "replica/logical" : "minimal");
   }

   if (walbridge_run_show("wal_segment_size", value, sizeof(value), ssl, socket))
   {
      pgmoneta_log_warn("walbridge: SHOW wal_segment_size failed; keeping default parameters");
   }
   else
   {
      int mb = atoi(value);
      if (mb > 0)
      {
         config->common.servers[srv].wal_size = mb * 1024 * 1024;
         pgmoneta_log_info("walbridge: primary wal_segment_size %dMB", mb);
      }
   }

   pgmoneta_close_ssl(ssl);
   pgmoneta_disconnect(socket);
}

int
pgmoneta_walbridge_start(char* configuration_path, char** argv)
{
   int srv = 0;
   char* lsn_path = NULL;
   char downstream_dir[MAX_PATH];
   struct lsn_map* map = NULL;
   pid_t receiver_pid;

   if (walbridge_read_configuration(configuration_path))
   {
      goto error;
   }

   enable_translation = true;
   pgmoneta_log_info("walbridge: starting PoC for server %d", srv);

   /* Cache the primary's parameters in shared memory for the receiver/sender */
   walbridge_fetch_server_info(srv);

   /* Downstream store and LSN map file under the server WAL directory */
   {
      char* wal_dir = pgmoneta_get_server_wal(srv);
      if (!wal_dir)
      {
         pgmoneta_log_error("walbridge: could not determine server WAL directory");
         goto error;
      }
      pgmoneta_snprintf(downstream_dir, sizeof(downstream_dir), "%s/walbridge", wal_dir);

      lsn_path = pgmoneta_append(lsn_path, wal_dir);
      if (!pgmoneta_ends_with(lsn_path, "/"))
      {
         lsn_path = pgmoneta_append_char(lsn_path, '/');
      }
      lsn_path = pgmoneta_append(lsn_path, "walbridge.lsnmap");
   }

   /* Do NOT wipe the downstream store or LSN map here: persisted state lets a
    * restart pick up exactly where the previous run left off. The receiver
    * resumes from <wal dir>/walbridge/state, and the LSN map is loaded below. */

   if (pgmoneta_lsn_map_create(lsn_path, &map))
   {
      pgmoneta_log_error("walbridge: failed to create LSN map at %s", lsn_path);
      free(lsn_path);
      goto error;
   }

   pgmoneta_log_info("walbridge: LSN map loaded (%s)", lsn_path);

   /* Run the receiver (WAL client + translation) in a child process */
   receiver_pid = fork();
   if (receiver_pid == -1)
   {
      pgmoneta_log_error("walbridge: fork failed: %m");
      pgmoneta_lsn_map_destroy(map);
      free(lsn_path);
      goto error;
   }
   if (receiver_pid == 0)
   {
      /* child: receiver (it forks the existing WAL client itself) */
      pgmoneta_walbridge_run_receiver(srv, argv, map);
      _exit(0);
   }

   /* parent: serve the translated stream to the replica */
   pgmoneta_walbridge_run_sender(srv, downstream_dir, lsn_path);

   pgmoneta_log_info("walbridge: sender stopped; waiting for receiver");
   waitpid(receiver_pid, NULL, 0);

   pgmoneta_lsn_map_destroy(map);
   map = NULL;
   free(lsn_path);
   lsn_path = NULL;

   pgmoneta_log_info("walbridge: exiting PoC starter");
   pgmoneta_stop_logging();
   return 0;

error:

   if (map != NULL)
   {
      pgmoneta_lsn_map_destroy(map);
   }
   free(lsn_path);
   pgmoneta_stop_logging();
   return 1;
}

/* Mirror the pgmoneta main tool startup: create shared memory, load and
 * validate the main configuration, users and admins before starting logging
 * and using config->running / pgmoneta_get_server_wal.
 */
static int
walbridge_read_configuration(char* configuration_path)
{
   struct main_configuration* config = NULL;
   char config_path_buffer[MAX_PATH];
   int ret;

   memset(config_path_buffer, 0, sizeof(config_path_buffer));

   if (pgmoneta_create_shared_memory(sizeof(struct main_configuration), HUGEPAGE_OFF, &shmem))
   {
      pgmoneta_log_error("walbridge: error in creating shared memory");
      return 1;
   }

   pgmoneta_init_main_configuration(shmem);
   config = (struct main_configuration*)shmem;

   if (configuration_path == NULL)
   {
      configuration_path = config_path_buffer;
      pgmoneta_normalize_path(NULL, "pgmoneta.conf", PGMONETA_DEFAULT_CONFIG_FILE_PATH, config_path_buffer, sizeof(config_path_buffer));
      if (configuration_path[0] == '\0')
      {
         configuration_path = (char*)PGMONETA_DEFAULT_CONFIG_FILE_PATH;
      }
   }

   ret = pgmoneta_validate_config_file(configuration_path);
   if (ret)
   {
      switch (ret)
      {
         case ENOENT:
            pgmoneta_log_error("Configuration file not found or not a regular file: %s", configuration_path);
            break;
         case EACCES:
            pgmoneta_log_error("Can't read configuration file: %s", configuration_path);
            break;
         case EINVAL:
            pgmoneta_log_error("Configuration file contains binary data or invalid path: %s", configuration_path);
            break;
         default:
            pgmoneta_log_error("Configuration file validation failed: %s", configuration_path);
            break;
      }
      return 1;
   }

   if (pgmoneta_read_main_configuration(shmem, configuration_path))
   {
      pgmoneta_log_error("Failed to read configuration file: %s", configuration_path);
      return 1;
   }

   memcpy(&config->common.configuration_path[0], configuration_path,
          MIN(strlen(configuration_path), (size_t)MAX_PATH - 1));

   {
      char* users_file = (char*)PGMONETA_DEFAULT_USERS_FILE_PATH;
      if (config->common.users_path[0] != '\0')
      {
         users_file = config->common.users_path;
      }
      ret = pgmoneta_read_users_configuration(shmem, users_file);
   }
   if (ret == 1)
   {
      pgmoneta_log_error("USERS configuration not found");
      return 1;
   }
   else if (ret == 2)
   {
      pgmoneta_log_error("Invalid master key file");
      return 1;
   }
   else if (ret == 3)
   {
      pgmoneta_log_error("USERS: Too many users defined %d (max %d)", config->common.number_of_users, NUMBER_OF_USERS);
      return 1;
   }

   if (pgmoneta_read_admins_configuration(shmem, (char*)CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH) == 0)
   {
      memcpy(&config->common.admins_path[0], CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH,
             MIN(strlen(CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH), (size_t)MAX_PATH - 1));
   }

   if (pgmoneta_start_logging())
   {
      pgmoneta_log_error("Failed to start logging");
      return 1;
   }

   if (pgmoneta_validate_main_configuration(shmem))
   {
      pgmoneta_log_error("Invalid main configuration");
      return 1;
   }
   if (pgmoneta_validate_users_configuration(shmem))
   {
      pgmoneta_log_error("Invalid USERS configuration");
      return 1;
   }
   if (pgmoneta_validate_admins_configuration(shmem))
   {
      pgmoneta_log_error("Invalid ADMINS configuration");
      return 1;
   }

   if (config->common.number_of_servers < 1)
   {
      pgmoneta_log_error("walbridge: no servers configured in %s", configuration_path);
      return 1;
   }

   return 0;
}
