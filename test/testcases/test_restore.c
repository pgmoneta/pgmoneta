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

#include <pgmoneta.h>
#include <info.h>
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

MCTF_TEST(test_pgmoneta_restore_full)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "current", 0) == 0, cleanup, "restore operation failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_restore_incremental_chain)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup_chain() == 0, cleanup, "backup chain failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "current", 0) == 0, cleanup, "restore operation failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_tablespace_full_restore_symlink)
{
   char* label = NULL;
   char* backup_dir = NULL;
   char tblspc_path[MAX_PATH];
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
               cleanup, "failed to create table on tablespace");
   pgmoneta_test_cleanup_query_response(&qr);

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "INSERT INTO test_tbl SELECT generate_series(1, 100);", &qr) == 0,
               cleanup, "failed to insert data");
   pgmoneta_test_cleanup_query_response(&qr);

   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, 0) == 0,
               cleanup, "full backup failed");

   label = pgmoneta_test_tablespace_newest_backup_label();
   MCTF_ASSERT_PTR_NONNULL(label, cleanup, "failed to get newest backup label");

   backup_dir = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(backup_dir, cleanup, "backup directory is null");

   MCTF_ASSERT(pgmoneta_load_info(backup_dir, label, &bck) == 0,
               cleanup, "failed to load backup info");

   ts_idx = pgmoneta_test_tablespace_index(bck);
   MCTF_ASSERT(ts_idx >= 0, cleanup, "tablespace '%s' not found in backup metadata", TBLSPC_NAME);

   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "current", 0) == 0,
               cleanup, "restore failed");

   memset(tblspc_path, 0, sizeof(tblspc_path));
   pgmoneta_snprintf(tblspc_path, sizeof(tblspc_path),
                     "%s/primary-%s/pg_tblspc/%s",
                     TEST_RESTORE_DIR, label, bck->tablespaces_oids[ts_idx]);

   MCTF_ASSERT(pgmoneta_is_symlink(tblspc_path), cleanup,
               "restored pg_tblspc/%s should be a symlink",
               bck->tablespaces_oids[ts_idx]);

   memset(link_target, 0, sizeof(link_target));
   link_len = readlink(tblspc_path, link_target, sizeof(link_target) - 1);
   MCTF_ASSERT(link_len > 0, cleanup, "readlink failed on %s", tblspc_path);

   memset(expected_target, 0, sizeof(expected_target));
   pgmoneta_snprintf(expected_target, sizeof(expected_target),
                     "../../primary-%s-%s/", label, TBLSPC_NAME);
   MCTF_ASSERT_STR_EQ(link_target, expected_target, cleanup,
                      "restored symlink target mismatch");

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
   free(bck);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_tablespace_incremental_restore_symlink)
{
   char* label = NULL;
   char* backup_dir = NULL;
   char tblspc_path[MAX_PATH];
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
      MCTF_SKIP("incremental restore with tablespaces requires PostgreSQL 17+; TEST_PG_VERSION=%s",
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

   ts_idx = pgmoneta_test_tablespace_index(bck);
   MCTF_ASSERT(ts_idx >= 0, cleanup, "tablespace '%s' not found in backup metadata", TBLSPC_NAME);

   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "current", 0) == 0,
               cleanup, "incremental restore failed");

   memset(tblspc_path, 0, sizeof(tblspc_path));
   pgmoneta_snprintf(tblspc_path, sizeof(tblspc_path),
                     "%s/primary-%s/pg_tblspc/%s",
                     TEST_RESTORE_DIR, label, bck->tablespaces_oids[ts_idx]);

   MCTF_ASSERT(pgmoneta_is_symlink(tblspc_path), cleanup,
               "restored pg_tblspc/%s should be a symlink",
               bck->tablespaces_oids[ts_idx]);

   memset(link_target, 0, sizeof(link_target));
   link_len = readlink(tblspc_path, link_target, sizeof(link_target) - 1);
   MCTF_ASSERT(link_len > 0, cleanup, "readlink failed on %s", tblspc_path);

   memset(expected_target, 0, sizeof(expected_target));
   pgmoneta_snprintf(expected_target, sizeof(expected_target),
                     "../../primary-%s-%s/", label, TBLSPC_NAME);
   MCTF_ASSERT_STR_EQ(link_target, expected_target, cleanup,
                      "restored symlink target mismatch");

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
   free(bck);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}
