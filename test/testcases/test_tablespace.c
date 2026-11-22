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
#include <deque.h>
#include <extraction.h>
#include <mctf.h>
#include <message.h>
#include <network.h>
#include <security.h>
#include <tsclient.h>
#include <tsclient_helpers.h>
#include <tscommon.h>
#include <utils.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Tablespace backup/restore test module.
 *
 * The two tablespaces (ts1, ts2) and their postgres-owned location directories
 * are created by the PostgreSQL test image (setup.sql + Dockerfile / check.sh).
 * The tables that use them live in PG_DATABASE (mydb).
 *
 * PostgreSQL 17 only - for now
 */

static int populate_tablespaces(SSL* ssl, int socket);
static int append_tablespace_rows(SSL* ssl, int socket);
static int count_tablespace_links(char* directory);

MCTF_TEST(test_pgmoneta_tablespace_backup)
{
   SSL* ssl = NULL;
   int socket = -1;
   struct json* response = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_connect_user(&ssl, &socket) == 0, cleanup, "Failed to connect to %s as the test user", "mydb");
   MCTF_ASSERT(populate_tablespaces(ssl, socket) == 0, cleanup, "Failed to create/populate tables in tablespaces");
   pgmoneta_test_cleanup_connection(&ssl, &socket);

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "Backup of a cluster with tablespaces failed");

   MCTF_ASSERT(pgmoneta_tsclient_list_backup("primary", NULL, &response, 0) == 0, cleanup, "List backup failed");
   MCTF_ASSERT(pgmoneta_tsclient_get_backup_count(response) >= 1, cleanup, "Expected at least one backup after a successful backup");

cleanup:

   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }

   pgmoneta_test_cleanup_connection(&ssl, &socket);
   pgmoneta_test_basedir_cleanup();

   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_tablespace_restore)
{
   SSL* ssl = NULL;
   int socket = -1;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_connect_user(&ssl, &socket) == 0, cleanup, "Failed to connect to %s as the test user", "mydb");
   MCTF_ASSERT(populate_tablespaces(ssl, socket) == 0, cleanup, "Failed to create/populate tables in tablespaces");
   pgmoneta_test_cleanup_connection(&ssl, &socket);

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "Backup of a cluster with tablespaces failed");

   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "current", false, 0) == 0, cleanup, "Restore of a cluster with tablespaces failed");
   MCTF_ASSERT(count_tablespace_links(TEST_RESTORE_DIR) >= 2, cleanup, "Restored cluster does not contain the two tablespaces");

cleanup:

   pgmoneta_test_cleanup_connection(&ssl, &socket);
   pgmoneta_test_basedir_cleanup();

   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_tablespace_backup_incremental)
{
   SSL* ssl = NULL;
   int socket = -1;
   struct json* response = NULL;
   struct json* full = NULL;
   struct json* incremental = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_connect_user(&ssl, &socket) == 0, cleanup, "Failed to connect to %s as the test user", "mydb");
   MCTF_ASSERT(populate_tablespaces(ssl, socket) == 0, cleanup, "Failed to create/populate tables in tablespaces");
   pgmoneta_test_cleanup_connection(&ssl, &socket);

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "Full backup of a cluster with tablespaces failed");

   MCTF_ASSERT(pgmoneta_test_connect_user(&ssl, &socket) == 0, cleanup, "Failed to reconnect to %s as the test user", "mydb");
   MCTF_ASSERT(append_tablespace_rows(ssl, socket) == 0, cleanup, "Failed to add rows to the tablespace tables");
   pgmoneta_test_cleanup_connection(&ssl, &socket);

   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", "newest", false, NULL, 0) == 0, cleanup, "Incremental backup of a cluster with tablespaces failed");

   MCTF_ASSERT(pgmoneta_tsclient_list_backup("primary", NULL, &response, 0) == 0, cleanup, "List backup failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_get_backup_count(response), 2, cleanup, "Expected one full plus one incremental backup");

   full = pgmoneta_tsclient_get_backup(response, 0);
   incremental = pgmoneta_tsclient_get_backup(response, 1);

   MCTF_ASSERT_PTR_NONNULL(full, cleanup, "Full backup entry missing");
   MCTF_ASSERT_PTR_NONNULL(incremental, cleanup, "Incremental backup entry missing");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(full), "FULL", cleanup, "Backup 0 should be FULL");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(incremental), "INCREMENTAL", cleanup, "Backup 1 should be INCREMENTAL");
   MCTF_ASSERT(pgmoneta_tsclient_verify_backup_chain(full, incremental), cleanup, "Incremental backup parent mismatch (should be the full backup)");

cleanup:

   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }

   pgmoneta_test_cleanup_connection(&ssl, &socket);
   pgmoneta_test_basedir_cleanup();

   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_tablespace_restore_incremental)
{
   SSL* ssl = NULL;
   int socket = -1;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_connect_user(&ssl, &socket) == 0, cleanup, "Failed to connect to %s as the test user", "mydb");
   MCTF_ASSERT(populate_tablespaces(ssl, socket) == 0, cleanup, "Failed to create/populate tables in tablespaces");
   pgmoneta_test_cleanup_connection(&ssl, &socket);

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "Full backup of a cluster with tablespaces failed");

   MCTF_ASSERT(pgmoneta_test_connect_user(&ssl, &socket) == 0, cleanup, "Failed to reconnect to %s as the test user", "mydb");
   MCTF_ASSERT(append_tablespace_rows(ssl, socket) == 0, cleanup, "Failed to add rows to the tablespace tables");
   pgmoneta_test_cleanup_connection(&ssl, &socket);

   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", "newest", false, NULL, 0) == 0, cleanup, "Incremental backup of a cluster with tablespaces failed");

   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "current", false, 0) == 0, cleanup, "Restore of an incremental backup with tablespaces failed");
   MCTF_ASSERT(count_tablespace_links(TEST_RESTORE_DIR) >= 2, cleanup, "Restored cluster does not contain the two tablespaces");

cleanup:

   pgmoneta_test_cleanup_connection(&ssl, &socket);
   pgmoneta_test_basedir_cleanup();

   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_tablespace_archive)
{
   SSL* ssl = NULL;
   int socket = -1;
   char archive_dir[MAX_PATH];
   char extract_dir[MAX_PATH];
   char archive[MAX_PATH];
   char* destination = NULL;
   int number_of_directories = 0;
   char** directories = NULL;
   struct deque* files = NULL;

   pgmoneta_test_setup();

   memset(archive_dir, 0, MAX_PATH);
   pgmoneta_snprintf(archive_dir, MAX_PATH, "%s/archive_tablespace", TEST_BASE_DIR);
   pgmoneta_mkdir(archive_dir);

   memset(extract_dir, 0, MAX_PATH);
   pgmoneta_snprintf(extract_dir, MAX_PATH, "%s/archive_tablespace_extract", TEST_BASE_DIR);
   pgmoneta_mkdir(extract_dir);

   MCTF_ASSERT(pgmoneta_test_connect_user(&ssl, &socket) == 0, cleanup, "Failed to connect to %s as the test user", "mydb");
   MCTF_ASSERT(populate_tablespaces(ssl, socket) == 0, cleanup, "Failed to create/populate tables in tablespaces");
   pgmoneta_test_cleanup_connection(&ssl, &socket);

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "Backup of a cluster with tablespaces failed");

   MCTF_ASSERT(pgmoneta_tsclient_archive("primary", "newest", "current", archive_dir, false, 0) == 0, cleanup, "Archive of a cluster with tablespaces failed");

   MCTF_ASSERT(pgmoneta_get_directories(archive_dir, &number_of_directories, &directories) == 0, cleanup, "Failed to list the archive directory");
   MCTF_ASSERT_INT_EQ(number_of_directories, 0, cleanup, "Archive left restored directories behind");

   MCTF_ASSERT(pgmoneta_get_files(PGMONETA_FILE_TYPE_ALL, archive_dir, false, &files) == 0, cleanup, "Failed to list the archive files");
   MCTF_ASSERT_INT_EQ(pgmoneta_deque_size(files), 1, cleanup, "Expected exactly one archive file");

   memset(archive, 0, MAX_PATH);
   pgmoneta_snprintf(archive, MAX_PATH, "%s/%s", archive_dir, (char*)pgmoneta_deque_peek(files, NULL));

   destination = extract_dir;
   MCTF_ASSERT(pgmoneta_extract_file(archive, PGMONETA_FILE_TYPE_UNKNOWN, false, NULL, &destination) == 0, cleanup, "Failed to extract the archive");
   MCTF_ASSERT(count_tablespace_links(extract_dir) >= 2, cleanup, "Archive does not contain the two tablespaces");

cleanup:

   for (int i = 0; i < number_of_directories; i++)
   {
      free(directories[i]);
   }
   free(directories);

   pgmoneta_deque_destroy(files);
   pgmoneta_delete_directory(archive_dir);
   pgmoneta_delete_directory(extract_dir);

   pgmoneta_test_cleanup_connection(&ssl, &socket);
   pgmoneta_test_basedir_cleanup();

   MCTF_FINISH();
}

static int
populate_tablespaces(SSL* ssl, int socket)
{
   int ret = 0;
   char* statements[] = {
      "DROP TABLE IF EXISTS tblspc_t1;",
      "CREATE TABLE tblspc_t1 (id integer, payload text) TABLESPACE ts1;",
      "INSERT INTO tblspc_t1 SELECT g, 'ts1_row_' || g FROM generate_series(1, 1000) g;",
      "DROP TABLE IF EXISTS tblspc_t2;",
      "CREATE TABLE tblspc_t2 (id integer, payload text) TABLESPACE ts2;",
      "INSERT INTO tblspc_t2 SELECT g, 'ts2_row_' || g FROM generate_series(1, 1000) g;",
      NULL};
   struct query_response* qr = NULL;

   for (int i = 0; ret == 0 && statements[i] != NULL; i++)
   {
      if (pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket, (char*)statements[i], &qr) != 0)
      {
         ret = 1;
      }
      pgmoneta_test_cleanup_query_response(&qr);
   }

   return ret;
}

static int
append_tablespace_rows(SSL* ssl, int socket)
{
   int ret = 0;
   char* statements[] = {
      "INSERT INTO tblspc_t1 SELECT g, 'ts1_inc_' || g FROM generate_series(1001, 2000) g;",
      "INSERT INTO tblspc_t2 SELECT g, 'ts2_inc_' || g FROM generate_series(1001, 2000) g;",
      NULL};
   struct query_response* qr = NULL;

   for (int i = 0; ret == 0 && statements[i] != NULL; i++)
   {
      if (pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket, (char*)statements[i], &qr) != 0)
      {
         ret = 1;
      }
      pgmoneta_test_cleanup_query_response(&qr);
   }

   return ret;
}

static int
count_tablespace_links(char* directory)
{
   DIR* root = NULL;
   struct dirent* entry = NULL;
   int count = 0;

   root = opendir(directory);
   if (root == NULL)
   {
      return 0;
   }

   while ((entry = readdir(root)) != NULL)
   {
      char* tblspc_dir = NULL;
      DIR* tblspc = NULL;
      struct dirent* link = NULL;

      if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      {
         continue;
      }

      tblspc_dir = pgmoneta_append(tblspc_dir, directory);

      if (!pgmoneta_ends_with(tblspc_dir, "/"))
      {
         tblspc_dir = pgmoneta_append_char(tblspc_dir, '/');
      }

      tblspc_dir = pgmoneta_append(tblspc_dir, entry->d_name);
      tblspc_dir = pgmoneta_append(tblspc_dir, "/pg_tblspc");

      tblspc = opendir(tblspc_dir);

      if (tblspc != NULL)
      {
         while ((link = readdir(tblspc)) != NULL)
         {
            char path[MAX_PATH];

            if (!strcmp(link->d_name, ".") || !strcmp(link->d_name, ".."))
            {
               continue;
            }

            memset(path, 0, MAX_PATH);
            pgmoneta_snprintf(path, MAX_PATH, "%s/%s", tblspc_dir, link->d_name);

            if (pgmoneta_is_directory(path))
            {
               count++;
            }
         }
         closedir(tblspc);
      }

      free(tblspc_dir);

      if (count > 0)
      {
         break;
      }
   }

   closedir(root);

   return count;
}
