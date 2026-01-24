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
#include <mctf.h>
#include <tsclient.h>
#include <tscommon.h>
#include <utils.h>

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

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

   /* Negative: Set unknown key */
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

   /* Use the same pattern as other backup tests - mode is already set by environment */
   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, 0) == 0, cleanup, "Backup primary failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_list_backup)
{
   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   MCTF_ASSERT(pgmoneta_tsclient_list_backup("primary", NULL, NULL, 0) == 0, cleanup, "List backup primary failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_info)
{
   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   MCTF_ASSERT(pgmoneta_tsclient_info("primary", "newest", 0) == 0, cleanup, "Info newest failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_verify)
{
   char path[MAX_PATH];
   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   snprintf(path, sizeof(path), "%s/verify_test", TEST_BASE_DIR);
   pgmoneta_mkdir(path);

   MCTF_ASSERT(pgmoneta_tsclient_verify("primary", "newest", path, NULL, 0) == 0, cleanup, "Verify newest failed");

   pgmoneta_delete_directory(path);

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_archive)
{
   char path[MAX_PATH];
   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   snprintf(path, sizeof(path), "%s/archive_test", TEST_BASE_DIR);
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

   if (pgmoneta_test_add_backup())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "current", 0) == 0, cleanup, "Restore newest failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

/* Backup Mutation Tests */

MCTF_TEST(test_cli_retain)
{
   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   MCTF_ASSERT(pgmoneta_tsclient_retain("primary", "newest", false, 0) == 0, cleanup, "Retain newest failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_expunge)
{
   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   MCTF_ASSERT(pgmoneta_tsclient_expunge("primary", "newest", false, 0) == 0, cleanup, "Expunge newest failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_annotate)
{
   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   MCTF_ASSERT(pgmoneta_tsclient_annotate("primary", "newest", "add", "testkey", "testcomment", 0) == 0, cleanup,
               "Annotate add failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_cli_delete)
{
   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

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

   snprintf(path, sizeof(path), "%s/pgmoneta_test_file", TEST_BASE_DIR);
   snprintf(path_aes, sizeof(path_aes), "%s/pgmoneta_test_file.aes", TEST_BASE_DIR);
   snprintf(path_zstd, sizeof(path_zstd), "%s/pgmoneta_test_file.zstd", TEST_BASE_DIR);

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

MCTF_TEST(test_cli_negative)
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
   MCTF_ASSERT(pgmoneta_tsclient_verify("invalid_server", "newest", NULL, NULL, MANAGEMENT_ERROR_VERIFY_NOSERVER) == 0, cleanup,
               "Verify invalid_server should fail with NOSERVER");

   /* Archive invalid server */
   snprintf(path, sizeof(path), "%s/archive_test_neg", TEST_BASE_DIR);
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
