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
#include <aes.h>
#include <configuration.h>
#include <mctf.h>
#include <json.h>
#include <tscommon.h>
#include <tswalutils.h>
#include <tswalinfo.h>
#include <walfile.h>
#include <utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define WAL_TEST_SUBDIR         "/walfiles"

#define WALINFO_AES_TEST_SUBDIR "/walinfo_aes"

/*
 * master.key contents from `pgmoneta-admin user master-key` (two lines, same as fputs order in admin.c).
 * Line 1: base64(master password). Line 2: base64(16-byte PBKDF2 salt). No embedded newlines in the macros.
 */
#define WALINFO_TEST_MASTER_KEY "cmVwbHBhc3M=\nCDpKWSBhtVbm+PkViwZONw==\n"

/**
 * Creates the master key and the encrypted WAL file.
 * @param master_key_path Full path to ~/.pgmoneta/master.key
 * @param wf WAL to write (caller owns; not destroyed here)
 * @param plain_wal_path Path for the unencrypted WAL segment
 * @param aes_wal_path Destination path for the encrypted WAL (typically plain + ".aes" or ".zstd.aes")
 * @param compress If non-zero, compress plain with zstd to plain.zstd then encrypt that file (requires zstd in PATH)
 * @return 0 on success, 1 on failure
 */
static int
create_master_key_and_encrypted_wal(const char* master_key_path, struct walfile* wf,
                                    char* plain_wal_path, char* aes_wal_path, int compress)
{
   FILE* f = NULL;
   char zstd_path[MAX_PATH];
   char* zstd_out = NULL;
   int zstd_exit = 1;
   char compress_cmd[MAX_PATH * 2];

   memset(zstd_path, 0, sizeof(zstd_path));

   // /* Drop cached master key/salt so encrypt matches this file (also clears security cache). */
   pgmoneta_clear_aes_cache();

   /* Invalid arguments: nothing to unlink (avoid unlink(NULL)). */
   if (master_key_path == NULL || wf == NULL || plain_wal_path == NULL || aes_wal_path == NULL)
   {
      return 1;
   }

   f = fopen(master_key_path, "w");
   if (f == NULL)
   {
      goto error;
   }
   if (fputs(WALINFO_TEST_MASTER_KEY, f) == EOF)
   {
      goto error;
   }
   /* After fclose, never pass f to fclose again (even if fclose returns non-zero). */
   if (fclose(f) != 0)
   {
      f = NULL;
      goto error;
   }
   f = NULL;

   if (chmod(master_key_path, 0600) != 0)
   {
      goto error;
   }

   if (pgmoneta_write_walfile(wf, 0, plain_wal_path) != 0)
   {
      goto error;
   }

   if (compress)
   {
      pgmoneta_snprintf(zstd_path, sizeof(zstd_path), "%s.zstd", plain_wal_path);
      memset(compress_cmd, 0, sizeof(compress_cmd));
      pgmoneta_snprintf(compress_cmd, sizeof(compress_cmd), "zstd -f -q \"%s\" -o \"%s\"", plain_wal_path,
                        zstd_path);
      if (pgmoneta_test_exec_command(compress_cmd, &zstd_out, &zstd_exit) != 0 || zstd_exit != 0)
      {
         goto error;
      }
      free(zstd_out);
      zstd_out = NULL;
      if (unlink(plain_wal_path) != 0)
      {
         goto error;
      }
      if (pgmoneta_encrypt_file(zstd_path, aes_wal_path) != 0)
      {
         goto error;
      }
   }
   else
   {
      if (pgmoneta_encrypt_file(plain_wal_path, aes_wal_path) != 0)
      {
         goto error;
      }
   }

   return 0;

error:
   free(zstd_out);
   zstd_out = NULL;
   if (f != NULL)
   {
      fclose(f);
      f = NULL;
   }
   if (master_key_path != NULL)
   {
      unlink(master_key_path);
   }
   if (plain_wal_path != NULL && pgmoneta_exists(plain_wal_path))
   {
      unlink(plain_wal_path);
   }
   if (zstd_path[0] != '\0' && pgmoneta_exists(zstd_path))
   {
      unlink(zstd_path);
   }
   if (aes_wal_path != NULL && pgmoneta_exists(aes_wal_path))
   {
      unlink(aes_wal_path);
   }
   return 1;
}

/**
 * @return 0 on success, 1 on failure
 */
static int
create_directories_for_encrypted_wal(char* base_dir, char* hidden_dir, char* master_key_path,
                                     char* wal_dir, char* plain_wal, char* aes_wal)
{
   if (base_dir == NULL || hidden_dir == NULL || master_key_path == NULL || wal_dir == NULL ||
       plain_wal == NULL || aes_wal == NULL)
   {
      return 1;
   }

   pgmoneta_snprintf(base_dir, MAX_PATH, "%s%s", TEST_BASE_DIR, WALINFO_AES_TEST_SUBDIR);
   pgmoneta_snprintf(hidden_dir, MAX_PATH, "%s/.pgmoneta", base_dir);
   pgmoneta_snprintf(master_key_path, MAX_PATH, "%s/master.key", hidden_dir);
   pgmoneta_snprintf(wal_dir, MAX_PATH, "%s%s", base_dir, WAL_TEST_SUBDIR);
   pgmoneta_snprintf(plain_wal, MAX_PATH, "%s%s", wal_dir, RANDOM_WALFILE_NAME);
   pgmoneta_snprintf(aes_wal, MAX_PATH, "%s.aes", plain_wal);

   if (pgmoneta_exists(base_dir))
   {
      pgmoneta_delete_directory(base_dir);
   }

   if (pgmoneta_mkdir(base_dir) != 0)
   {
      return 1;
   }
   if (pgmoneta_mkdir(hidden_dir) != 0)
   {
      return 1;
   }
   if (chmod(hidden_dir, 0700) != 0)
   {
      return 1;
   }
   if (pgmoneta_mkdir(wal_dir) != 0)
   {
      return 1;
   }

   return 0;
}

static int
create_mock_wal_file(char* path, struct walfile** wf_out)
{
   struct walfile* wf = NULL;

   wf = pgmoneta_test_generate_check_point_shutdown_v17();
   if (wf == NULL)
   {
      return 1;
   }

   if (pgmoneta_write_walfile(wf, -1, path) != 0)
   {
      pgmoneta_destroy_walfile(wf);
      return 1;
   }

   *wf_out = wf;
   return 0;
}

static int
prepare_wal_test_path(char* path, size_t size)
{
   char wal_dir[MAX_PATH];

   if (path == NULL || size == 0)
   {
      return 1;
   }

   memset(wal_dir, 0, sizeof(wal_dir));
   pgmoneta_snprintf(wal_dir, sizeof(wal_dir), "%s%s", TEST_BASE_DIR, WAL_TEST_SUBDIR);

   if (access(wal_dir, F_OK) != 0)
   {
      if (mkdir(wal_dir, 0700) != 0)
      {
         return 1;
      }
   }

   memset(path, 0, size);
   pgmoneta_snprintf(path, size, "%s%s", wal_dir, RANDOM_WALFILE_NAME);
   return 0;
}

#define PREPARE_AND_CREATE_WAL(path, wf)                                                                              \
   do                                                                                                                 \
   {                                                                                                                  \
      MCTF_ASSERT_INT_EQ(prepare_wal_test_path((path), sizeof(path)), 0, cleanup, "Failed to prepare WAL test path"); \
      MCTF_ASSERT_INT_EQ(create_mock_wal_file((path), &(wf)), 0, cleanup, "Failed to create mock WAL file");          \
   }                                                                                                                  \
   while (0)

MCTF_TEST(test_walinfo_describe)
{
   char path[MAX_PATH];
   char* output = NULL;
   struct walfile* wf = NULL;

   pgmoneta_test_setup();

   PREPARE_AND_CREATE_WAL(path, wf);

   // Describe the WAL file using direct function calls
   MCTF_ASSERT_INT_EQ(pgmoneta_tswalinfo_describe(path, &output), 0, cleanup, "Failed to describe WAL");
   MCTF_ASSERT_PTR_NONNULL(output, cleanup, "Output is NULL");

   // Verify the output containing expected record type
   MCTF_ASSERT(strstr(output, "XLOG") != NULL, cleanup, "Missing XLOG in output");
   MCTF_ASSERT(strstr(output, "CHECKPOINT_SHUTDOWN") != NULL, cleanup, "Missing CHECKPOINT_SHUTDOWN in output");

cleanup:
   free(output);
   if (pgmoneta_exists(path))
   {
      unlink(path);
   }
   if (wf != NULL)
   {
      pgmoneta_destroy_walfile(wf);
   }

   pgmoneta_test_teardown();

   MCTF_FINISH();
}

MCTF_TEST(test_walinfo_describe_null_path)
{
   char* output = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT_INT_EQ(pgmoneta_tswalinfo_describe(NULL, &output), 1, cleanup, "NULL path should fail");
   MCTF_ASSERT_PTR_NULL(output, cleanup, "Output should remain NULL on NULL path");

cleanup:
   free(output);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_walinfo_describe_null_output)
{
   char path[MAX_PATH];

   pgmoneta_test_setup();

   MCTF_ASSERT_INT_EQ(prepare_wal_test_path(path, sizeof(path)), 0, cleanup, "Failed to prepare WAL test path");

   MCTF_ASSERT_INT_EQ(pgmoneta_tswalinfo_describe(path, NULL), 1, cleanup, "NULL output pointer should fail");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_walinfo_describe_nonexistent_path)
{
   char path[MAX_PATH];
   char* output = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT_INT_EQ(prepare_wal_test_path(path, sizeof(path)), 0, cleanup, "Failed to prepare WAL test path");

   if (pgmoneta_exists(path))
   {
      unlink(path);
   }

   MCTF_ASSERT_INT_EQ(pgmoneta_tswalinfo_describe(path, &output), 1, cleanup, "Nonexistent WAL path should fail");
   MCTF_ASSERT_PTR_NULL(output, cleanup, "Output should remain NULL when describe fails");

cleanup:
   free(output);
   if (pgmoneta_exists(path))
   {
      unlink(path);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_walinfo_cli_filter_rmgr_include_xlog)
{
   char path[MAX_PATH];
   char* output = NULL;
   int exit_code = 1;
   struct walfile* wf = NULL;

   pgmoneta_test_setup();

   PREPARE_AND_CREATE_WAL(path, wf);
   MCTF_ASSERT_INT_EQ(pgmoneta_walinfo_cli(path, "-r XLOG", &output, &exit_code), 0,
                      cleanup, "Failed to execute walinfo CLI");
   MCTF_ASSERT_INT_EQ(exit_code, 0, cleanup, "walinfo CLI returned non-zero exit code, output: %s", output ? output : "<null>");
   MCTF_ASSERT(strstr(output, "CHECKPOINT_SHUTDOWN") != NULL, cleanup, "Expected record missing with --rmgr XLOG");

cleanup:
   free(output);
   if (pgmoneta_exists(path))
   {
      unlink(path);
   }
   if (wf != NULL)
   {
      pgmoneta_destroy_walfile(wf);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_walinfo_cli_filter_rmgr_exclude_xlog)
{
   char path[MAX_PATH];
   char* output = NULL;
   int exit_code = 1;
   struct walfile* wf = NULL;

   pgmoneta_test_setup();

   PREPARE_AND_CREATE_WAL(path, wf);
   MCTF_ASSERT_INT_EQ(pgmoneta_walinfo_cli(path, "-r Transaction", &output, &exit_code), 0,
                      cleanup, "Failed to execute walinfo CLI");
   MCTF_ASSERT_INT_EQ(exit_code, 0, cleanup, "walinfo CLI returned non-zero exit code, output: %s", output ? output : "<null>");
   MCTF_ASSERT(strstr(output, "CHECKPOINT_SHUTDOWN") == NULL, cleanup, "Record should be excluded with --rmgr Transaction");

cleanup:
   free(output);
   if (pgmoneta_exists(path))
   {
      unlink(path);
   }
   if (wf != NULL)
   {
      pgmoneta_destroy_walfile(wf);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_walinfo_cli_filter_xid)
{
   char path[MAX_PATH];
   char* output = NULL;
   int exit_code = 1;
   struct walfile* wf = NULL;

   pgmoneta_test_setup();

   PREPARE_AND_CREATE_WAL(path, wf);

   MCTF_ASSERT_INT_EQ(pgmoneta_walinfo_cli(path, "-x 0", &output, &exit_code), 0,
                      cleanup, "Failed to execute walinfo CLI with --xid 0");
   MCTF_ASSERT_INT_EQ(exit_code, 0, cleanup, "walinfo CLI with --xid 0 returned non-zero exit code, output: %s", output ? output : "<null>");
   MCTF_ASSERT(strstr(output, "CHECKPOINT_SHUTDOWN") != NULL, cleanup, "xid 0 should include record");

   free(output);
   output = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_walinfo_cli(path, "-x 42", &output, &exit_code), 0,
                      cleanup, "Failed to execute walinfo CLI with --xid 42");
   MCTF_ASSERT_INT_EQ(exit_code, 0, cleanup, "walinfo CLI with --xid 42 returned non-zero exit code, output: %s", output ? output : "<null>");
   MCTF_ASSERT(strstr(output, "CHECKPOINT_SHUTDOWN") == NULL, cleanup, "xid 42 should exclude record");

cleanup:
   free(output);
   if (pgmoneta_exists(path))
   {
      unlink(path);
   }
   if (wf != NULL)
   {
      pgmoneta_destroy_walfile(wf);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_walinfo_cli_usage_without_wal_argument)
{
   char* output = NULL;
   int exit_code = 0;

   pgmoneta_test_setup();

   MCTF_ASSERT_INT_EQ(pgmoneta_walinfo_cli(NULL, NULL, &output, &exit_code), 0,
                      cleanup, "Failed to execute walinfo CLI without arguments");
   MCTF_ASSERT(exit_code != 0, cleanup, "walinfo without WAL argument should exit non-zero");
   MCTF_ASSERT(strstr(output, "Usage:") != NULL, cleanup, "Expected Usage output is missing");
   MCTF_ASSERT(strstr(output, "pgmoneta-walinfo") != NULL, cleanup, "Expected tool name is missing in help output");

cleanup:
   free(output);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_walinfo_cli_version_flag)
{
   char* output = NULL;
   int exit_code = 0;

   pgmoneta_test_setup();

   MCTF_ASSERT_INT_EQ(pgmoneta_walinfo_cli(NULL, "-V", &output, &exit_code), 0,
                      cleanup, "Failed to execute walinfo CLI with -V");
   MCTF_ASSERT(exit_code != 0, cleanup, "walinfo -V should exit non-zero");
   MCTF_ASSERT(strstr(output, "pgmoneta-walinfo") != NULL, cleanup, "Expected tool name in version output");
   MCTF_ASSERT(strstr(output, VERSION) != NULL, cleanup, "Expected version string in output");

cleanup:
   free(output);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_walinfo_cli_compressed_wal_file)
{
   char path[MAX_PATH];
   char compressed_path[MAX_PATH];
   char* output = NULL;
   char* compress_output = NULL;
   int exit_code = 1;
   int compress_exit = 1;
   struct walfile* wf = NULL;

   pgmoneta_test_setup();

   PREPARE_AND_CREATE_WAL(path, wf);

   memset(compressed_path, 0, sizeof(compressed_path));
   pgmoneta_snprintf(compressed_path, sizeof(compressed_path), "%s.zstd", path);

   {
      char compress_cmd[MAX_PATH * 2];
      memset(compress_cmd, 0, sizeof(compress_cmd));
      pgmoneta_snprintf(compress_cmd, sizeof(compress_cmd), "zstd -f -q \"%s\" -o \"%s\"", path, compressed_path);
      MCTF_ASSERT_INT_EQ(pgmoneta_test_exec_command(compress_cmd, &compress_output, &compress_exit), 0,
                         cleanup, "Failed to run zstd compression command");
      MCTF_ASSERT_INT_EQ(compress_exit, 0, cleanup, "zstd compression failed, output: %s",
                         compress_output ? compress_output : "<null>");
   }

   MCTF_ASSERT_INT_EQ(pgmoneta_walinfo_cli(compressed_path, NULL, &output, &exit_code), 0,
                      cleanup, "Failed to execute walinfo CLI on compressed file");
   MCTF_ASSERT_INT_EQ(exit_code, 0, cleanup, "walinfo CLI on compressed file returned non-zero exit code, output: %s",
                      output ? output : "<null>");
   MCTF_ASSERT(strstr(output, "XLOG") != NULL, cleanup, "Missing XLOG in compressed WAL output");
   MCTF_ASSERT(strstr(output, "CHECKPOINT_SHUTDOWN") != NULL, cleanup, "Missing CHECKPOINT_SHUTDOWN in compressed WAL output");

cleanup:
   free(output);
   free(compress_output);
   if (pgmoneta_exists(compressed_path))
   {
      unlink(compressed_path);
   }
   if (pgmoneta_exists(path))
   {
      unlink(path);
   }
   if (wf != NULL)
   {
      pgmoneta_destroy_walfile(wf);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_walinfo_cli_tar_archive)
{
   char path[MAX_PATH];
   char tar_dir[MAX_PATH];
   char tar_path[MAX_PATH];
   char* output = NULL;
   char* tar_output = NULL;
   int exit_code = 1;
   int tar_exit = 1;
   struct walfile* wf = NULL;

   pgmoneta_test_setup();

   PREPARE_AND_CREATE_WAL(path, wf);

   memset(tar_dir, 0, sizeof(tar_dir));
   pgmoneta_snprintf(tar_dir, sizeof(tar_dir), "%s%s/tar_test", TEST_BASE_DIR, WAL_TEST_SUBDIR);

   memset(tar_path, 0, sizeof(tar_path));
   pgmoneta_snprintf(tar_path, sizeof(tar_path), "%s%s/wal_archive.tar", TEST_BASE_DIR, WAL_TEST_SUBDIR);

   {
      char cmd[MAX_PATH * 3];

      memset(cmd, 0, sizeof(cmd));
      pgmoneta_snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", tar_dir);
      MCTF_ASSERT_INT_EQ(pgmoneta_test_exec_command(cmd, &tar_output, &tar_exit), 0,
                         cleanup, "Failed to create tar staging directory");
      MCTF_ASSERT_INT_EQ(tar_exit, 0, cleanup, "mkdir failed, output: %s", tar_output ? tar_output : "<null>");
      free(tar_output);
      tar_output = NULL;

      memset(cmd, 0, sizeof(cmd));
      pgmoneta_snprintf(cmd, sizeof(cmd), "cp \"%s\" \"%s/\"", path, tar_dir);
      MCTF_ASSERT_INT_EQ(pgmoneta_test_exec_command(cmd, &tar_output, &tar_exit), 0,
                         cleanup, "Failed to copy WAL file into tar staging directory");
      MCTF_ASSERT_INT_EQ(tar_exit, 0, cleanup, "cp failed, output: %s", tar_output ? tar_output : "<null>");
      free(tar_output);
      tar_output = NULL;

      memset(cmd, 0, sizeof(cmd));
      pgmoneta_snprintf(cmd, sizeof(cmd), "tar cf \"%s\" -C \"%s\" .", tar_path, tar_dir);
      MCTF_ASSERT_INT_EQ(pgmoneta_test_exec_command(cmd, &tar_output, &tar_exit), 0,
                         cleanup, "Failed to create tar archive");
      MCTF_ASSERT_INT_EQ(tar_exit, 0, cleanup, "tar failed, output: %s", tar_output ? tar_output : "<null>");
   }

   MCTF_ASSERT_INT_EQ(pgmoneta_walinfo_cli(tar_path, NULL, &output, &exit_code), 0,
                      cleanup, "Failed to execute walinfo CLI on tar archive");
   MCTF_ASSERT_INT_EQ(exit_code, 0, cleanup, "walinfo CLI on tar archive returned non-zero exit code, output: %s",
                      output ? output : "<null>");
   MCTF_ASSERT(strstr(output, "XLOG") != NULL, cleanup, "Missing XLOG in tar archive WAL output");
   MCTF_ASSERT(strstr(output, "CHECKPOINT_SHUTDOWN") != NULL, cleanup, "Missing CHECKPOINT_SHUTDOWN in tar archive WAL output");

cleanup:
   free(output);
   free(tar_output);
   if (pgmoneta_exists(tar_path))
   {
      unlink(tar_path);
   }
   if (pgmoneta_exists(tar_dir))
   {
      pgmoneta_delete_directory(tar_dir);
   }
   if (pgmoneta_exists(path))
   {
      unlink(path);
   }
   if (wf != NULL)
   {
      pgmoneta_destroy_walfile(wf);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_walinfo_cli_directory)
{
   char path[MAX_PATH];
   char wal_dir[MAX_PATH];
   char* output = NULL;
   int exit_code = 1;
   struct walfile* wf = NULL;

   pgmoneta_test_setup();

   memset(wal_dir, 0, sizeof(wal_dir));
   pgmoneta_snprintf(wal_dir, sizeof(wal_dir), "%s%s", TEST_BASE_DIR, WAL_TEST_SUBDIR);

   PREPARE_AND_CREATE_WAL(path, wf);

   MCTF_ASSERT_INT_EQ(pgmoneta_walinfo_cli(wal_dir, NULL, &output, &exit_code), 0,
                      cleanup, "Failed to execute walinfo CLI on directory");
   MCTF_ASSERT_INT_EQ(exit_code, 0, cleanup, "walinfo CLI on directory returned non-zero exit code, output: %s",
                      output ? output : "<null>");
   MCTF_ASSERT(strstr(output, "XLOG") != NULL, cleanup, "Missing XLOG in directory WAL output");
   MCTF_ASSERT(strstr(output, "CHECKPOINT_SHUTDOWN") != NULL, cleanup, "Missing CHECKPOINT_SHUTDOWN in directory WAL output");

cleanup:
   free(output);
   if (pgmoneta_exists(path))
   {
      unlink(path);
   }
   if (wf != NULL)
   {
      pgmoneta_destroy_walfile(wf);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_walinfo_cli_summary_flag)
{
   char path[MAX_PATH];
   char* output = NULL;
   int exit_code = 1;
   struct walfile* wf = NULL;

   pgmoneta_test_setup();

   PREPARE_AND_CREATE_WAL(path, wf);

   MCTF_ASSERT_INT_EQ(pgmoneta_walinfo_cli(path, "-S", &output, &exit_code), 0,
                      cleanup, "Failed to execute walinfo CLI with -S");
   MCTF_ASSERT_INT_EQ(exit_code, 0, cleanup, "walinfo CLI with -S returned non-zero exit code, output: %s",
                      output ? output : "<null>");

   MCTF_ASSERT(strstr(output, "XLOG") != NULL, cleanup, "Expected XLOG resource manager in summary output");
   MCTF_ASSERT(strstr(output, "100.00%") != NULL, cleanup, "Expected 100.00%% in summary output");
   MCTF_ASSERT(strstr(output, "Total") != NULL, cleanup, "Expected Total row in summary output");

cleanup:
   free(output);
   if (pgmoneta_exists(path))
   {
      unlink(path);
   }
   if (wf != NULL)
   {
      pgmoneta_destroy_walfile(wf);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_walinfo_cli_summary_flag_with_json_output)
{
   char path[MAX_PATH];
   char* output = NULL;
   int exit_code = 1;
   struct walfile* wf = NULL;
   struct json* root = NULL;
   struct json* wal_stats = NULL;
   struct json_iterator* iter = NULL;
   struct json* first_row = NULL;
   int count = 0;

   pgmoneta_test_setup();

   PREPARE_AND_CREATE_WAL(path, wf);

   MCTF_ASSERT_INT_EQ(pgmoneta_walinfo_cli(path, "-S -F json", &output, &exit_code), 0,
                      cleanup, "Failed to execute walinfo CLI with -S -F json");
   MCTF_ASSERT_INT_EQ(exit_code, 0, cleanup, "walinfo CLI with -S -F json returned non-zero exit code, output: %s",
                      output ? output : "<null>");

   MCTF_ASSERT_INT_EQ(pgmoneta_json_parse_string(output, &root), 0, cleanup, "Failed to parse summary JSON output");
   MCTF_ASSERT_PTR_NONNULL(root, cleanup, "Parsed summary JSON is NULL");

   wal_stats = (struct json*)pgmoneta_json_get(root, "wal_stats");
   MCTF_ASSERT_PTR_NONNULL(wal_stats, cleanup, "Missing wal_stats array in summary JSON");

   MCTF_ASSERT_INT_EQ(pgmoneta_json_iterator_create(wal_stats, &iter), 0, cleanup, "Failed to create JSON iterator");
   MCTF_ASSERT(pgmoneta_json_iterator_next(iter), cleanup, "wal_stats array is empty");
   MCTF_ASSERT_PTR_NONNULL(iter->value, cleanup, "wal_stats first entry value is NULL");
   first_row = (struct json*)iter->value->data;
   MCTF_ASSERT_PTR_NONNULL(first_row, cleanup, "wal_stats first entry JSON object is NULL");

   count = (int)pgmoneta_json_get(first_row, "count");
   MCTF_ASSERT_INT_EQ(count, 1, cleanup, "Expected summary count to be 1");

cleanup:
   pgmoneta_json_iterator_destroy(iter);
   pgmoneta_json_destroy(root);
   free(output);
   if (pgmoneta_exists(path))
   {
      unlink(path);
   }
   if (wf != NULL)
   {
      pgmoneta_destroy_walfile(wf);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_walinfo_encrypted_wal)
{
   struct walfile* wf = NULL;
   struct main_configuration* config = NULL;
   char saved_home[MAX_PATH];
   int saved_encryption = ENCRYPTION_NONE;
   char base_dir[MAX_PATH];
   char hidden_dir[MAX_PATH];
   char master_key_path[MAX_PATH];
   char wal_dir[MAX_PATH];
   char plain_wal[MAX_PATH];
   char aes_wal[MAX_PATH];
   char walinfo_bin[MAX_PATH];
   char cmd[MAX_PATH * 4];
   char* output = NULL;
   int exit_code = 1;

   pgmoneta_test_setup();

   memset(saved_home, 0, sizeof(saved_home));
   memset(base_dir, 0, sizeof(base_dir));
   memset(hidden_dir, 0, sizeof(hidden_dir));
   memset(master_key_path, 0, sizeof(master_key_path));
   memset(wal_dir, 0, sizeof(wal_dir));
   memset(plain_wal, 0, sizeof(plain_wal));
   memset(aes_wal, 0, sizeof(aes_wal));

   config = (struct main_configuration*)shmem;
   MCTF_ASSERT_PTR_NONNULL(config, cleanup, "configuration is null");

   memcpy(saved_home, config->common.home_dir, sizeof(saved_home));
   saved_encryption = config->encryption;

   MCTF_ASSERT_INT_EQ(pgmoneta_test_resolve_binary_path("pgmoneta-walinfo", walinfo_bin), 0, cleanup,
                      "pgmoneta-walinfo binary not found or not executable");

   MCTF_ASSERT_INT_EQ(create_directories_for_encrypted_wal(base_dir, hidden_dir, master_key_path, wal_dir, plain_wal,
                                                           aes_wal),
                      0, cleanup, "creating required directories for encrypted WAL test failed");

   memset(config->common.home_dir, 0, sizeof(config->common.home_dir));
   pgmoneta_snprintf(config->common.home_dir, sizeof(config->common.home_dir), "%s", base_dir);
   config->encryption = ENCRYPTION_AES_256_GCM;

   wf = pgmoneta_test_generate_mixed_heap_wal_v17();
   MCTF_ASSERT_PTR_NONNULL(wf, cleanup, "failed to generate walfile");

   MCTF_ASSERT_INT_EQ(create_master_key_and_encrypted_wal(master_key_path, wf, plain_wal, aes_wal, 0), 0, cleanup,
                      "master key + plain WAL + encrypted WAL creation failed");

   MCTF_ASSERT(pgmoneta_exists(aes_wal), cleanup, "encrypted WAL missing");
   MCTF_ASSERT(!pgmoneta_exists(plain_wal), cleanup, "plain WAL should be removed after encrypt");

   pgmoneta_snprintf(cmd, sizeof(cmd), "env HOME=\"%s\" \"%s\" \"%s\"", base_dir, walinfo_bin, aes_wal);

   MCTF_ASSERT_INT_EQ(pgmoneta_test_exec_command(cmd, &output, &exit_code), 0, cleanup,
                      "failed to run pgmoneta-walinfo on .aes file");
   MCTF_ASSERT_INT_EQ(exit_code, 0, cleanup, "walinfo on .aes failed, output: %s",
                      output != NULL ? output : "<null>");
   MCTF_ASSERT(output != NULL && strstr(output, "CHECKPOINT_SHUTDOWN") != NULL, cleanup,
               "expected CHECKPOINT_SHUTDOWN in walinfo output, got: %s", output != NULL ? output : "<null>");

   free(output);
   output = NULL;

   if (pgmoneta_exists(aes_wal))
   {
      unlink(aes_wal);
   }
   /* Test compressed + encrypted WAL */
   pgmoneta_snprintf(aes_wal, sizeof(aes_wal), "%s.zstd.aes", plain_wal);

   MCTF_ASSERT_INT_EQ(create_master_key_and_encrypted_wal(master_key_path, wf, plain_wal, aes_wal, 1), 0, cleanup,
                      "master key + zstd + encrypted WAL creation failed");

   MCTF_ASSERT(pgmoneta_exists(aes_wal), cleanup, "encrypted compressed WAL missing");
   MCTF_ASSERT(!pgmoneta_exists(plain_wal), cleanup, "plain WAL should be removed after zstd");

   pgmoneta_snprintf(cmd, sizeof(cmd), "env HOME=\"%s\" \"%s\" \"%s\"", base_dir, walinfo_bin, aes_wal);

   MCTF_ASSERT_INT_EQ(pgmoneta_test_exec_command(cmd, &output, &exit_code), 0, cleanup,
                      "failed to run pgmoneta-walinfo on .zstd.aes file");
   MCTF_ASSERT_INT_EQ(exit_code, 0, cleanup, "walinfo on .zstd.aes failed, output: %s",
                      output != NULL ? output : "<null>");
   MCTF_ASSERT(output != NULL && strstr(output, "CHECKPOINT_SHUTDOWN") != NULL, cleanup,
               "expected CHECKPOINT_SHUTDOWN in .zstd.aes walinfo output, got: %s",
               output != NULL ? output : "<null>");

cleanup:
   free(output);

   memcpy(config->common.home_dir, saved_home, sizeof(config->common.home_dir));
   config->encryption = saved_encryption;

   if (base_dir[0] != '\0' && pgmoneta_exists(base_dir))
   {
      pgmoneta_delete_directory(base_dir);
   }

   if (wf != NULL)
   {
      pgmoneta_destroy_walfile(wf);
   }

   pgmoneta_test_teardown();
   MCTF_FINISH();
}
