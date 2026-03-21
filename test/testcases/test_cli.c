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

#include <management.h>
#include <aes.h>
#include <configuration.h>
#include <info.h>
#include <mctf.h>
#include <tsclient.h>
#include <tsclient_helpers.h>
#include <tscommon.h>
#include <utils.h>

#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

typedef bool (*keep_path_fn)(const char*);

static int count_encrypted_files(char* path, int* count);
static int get_failed_verify_count(struct json* response);
static bool should_keep_aes_manifest_path(const char* path);
static bool should_keep_aes_disk_path(const char* path);
static bool should_keep_incremental_manifest_path(const char* path);
static bool should_keep_incremental_disk_path(const char* path);
static int prune_aes_backup_tree(char* path, char* relative_path, keep_path_fn keep);
static int rewrite_aes_manifest(char* manifest_path, keep_path_fn keep);
static int reduce_aes_backup_fixture(char* backup_root, int backup_type);
static int encrypt_backup_fixture(char* backup_root, char* backup_dir, struct backup* backup_info);

/* Basic CLI Tests */

MCTF_TEST(test_cli_ping)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_tsclient_ping(0) == 0, cleanup, "Ping failed");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_status)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_tsclient_status(0) == 0, cleanup, "Status failed");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_status_details)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_tsclient_status_details(0) == 0, cleanup, "Status details failed");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_conf_ls)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_tsclient_conf_ls(0) == 0, cleanup, "Conf ls failed");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_conf_reload)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_tsclient_reload(0) == 0, cleanup, "Conf reload failed");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_conf_get)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_tsclient_conf_get("log_level", 0) == 0, cleanup, "Conf get log_level failed");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_conf_set)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_tsclient_conf_set("log_level", "info", 0) == 0, cleanup, "Conf set log_level=info failed");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_cli_conf_set_invalid_key)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_tsclient_conf_set("invalid_key", "value", MANAGEMENT_ERROR_CONF_SET_ERROR) == 0, cleanup,
               "Conf set invalid_key should fail with ERROR");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_mode)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_tsclient_mode("primary", "online", 0) == 0, cleanup, "Mode online failed");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_cli_mode_invalid_server)
{
   pgmoneta_test_setup();

   /* Negative */
   MCTF_ASSERT(pgmoneta_tsclient_mode("invalid_server", "online", MANAGEMENT_ERROR_MODE_NOSERVER) == 0, cleanup,
               "Mode invalid_server should fail with NOSERVER");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

/* Backup Tests */

MCTF_TEST(test_cli_backup)
{
   pgmoneta_test_setup();

   /* Ensure server is online before backup */
   MCTF_ASSERT(pgmoneta_tsclient_mode("primary", "online", 0) == 0, cleanup, "Mode online failed");
   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, 0) == 0, cleanup, "Backup primary failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_list_backup)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(pgmoneta_tsclient_list_backup("primary", NULL, NULL, 0) == 0, cleanup, "List backup primary failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_info)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(pgmoneta_tsclient_info("primary", "newest", 0) == 0, cleanup, "Info newest failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_verify)
{
   char path[MAX_PATH];
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   pgmoneta_snprintf(path, sizeof(path), "%s/verify_test", TEST_BASE_DIR);
   pgmoneta_mkdir(path);

   MCTF_ASSERT(pgmoneta_tsclient_verify("primary", "newest", path, NULL, NULL, 0) == 0, cleanup, "Verify newest failed");

   pgmoneta_delete_directory(path);

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_archive)
{
   char path[MAX_PATH];
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   pgmoneta_snprintf(path, sizeof(path), "%s/archive_test", TEST_BASE_DIR);
   pgmoneta_mkdir(path);

   MCTF_ASSERT(pgmoneta_tsclient_archive("primary", "newest", NULL, path, 0) == 0, cleanup, "Archive newest failed");

   pgmoneta_delete_directory(path);

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_restore)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "current", 0) == 0, cleanup, "Restore newest failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_backup_verify_restore_aes)
{
   char backup_root[MAX_PATH];
   char verify_path[MAX_PATH];
   char restore_path[MAX_PATH];
   char global_pg_control[MAX_PATH];
   char global_pg_control_aes[MAX_PATH];
   char postgresql_conf[MAX_PATH];
   char postgresql_conf_aes[MAX_PATH];
   char pg_hba_conf[MAX_PATH];
   char pg_hba_conf_aes[MAX_PATH];
   struct json* list_response = NULL;
   struct json* verify_response = NULL;
   struct json* backup = NULL;
   struct backup* backup_info = NULL;
   char* label = NULL;
   char* backup_dir = NULL;
   char* backup_data_dir = NULL;
   int encrypted_files = 0;

   memset(backup_root, 0, sizeof(backup_root));
   memset(verify_path, 0, sizeof(verify_path));
   memset(restore_path, 0, sizeof(restore_path));
   memset(global_pg_control, 0, sizeof(global_pg_control));
   memset(global_pg_control_aes, 0, sizeof(global_pg_control_aes));
   memset(postgresql_conf, 0, sizeof(postgresql_conf));
   memset(postgresql_conf_aes, 0, sizeof(postgresql_conf_aes));
   memset(pg_hba_conf, 0, sizeof(pg_hba_conf));
   memset(pg_hba_conf_aes, 0, sizeof(pg_hba_conf_aes));

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "AES backup primary failed");

   MCTF_ASSERT(pgmoneta_tsclient_list_backup("primary", NULL, &list_response, 0) == 0, cleanup, "List backup primary failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_get_backup_count(list_response), 1, cleanup, "backup count mismatch");

   backup = pgmoneta_tsclient_get_backup(list_response, 0);
   MCTF_ASSERT_PTR_NONNULL(backup, cleanup, "backup 0 null");

   label = pgmoneta_tsclient_get_backup_label(backup);
   MCTF_ASSERT_PTR_NONNULL(label, cleanup, "backup label null");
   label = strdup(label);
   MCTF_ASSERT_PTR_NONNULL(label, cleanup, "backup label allocation failed");

   backup_dir = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(backup_dir, cleanup, "backup directory null");
   MCTF_ASSERT_INT_EQ(pgmoneta_load_info(backup_dir, label, &backup_info), 0, cleanup, "load_info failed");
   MCTF_ASSERT_PTR_NONNULL(backup_info, cleanup, "backup info null");

   backup_data_dir = pgmoneta_get_server_backup_identifier_data(PRIMARY_SERVER, label);
   MCTF_ASSERT_PTR_NONNULL(backup_data_dir, cleanup, "backup data directory null");
   pgmoneta_snprintf(backup_root, sizeof(backup_root), "%s/%s", backup_dir, label);

   /* Stop WAL activity before rewriting the local backup fixture. */
   MCTF_ASSERT(pgmoneta_tsclient_mode("primary", "offline", 0) == 0, cleanup, "Mode offline failed");

   MCTF_ASSERT_INT_EQ(reduce_aes_backup_fixture(backup_root, TYPE_FULL), 0, cleanup, "Reducing AES backup fixture failed");
   MCTF_ASSERT_INT_EQ(encrypt_backup_fixture(backup_root, backup_dir, backup_info), 0, cleanup,
                      "Encrypting AES backup fixture failed");
   free(backup_info);
   backup_info = NULL;
   pgmoneta_json_destroy(list_response);
   list_response = NULL;

   MCTF_ASSERT(pgmoneta_tsclient_list_backup("primary", NULL, &list_response, 0) == 0, cleanup, "List backup primary failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_get_backup_count(list_response), 1, cleanup, "backup count mismatch");
   backup = pgmoneta_tsclient_get_backup(list_response, 0);
   MCTF_ASSERT_PTR_NONNULL(backup, cleanup, "backup 0 null");
   MCTF_ASSERT_INT_EQ((int)pgmoneta_json_get(backup, MANAGEMENT_ARGUMENT_ENCRYPTION), ENCRYPTION_AES_256_CBC, cleanup,
                      "backup JSON encryption mismatch");

   MCTF_ASSERT_INT_EQ(pgmoneta_load_info(backup_dir, label, &backup_info), 0, cleanup, "load_info failed");
   MCTF_ASSERT_PTR_NONNULL(backup_info, cleanup, "backup info null");
   MCTF_ASSERT_INT_EQ(backup_info->encryption, ENCRYPTION_AES_256_CBC, cleanup, "backup.info encryption mismatch");
   MCTF_ASSERT_INT_EQ(count_encrypted_files(backup_data_dir, &encrypted_files), 0, cleanup, "encrypted file scan failed");
   MCTF_ASSERT(encrypted_files > 0, cleanup, "Expected encrypted backup files ending with .aes");

   pgmoneta_snprintf(verify_path, sizeof(verify_path), "%s/verify_aes", TEST_BASE_DIR);
   pgmoneta_mkdir(verify_path);

   MCTF_ASSERT(pgmoneta_tsclient_verify("primary", "newest", verify_path, NULL, &verify_response, 0) == 0, cleanup,
               "Verify newest failed");
   MCTF_ASSERT_INT_EQ(get_failed_verify_count(verify_response), 0, cleanup, "Verify reported failed files");

   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "current", 0) == 0, cleanup, "Restore newest failed");

   pgmoneta_snprintf(restore_path, sizeof(restore_path), "%s/primary-%s", TEST_RESTORE_DIR, label);
   pgmoneta_snprintf(global_pg_control, sizeof(global_pg_control), "%s/global/pg_control", restore_path);
   pgmoneta_snprintf(global_pg_control_aes, sizeof(global_pg_control_aes), "%s/global/pg_control.aes", restore_path);
   pgmoneta_snprintf(postgresql_conf, sizeof(postgresql_conf), "%s/postgresql.conf", restore_path);
   pgmoneta_snprintf(postgresql_conf_aes, sizeof(postgresql_conf_aes), "%s/postgresql.conf.aes", restore_path);
   pgmoneta_snprintf(pg_hba_conf, sizeof(pg_hba_conf), "%s/pg_hba.conf", restore_path);
   pgmoneta_snprintf(pg_hba_conf_aes, sizeof(pg_hba_conf_aes), "%s/pg_hba.conf.aes", restore_path);

   MCTF_ASSERT(pgmoneta_exists(restore_path), cleanup, "restore directory missing");
   MCTF_ASSERT(pgmoneta_exists(global_pg_control), cleanup, "restored global/pg_control missing");
   MCTF_ASSERT(pgmoneta_exists(postgresql_conf), cleanup, "restored postgresql.conf missing");
   MCTF_ASSERT(pgmoneta_exists(pg_hba_conf), cleanup, "restored pg_hba.conf missing");
   MCTF_ASSERT(!pgmoneta_exists(global_pg_control_aes), cleanup, "restored global/pg_control should not remain encrypted");
   MCTF_ASSERT(!pgmoneta_exists(postgresql_conf_aes), cleanup, "restored postgresql.conf should not remain encrypted");
   MCTF_ASSERT(!pgmoneta_exists(pg_hba_conf_aes), cleanup, "restored pg_hba.conf should not remain encrypted");

cleanup:
   if (verify_response != NULL)
   {
      pgmoneta_json_destroy(verify_response);
   }
   if (list_response != NULL)
   {
      pgmoneta_json_destroy(list_response);
   }
   if (backup_info != NULL)
   {
      free(backup_info);
   }
   if (label != NULL)
   {
      free(label);
   }
   if (backup_dir != NULL)
   {
      free(backup_dir);
   }
   if (backup_data_dir != NULL)
   {
      free(backup_data_dir);
   }
   if (strlen(verify_path) > 0)
   {
      pgmoneta_delete_directory(verify_path);
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_backup_verify_restore_aes_chain)
{
   char backup_root[MAX_PATH];
   char verify_path[MAX_PATH];
   char restore_path[MAX_PATH];
   char global_pg_control[MAX_PATH];
   char global_pg_control_aes[MAX_PATH];
   char postgresql_conf[MAX_PATH];
   char postgresql_conf_aes[MAX_PATH];
   char pg_hba_conf[MAX_PATH];
   char pg_hba_conf_aes[MAX_PATH];
   struct json* list_response = NULL;
   struct json* verify_response = NULL;
   struct json* b[3] = {NULL, NULL, NULL};
   struct backup* backup_info = NULL;
   char* labels[3] = {NULL, NULL, NULL};
   char* newest_label = NULL;
   char* backup_dir = NULL;
   char* backup_data_dir = NULL;
   struct main_configuration* config = NULL;
   int original_workers = 0;
   int original_server_workers = -1;
   int encrypted_files = 0;

   memset(backup_root, 0, sizeof(backup_root));
   memset(verify_path, 0, sizeof(verify_path));
   memset(restore_path, 0, sizeof(restore_path));
   memset(global_pg_control, 0, sizeof(global_pg_control));
   memset(global_pg_control_aes, 0, sizeof(global_pg_control_aes));
   memset(postgresql_conf, 0, sizeof(postgresql_conf));
   memset(postgresql_conf_aes, 0, sizeof(postgresql_conf_aes));
   memset(pg_hba_conf, 0, sizeof(pg_hba_conf));
   memset(pg_hba_conf_aes, 0, sizeof(pg_hba_conf_aes));

   pgmoneta_test_setup();
   config = (struct main_configuration*)shmem;
   MCTF_ASSERT_PTR_NONNULL(config, cleanup_chain, "config null");

   /* Create a FULL -> INCREMENTAL -> INCREMENTAL chain. */
   MCTF_ASSERT(pgmoneta_test_add_backup_chain() == 0, cleanup_chain,
               "backup chain failed during setup");

   /* Load the chain and validate its shape. */
   MCTF_ASSERT(pgmoneta_tsclient_list_backup("primary", NULL, &list_response, 0) == 0, cleanup_chain,
               "List backup primary failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_get_backup_count(list_response), 3, cleanup_chain,
                      "backup count mismatch");

   for (int i = 0; i < 3; i++)
   {
      b[i] = pgmoneta_tsclient_get_backup(list_response, i);
      MCTF_ASSERT_PTR_NONNULL(b[i], cleanup_chain, "backup %d null", i);
   }

   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(b[0]), "FULL", cleanup_chain,
                      "backup 0 type mismatch");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(b[1]), "INCREMENTAL", cleanup_chain,
                      "backup 1 type mismatch");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(b[2]), "INCREMENTAL", cleanup_chain,
                      "backup 2 type mismatch");

   MCTF_ASSERT(pgmoneta_tsclient_verify_backup_chain(b[0], b[1]), cleanup_chain,
               "backup 1 parent mismatch (should be b0)");
   MCTF_ASSERT(pgmoneta_tsclient_verify_backup_chain(b[1], b[2]), cleanup_chain,
               "backup 2 parent mismatch (should be b1)");

   /* Freeze server activity before mutating the backup fixtures. */
   MCTF_ASSERT(pgmoneta_tsclient_mode("primary", "offline", 0) == 0, cleanup_chain,
               "Mode offline failed");

   /* Capture labels so the chain can be processed deterministically. */
   backup_dir = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(backup_dir, cleanup_chain, "backup directory null");

   for (int i = 0; i < 3; i++)
   {
      char* lbl = pgmoneta_tsclient_get_backup_label(b[i]);
      MCTF_ASSERT_PTR_NONNULL(lbl, cleanup_chain, "backup %d label null", i);
      labels[i] = strdup(lbl);
      MCTF_ASSERT_PTR_NONNULL(labels[i], cleanup_chain, "backup %d label alloc failed", i);
   }
   newest_label = labels[2];

   original_workers = config->workers;
   original_server_workers = config->common.servers[PRIMARY_SERVER].workers;
   config->workers = 4;
   config->common.servers[PRIMARY_SERVER].workers = -1;

   /* Encrypt from newest to oldest.
    * Incremental backups can contain symlinks back into older backups, so we
    * need to materialize their encrypted copies before the older plaintext
    * targets are replaced with .aes files. */
   for (int i = 2; i >= 0; i--)
   {
      memset(backup_root, 0, sizeof(backup_root));
      pgmoneta_snprintf(backup_root, sizeof(backup_root), "%s/%s", backup_dir, labels[i]);

      MCTF_ASSERT_INT_EQ(pgmoneta_load_info(backup_dir, labels[i], &backup_info), 0, cleanup_chain,
                         "load_info failed for backup %d", i);
      MCTF_ASSERT_PTR_NONNULL(backup_info, cleanup_chain, "backup info null for backup %d", i);

      /* Keep a small restore-valid fixture before local encryption. */
      MCTF_ASSERT_INT_EQ(reduce_aes_backup_fixture(backup_root, backup_info->type), 0, cleanup_chain,
                         "Reducing AES backup fixture failed for backup %d", i);

      MCTF_ASSERT_INT_EQ(encrypt_backup_fixture(backup_root, backup_dir, backup_info), 0, cleanup_chain,
                         "Encrypting backup fixture failed for backup %d", i);
      free(backup_info);
      backup_info = NULL;

      /* Reload the saved metadata and confirm AES is persisted. */
      MCTF_ASSERT_INT_EQ(pgmoneta_load_info(backup_dir, labels[i], &backup_info), 0, cleanup_chain,
                         "reload load_info failed for backup %d", i);
      MCTF_ASSERT_PTR_NONNULL(backup_info, cleanup_chain, "reload backup info null for backup %d", i);
      MCTF_ASSERT_INT_EQ(backup_info->encryption, ENCRYPTION_AES_256_CBC, cleanup_chain,
                         "backup.info encryption mismatch for backup %d", i);

      /* Keep the direct on-disk encrypted-file assertion on the full backup.
       * The chain backups are validated through persisted metadata plus
       * verify/restore across the chain. */
      if (backup_info->type == TYPE_FULL)
      {
         encrypted_files = 0;
         backup_data_dir = pgmoneta_get_server_backup_identifier_data(PRIMARY_SERVER, labels[i]);
         MCTF_ASSERT_PTR_NONNULL(backup_data_dir, cleanup_chain, "backup data dir null for backup %d", i);
         MCTF_ASSERT_INT_EQ(count_encrypted_files(backup_data_dir, &encrypted_files), 0, cleanup_chain,
                            "encrypted file scan failed for backup %d", i);
         MCTF_ASSERT(encrypted_files > 0, cleanup_chain,
                     "Expected encrypted files for backup %d", i);
         free(backup_data_dir);
         backup_data_dir = NULL;
      }
      free(backup_info);
      backup_info = NULL;
   }

   /* Re-list the chain and confirm the management metadata is encrypted. */
   pgmoneta_json_destroy(list_response);
   list_response = NULL;

   MCTF_ASSERT(pgmoneta_tsclient_list_backup("primary", NULL, &list_response, 0) == 0, cleanup_chain,
               "Re-list backup primary failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_get_backup_count(list_response), 3, cleanup_chain,
                      "re-list backup count mismatch");

   for (int i = 0; i < 3; i++)
   {
      struct json* bk = pgmoneta_tsclient_get_backup(list_response, i);
      MCTF_ASSERT_PTR_NONNULL(bk, cleanup_chain, "re-list backup %d null", i);
      MCTF_ASSERT_INT_EQ((int)pgmoneta_json_get(bk, MANAGEMENT_ARGUMENT_ENCRYPTION), ENCRYPTION_AES_256_CBC,
                         cleanup_chain, "re-list backup %d JSON encryption mismatch", i);
   }

   /* Verify each backup in the chain. */
   for (int i = 0; i < 3; i++)
   {
      pgmoneta_snprintf(verify_path, sizeof(verify_path), "%s/verify_aes_chain_%d", TEST_BASE_DIR, i);
      pgmoneta_mkdir(verify_path);

      verify_response = NULL;
      MCTF_ASSERT(pgmoneta_tsclient_verify("primary", labels[i], verify_path, NULL, &verify_response, 0) == 0,
                  cleanup_chain, "Verify backup %d failed", i);
      MCTF_ASSERT_INT_EQ(get_failed_verify_count(verify_response), 0, cleanup_chain,
                         "Verify backup %d reported failed files", i);
      pgmoneta_json_destroy(verify_response);
      verify_response = NULL;

      pgmoneta_delete_directory(verify_path);
      memset(verify_path, 0, sizeof(verify_path));
   }

   /* Restore the newest backup and confirm plaintext output. */
   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", newest_label, "current", 0) == 0, cleanup_chain,
               "Restore newest failed");

   pgmoneta_snprintf(restore_path, sizeof(restore_path), "%s/primary-%s", TEST_RESTORE_DIR, newest_label);
   pgmoneta_snprintf(global_pg_control, sizeof(global_pg_control), "%s/global/pg_control", restore_path);
   pgmoneta_snprintf(global_pg_control_aes, sizeof(global_pg_control_aes), "%s/global/pg_control.aes", restore_path);
   pgmoneta_snprintf(postgresql_conf, sizeof(postgresql_conf), "%s/postgresql.conf", restore_path);
   pgmoneta_snprintf(postgresql_conf_aes, sizeof(postgresql_conf_aes), "%s/postgresql.conf.aes", restore_path);
   pgmoneta_snprintf(pg_hba_conf, sizeof(pg_hba_conf), "%s/pg_hba.conf", restore_path);
   pgmoneta_snprintf(pg_hba_conf_aes, sizeof(pg_hba_conf_aes), "%s/pg_hba.conf.aes", restore_path);

   MCTF_ASSERT(pgmoneta_exists(restore_path), cleanup_chain, "restore directory missing");
   MCTF_ASSERT(pgmoneta_exists(global_pg_control), cleanup_chain, "restored global/pg_control missing");
   MCTF_ASSERT(pgmoneta_exists(postgresql_conf), cleanup_chain, "restored postgresql.conf missing");
   MCTF_ASSERT(pgmoneta_exists(pg_hba_conf), cleanup_chain, "restored pg_hba.conf missing");
   MCTF_ASSERT(!pgmoneta_exists(global_pg_control_aes), cleanup_chain,
               "restored global/pg_control should not remain encrypted");
   MCTF_ASSERT(!pgmoneta_exists(postgresql_conf_aes), cleanup_chain,
               "restored postgresql.conf should not remain encrypted");
   MCTF_ASSERT(!pgmoneta_exists(pg_hba_conf_aes), cleanup_chain,
               "restored pg_hba.conf should not remain encrypted");

cleanup_chain:
   if (config != NULL)
   {
      config->workers = original_workers;
      config->common.servers[PRIMARY_SERVER].workers = original_server_workers;
   }
   if (verify_response != NULL)
   {
      pgmoneta_json_destroy(verify_response);
   }
   if (list_response != NULL)
   {
      pgmoneta_json_destroy(list_response);
   }
   if (backup_info != NULL)
   {
      free(backup_info);
   }
   for (int i = 0; i < 3; i++)
   {
      if (labels[i] != NULL)
      {
         free(labels[i]);
      }
   }
   if (backup_dir != NULL)
   {
      free(backup_dir);
   }
   if (backup_data_dir != NULL)
   {
      free(backup_data_dir);
   }
   if (strlen(verify_path) > 0)
   {
      pgmoneta_delete_directory(verify_path);
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

/* Backup Mutation Tests */

MCTF_TEST(test_cli_retain)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(pgmoneta_tsclient_retain("primary", "newest", false, 0) == 0, cleanup, "Retain newest failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_expunge)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(pgmoneta_tsclient_expunge("primary", "newest", false, 0) == 0, cleanup, "Expunge newest failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_annotate)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(pgmoneta_tsclient_annotate("primary", "newest", "add", "testkey", "testcomment", 0) == 0, cleanup,
               "Annotate add failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_delete)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(pgmoneta_tsclient_delete("primary", "oldest", 0) == 0, cleanup, "Delete oldest failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

/* Admin/Utility Tests */

MCTF_TEST(test_cli_reset)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_tsclient_reset(0) == 0, cleanup, "Reset (clear prometheus) failed");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_utility_positive)
{
   FILE* fp;
   char path[MAX_PATH];
   char path_aes[MAX_PATH];
   char path_zstd[MAX_PATH];

   pgmoneta_test_setup();

   pgmoneta_snprintf(path, sizeof(path), "%s/pgmoneta_test_file", TEST_BASE_DIR);
   pgmoneta_snprintf(path_aes, sizeof(path_aes), "%s/pgmoneta_test_file.aes", TEST_BASE_DIR);
   pgmoneta_snprintf(path_zstd, sizeof(path_zstd), "%s/pgmoneta_test_file.zstd", TEST_BASE_DIR);

   /* Setup: Create a dummy file */
   fp = fopen(path, "w");
   MCTF_ASSERT_PTR_NONNULL(fp, cleanup, "Failed to create test file");
   fprintf(fp, "test content for encrypt/compress testing");
   fclose(fp);

   /* Positive: Encrypt */
   MCTF_ASSERT(pgmoneta_tsclient_encrypt(path, 0) == 0, cleanup, "Encrypt failed");

   /* Positive: Decrypt (encrypt creates .aes file) */
   MCTF_ASSERT(pgmoneta_tsclient_decrypt(path_aes, 0) == 0, cleanup, "Decrypt failed");

   /* Positive: Compress */
   MCTF_ASSERT(pgmoneta_tsclient_compress(path, 0) == 0, cleanup, "Compress failed");

   /* Positive: Decompress (compress creates .zstd file) */
   MCTF_ASSERT(pgmoneta_tsclient_decompress(path_zstd, 0) == 0, cleanup, "Decompress failed");

cleanup:
   /* Cleanup test files */
   remove(path);
   remove(path_aes);
   remove(path_zstd);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

/* Negative Tests */

MCTF_TEST_NEGATIVE(test_cli_negative)
{
   char path[MAX_PATH];
   pgmoneta_test_setup();

   /* Backup invalid server */
   MCTF_ASSERT(pgmoneta_tsclient_backup("invalid_server", NULL, MANAGEMENT_ERROR_BACKUP_NOSERVER) == 0, cleanup,
               "Backup invalid_server should fail with NOSERVER");

   /* List backup invalid server */
   MCTF_ASSERT(pgmoneta_tsclient_list_backup("invalid_server", NULL, NULL, MANAGEMENT_ERROR_LIST_BACKUP_NOSERVER) == 0, cleanup,
               "List backup invalid_server should fail with NOSERVER");

   /* Delete invalid server */
   MCTF_ASSERT(pgmoneta_tsclient_delete("invalid_server", "oldest", MANAGEMENT_ERROR_DELETE_NOSERVER) == 0, cleanup,
               "Delete invalid_server should fail with NOSERVER");

   /* Retain invalid server */
   MCTF_ASSERT(pgmoneta_tsclient_retain("invalid_server", "newest", false, MANAGEMENT_ERROR_RETAIN_NOSERVER) == 0, cleanup,
               "Retain invalid_server should fail with NOSERVER");

   /* Expunge invalid server */
   MCTF_ASSERT(pgmoneta_tsclient_expunge("invalid_server", "newest", false, MANAGEMENT_ERROR_EXPUNGE_NOSERVER) == 0, cleanup,
               "Expunge invalid_server should fail with NOSERVER");

   /* Info invalid server */
   MCTF_ASSERT(pgmoneta_tsclient_info("invalid_server", "newest", MANAGEMENT_ERROR_INFO_NOSERVER) == 0, cleanup,
               "Info invalid_server should fail with NOSERVER");

   /* Annotate invalid server */
   MCTF_ASSERT(pgmoneta_tsclient_annotate("invalid_server", "newest", "add", "k", "c", MANAGEMENT_ERROR_ANNOTATE_NOSERVER) == 0, cleanup,
               "Annotate invalid_server should fail with NOSERVER");

   /* Verify invalid server */
   MCTF_ASSERT(pgmoneta_tsclient_verify("invalid_server", "newest", NULL, NULL, NULL, MANAGEMENT_ERROR_VERIFY_NOSERVER) == 0, cleanup,
               "Verify invalid_server should fail with NOSERVER");

   /* Archive invalid server */
   pgmoneta_snprintf(path, sizeof(path), "%s/archive_test_neg", TEST_BASE_DIR);
   pgmoneta_mkdir(path);
   MCTF_ASSERT(pgmoneta_tsclient_archive("invalid_server", "newest", NULL, path, MANAGEMENT_ERROR_ARCHIVE_NOSERVER) == 0, cleanup,
               "Archive invalid_server should fail with NOSERVER");
   pgmoneta_delete_directory(path);

   /* Restore invalid server */
   MCTF_ASSERT(pgmoneta_tsclient_restore("invalid_server", "newest", "current", MANAGEMENT_ERROR_RESTORE_NOSERVER) == 0, cleanup,
               "Restore invalid_server should fail with NOSERVER");

   /* Encrypt/Decrypt with non-existent file */
   MCTF_ASSERT(pgmoneta_tsclient_encrypt("/nonexistent/path/file.txt", MANAGEMENT_ERROR_ENCRYPT_NOFILE) == 0, cleanup,
               "Encrypt nonexistent file should fail with NOFILE");
   MCTF_ASSERT(pgmoneta_tsclient_decrypt("/nonexistent/path/file.txt.aes", MANAGEMENT_ERROR_DECRYPT_NOFILE) == 0, cleanup,
               "Decrypt nonexistent file should fail with NOFILE");

   /* Compress/Decompress with non-existent file */
   MCTF_ASSERT(pgmoneta_tsclient_compress("/nonexistent/path/file.txt", MANAGEMENT_ERROR_ZSTD_NOFILE) == 0, cleanup,
               "Compress nonexistent file should fail with NOFILE");
   MCTF_ASSERT(pgmoneta_tsclient_decompress("/nonexistent/path/file.txt.zstd", MANAGEMENT_ERROR_ZSTD_NOFILE) == 0, cleanup,
               "Decompress nonexistent file should fail with NOFILE");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

/* Shutdown Test - Must be last */

// MCTF_TEST(test_cli_shutdown)
// {
//    FILE* fp;
//    char pid_str[32];
//    pid_t pid = -1;

//    pgmoneta_test_setup();

//    /* Get PID of pgmoneta */
//    fp = popen("pidof pgmoneta", "r");
//    if (fp != NULL)
//    {
//       if (fgets(pid_str, sizeof(pid_str), fp) != NULL)
//       {
//          pid = atoi(pid_str);
//       }
//       pclose(fp);
//    }

//    MCTF_ASSERT(pid > 0, cleanup, "Could not find pgmoneta PID");
//    MCTF_ASSERT(kill(pid, 0) == 0, cleanup, "pgmoneta process does not exist");

//    /* Shutdown */
//    MCTF_ASSERT(pgmoneta_tsclient_shutdown(0) == 0, cleanup, "Shutdown failed");

//    /* Wait for shutdown */
//    sleep(3);

//    /* Check if process is gone */
//    if (kill(pid, 0) == 0)
//    {
//       mctf_errno = __LINE__;
//       goto cleanup;
//    }

//    MCTF_ASSERT(errno == ESRCH, cleanup, "Unexpected error checking PID");

// cleanup:
//    /* No teardown - server is dead */
//    MCTF_FINISH();
// }

static int
count_encrypted_files(char* path, int* count)
{
   DIR* dir = NULL;
   struct dirent* entry = NULL;
   struct stat st;
   char child[MAX_PATH];

   if (path == NULL || count == NULL)
   {
      goto error;
   }

   dir = opendir(path);
   if (dir == NULL)
   {
      goto error;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      {
         continue;
      }

      memset(child, 0, sizeof(child));
      pgmoneta_snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);

      if (lstat(child, &st))
      {
         goto error;
      }

      if (S_ISDIR(st.st_mode))
      {
         if (count_encrypted_files(child, count))
         {
            goto error;
         }
      }
      else if (pgmoneta_is_encrypted(entry->d_name))
      {
         (*count)++;
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

static int
get_failed_verify_count(struct json* response)
{
   struct json* response_obj = NULL;
   struct json* files_obj = NULL;
   struct json* failed = NULL;

   if (response == NULL)
   {
      return -1;
   }

   response_obj = (struct json*)pgmoneta_json_get(response, MANAGEMENT_CATEGORY_RESPONSE);
   if (response_obj == NULL)
   {
      return -1;
   }

   files_obj = (struct json*)pgmoneta_json_get(response_obj, MANAGEMENT_ARGUMENT_FILES);
   if (files_obj == NULL)
   {
      return -1;
   }

   failed = (struct json*)pgmoneta_json_get(files_obj, MANAGEMENT_ARGUMENT_FAILED);
   return (int)pgmoneta_json_array_length(failed);
}

static int
reduce_aes_backup_fixture(char* backup_root, int backup_type)
{
   char manifest_path[MAX_PATH];
   char data_path[MAX_PATH];
   keep_path_fn manifest_keep = NULL;
   keep_path_fn disk_keep = NULL;

   if (backup_root == NULL)
   {
      return 1;
   }

   if (backup_type == TYPE_INCREMENTAL)
   {
      manifest_keep = should_keep_incremental_manifest_path;
      disk_keep = should_keep_incremental_disk_path;
   }
   else
   {
      manifest_keep = should_keep_aes_manifest_path;
      disk_keep = should_keep_aes_disk_path;
   }

   memset(manifest_path, 0, sizeof(manifest_path));
   memset(data_path, 0, sizeof(data_path));

   pgmoneta_snprintf(manifest_path, sizeof(manifest_path), "%s/backup.manifest", backup_root);
   pgmoneta_snprintf(data_path, sizeof(data_path), "%s/data", backup_root);

   if (rewrite_aes_manifest(manifest_path, manifest_keep))
   {
      return 1;
   }

   if (prune_aes_backup_tree(data_path, "", disk_keep))
   {
      return 1;
   }

   return 0;
}

static bool
should_keep_aes_manifest_path(const char* path)
{
   static const char* keep_paths[] = {
      "backup_label",
      "PG_VERSION",
      "pg_hba.conf",
      "pg_ident.conf",
      "postgresql.auto.conf",
      "postgresql.conf",
      "global/pg_control",
      "global/pg_filenode.map",
      NULL};

   for (int i = 0; keep_paths[i] != NULL; i++)
   {
      if (!strcmp(path, keep_paths[i]))
      {
         return true;
      }
   }

   return false;
}

static bool
should_keep_aes_disk_path(const char* path)
{
   static const char* keep_paths[] = {
      "backup_label",
      "PG_VERSION.zstd",
      "PG_VERSION.zstd.aes",
      "pg_hba.conf.zstd",
      "pg_hba.conf.zstd.aes",
      "pg_ident.conf.zstd",
      "pg_ident.conf.zstd.aes",
      "postgresql.auto.conf.zstd",
      "postgresql.auto.conf.zstd.aes",
      "postgresql.conf.zstd",
      "postgresql.conf.zstd.aes",
      "global/pg_control.zstd",
      "global/pg_control.zstd.aes",
      "global/pg_filenode.map.zstd",
      "global/pg_filenode.map.zstd.aes",
      NULL};

   for (int i = 0; keep_paths[i] != NULL; i++)
   {
      if (!strcmp(path, keep_paths[i]))
      {
         return true;
      }
   }

   return false;
}

static bool
should_keep_incremental_manifest_path(const char* path)
{
   if (path == NULL)
   {
      return false;
   }

   if (!strcmp(path, "backup_manifest"))
   {
      return true;
   }

   return should_keep_aes_manifest_path(path);
}

static bool
should_keep_incremental_disk_path(const char* path)
{
   if (path == NULL)
   {
      return false;
   }

   if (!strcmp(path, "backup_manifest"))
   {
      return true;
   }

   return should_keep_aes_disk_path(path);
}

static int
prune_aes_backup_tree(char* path, char* relative_path, keep_path_fn keep)
{
   DIR* dir = NULL;
   struct dirent* entry = NULL;
   char child[MAX_PATH];
   char child_relative[MAX_PATH];
   struct stat st;

   if (path == NULL || relative_path == NULL)
   {
      goto error;
   }

   dir = opendir(path);
   if (dir == NULL)
   {
      goto error;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      {
         continue;
      }

      memset(child, 0, sizeof(child));
      memset(child_relative, 0, sizeof(child_relative));
      pgmoneta_snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);

      if (strlen(relative_path) == 0)
      {
         pgmoneta_snprintf(child_relative, sizeof(child_relative), "%s", entry->d_name);
      }
      else
      {
         pgmoneta_snprintf(child_relative, sizeof(child_relative), "%s/%s", relative_path, entry->d_name);
      }

      if (lstat(child, &st))
      {
         goto error;
      }

      if (S_ISDIR(st.st_mode))
      {
         if (prune_aes_backup_tree(child, child_relative, keep))
         {
            goto error;
         }
      }
      else if (!keep(child_relative))
      {
         if (pgmoneta_delete_file(child, NULL))
         {
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

static int
rewrite_aes_manifest(char* manifest_path, keep_path_fn keep)
{
   FILE* input = NULL;
   FILE* output = NULL;
   char temp_path[MAX_PATH];
   char line[INFO_BUFFER_SIZE];
   char* comma = NULL;
   size_t path_length = 0;
   char path[MAX_PATH];
   bool remove_temp = false;

   if (manifest_path == NULL)
   {
      goto error;
   }

   memset(temp_path, 0, sizeof(temp_path));
   memset(line, 0, sizeof(line));
   pgmoneta_snprintf(temp_path, sizeof(temp_path), "%s.tmp", manifest_path);

   input = fopen(manifest_path, "r");
   if (input == NULL)
   {
      goto error;
   }

   output = fopen(temp_path, "w");
   if (output == NULL)
   {
      goto error;
   }
   remove_temp = true;

   while (fgets(line, sizeof(line), input) != NULL)
   {
      comma = strchr(line, ',');
      if (comma == NULL)
      {
         continue;
      }

      memset(path, 0, sizeof(path));
      path_length = (size_t)(comma - line);
      if (path_length >= sizeof(path))
      {
         goto error;
      }

      memcpy(path, line, path_length);

      if (keep(path))
      {
         if (fputs(line, output) == EOF)
         {
            goto error;
         }
      }
   }

   if (fflush(output))
   {
      goto error;
   }

   if (fclose(output))
   {
      output = NULL;
      goto error;
   }
   output = NULL;

   if (fclose(input))
   {
      input = NULL;
      goto error;
   }
   input = NULL;

   if (rename(temp_path, manifest_path))
   {
      goto error;
   }
   remove_temp = false;

   return 0;

error:
   if (output != NULL)
   {
      fclose(output);
   }
   if (input != NULL)
   {
      fclose(input);
   }
   if (remove_temp)
   {
      remove(temp_path);
   }
   return 1;
}

static int
encrypt_backup_fixture(char* backup_root, char* backup_dir, struct backup* backup_info)
{
   struct main_configuration* config = NULL;
   struct workers* workers = NULL;
   char data_path[MAX_PATH];
   int original_encryption = ENCRYPTION_NONE;
   int number_of_workers = 0;

   if (backup_root == NULL || backup_dir == NULL || backup_info == NULL)
   {
      goto error;
   }

   config = (struct main_configuration*)shmem;
   if (config == NULL)
   {
      goto error;
   }

   memset(data_path, 0, sizeof(data_path));
   pgmoneta_snprintf(data_path, sizeof(data_path), "%s/data", backup_root);
   original_encryption = config->encryption;
   config->encryption = ENCRYPTION_AES_256_CBC;
   number_of_workers = pgmoneta_get_number_of_workers(PRIMARY_SERVER);
   if (number_of_workers > 0)
   {
      if (pgmoneta_workers_initialize(number_of_workers, &workers))
      {
         goto error;
      }
   }

   if (pgmoneta_encrypt_data(data_path, workers))
   {
      goto error;
   }

   if (pgmoneta_encrypt_tablespaces(backup_root, workers))
   {
      goto error;
   }

   if (workers != NULL)
   {
      pgmoneta_workers_wait(workers);
      if (!workers->outcome)
      {
         goto error;
      }
      pgmoneta_workers_destroy(workers);
      workers = NULL;
   }

   backup_info->encryption = ENCRYPTION_AES_256_CBC;
   if (pgmoneta_save_info(backup_dir, backup_info))
   {
      goto error;
   }

   config->encryption = original_encryption;

   return 0;

error:
   if (workers != NULL)
   {
      pgmoneta_workers_destroy(workers);
   }
   if (config != NULL)
   {
      config->encryption = original_encryption;
   }
   return 1;
}
