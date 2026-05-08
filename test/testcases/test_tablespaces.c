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
#include <mctf.h>
#include <utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TBLSPC_NAME "tblspc_test_ts"

/**
 * Helper: get the PG major version from TEST_PG_VERSION env var.
 * Returns 0 if unset.
 */
static int
get_pg_version(void)
{
   char* v = getenv("TEST_PG_VERSION");
   if (v != NULL)
   {
      return atoi(v);
   }
   return 0;
}

/**
 * Helper: get the label of the newest backup via list-backup.
 * Caller must free the returned string.
 * @return dynamically allocated label string, or NULL on error
 */
static char*
get_newest_backup_label(void)
{
   struct json* response = NULL;
   struct json* backup = NULL;
   char* l = NULL;
   char* result = NULL;
   int count = 0;

   if (pgmoneta_tsclient_list_backup("primary", NULL, &response, 0))
   {
      goto error;
   }

   count = pgmoneta_tsclient_get_backup_count(response);
   if (count <= 0)
   {
      goto error;
   }

   backup = pgmoneta_tsclient_get_backup(response, count - 1);
   if (backup == NULL)
   {
      goto error;
   }

   l = pgmoneta_tsclient_get_backup_label(backup);
   if (l != NULL)
   {
      result = strdup(l);
   }

   pgmoneta_json_destroy(response);

   return result;

error:

   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }

   return NULL;
}

/**
 * Helper: find the index in backup->tablespaces[] matching TBLSPC_NAME.
 * The backup stores names as "tblspc_<spcname>" (wf_backup.c:385).
 * @return index >= 0 on success, -1 if not found
 */
static int
find_tablespace_index(struct backup* bck)
{
   for (uint64_t i = 0; i < bck->number_of_tablespaces; i++)
   {
      if (!strcmp(bck->tablespaces[i], TBLSPC_NAME))
      {
         return (int)i;
      }
   }
   return -1;
}

/**
 * Helper: safely free a query response and set pointer to NULL.
 */
static void
cleanup_query_response(struct query_response** qr)
{
   if (qr != NULL && *qr != NULL)
   {
      pgmoneta_free_query_response(*qr);
      *qr = NULL;
   }
}

MCTF_TEST_NEGATIVE(test_tablespace_full_backup_symlink)
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

   /* Connect as myuser to mydb for DML */
   MCTF_ASSERT(pgmoneta_server_authenticate(PRIMARY_SERVER, "mydb",
                                            "myuser", "mypass",
                                            false, &ssl, &socket) == 0,
               cleanup, "failed to authenticate as myuser");

   /* Create test table on tablespace */
   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "DROP TABLE IF EXISTS test_tbl;", &qr) == 0,
               cleanup, "failed to drop table");
   cleanup_query_response(&qr);

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "CREATE TABLE test_tbl (id INT) TABLESPACE test_ts;", &qr) == 0,
               cleanup, "failed to create table on tablespace test_ts");
   cleanup_query_response(&qr);

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "INSERT INTO test_tbl SELECT generate_series(1, 100);", &qr) == 0,
               cleanup, "failed to insert data into test_tbl");
   cleanup_query_response(&qr);

   /* Full backup */
   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, 0) == 0,
               cleanup, "full backup failed");

   /* Get the backup label */
   label = get_newest_backup_label();
   MCTF_ASSERT_PTR_NONNULL(label, cleanup, "failed to get newest backup label");

   /* Load backup info */
   backup_dir = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(backup_dir, cleanup, "backup directory is null");

   MCTF_ASSERT(pgmoneta_load_info(backup_dir, label, &bck) == 0,
               cleanup, "failed to load backup info");
   MCTF_ASSERT_PTR_NONNULL(bck, cleanup, "backup info is null");

   /* Find our tablespace by name, not by assuming index 0 */
   ts_idx = find_tablespace_index(bck);
   MCTF_ASSERT(ts_idx >= 0, cleanup,
               "tablespace '%s' not found in backup metadata", TBLSPC_NAME);

   /* Verify pg_tblspc/<oid> symlink exists in backup data */
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

   /* Verify exact symlink target */
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

   /* Clean up table */
   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "DROP TABLE IF EXISTS test_tbl;", &qr) == 0,
               cleanup, "failed to drop test_tbl");
   cleanup_query_response(&qr);

cleanup:
   cleanup_query_response(&qr);
   if (ssl != NULL)
   {
      pgmoneta_close_ssl(ssl);
   }
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   if (label != NULL)
   {
      free(label);
   }
   if (backup_dir != NULL)
   {
      free(backup_dir);
   }
   if (backup_base != NULL)
   {
      free(backup_base);
   }
   if (bck != NULL)
   {
      free(bck);
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_tablespace_full_restore_symlink)
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

   /* Connect as myuser to create table on tablespace */
   MCTF_ASSERT(pgmoneta_server_authenticate(PRIMARY_SERVER, "mydb",
                                            "myuser", "mypass",
                                            false, &ssl, &socket) == 0,
               cleanup, "failed to authenticate as myuser");

   /* Create test table on tablespace */
   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "DROP TABLE IF EXISTS test_tbl;", &qr) == 0,
               cleanup, "failed to drop table");
   cleanup_query_response(&qr);

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "CREATE TABLE test_tbl (id INT) TABLESPACE test_ts;", &qr) == 0,
               cleanup, "failed to create table on tablespace");
   cleanup_query_response(&qr);

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "INSERT INTO test_tbl SELECT generate_series(1, 100);", &qr) == 0,
               cleanup, "failed to insert data");
   cleanup_query_response(&qr);

   /* Full backup */
   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, 0) == 0,
               cleanup, "full backup failed");

   /* Get label */
   label = get_newest_backup_label();
   MCTF_ASSERT_PTR_NONNULL(label, cleanup, "failed to get newest backup label");

   /* Load backup info for tablespace OID */
   backup_dir = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(backup_dir, cleanup, "backup directory is null");

   MCTF_ASSERT(pgmoneta_load_info(backup_dir, label, &bck) == 0,
               cleanup, "failed to load backup info");

   ts_idx = find_tablespace_index(bck);
   MCTF_ASSERT(ts_idx >= 0, cleanup, "tablespace '%s' not found in backup metadata", TBLSPC_NAME);

   /* Restore */
   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "current", 0) == 0,
               cleanup, "restore failed");

   /* Verify restored pg_tblspc/<oid> symlink.
    * Restore target layout: TEST_RESTORE_DIR/primary-<label>/pg_tblspc/<oid>. */
   memset(tblspc_path, 0, sizeof(tblspc_path));
   pgmoneta_snprintf(tblspc_path, sizeof(tblspc_path),
                     "%s/primary-%s/pg_tblspc/%s",
                     TEST_RESTORE_DIR, label, bck->tablespaces_oids[ts_idx]);

   MCTF_ASSERT(pgmoneta_is_symlink(tblspc_path), cleanup,
               "restored pg_tblspc/%s should be a symlink",
               bck->tablespaces_oids[ts_idx]);

   /* Verify exact symlink target: ../../primary-<label>-tblspc_test_ts/
    * This mirrors the relative tablespace link created by restore.c. */
   memset(link_target, 0, sizeof(link_target));
   link_len = readlink(tblspc_path, link_target, sizeof(link_target) - 1);
   MCTF_ASSERT(link_len > 0, cleanup, "readlink failed on %s", tblspc_path);

   memset(expected_target, 0, sizeof(expected_target));
   pgmoneta_snprintf(expected_target, sizeof(expected_target),
                     "../../primary-%s-%s/", label, TBLSPC_NAME);
   MCTF_ASSERT_STR_EQ(link_target, expected_target, cleanup,
                      "restored symlink target mismatch");

   /* Clean up table */
   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "DROP TABLE IF EXISTS test_tbl;", &qr) == 0,
               cleanup, "failed to drop test_tbl");
   cleanup_query_response(&qr);

cleanup:
   cleanup_query_response(&qr);
   if (ssl != NULL)
   {
      pgmoneta_close_ssl(ssl);
   }
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   if (label != NULL)
   {
      free(label);
   }
   if (backup_dir != NULL)
   {
      free(backup_dir);
   }
   if (bck != NULL)
   {
      free(bck);
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_tablespace_incremental_backup_symlink)
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

   /* Version gate: incremental backup with tablespaces only on PG 17+ */
   if (get_pg_version() < 17)
   {
      MCTF_SKIP("incremental backup with tablespaces requires PostgreSQL 17+; TEST_PG_VERSION=%s",
                getenv("TEST_PG_VERSION") != NULL ? getenv("TEST_PG_VERSION") : "(unset)");
   }

   /* Connect as myuser */
   MCTF_ASSERT(pgmoneta_server_authenticate(PRIMARY_SERVER, "mydb",
                                            "myuser", "mypass",
                                            false, &ssl, &socket) == 0,
               cleanup, "failed to authenticate as myuser");

   /* Create test table on tablespace */
   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "DROP TABLE IF EXISTS test_tbl;", &qr) == 0,
               cleanup, "failed to drop table");
   cleanup_query_response(&qr);

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "CREATE TABLE test_tbl (id INT) TABLESPACE test_ts;", &qr) == 0,
               cleanup, "failed to create table on tablespace");
   cleanup_query_response(&qr);

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "INSERT INTO test_tbl SELECT generate_series(1, 100);", &qr) == 0,
               cleanup, "failed to insert data");
   cleanup_query_response(&qr);

   /* Full backup first */
   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, 0) == 0,
               cleanup, "full backup failed");

   /* Insert more data */
   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "INSERT INTO test_tbl SELECT generate_series(101, 200);", &qr) == 0,
               cleanup, "failed to insert additional data");
   cleanup_query_response(&qr);

   /* Incremental backup */
   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", "newest", 0) == 0,
               cleanup, "incremental backup failed");

   /* Get the newest (incremental) backup label */
   label = get_newest_backup_label();
   MCTF_ASSERT_PTR_NONNULL(label, cleanup, "failed to get newest backup label");

   /* Load backup info */
   backup_dir = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(backup_dir, cleanup, "backup directory is null");

   MCTF_ASSERT(pgmoneta_load_info(backup_dir, label, &bck) == 0,
               cleanup, "failed to load backup info");
   MCTF_ASSERT_PTR_NONNULL(bck, cleanup, "backup info is null");

   /* Find tablespace by name */
   ts_idx = find_tablespace_index(bck);
   MCTF_ASSERT(ts_idx >= 0, cleanup,
               "tablespace '%s' not found in incremental backup metadata", TBLSPC_NAME);

   /* Verify pg_tblspc/<oid> symlink exists */
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

   /* Verify exact symlink target */
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

   /* Clean up table */
   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "DROP TABLE IF EXISTS test_tbl;", &qr) == 0,
               cleanup, "failed to drop test_tbl");
   cleanup_query_response(&qr);

cleanup:
   cleanup_query_response(&qr);
   if (ssl != NULL)
   {
      pgmoneta_close_ssl(ssl);
   }
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   if (label != NULL)
   {
      free(label);
   }
   if (backup_dir != NULL)
   {
      free(backup_dir);
   }
   if (backup_base != NULL)
   {
      free(backup_base);
   }
   if (bck != NULL)
   {
      free(bck);
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_tablespace_incremental_restore_symlink)
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

   /* Version gate */
   if (get_pg_version() < 17)
   {
      MCTF_SKIP("incremental restore with tablespaces requires PostgreSQL 17+; TEST_PG_VERSION=%s",
                getenv("TEST_PG_VERSION") != NULL ? getenv("TEST_PG_VERSION") : "(unset)");
   }

   /* Connect as myuser */
   MCTF_ASSERT(pgmoneta_server_authenticate(PRIMARY_SERVER, "mydb",
                                            "myuser", "mypass",
                                            false, &ssl, &socket) == 0,
               cleanup, "failed to authenticate as myuser");

   /* Create test table on tablespace */
   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "DROP TABLE IF EXISTS test_tbl;", &qr) == 0,
               cleanup, "failed to drop table");
   cleanup_query_response(&qr);

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "CREATE TABLE test_tbl (id INT) TABLESPACE test_ts;", &qr) == 0,
               cleanup, "failed to create table on tablespace");
   cleanup_query_response(&qr);

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "INSERT INTO test_tbl SELECT generate_series(1, 100);", &qr) == 0,
               cleanup, "failed to insert data");
   cleanup_query_response(&qr);

   /* Full backup */
   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, 0) == 0,
               cleanup, "full backup failed");

   /* More data + incremental */
   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "INSERT INTO test_tbl SELECT generate_series(101, 200);", &qr) == 0,
               cleanup, "failed to insert additional data");
   cleanup_query_response(&qr);

   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", "newest", 0) == 0,
               cleanup, "incremental backup failed");

   /* Get label */
   label = get_newest_backup_label();
   MCTF_ASSERT_PTR_NONNULL(label, cleanup, "failed to get newest backup label");

   /* Load backup info */
   backup_dir = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(backup_dir, cleanup, "backup directory is null");

   MCTF_ASSERT(pgmoneta_load_info(backup_dir, label, &bck) == 0,
               cleanup, "failed to load backup info");

   ts_idx = find_tablespace_index(bck);
   MCTF_ASSERT(ts_idx >= 0, cleanup, "tablespace '%s' not found in backup metadata", TBLSPC_NAME);

   /* Restore */
   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "current", 0) == 0,
               cleanup, "incremental restore failed");

   /* Verify restored pg_tblspc/<oid> symlink.
    * Restore layout: TEST_RESTORE_DIR/primary-<label>/pg_tblspc/<oid> */
   memset(tblspc_path, 0, sizeof(tblspc_path));
   pgmoneta_snprintf(tblspc_path, sizeof(tblspc_path),
                     "%s/primary-%s/pg_tblspc/%s",
                     TEST_RESTORE_DIR, label, bck->tablespaces_oids[ts_idx]);

   MCTF_ASSERT(pgmoneta_is_symlink(tblspc_path), cleanup,
               "restored pg_tblspc/%s should be a symlink",
               bck->tablespaces_oids[ts_idx]);

   /* Verify exact symlink target: ../../primary-<label>-tblspc_test_ts */
   memset(link_target, 0, sizeof(link_target));
   link_len = readlink(tblspc_path, link_target, sizeof(link_target) - 1);
   MCTF_ASSERT(link_len > 0, cleanup, "readlink failed on %s", tblspc_path);

   memset(expected_target, 0, sizeof(expected_target));
   pgmoneta_snprintf(expected_target, sizeof(expected_target),
                     "../../primary-%s-%s/", label, TBLSPC_NAME);
   MCTF_ASSERT_STR_EQ(link_target, expected_target, cleanup,
                      "restored symlink target mismatch");

   /* Clean up table */
   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                           "DROP TABLE IF EXISTS test_tbl;", &qr) == 0,
               cleanup, "failed to drop test_tbl");
   cleanup_query_response(&qr);

cleanup:
   cleanup_query_response(&qr);
   if (ssl != NULL)
   {
      pgmoneta_close_ssl(ssl);
   }
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   if (label != NULL)
   {
      free(label);
   }
   if (backup_dir != NULL)
   {
      free(backup_dir);
   }
   if (bck != NULL)
   {
      free(bck);
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}
