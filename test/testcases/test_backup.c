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
#include <info.h>
#include <logging.h>
#include <network.h>
#include <security.h>
#include <server.h>
#include <tsclient.h>
#include <tsclient_helpers.h>
#include <tscommon.h>
#include <tstablespaces.h>
#include <mctf.h>
#include <utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

MCTF_TEST(test_tablespace_full_backup_symlink)
{
   char* label = NULL;
   char* backup_dir = NULL;
   char* backup_base = NULL;
   char tblspc_dir[MAX_PATH];
   char oid_path[MAX_PATH];
   char link_target[MAX_PATH];
   char expected_target[MAX_PATH];
   struct backup* bck = NULL;
   int ts_idx = -1;
   SSL* ssl = NULL;
   int socket = -1;
   struct query_response* qr = NULL;
   ssize_t link_len = 0;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_tablespace_create() == 0, cleanup,
               "failed to create test tablespace");

   MCTF_ASSERT(pgmoneta_server_authenticate(PRIMARY_SERVER, "mydb",
                                            "myuser", "mypass",
                                            false, &ssl, &socket) == 0,
               cleanup, "failed to authenticate as myuser");

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "DROP TABLE IF EXISTS test_tbl;", &qr) == 0,
               cleanup, "failed to drop table");
   pgmoneta_test_cleanup_query_response(&qr);

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "CREATE TABLE test_tbl (id INT) TABLESPACE test_ts;", &qr) == 0,
               cleanup, "failed to create table on tablespace test_ts");
   pgmoneta_test_cleanup_query_response(&qr);

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "INSERT INTO test_tbl SELECT generate_series(1, 100);", &qr) == 0,
               cleanup, "failed to insert data into test_tbl");
   pgmoneta_test_cleanup_query_response(&qr);

   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, 0) == 0,
               cleanup, "full backup failed");

   label = pgmoneta_test_tablespace_newest_backup_label();
   MCTF_ASSERT_PTR_NONNULL(label, cleanup, "failed to get newest backup label");

   backup_dir = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(backup_dir, cleanup, "backup directory is null");

   MCTF_ASSERT(pgmoneta_load_info(backup_dir, label, &bck) == 0,
               cleanup, "failed to load backup info");
   MCTF_ASSERT_PTR_NONNULL(bck, cleanup, "backup info is null");

   ts_idx = pgmoneta_test_tablespace_index(bck);
   MCTF_ASSERT(ts_idx >= 0, cleanup,
               "tablespace '%s' not found in backup metadata", TBLSPC_NAME);

   memset(tblspc_dir, 0, sizeof(tblspc_dir));
   pgmoneta_snprintf(tblspc_dir, sizeof(tblspc_dir), "%s/%s/data/pg_tblspc",
                     backup_dir, label);
   MCTF_ASSERT(pgmoneta_exists(tblspc_dir), cleanup,
               "pg_tblspc directory missing in backup");

   memset(oid_path, 0, sizeof(oid_path));
   pgmoneta_snprintf(oid_path, sizeof(oid_path), "%s/%s",
                     tblspc_dir, bck->tablespaces_oids[ts_idx]);
   MCTF_ASSERT(pgmoneta_is_symlink(oid_path), cleanup,
               "pg_tblspc/%s should be a symlink in backup",
               bck->tablespaces_oids[ts_idx]);

   memset(link_target, 0, sizeof(link_target));
   link_len = readlink(oid_path, link_target, sizeof(link_target) - 1);
   MCTF_ASSERT(link_len > 0, cleanup, "readlink failed on %s", oid_path);

   memset(expected_target, 0, sizeof(expected_target));
   backup_base = pgmoneta_get_server_backup_identifier(PRIMARY_SERVER, label);
   MCTF_ASSERT_PTR_NONNULL(backup_base, cleanup, "backup base directory is null");
   pgmoneta_snprintf(expected_target, sizeof(expected_target), "%s%s/",
                     backup_base, TBLSPC_NAME);
   MCTF_ASSERT_STR_EQ(link_target, expected_target, cleanup,
                      "backup symlink target mismatch");

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "DROP TABLE IF EXISTS test_tbl;", &qr) == 0,
               cleanup, "failed to drop test_tbl");
   pgmoneta_test_cleanup_query_response(&qr);

cleanup:
   pgmoneta_test_cleanup_query_response(&qr);
   if (ssl != NULL)
   {
      pgmoneta_close_ssl(ssl);
   }
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   free(label);
   free(backup_dir);
   free(backup_base);
   free(bck);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_tablespace_incremental_backup_symlink)
{
   char* label = NULL;
   char* backup_dir = NULL;
   char* backup_base = NULL;
   char tblspc_dir[MAX_PATH];
   char oid_path[MAX_PATH];
   char link_target[MAX_PATH];
   char expected_target[MAX_PATH];
   struct backup* bck = NULL;
   int ts_idx = -1;
   SSL* ssl = NULL;
   int socket = -1;
   struct query_response* qr = NULL;
   ssize_t link_len = 0;

   pgmoneta_test_setup();

   if (pgmoneta_test_tablespace_pg_version() < 17)
   {
      MCTF_SKIP("incremental backup with tablespaces requires PostgreSQL 17+; TEST_PG_VERSION=%s",
                getenv("TEST_PG_VERSION") != NULL ? getenv("TEST_PG_VERSION") : "(unset)");
   }

   MCTF_ASSERT(pgmoneta_test_tablespace_create() == 0, cleanup,
               "failed to create test tablespace");

   MCTF_ASSERT(pgmoneta_server_authenticate(PRIMARY_SERVER, "mydb",
                                            "myuser", "mypass",
                                            false, &ssl, &socket) == 0,
               cleanup, "failed to authenticate as myuser");

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "DROP TABLE IF EXISTS test_tbl;", &qr) == 0,
               cleanup, "failed to drop table");
   pgmoneta_test_cleanup_query_response(&qr);

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "CREATE TABLE test_tbl (id INT) TABLESPACE test_ts;", &qr) == 0,
               cleanup, "failed to create table on tablespace");
   pgmoneta_test_cleanup_query_response(&qr);

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "INSERT INTO test_tbl SELECT generate_series(1, 100);", &qr) == 0,
               cleanup, "failed to insert data");
   pgmoneta_test_cleanup_query_response(&qr);

   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, 0) == 0,
               cleanup, "full backup failed");

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "INSERT INTO test_tbl SELECT generate_series(101, 200);", &qr) == 0,
               cleanup, "failed to insert additional data");
   pgmoneta_test_cleanup_query_response(&qr);

   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", "newest", 0) == 0,
               cleanup, "incremental backup failed");

   label = pgmoneta_test_tablespace_newest_backup_label();
   MCTF_ASSERT_PTR_NONNULL(label, cleanup, "failed to get newest backup label");

   backup_dir = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(backup_dir, cleanup, "backup directory is null");

   MCTF_ASSERT(pgmoneta_load_info(backup_dir, label, &bck) == 0,
               cleanup, "failed to load backup info");
   MCTF_ASSERT_PTR_NONNULL(bck, cleanup, "backup info is null");

   ts_idx = pgmoneta_test_tablespace_index(bck);
   MCTF_ASSERT(ts_idx >= 0, cleanup,
               "tablespace '%s' not found in incremental backup metadata", TBLSPC_NAME);

   memset(tblspc_dir, 0, sizeof(tblspc_dir));
   pgmoneta_snprintf(tblspc_dir, sizeof(tblspc_dir), "%s/%s/data/pg_tblspc",
                     backup_dir, label);
   MCTF_ASSERT(pgmoneta_exists(tblspc_dir), cleanup,
               "pg_tblspc directory missing in incremental backup");

   memset(oid_path, 0, sizeof(oid_path));
   pgmoneta_snprintf(oid_path, sizeof(oid_path), "%s/%s",
                     tblspc_dir, bck->tablespaces_oids[ts_idx]);
   MCTF_ASSERT(pgmoneta_is_symlink(oid_path), cleanup,
               "pg_tblspc/%s should be a symlink in incremental backup",
               bck->tablespaces_oids[ts_idx]);

   memset(link_target, 0, sizeof(link_target));
   link_len = readlink(oid_path, link_target, sizeof(link_target) - 1);
   MCTF_ASSERT(link_len > 0, cleanup, "readlink failed on %s", oid_path);

   memset(expected_target, 0, sizeof(expected_target));
   backup_base = pgmoneta_get_server_backup_identifier(PRIMARY_SERVER, label);
   MCTF_ASSERT_PTR_NONNULL(backup_base, cleanup, "backup base directory is null");
   pgmoneta_snprintf(expected_target, sizeof(expected_target), "%s%s/",
                     backup_base, TBLSPC_NAME);
   MCTF_ASSERT_STR_EQ(link_target, expected_target, cleanup,
                      "incremental backup symlink target mismatch");

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "DROP TABLE IF EXISTS test_tbl;", &qr) == 0,
               cleanup, "failed to drop test_tbl");
   pgmoneta_test_cleanup_query_response(&qr);

cleanup:
   pgmoneta_test_cleanup_query_response(&qr);
   if (ssl != NULL)
   {
      pgmoneta_close_ssl(ssl);
   }
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   free(label);
   free(backup_dir);
   free(backup_base);
   free(bck);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}
