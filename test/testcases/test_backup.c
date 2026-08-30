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
#include <tsclient.h>
#include <tsclient_helpers.h>
#include <tscommon.h>
#include <mctf.h>
#include <utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "security.h"

static bool
poll_replica(uint64_t* primary_lsn)
{
   uint64_t replay_lsn = 0;
   SSL* ssl = NULL;
   int socket = -1;
   struct query_response* qr = NULL;
   char* lsn_str = NULL;
   int timeout = 60; //60s
   int ret = 0;
   if (pgmoneta_server_authenticate(REPLICA_SERVER, "mydb", "myuser", "mypass", false, &ssl, &socket) != 0)
   {
      ret = 1;
   }

   time_t start_time = time(NULL);
   while (difftime(time(NULL), start_time) < timeout)
   {
      if (pgmoneta_test_execute_query(REPLICA_SERVER, ssl, socket, "SELECT pg_last_wal_replay_lsn();", &qr) != 0)
      {
         ret = 1;
      }

      lsn_str = pgmoneta_query_response_get_data(qr, 0);

      ret = 1;
      if (lsn_str != NULL && pgmoneta_string_to_lsn(lsn_str, &replay_lsn) == 0)
      {
         if (replay_lsn >= *primary_lsn)
         {
            ret = 0;
            break;
         }
      }
      SLEEP(1);
   }

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

MCTF_TEST(test_pgmoneta_backup_full)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

cleanup:
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
      MCTF_SKIP("This test requires PostgreSQL 14 and 17+");
   }

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_tsclient_mode("replica", "online", 0) == 0,
               cleanup, "replica mode failed");

   MCTF_ASSERT(pgmoneta_tsclient_backup("replica", NULL, 0) == 0,
               cleanup, "backup mode failed");

   MCTF_ASSERT(pgmoneta_tsclient_restore("replica", "newest", "current", 0) == 0,
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

   char* pg_version_str = NULL;
   int pg_version = 0;
   pg_version_str = getenv("TEST_PG_VERSION");
   if (pg_version_str != NULL && strlen(pg_version_str) > 0)
   {
      pg_version = atoi(pg_version_str);
   }
   if (!check_replica_start() || pg_version < 17)
   {
      MCTF_SKIP("This test requires PostgreSQL 17+; TEST_PG_VERSION=%s",
                pg_version_str != NULL ? pg_version_str : "(unset)");
   }

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_tsclient_mode("replica", "online", 0) == 0,
               cleanup, "replica mode failed");

   MCTF_ASSERT(pgmoneta_tsclient_backup("replica", NULL, 0) == 0,
               cleanup, "backup mode failed");

   MCTF_ASSERT(insert_data_in_primary_server() == 0,
               cleanup, "insert data in primary failed");

   MCTF_ASSERT(pgmoneta_tsclient_backup("replica", "newest", 0) == 0,
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

   MCTF_ASSERT(pgmoneta_tsclient_restore("replica", "newest", "current", 0) == 0,
               cleanup, "restore incremental replica backup failed");

cleanup:
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}
