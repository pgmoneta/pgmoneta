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
#include <aes.h>
#include <art.h>
#include <compression.h>
#include <hot_standby.h>
#include <info.h>
#include <logging.h>
#include <management.h>
#include <mctf.h>
#include <tsclient.h>
#include <tsclient_helpers.h>
#include <tscommon.h>
#include <utils.h>
#include <workflow.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run_queries(char* queries[]);
static int relation_in_standby(char* standby_dir, char* relation);
static bool standby_has(char* standby_dir, char* relative);
static int create_file(char* dir, char* name);

static int
check_files_recursive(const char* dir_path, int* found_files)
{
   DIR* dir;
   struct dirent* entry;
   char path[MAX_PATH];

   if (!(dir = opendir(dir_path)))
   {
      goto error;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_DIR)
      {
         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }
         pgmoneta_snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
         if (check_files_recursive(path, found_files))
         {
            goto error;
         }
      }
      else
      {
         (*found_files)++;
         if (pgmoneta_is_encrypted(entry->d_name))
         {
            pgmoneta_log_error("File %s/%s is encrypted (.aes)", dir_path, entry->d_name);
            goto error;
         }
         if (pgmoneta_is_compressed(entry->d_name))
         {
            pgmoneta_log_error("File %s/%s is compressed", dir_path, entry->d_name);
            goto error;
         }
      }
   }

   closedir(dir);
   return 0;

error:
   if (dir != NULL)
   {
      closedir(dir);
   }
   return 1;
}

MCTF_TEST(test_pgmoneta_hot_standby_basic)
{
   char standby_dir[MAX_PATH];
   char pg_version_file[MAX_PATH];
   int found_files = 0;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   pgmoneta_snprintf(standby_dir, sizeof(standby_dir), "%s/primary", TEST_HOT_STANDBY_DIR);
   MCTF_ASSERT(pgmoneta_exists(standby_dir), cleanup, "hot standby directory does not exist: %s", standby_dir);

   pgmoneta_snprintf(pg_version_file, sizeof(pg_version_file), "%s/PG_VERSION", standby_dir);
   MCTF_ASSERT(pgmoneta_exists(pg_version_file), cleanup, "PG_VERSION file missing in hot standby");

   MCTF_ASSERT(check_files_recursive(standby_dir, &found_files) == 0, cleanup, "Found encrypted or compressed files in hot standby");
   MCTF_ASSERT(found_files > 0, cleanup, "No files found in hot standby directory");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_hot_standby_overrides)
{
   char overrides_dir[MAX_PATH];
   char override_src[MAX_PATH];
   char override_dst[MAX_PATH];
   char standby_dir[MAX_PATH];
   int found_files = 0;
   FILE* f = NULL;

   pgmoneta_test_setup();

   /* Prepare overrides source directory and file */
   pgmoneta_snprintf(overrides_dir, sizeof(overrides_dir), "%s/overrides", TEST_HOT_STANDBY_DIR);
   pgmoneta_mkdir(overrides_dir);

   pgmoneta_snprintf(override_src, sizeof(override_src), "%s/override_marker.txt", overrides_dir);
   f = fopen(override_src, "w");
   MCTF_ASSERT(f != NULL, cleanup, "Failed to create override source file: %s", override_src);
   fprintf(f, "hot-standby-override");
   fclose(f);
   f = NULL;

   /* Create a backup which should trigger hot_standby and overrides handling */
   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   /* Standby destination for the primary server */
   pgmoneta_snprintf(standby_dir, sizeof(standby_dir), "%s/primary", TEST_HOT_STANDBY_DIR);
   MCTF_ASSERT(pgmoneta_exists(standby_dir), cleanup, "hot standby directory does not exist: %s", standby_dir);

   /* The override file should have been copied into the standby destination */
   pgmoneta_snprintf(override_dst, sizeof(override_dst), "%s/override_marker.txt", standby_dir);
   MCTF_ASSERT(pgmoneta_exists(override_dst), cleanup, "override file missing in hot standby: %s", override_dst);

   /* Ensure there are no encrypted or compressed files in the final standby layout */
   MCTF_ASSERT(check_files_recursive(standby_dir, &found_files) == 0, cleanup, "Found encrypted or compressed files in hot standby (overrides)");
   MCTF_ASSERT(found_files > 0, cleanup, "No files found in hot standby directory (overrides)");

cleanup:
   if (f != NULL)
   {
      fclose(f);
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_hot_standby_update)
{
   char standby_dir[MAX_PATH];
   char relative[MAX_PATH];
   char* pg_version_str = NULL;
   bool pg17 = false;
   char* old_db = NULL;
   char* new_db = NULL;
   char* dropped = NULL;
   int found_files = 0;
   char* before[] = {
      "CREATE TABLE hs_dropped (i int); INSERT INTO hs_dropped VALUES (1)",
      "CREATE DATABASE hs_old_db",
      NULL};
   char* after[] = {
      "DROP TABLE hs_dropped",
      "DROP DATABASE hs_old_db",
      "CREATE DATABASE hs_new_db",
      "CREATE TABLE hs_added (i int); INSERT INTO hs_added SELECT generate_series(1, 1000)",
      NULL};
   char* cleanup_queries[] = {
      "DROP TABLE IF EXISTS hs_dropped, hs_added, hs_in_ts1",
      "DROP DATABASE IF EXISTS hs_old_db",
      "DROP DATABASE IF EXISTS hs_new_db",
      NULL};

   pgmoneta_test_setup();

   pg_version_str = getenv("TEST_PG_VERSION");
   pg17 = pg_version_str != NULL && atoi(pg_version_str) == 17;

   pgmoneta_snprintf(standby_dir, sizeof(standby_dir), "%s/primary", TEST_HOT_STANDBY_DIR);

   MCTF_ASSERT(run_queries(before) == 0, cleanup, "failed to prepare the first backup");
   MCTF_ASSERT(pgmoneta_test_superuser_query("SELECT oid FROM pg_database WHERE datname = 'hs_old_db'", &old_db) == 0, cleanup, "failed to get the hs_old_db oid");
   MCTF_ASSERT(pgmoneta_test_superuser_query("SELECT pg_relation_filepath('hs_dropped')", &dropped) == 0, cleanup, "failed to get the hs_dropped path");
   if (pg17)
   {
      MCTF_ASSERT(pgmoneta_test_superuser_query("CREATE TABLE hs_in_ts1 (i int) TABLESPACE ts1; INSERT INTO hs_in_ts1 VALUES (1)", NULL) == 0, cleanup, "failed to create hs_in_ts1");
   }

   /* First backup: full copy into the hot standby */
   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "first backup failed");
   MCTF_ASSERT(create_file(standby_dir, "hs_marker") == 0, cleanup, "failed to create the marker");

   MCTF_ASSERT(run_queries(after) == 0, cleanup, "failed to prepare the second backup");
   MCTF_ASSERT(pgmoneta_test_superuser_query("SELECT oid FROM pg_database WHERE datname = 'hs_new_db'", &new_db) == 0, cleanup, "failed to get the hs_new_db oid");
   if (pg17)
   {
      MCTF_ASSERT(pgmoneta_test_superuser_query("INSERT INTO hs_in_ts1 SELECT generate_series(1, 1000)", NULL) == 0, cleanup, "failed to update hs_in_ts1");
   }

   /* Second backup: update path, with compression and encryption */
   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "second backup failed");

   MCTF_ASSERT(standby_has(standby_dir, "hs_marker"), cleanup, "hot standby was not updated in place");
   MCTF_ASSERT(check_files_recursive(standby_dir, &found_files) == 0, cleanup, "Found encrypted or compressed files in hot standby (update)");
   MCTF_ASSERT(relation_in_standby(standby_dir, "hs_added") == 0, cleanup, "added file missing or wrong size in hot standby");
   MCTF_ASSERT(!standby_has(standby_dir, dropped), cleanup, "deleted file still in hot standby: %s", dropped);

   pgmoneta_snprintf(relative, sizeof(relative), "base/%s", new_db);
   MCTF_ASSERT(standby_has(standby_dir, relative), cleanup, "added database missing in hot standby: %s", relative);
   pgmoneta_snprintf(relative, sizeof(relative), "base/%s", old_db);
   MCTF_ASSERT(!standby_has(standby_dir, relative), cleanup, "deleted database still in hot standby: %s", relative);

   if (pg17)
   {
      MCTF_ASSERT(relation_in_standby(standby_dir, "hs_in_ts1") == 0, cleanup, "tablespace file missing or wrong size in hot standby");
   }

cleanup:
   run_queries(cleanup_queries);
   free(old_db);
   free(new_db);
   free(dropped);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_hot_standby_incremental)
{
   char standby_dir[MAX_PATH];
   char overrides_dir[MAX_PATH];
   char override_src[MAX_PATH];
   char override_dst[MAX_PATH];
   char* added_queries[] = {
      "CREATE TABLE hs_incremental_added (i int); INSERT INTO hs_incremental_added SELECT generate_series(1, 1000)",
      NULL};
   char* second_added_queries[] = {
      "CREATE TABLE hs_incremental_added2 (i int); INSERT INTO hs_incremental_added2 SELECT generate_series(1, 1000)",
      NULL};
   int found_files = 0;
   FILE* f = NULL;

   pgmoneta_test_setup();

   pgmoneta_snprintf(standby_dir, sizeof(standby_dir), "%s/primary", TEST_HOT_STANDBY_DIR);

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "full backup failed");
   MCTF_ASSERT(create_file(standby_dir, "hs_incremental_marker") == 0, cleanup, "failed to create the marker");

   pgmoneta_snprintf(overrides_dir, sizeof(overrides_dir), "%s/overrides", TEST_HOT_STANDBY_DIR);
   pgmoneta_mkdir(overrides_dir);
   pgmoneta_snprintf(override_src, sizeof(override_src), "%s/override_marker_inc.txt", overrides_dir);
   f = fopen(override_src, "w");
   MCTF_ASSERT(f != NULL, cleanup, "Failed to create override source file: %s", override_src);
   fprintf(f, "hot-standby-incremental-override");
   fclose(f);
   f = NULL;

   MCTF_ASSERT(run_queries(added_queries) == 0, cleanup, "failed to create the first incremental relation");
   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", "newest", 0) == 0, cleanup, "first incremental backup failed");

   pgmoneta_snprintf(override_dst, sizeof(override_dst), "%s/override_marker_inc.txt", standby_dir);
   MCTF_ASSERT(standby_has(standby_dir, "hs_incremental_marker"), cleanup, "hot standby was not updated by the first incremental backup");
   MCTF_ASSERT(pgmoneta_exists(override_dst), cleanup, "override file added after the full backup is missing in hot standby");
   MCTF_ASSERT(relation_in_standby(standby_dir, "hs_incremental_added") == 0, cleanup, "relation added after the full backup is missing or has the wrong size in hot standby");

   MCTF_ASSERT(run_queries(second_added_queries) == 0, cleanup, "failed to create the second incremental relation");
   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", "newest", 0) == 0, cleanup, "second incremental backup failed");
   MCTF_ASSERT(relation_in_standby(standby_dir, "hs_incremental_added2") == 0, cleanup, "relation added before the second incremental backup is missing or has the wrong size in hot standby");

   MCTF_ASSERT(check_files_recursive(standby_dir, &found_files) == 0, cleanup, "Found encrypted or compressed files in hot standby (incremental)");
   MCTF_ASSERT(found_files > 0, cleanup, "No files found in hot standby directory (incremental)");

cleanup:
   if (f != NULL)
   {
      fclose(f);
   }
   pgmoneta_test_superuser_query("DROP TABLE IF EXISTS hs_incremental_added, hs_incremental_added2", NULL);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_hot_standby_tablespace)
{
   char standby_dir[MAX_PATH];
   char relative[MAX_PATH];
   char* pg_version_str = NULL;
   char* oid = NULL;
   char* create[] = {
      "COPY (SELECT 1) TO PROGRAM 'mkdir -p /tmp/pgmoneta_tblspc/hs_ts'",
      "CREATE TABLESPACE hs_ts LOCATION '/tmp/pgmoneta_tblspc/hs_ts'",
      "CREATE TABLE hs_tst (i int) TABLESPACE hs_ts; INSERT INTO hs_tst SELECT generate_series(1, 1000)",
      NULL};
   char* drop[] = {
      "DROP TABLE IF EXISTS hs_tst",
      "DROP TABLESPACE IF EXISTS hs_ts",
      NULL};

   pgmoneta_test_setup();

   pg_version_str = getenv("TEST_PG_VERSION");
   if (pg_version_str == NULL || atoi(pg_version_str) != 17)
   {
      MCTF_SKIP("tablespaces are set up for PostgreSQL 17 only; TEST_PG_VERSION=%s", pg_version_str != NULL ? pg_version_str : "(unset)");
   }

   pgmoneta_snprintf(standby_dir, sizeof(standby_dir), "%s/primary", TEST_HOT_STANDBY_DIR);

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "first backup failed");

   /* A new tablespace makes the next backup take the full copy path */
   MCTF_ASSERT(create_file(standby_dir, "hs_marker") == 0, cleanup, "failed to create the marker");
   MCTF_ASSERT(run_queries(create) == 0, cleanup, "failed to create the hs_ts tablespace");
   MCTF_ASSERT(pgmoneta_test_superuser_query("SELECT oid FROM pg_tablespace WHERE spcname = 'hs_ts'", &oid) == 0, cleanup, "failed to get the hs_ts oid");
   pgmoneta_snprintf(relative, sizeof(relative), "pg_tblspc/%s", oid);

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "second backup failed");
   MCTF_ASSERT(!standby_has(standby_dir, "hs_marker"), cleanup, "added tablespace did not force a full copy");
   MCTF_ASSERT(standby_has(standby_dir, relative), cleanup, "added tablespace missing in hot standby: %s", relative);
   MCTF_ASSERT(relation_in_standby(standby_dir, "hs_tst") == 0, cleanup, "tablespace file missing or wrong size in hot standby");

   /* A dropped tablespace also makes the next backup take the full copy path */
   MCTF_ASSERT(create_file(standby_dir, "hs_marker") == 0, cleanup, "failed to create the marker");
   MCTF_ASSERT(run_queries(drop) == 0, cleanup, "failed to drop the hs_ts tablespace");

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "third backup failed");
   MCTF_ASSERT(!standby_has(standby_dir, "hs_marker"), cleanup, "dropped tablespace did not force a full copy");
   MCTF_ASSERT(!standby_has(standby_dir, relative), cleanup, "dropped tablespace still in hot standby: %s", relative);

cleanup:
   run_queries(drop);
   pgmoneta_test_superuser_query("COPY (SELECT 1) TO PROGRAM 'rmdir /tmp/pgmoneta_tblspc/hs_ts'", NULL);
   /* The hot standby copy of the tablespace, default mapping is <location>hs */
   pgmoneta_delete_directory("/tmp/pgmoneta_tblspc/hs_tshs");
   free(oid);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_hot_standby_two_directories)
{
   char standby_dir[MAX_PATH];
   char second_dir[MAX_PATH];
   int found_files = 0;
   FILE* f = NULL;
   struct main_configuration* config = NULL;

   pgmoneta_test_setup();

   config = (struct main_configuration*)shmem;

   pgmoneta_snprintf(standby_dir, sizeof(standby_dir), "%s/primary", TEST_HOT_STANDBY_DIR);
   pgmoneta_snprintf(second_dir, sizeof(second_dir), "%s/hs2/primary", TEST_HOT_STANDBY_DIR);

   /* [primary] is the last section, so this line overrides hot_standby */
   f = fopen(config->common.configuration_path, "a");
   MCTF_ASSERT(f != NULL, cleanup, "failed to open %s", config->common.configuration_path);
   fprintf(f, "\nhot_standby = %s,%s/hs2\n", TEST_HOT_STANDBY_DIR, TEST_HOT_STANDBY_DIR);
   fclose(f);
   f = NULL;
   MCTF_ASSERT(pgmoneta_tsclient_reload(0) == 0, cleanup, "failed to reload pgmoneta");

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "first backup failed");

   /* First directory takes the update path, the second one a full copy */
   MCTF_ASSERT(create_file(standby_dir, "hs_marker") == 0, cleanup, "failed to create the marker");
   pgmoneta_delete_directory(second_dir);

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "second backup failed");
   MCTF_ASSERT(standby_has(standby_dir, "hs_marker"), cleanup, "first hot standby was not updated in place");
   MCTF_ASSERT(standby_has(second_dir, "PG_VERSION"), cleanup, "PG_VERSION missing in second hot standby");
   MCTF_ASSERT(!standby_has(second_dir, "backup.info"), cleanup, "second hot standby holds the backup directory");
   MCTF_ASSERT(check_files_recursive(second_dir, &found_files) == 0, cleanup, "Found encrypted or compressed files in second hot standby");

cleanup:
   if (f != NULL)
   {
      fclose(f);
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_pgmoneta_hot_standby_reset)
{
   char standby_dir[MAX_PATH];
   char global_dir[MAX_PATH];

   pgmoneta_test_setup();

   pgmoneta_snprintf(standby_dir, sizeof(standby_dir), "%s/primary", TEST_HOT_STANDBY_DIR);
   pgmoneta_snprintf(global_dir, sizeof(global_dir), "%s/global", standby_dir);

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "first backup failed");

   /* A file in place of global/ makes the update path fail */
   MCTF_ASSERT(pgmoneta_delete_directory(global_dir) == 0, cleanup, "failed to delete %s", global_dir);
   MCTF_ASSERT(create_file(standby_dir, "global") == 0, cleanup, "failed to create %s", global_dir);

   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, MANAGEMENT_ERROR_BACKUP_EXECUTE) == 0, cleanup, "second backup did not fail");
   MCTF_ASSERT(!pgmoneta_exists(standby_dir), cleanup, "failed hot standby was not removed");

   /* The next backup takes the full copy path */
   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "third backup failed");
   MCTF_ASSERT(standby_has(standby_dir, "PG_VERSION"), cleanup, "PG_VERSION missing in hot standby");
   MCTF_ASSERT(pgmoneta_is_directory(global_dir), cleanup, "global is not a directory in hot standby");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

static int
run_queries(char* queries[])
{
   int ret = 0;

   for (int i = 0; queries[i] != NULL; i++)
   {
      if (pgmoneta_test_superuser_query(queries[i], NULL))
      {
         ret = 1;
      }
   }

   return ret;
}

static int
relation_in_standby(char* standby_dir, char* relation)
{
   char query[MAX_PATH];
   char path[MAX_PATH];
   char* result = NULL;
   char* size = NULL;
   int ret = 1;

   pgmoneta_snprintf(query, sizeof(query), "SELECT pg_relation_filepath('%s') || '|' || pg_relation_size('%s')", relation, relation);

   if (pgmoneta_test_superuser_query(query, &result) || (size = strchr(result, '|')) == NULL)
   {
      goto cleanup;
   }

   *size = '\0';
   size++;

   pgmoneta_snprintf(path, sizeof(path), "%s/%s", standby_dir, result);

   if (pgmoneta_exists(path) && pgmoneta_get_file_size(path) == strtoull(size, NULL, 10))
   {
      ret = 0;
   }

cleanup:
   free(result);

   return ret;
}

static bool
standby_has(char* standby_dir, char* relative)
{
   char path[MAX_PATH];

   pgmoneta_snprintf(path, sizeof(path), "%s/%s", standby_dir, relative);

   return pgmoneta_exists(path);
}

static int
create_file(char* dir, char* name)
{
   char path[MAX_PATH];
   FILE* f = NULL;

   pgmoneta_snprintf(path, sizeof(path), "%s/%s", dir, name);

   f = fopen(path, "w");
   if (f == NULL)
   {
      return 1;
   }

   fclose(f);

   return 0;
}
