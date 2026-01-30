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
 *
 */
#include <pgmoneta.h>
#include <configuration.h>
#include <logging.h>
#include <message.h>
#include <network.h>
#include <security.h>
#include <server.h>
#include <shmem.h>
#include <tsclient.h>
#include <tscommon.h>
#include <utils.h>
#include <walfile.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ENV_VAR_CONF_PATH        "PGMONETA_TEST_CONF"
#define ENV_VAR_CONF_SAMPLE_PATH "PGMONETA_TEST_CONF_SAMPLE"
#define ENV_VAR_USER_CONF        "PGMONETA_TEST_USER_CONF"
#define ENV_VAR_RESTORE_DIR      "PGMONETA_TEST_RESTORE_DIR"

char TEST_CONFIG_SAMPLE_PATH[MAX_PATH];
char TEST_RESTORE_DIR[MAX_PATH];
char TEST_BASE_DIR[MAX_PATH];

void
pgmoneta_test_environment_create(void)
{
   struct main_configuration* config;
   char* conf_path = NULL;
   char* conf_sample_path = NULL;
   char* user_conf_path = NULL;
   char* restore_dir = NULL;
   char* base_dir = NULL;
   int ret = 0;
   size_t size = 0;

   memset(TEST_CONFIG_SAMPLE_PATH, 0, sizeof(TEST_CONFIG_SAMPLE_PATH));
   memset(TEST_RESTORE_DIR, 0, sizeof(TEST_RESTORE_DIR));
   memset(TEST_BASE_DIR, 0, sizeof(TEST_BASE_DIR));

   conf_path = getenv(ENV_VAR_CONF_PATH);
   assert(conf_path != NULL);
   // Create the shared memory for the configuration
   size = sizeof(struct main_configuration);
   assert(!pgmoneta_create_shared_memory(size, HUGEPAGE_OFF, &shmem));

   assert(!pgmoneta_init_main_configuration(shmem));

   // Try reading configuration from the configuration path
   assert(!pgmoneta_read_main_configuration(shmem, conf_path));
   
   // Validate the configuration is valid
   assert(!pgmoneta_test_validate_configuration(shmem));
   
   config = (struct main_configuration*)shmem;

   // some validations just to be safe
   memcpy(&config->common.configuration_path[0], conf_path, MIN(strlen(conf_path), MAX_PATH - 1));
   assert(config->common.number_of_servers > 0);
   assert(pgmoneta_compare_string(config->common.servers[0].name, "primary"));

   conf_sample_path = getenv(ENV_VAR_CONF_SAMPLE_PATH);
   assert(conf_sample_path != NULL);
   memcpy(TEST_CONFIG_SAMPLE_PATH, conf_sample_path, strlen(conf_sample_path));

   restore_dir = getenv(ENV_VAR_RESTORE_DIR);
   assert(restore_dir != NULL);
   memcpy(TEST_RESTORE_DIR, restore_dir, strlen(restore_dir));

   base_dir = getenv(ENV_VAR_BASE_DIR);
   assert(base_dir != NULL);
   memcpy(TEST_BASE_DIR, base_dir, strlen(base_dir));

   user_conf_path = getenv(ENV_VAR_USER_CONF);
   assert(user_conf_path != NULL);

   pgmoneta_start_logging();

   // Try reading the users configuration path
   assert(!pgmoneta_read_users_configuration(shmem, user_conf_path));
}

void
pgmoneta_test_environment_destroy(void)
{
   size_t size;

   size = sizeof(struct main_configuration);

   memset(TEST_CONFIG_SAMPLE_PATH, 0, sizeof(TEST_CONFIG_SAMPLE_PATH));
   memset(TEST_RESTORE_DIR, 0, sizeof(TEST_RESTORE_DIR));

   pgmoneta_stop_logging();

   pgmoneta_destroy_shared_memory(shmem, size);
}

int
pgmoneta_test_validate_configuration(void* shmem)
{
   if (shmem == NULL)
   {
      return 1;
   }

   // Use the existing validation function for comprehensive checks
   return pgmoneta_validate_main_configuration(shmem);
}

int
pgmoneta_test_add_backup(void)
{
   /* Ensure server is online before backup */
   if (pgmoneta_tsclient_mode("primary", "online", 0))
   {
      return 1;
   }

   if (pgmoneta_tsclient_backup("primary", NULL, 0))
   {
      return 1;
   }
   return 0;
}

int
pgmoneta_test_add_backup_chain(void)
{
   /* Ensure server is online before backup */
   if (pgmoneta_tsclient_mode("primary", "online", 0))
   {
      return 1;
   }

   if (pgmoneta_tsclient_backup("primary", NULL, 0))
   {
      return 1;
   }
   
   if (pgmoneta_tsclient_backup("primary", "newest", 0))
   {
      return 1;
   }
   
   if (pgmoneta_tsclient_backup("primary", "newest", 0))
   {
      return 1;
   }
   
   return 0;
}

void
pgmoneta_test_basedir_cleanup(void)
{
   char* backup_dir = NULL;
   char* wal_dir = NULL;
   bool restart = false;
   struct main_configuration* config;
   int ret = 0;

   config = (struct main_configuration*)shmem;

   if (config == NULL)
   {
      assert(0 && "pgmoneta_test_basedir_cleanup: config is NULL");
   }

   wal_dir = pgmoneta_append(wal_dir, TEST_BASE_DIR);
   wal_dir = pgmoneta_append(wal_dir, "/walfiles");

   backup_dir = pgmoneta_get_server_backup(PRIMARY_SERVER);

   pgmoneta_delete_directory(backup_dir);
   pgmoneta_mkdir(backup_dir);

   pgmoneta_delete_directory(TEST_RESTORE_DIR);
   pgmoneta_mkdir(TEST_RESTORE_DIR);

   pgmoneta_delete_directory(wal_dir);
   pgmoneta_mkdir(wal_dir);

   if (pgmoneta_delete_file(config->common.configuration_path, NULL))
   {
      pgmoneta_log_error("pgmoneta_test_basedir_cleanup: failed to delete config file");
   }

   if (pgmoneta_copy_file(TEST_CONFIG_SAMPLE_PATH, config->common.configuration_path, NULL))
   {
      pgmoneta_log_error("pgmoneta_test_basedir_cleanup: failed to copy config file");
   }
   else
   {
   pgmoneta_reload_configuration(&restart);
   }

   if (pgmoneta_tsclient_reload(0))
   {
      pgmoneta_log_error("pgmoneta_test_basedir_cleanup: failed to reload configuration");
   }


   free(backup_dir);
   free(wal_dir);
   pgmoneta_test_teardown();
}

void
pgmoneta_test_setup(void)
{
   pgmoneta_memory_init();
}

void
pgmoneta_test_teardown(void)
{
   if (partial_record != NULL)
   {
      if (partial_record->xlog_record != NULL)
      {
         free(partial_record->xlog_record);
      }
      if (partial_record->data_buffer != NULL)
      {
         free(partial_record->data_buffer);
      }
      free(partial_record);
      partial_record = NULL;
   }

   pgmoneta_memory_destroy();
}

int
pgmoneta_test_backup(const char* server_name, const char* backup_name)
{
   /* Cast const away as pgmoneta_tsclient_backup doesn't modify the strings */
   return pgmoneta_tsclient_backup((char*)server_name, (char*)backup_name, 0);
}

int
pgmoneta_test_execute_query(int srv, SSL* ssl, int skt, char* query, struct query_response** qr)
{
   struct message* msg = NULL;
   struct query_response* response = NULL;

   /* create and execute the query */
   pgmoneta_create_query_message(query, &msg);
   if (pgmoneta_query_execute(ssl, skt, msg, &response) || response == NULL)
   {
      goto error;
   }

   *qr = response;

   pgmoneta_free_message(msg);
   return 0;

error:

   pgmoneta_free_message(msg);
   pgmoneta_free_query_response(response);

   return 1;
}
