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

static int
walbridge_read_configuration(int srv, char* configuration_path, char* users_path, char* admins_path, char* directory_path);

static void
walbridge_reset_state(char* downstream_dir, char* lsn_path)
{
   DIR* dir = opendir(downstream_dir);
   if (dir)
   {
      struct dirent* e;
      while ((e = readdir(dir)) != NULL)
      {
         char path[PATH_MAX];

         if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
         {
            continue;
         }
         pgmoneta_snprintf(path, sizeof(path), "%s/%s", downstream_dir, e->d_name);
         unlink(path);
      }
      closedir(dir);
   }

   unlink(lsn_path);
}

int
pgmoneta_walbridge_start(int srv, char* configuration_path, char* users_path, char* admins_path, char* directory_path, char** argv)
{
   char* lsn_path = NULL;
   char downstream_dir[MAX_PATH];
   struct lsn_map* map = NULL;
   pid_t receiver_pid;

   if (walbridge_read_configuration(srv, configuration_path, users_path, admins_path, directory_path))
   {
      goto error;
   }

   pgmoneta_log_info("walbridge: starting PoC for server %d", srv);

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

   walbridge_reset_state(downstream_dir, lsn_path);

   if (lsn_map_create(lsn_path, &map))
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
      lsn_map_destroy(map);
      free(lsn_path);
      goto error;
   }
   if (receiver_pid == 0)
   {
      /* child: receiver (it forks the existing WAL client itself) */
      walbridge_run_receiver(srv, argv, map);
      _exit(0);
   }

   /* parent: serve the translated stream to the replica */
   walbridge_run_sender(srv, downstream_dir, lsn_path);

   pgmoneta_log_info("walbridge: sender stopped; waiting for receiver");
   waitpid(receiver_pid, NULL, 0);

   lsn_map_destroy(map);
   map = NULL;
   free(lsn_path);
   lsn_path = NULL;

   pgmoneta_log_info("walbridge: exiting PoC starter");
   pgmoneta_stop_logging();
   return 0;

error:

   if (map != NULL)
   {
      lsn_map_destroy(map);
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
walbridge_read_configuration(int srv, char* configuration_path, char* users_path, char* admins_path, char* directory_path)
{
   struct main_configuration* config = NULL;
   char config_path_buffer[MAX_PATH];
   char users_path_buffer[MAX_PATH];
   char admins_path_buffer[MAX_PATH];
   char* adjusted_dir_path = NULL;
   int ret = 0;
   struct stat path_stat;

   memset(config_path_buffer, 0, sizeof(config_path_buffer));
   memset(users_path_buffer, 0, sizeof(users_path_buffer));
   memset(admins_path_buffer, 0, sizeof(admins_path_buffer));

   if (pgmoneta_create_shared_memory(sizeof(struct main_configuration), HUGEPAGE_OFF, &shmem))
   {
      pgmoneta_log_error("walbridge: error in creating shared memory");
      return 1;
   }

   pgmoneta_init_main_configuration(shmem);
   config = (struct main_configuration*)shmem;

   if (directory_path == NULL)
   {
      directory_path = getenv("PGMONETA_CONFIG_DIR");
   }

   if (directory_path != NULL)
   {
      if (access(directory_path, F_OK) != 0)
      {
         pgmoneta_log_error("Configuration directory not found: %s", directory_path);
         return 1;
      }

      if (stat(directory_path, &path_stat) == 0)
      {
         if (!S_ISDIR(path_stat.st_mode))
         {
            pgmoneta_log_error("Path is not a directory: %s", directory_path);
            return 1;
         }
      }

      if (access(directory_path, R_OK | X_OK) != 0)
      {
         pgmoneta_log_error("Insufficient permissions for directory: %s", directory_path);
         return 1;
      }

      if (directory_path[strlen(directory_path) - 1] != '/')
      {
         adjusted_dir_path = pgmoneta_append(strdup(directory_path), "/");
      }
      else
      {
         adjusted_dir_path = strdup(directory_path);
      }

      if (adjusted_dir_path == NULL)
      {
         pgmoneta_log_error("Memory allocation failed while copying directory path.");
         return 1;
      }

      if (!configuration_path &&
          pgmoneta_normalize_path(adjusted_dir_path, "pgmoneta.conf", PGMONETA_DEFAULT_CONFIG_FILE_PATH, config_path_buffer, sizeof(config_path_buffer)) == 0 &&
          strlen(config_path_buffer) > 0)
      {
         configuration_path = config_path_buffer;
      }

      if (!users_path &&
          pgmoneta_normalize_path(adjusted_dir_path, "pgmoneta_users.conf", PGMONETA_DEFAULT_USERS_FILE_PATH, users_path_buffer, sizeof(users_path_buffer)) == 0 &&
          strlen(users_path_buffer) > 0)
      {
         users_path = users_path_buffer;
      }

      if (!admins_path &&
          pgmoneta_normalize_path(adjusted_dir_path, "pgmoneta_admins.conf", CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH, admins_path_buffer, sizeof(admins_path_buffer)) == 0 &&
          strlen(admins_path_buffer) > 0)
      {
         admins_path = admins_path_buffer;
      }

      free(adjusted_dir_path);
      adjusted_dir_path = NULL;
   }

   if (configuration_path != NULL)
   {
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
   }
   else
   {
      if (pgmoneta_read_main_configuration(shmem, (char*)PGMONETA_DEFAULT_CONFIG_FILE_PATH))
      {
         pgmoneta_log_error("Failed to read configuration file: %s", PGMONETA_DEFAULT_CONFIG_FILE_PATH);
         return 1;
      }
   }

   memcpy(&config->common.configuration_path[0], configuration_path,
          MIN(strlen(configuration_path), (size_t)MAX_PATH - 1));

   if (users_path != NULL)
   {
      ret = pgmoneta_read_users_configuration(shmem, users_path);
      if (ret == 1)
      {
         pgmoneta_log_error("USERS configuration not found: %s", users_path);
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
   }
   else
   {
      ret = pgmoneta_read_users_configuration(shmem, (char*)PGMONETA_DEFAULT_USERS_FILE_PATH);
      if (ret == 1)
      {
         pgmoneta_log_error("USERS configuration not found: %s", PGMONETA_DEFAULT_USERS_FILE_PATH);
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
   }

   memcpy(&config->common.users_path[0], users_path, MIN(strlen(users_path), (size_t)MAX_PATH - 1));

   if (admins_path != NULL)
   {
      ret = pgmoneta_read_admins_configuration(shmem, admins_path);
      if (ret == 1)
      {
         pgmoneta_log_error("ADMINS configuration not found: %s", admins_path);
         return 1;
      }
      else if (ret == 2)
      {
         pgmoneta_log_error("Invalid master key file");
         return 1;
      }
      else if (ret == 3)
      {
         pgmoneta_log_error("ADMINS: Too many admins defined %d (max %d)", config->common.number_of_admins, NUMBER_OF_ADMINS);
         return 1;
      }
      memcpy(&config->common.admins_path[0], admins_path, MIN(strlen(admins_path), (size_t)MAX_PATH - 1));
   }
   else
   {
      if (pgmoneta_read_admins_configuration(shmem, (char*)CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH) == 0)
      {
         memcpy(&config->common.admins_path[0], CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH,
                MIN(strlen(CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH), (size_t)MAX_PATH - 1));
      }
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

   if (srv < 0 || srv >= NUMBER_OF_SERVERS || srv >= config->common.number_of_servers)
   {
      pgmoneta_log_error("walbridge: invalid server index %d (configured servers: %d)", srv, config->common.number_of_servers);
      return 1;
   }

   return 0;
}
