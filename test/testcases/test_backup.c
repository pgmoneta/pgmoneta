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
#include <backup.h>
#include <delete.h>
#include <deque.h>
#include <info.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <tsclient.h>
#include <tsclient_helpers.h>
#include <tscommon.h>
#include <mctf.h>
#include <utils.h>
#include <walfile/pg_control.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "security.h"

static int
create_incomplete_backup(char* label, size_t size)
{
   time_t now;
   char* data = NULL;
   int ret = 1;

   now = time(NULL);
   if (strftime(label, size, "%Y%m%d%H%M%S", localtime(&now)) == 0)
   {
      goto done;
   }

   data = pgmoneta_get_server_backup_identifier_data(PRIMARY_SERVER, label);
   if (data == NULL || pgmoneta_exists(data) || pgmoneta_mkdir(data))
   {
      goto done;
   }

   ret = 0;

done:
   free(data);

   return ret;
}

static int
count_backup_directories(void)
{
   char* d = NULL;
   char** dirs = NULL;
   int n = 0;

   d = pgmoneta_get_server_backup(PRIMARY_SERVER);
   pgmoneta_get_directories(d, &n, &dirs);

   for (int i = 0; i < n; i++)
   {
      free(dirs[i]);
   }
   free(dirs);
   free(d);

   return n;
}

static int
count_incomplete_markers(void)
{
   char* d = NULL;
   int n = 0;
   struct deque* files = NULL;
   struct deque_iterator* iter = NULL;

   d = pgmoneta_get_server_backup(PRIMARY_SERVER);

   if (!pgmoneta_get_files(PGMONETA_FILE_TYPE_ALL, d, false, &files) && !pgmoneta_deque_iterator_create(files, &iter))
   {
      while (pgmoneta_deque_iterator_next(iter))
      {
         if (pgmoneta_ends_with((char*)pgmoneta_value_data(iter->value), BACKUP_INCOMPLETE_SUFFIX))
         {
            n++;
         }
      }
   }

   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_deque_destroy(files);
   free(d);

   return n;
}

static int
create_file(char* path, char* content)
{
   FILE* f = NULL;

   if (path == NULL || (f = fopen(path, "w")) == NULL)
   {
      return 1;
   }

   fputs(content, f);
   fclose(f);

   return 0;
}

static bool
poll_replica(uint64_t* primary_lsn)
{
   uint64_t replay_lsn = 0;
   SSL* ssl = NULL;
   int socket = -1;
   struct query_response* qr = NULL;
   char* lsn_str = NULL;
   int timeout = 60; //60s
   int ret = 1;
   if (pgmoneta_server_authenticate(REPLICA_SERVER, "mydb", "myuser", "mypass", false, &ssl, &socket) != 0)
   {
      goto cleanup;
   }

   time_t start_time = time(NULL);
   while (difftime(time(NULL), start_time) < timeout)
   {
      if (pgmoneta_test_execute_query(REPLICA_SERVER, ssl, socket, "SELECT pg_last_wal_replay_lsn();", &qr) == 0)
      {
         lsn_str = pgmoneta_query_response_get_data(qr, 0);
         if (lsn_str != NULL && pgmoneta_string_to_lsn(lsn_str, &replay_lsn) == 0)
         {
            if (replay_lsn >= *primary_lsn)
            {
               ret = 0;
               pgmoneta_test_cleanup_query_response(&qr);
               break;
            }
         }
      }
      pgmoneta_test_cleanup_query_response(&qr);
      sleep(1);
   }

cleanup:
   pgmoneta_test_cleanup_query_response(&qr);
   pgmoneta_test_cleanup_connection(&ssl, &socket);
   return ret;
}

static int
insert_data_in_primary_server()
{
   SSL* ssl = NULL;
   int socket = -1;
   int ret = 0;
   char* statements[] = {
      "DROP TABLE IF EXISTS test;",
      "CREATE TABLE test (id integer, payload text);",
      "INSERT INTO test SELECT g, 'row_' || g FROM generate_series(1, 1000) g;",
      NULL};
   struct query_response* qr = NULL;
   char* lsn_str = NULL;
   uint64_t primary_lsn = 0;

   if (pgmoneta_test_connect_user(&ssl, &socket) != 0)
   {
      ret = 1;
   }

   for (int i = 0; ret == 0 && statements[i] != NULL; i++)
   {
      if (pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket, (char*)statements[i], &qr) != 0)
      {
         ret = 1;
      }
      pgmoneta_test_cleanup_query_response(&qr);
   }

   if (pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket, "CHECKPOINT;", &qr) != 0)
   {
      ret = 1;
   }
   pgmoneta_test_cleanup_query_response(&qr);

   if (pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket, "SELECT pg_current_wal_lsn();", &qr) != 0)
   {
      ret = 1;
   }

   lsn_str = pgmoneta_query_response_get_data(qr, 0);
   if (lsn_str == NULL || pgmoneta_string_to_lsn(lsn_str, &primary_lsn) != 0)
   {
      ret = 1;
   }
   pgmoneta_test_cleanup_connection(&ssl, &socket);
   pgmoneta_test_cleanup_query_response(&qr);

   if (ret == 0)
   {
      ret = poll_replica(&primary_lsn);
   }

   return ret;
}

static bool
check_replica_start()
{
   struct main_configuration* config;
   config = (struct main_configuration*)shmem;
   return pgmoneta_test_has_replica(config);
}

static int
verify_restored_backup(const char* restore_path, const char* query, int expected_rows)
{
   int restored_port = -1;
   char command[2048];
   char* output = NULL;
   int exit_code = 0;
   const char* ci_env = NULL;
   const char* local_env = NULL;
   const char* version_env = NULL;
   const char* version = NULL;
   bool local_mode = false;
   int ret = 1;

   restored_port = pgmoneta_start_restored_backup(restore_path, RESTORED_BACKUP_DEFAULT_PORT);
   if (restored_port <= 0)
   {
      pgmoneta_log_error("verify_restored_backup: failed to start restored backup at %s", restore_path);
      goto cleanup;
   }

   ci_env = getenv("CI");
   local_env = getenv("PGMONETA_TEST_LOCAL_RESTORE");

   local_mode = (ci_env != NULL && strcmp(ci_env, "true") == 0) ||
                (local_env != NULL && strcmp(local_env, "1") == 0);

   version_env = getenv("TEST_PG_VERSION");
   version = (version_env != NULL && version_env[0] != '\0') ? version_env : "17";

   if (local_mode)
   {
      pgmoneta_snprintf(command, sizeof(command),
                        "PGPASSWORD=mypass psql -h 127.0.0.1 -p %d "
                        "-U myuser -d mydb -tAc "
                        "\"%s\"",
                        restored_port, query);
   }
   else
   {
      const char* container_engine = "podman";

      if (access("/usr/bin/podman", X_OK) != 0 &&
          access("/usr/local/bin/podman", X_OK) != 0)
      {
         container_engine = "docker";
      }

      pgmoneta_snprintf(command, sizeof(command),
                        "PGPASSWORD=mypass %s exec %s "
                        "/usr/pgsql-%s/bin/psql "
                        "-h localhost -p 5432 -U myuser -d mydb -tAc "
                        "\"%s\"",
                        container_engine,
                        "pgmoneta-test-restored-backup",
                        version, query);
   }

   if (pgmoneta_test_exec_command(command, &output, &exit_code) != 0 || exit_code != 0)
   {
      pgmoneta_log_error("verify_restored_backup: psql failed (exit=%d): %s",
                         exit_code, output ? output : "(null)");
      goto cleanup;
   }

   if (output == NULL || atoi(output) != expected_rows)
   {
      pgmoneta_log_error("verify_restored_backup: expected %d rows, got %s",
                         expected_rows, output ? output : "(null)");
      goto cleanup;
   }

   ret = 0;

cleanup:
   pgmoneta_stop_restored_backup();
   free(output);

   return ret;
}

MCTF_TEST(test_pgmoneta_backup_full)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_backup_without_backup_directory)
{
   char* server_backup = NULL;

   pgmoneta_test_setup();

   server_backup = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(server_backup, cleanup, "server backup directory null");
   MCTF_ASSERT(!pgmoneta_delete_directory(server_backup), cleanup, "could not remove the backup directory");

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed without a backup directory");

cleanup:
   free(server_backup);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_backup_incremental_basic)
{
   struct json* response = NULL;
   int num_backups = 0;
   struct json* b0 = NULL;
   struct json* b1 = NULL;
   struct json* b2 = NULL;
   uint32_t block_size = 0;
   uint32_t relseg_size = 0;
   uint32_t expected_block_size = 0;
   uint32_t expected_relseg_size = 0;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup_chain() == 0, cleanup, "backup chain failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), cleanup, "list backup failed");

   num_backups = pgmoneta_tsclient_get_backup_count(response);
   MCTF_ASSERT_INT_EQ(num_backups, 3, cleanup, "backup count mismatch");

   b0 = pgmoneta_tsclient_get_backup(response, 0);
   b1 = pgmoneta_tsclient_get_backup(response, 1);
   b2 = pgmoneta_tsclient_get_backup(response, 2);

   MCTF_ASSERT_PTR_NONNULL(b0, cleanup, "backup 0 null");
   MCTF_ASSERT_PTR_NONNULL(b1, cleanup, "backup 1 null");
   MCTF_ASSERT_PTR_NONNULL(b2, cleanup, "backup 2 null");

   MCTF_ASSERT(pgmoneta_read_backup_page_layout(PRIMARY_SERVER,
                                                pgmoneta_tsclient_get_backup_label(b0),
                                                &block_size, &relseg_size) == 0 &&
                  block_size > 0 && relseg_size > 0,
               cleanup, "failed to read full backup page layout");

   expected_block_size = block_size;
   expected_relseg_size = relseg_size;

   MCTF_ASSERT(pgmoneta_read_backup_page_layout(PRIMARY_SERVER,
                                                pgmoneta_tsclient_get_backup_label(b1),
                                                &block_size, &relseg_size) == 0 &&
                  block_size == expected_block_size &&
                  relseg_size == expected_relseg_size,
               cleanup, "first incremental backup page layout mismatch");

   MCTF_ASSERT(pgmoneta_read_backup_page_layout(PRIMARY_SERVER,
                                                pgmoneta_tsclient_get_backup_label(b2),
                                                &block_size, &relseg_size) == 0 &&
                  block_size == expected_block_size &&
                  relseg_size == expected_relseg_size,
               cleanup, "second incremental backup page layout mismatch");

   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(b0), "FULL", cleanup, "backup 0 type mismatch");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(b1), "INCREMENTAL", cleanup, "backup 1 type mismatch");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(b2), "INCREMENTAL", cleanup, "backup 2 type mismatch");

   MCTF_ASSERT(pgmoneta_tsclient_verify_backup_chain(b0, b1), cleanup, "backup 1 parent mismatch (should be b0)");
   MCTF_ASSERT(pgmoneta_tsclient_verify_backup_chain(b1, b2), cleanup, "backup 2 parent mismatch (should be b1)");

cleanup:
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_replica_backup_full)
{
   if (!check_replica_start())
   {
      MCTF_SKIP("This test requires PostgreSQL 17+");
   }

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_tsclient_mode("replica", "online", 0) == 0,
               cleanup, "replica mode failed");

   MCTF_ASSERT(pgmoneta_tsclient_backup("replica", NULL, false, NULL, 0) == 0,
               cleanup, "backup mode failed");

   MCTF_ASSERT(pgmoneta_tsclient_restore("replica", "newest", "current", NULL, 0) == 0,
               cleanup, "restore full replica backup failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_replica_backup_incremental)
{
   struct json* response = NULL;
   struct json* b0 = NULL;
   struct json* b1 = NULL;

   char* label = NULL;
   char restore_path[MAX_PATH] = {0};

   if (!check_replica_start())
   {
      MCTF_SKIP("This test requires PostgreSQL 17+");
   }

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_tsclient_mode("replica", "online", 0) == 0,
               cleanup, "replica mode failed");

   MCTF_ASSERT(pgmoneta_tsclient_backup("replica", NULL, false, NULL, 0) == 0,
               cleanup, "backup mode failed");

   MCTF_ASSERT(insert_data_in_primary_server() == 0,
               cleanup, "insert data in primary failed");

   MCTF_ASSERT(pgmoneta_tsclient_backup("replica", "newest", false, NULL, 0) == 0,
               cleanup, "incremental replica backup failed");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("replica", NULL, &response, 0),
               cleanup, "list backup failed");

   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_get_backup_count(response), 2,
                      cleanup, "backup count mismatch");

   b0 = pgmoneta_tsclient_get_backup(response, 0);
   b1 = pgmoneta_tsclient_get_backup(response, 1);

   MCTF_ASSERT_PTR_NONNULL(b0, cleanup, "backup 0 null");
   MCTF_ASSERT_PTR_NONNULL(b1, cleanup, "backup 1 null");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(b0), "FULL",
                      cleanup, "backup 0 type mismatch");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(b1), "INCREMENTAL",
                      cleanup, "backup 1 type mismatch");
   MCTF_ASSERT(pgmoneta_tsclient_verify_backup_chain(b0, b1),
               cleanup, "backup 1 parent mismatch (should be b0)");

   MCTF_ASSERT(pgmoneta_tsclient_restore("replica", "newest", "current", NULL, 0) == 0,
               cleanup, "restore incremental replica backup failed");

   label = pgmoneta_tsclient_get_backup_label(b1);
   MCTF_ASSERT(label != NULL, cleanup, "failed to get replica backup label");

   label = strdup(label);
   MCTF_ASSERT(label != NULL, cleanup, "failed to duplicate replica backup label");

   pgmoneta_snprintf(restore_path, sizeof(restore_path),
                     "%s/replica-%s", TEST_RESTORE_DIR, label);

   MCTF_ASSERT(verify_restored_backup(restore_path,
                                      "SELECT count(*) FROM test;",
                                      1000) == 0,
               cleanup, "failed to verify restored replica backup");

cleanup:
   free(label);

   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }

   pgmoneta_stop_restored_backup();
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_backup_incomplete_not_listed)
{
   char label[MISC_LENGTH];
   char* complete = NULL;
   char* server_backup = NULL;
   struct backup* newest = NULL;
   struct json* response = NULL;
   struct json* backup = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), cleanup, "list backup failed");
   backup = pgmoneta_tsclient_get_backup(response, 0);
   MCTF_ASSERT_PTR_NONNULL(backup, cleanup, "backup 0 null");
   MCTF_ASSERT_PTR_NONNULL(pgmoneta_tsclient_get_backup_label(backup), cleanup, "backup 0 label null");
   complete = strdup(pgmoneta_tsclient_get_backup_label(backup));
   MCTF_ASSERT_PTR_NONNULL(complete, cleanup, "strdup failed");
   pgmoneta_json_destroy(response);
   response = NULL;

   MCTF_ASSERT(!create_incomplete_backup(label, sizeof(label)), cleanup, "could not create the incomplete backup");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), cleanup, "list backup failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_get_backup_count(response), 1, cleanup, "the incomplete backup is listed");

   server_backup = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(server_backup, cleanup, "server backup directory null");
   MCTF_ASSERT(!pgmoneta_load_info(server_backup, "newest", &newest), cleanup, "could not load the newest backup");
   MCTF_ASSERT_PTR_NONNULL(newest, cleanup, "newest backup null");
   MCTF_ASSERT_STR_EQ(newest->label, complete, cleanup, "newest is not the complete backup");

cleanup:
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }
   free(newest);
   free(server_backup);
   free(complete);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_backup_incomplete_restore_newest)
{
   char label[MISC_LENGTH];

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");
   MCTF_ASSERT(!create_incomplete_backup(label, sizeof(label)), cleanup, "could not create the incomplete backup");

   MCTF_ASSERT(!pgmoneta_tsclient_restore("primary", "newest", "current", false, 0), cleanup, "restore of newest failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_backup_incomplete_incremental_newest)
{
   char label[MISC_LENGTH];

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");
   MCTF_ASSERT(!create_incomplete_backup(label, sizeof(label)), cleanup, "could not create the incomplete backup");

   MCTF_ASSERT(!pgmoneta_tsclient_backup("primary", "newest", false, NULL, 0), cleanup, "incremental backup of newest failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_backup_without_metadata_deleted)
{
   char label[MISC_LENGTH];
   char* incomplete = NULL;
   struct json* response = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");
   MCTF_ASSERT(!create_incomplete_backup(label, sizeof(label)), cleanup, "could not create the incomplete backup");

   incomplete = pgmoneta_get_server_backup_identifier(PRIMARY_SERVER, label);
   MCTF_ASSERT_PTR_NONNULL(incomplete, cleanup, "incomplete backup directory null");

   MCTF_ASSERT(!pgmoneta_delete_incomplete_backups(PRIMARY_SERVER), cleanup, "could not delete the incomplete backup");
   MCTF_ASSERT(!pgmoneta_exists(incomplete), cleanup, "the incomplete backup still exists");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), cleanup, "list backup failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_get_backup_count(response), 1, cleanup, "the complete backup was deleted");

cleanup:
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }
   free(incomplete);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_backup_killed)
{
   int socket = -1;
   pid_t backup = 0;
   bool throttled = false;
   struct json* response = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");
   MCTF_ASSERT(!pgmoneta_tsclient_conf_set("server.primary.max_rate", "32768", 0), cleanup, "could not slow down backups");
   throttled = true;

   MCTF_ASSERT(!pgmoneta_connect_unix_socket(config->common.unix_socket_dir, MAIN_UDS, &socket), cleanup, "could not connect");
   MCTF_ASSERT(!pgmoneta_management_request_backup(NULL, socket, "primary", MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, NULL, MANAGEMENT_OUTPUT_FORMAT_JSON, false),
               cleanup, "could not request a backup");

   for (int i = 0; i < 100 && backup <= 0; i++)
   {
      SLEEP(100000000L);
      if (count_backup_directories() == 2)
      {
         backup = pgmoneta_test_find_process("backup/primary");
      }
   }
   MCTF_ASSERT(backup > 0, cleanup, "the backup did not start");
   MCTF_ASSERT_INT_EQ(count_incomplete_markers(), 1, cleanup, "the backup is not marked as incomplete");

   MCTF_ASSERT(!kill(backup, SIGKILL), cleanup, "could not kill the backup");
   backup = 0;

   for (int i = 0; i < 100 && (count_backup_directories() != 1 || pgmoneta_test_find_process("cleanup/primary") > 0); i++)
   {
      SLEEP(100000000L);
   }
   MCTF_ASSERT_INT_EQ(count_backup_directories(), 1, cleanup, "the killed backup was not removed");
   MCTF_ASSERT_INT_EQ(count_incomplete_markers(), 0, cleanup, "the incomplete marker was not removed");
   MCTF_ASSERT(pgmoneta_test_find_process("cleanup/primary") == 0, cleanup, "the cleanup did not finish");

   MCTF_ASSERT(!pgmoneta_tsclient_conf_set("server.primary.max_rate", "0", 0), cleanup, "could not reset max_rate");
   throttled = false;
   MCTF_ASSERT(!pgmoneta_tsclient_backup("primary", NULL, false, NULL, 0), cleanup, "backup after the killed one failed");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), cleanup, "list backup failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_get_backup_count(response), 2, cleanup, "backup count mismatch");

cleanup:
   if (backup > 0)
   {
      kill(backup, SIGKILL);
   }
   if (throttled)
   {
      pgmoneta_tsclient_conf_set("server.primary.max_rate", "0", 0);
   }
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }
   pgmoneta_disconnect(socket);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_backup_async_killed)
{
   pid_t backup = 0;
   bool throttled = false;
   char* job_id = NULL;
   char* state = NULL;
   struct json* response = NULL;
   struct json* job = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");
   MCTF_ASSERT(!pgmoneta_tsclient_conf_set("server.primary.max_rate", "32768", 0), cleanup, "could not slow down backups");
   throttled = true;

   MCTF_ASSERT(!pgmoneta_tsclient_backup("primary", NULL, true, &response, 0), cleanup, "async backup request failed");
   job_id = pgmoneta_append(NULL, (char*)pgmoneta_json_get((struct json*)pgmoneta_json_get(response, MANAGEMENT_CATEGORY_RESPONSE), MANAGEMENT_ARGUMENT_JOB_ID));
   MCTF_ASSERT_PTR_NONNULL(job_id, cleanup, "async backup response has no job id");
   pgmoneta_json_destroy(response);
   response = NULL;

   for (int i = 0; i < 100 && backup <= 0; i++)
   {
      SLEEP(100000000L);
      if (count_backup_directories() == 2)
      {
         backup = pgmoneta_test_find_process("backup/primary");
      }
   }
   MCTF_ASSERT(backup > 0, cleanup, "the backup did not start");

   MCTF_ASSERT(!kill(backup, SIGKILL), cleanup, "could not kill the backup");
   backup = 0;

   for (int i = 0; i < 100 && (count_backup_directories() != 1 || pgmoneta_test_find_process("cleanup/primary") > 0); i++)
   {
      SLEEP(100000000L);
   }
   MCTF_ASSERT_INT_EQ(count_backup_directories(), 1, cleanup, "the killed backup was not removed");
   MCTF_ASSERT(pgmoneta_test_find_process("cleanup/primary") == 0, cleanup, "the cleanup did not finish");

   MCTF_ASSERT(!pgmoneta_tsclient_job(job_id, &response, 0), cleanup, "job lookup failed");
   job = (struct json*)pgmoneta_json_get((struct json*)pgmoneta_json_get(response, MANAGEMENT_CATEGORY_RESPONSE), MANAGEMENT_CATEGORY_JOB);
   MCTF_ASSERT_PTR_NONNULL(job, cleanup, "job lookup response has no job");
   state = (char*)pgmoneta_json_get(job, MANAGEMENT_ARGUMENT_JOB_STATE);
   MCTF_ASSERT(state != NULL && pgmoneta_compare_string(state, "Failed"), cleanup, "the job of the killed backup is %s", state != NULL ? state : "missing");

cleanup:
   if (backup > 0)
   {
      kill(backup, SIGKILL);
   }
   if (throttled)
   {
      pgmoneta_tsclient_conf_set("server.primary.max_rate", "0", 0);
   }
   pgmoneta_json_destroy(response);
   if (job_id != NULL)
   {
      pgmoneta_tsclient_job_remove_job(job_id, NULL, 0);
   }
   free(job_id);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_backup_marked_incomplete_deleted)
{
   char label[MISC_LENGTH];
   char* incomplete = NULL;
   char* info = NULL;
   char* server_backup = NULL;
   char* marker = NULL;
   struct json* response = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");
   MCTF_ASSERT(!create_incomplete_backup(label, sizeof(label)), cleanup, "could not create the incomplete backup");

   incomplete = pgmoneta_get_server_backup_identifier(PRIMARY_SERVER, label);
   server_backup = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT(incomplete != NULL && server_backup != NULL, cleanup, "backup directory null");
   info = pgmoneta_format_and_append(NULL, "%sbackup.info", incomplete);
   marker = pgmoneta_format_and_append(NULL, "%s%s%s", server_backup, label, BACKUP_INCOMPLETE_SUFFIX);

   /* what pgmoneta leaves when it dies together with the backup after backup.info is written */
   MCTF_ASSERT(!create_file(info, "STATUS=1\n"), cleanup, "could not create backup.info");
   MCTF_ASSERT(!create_file(marker, ""), cleanup, "could not create the incomplete marker");

   MCTF_ASSERT(!pgmoneta_delete_incomplete_backups(PRIMARY_SERVER), cleanup, "could not delete the incomplete backup");

   MCTF_ASSERT(!pgmoneta_exists(incomplete), cleanup, "the incomplete backup still exists");
   MCTF_ASSERT(!pgmoneta_exists(marker), cleanup, "the incomplete marker still exists");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), cleanup, "list backup failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_get_backup_count(response), 1, cleanup, "the complete backup was deleted");

cleanup:
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }
   free(marker);
   free(server_backup);
   free(info);
   free(incomplete);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}
