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
#include <mctf.h>
#include <tscommon.h>
#include <tswalfilter.h>
#include <tswalinfo.h>
#include <tswalutils.h>
#include <walfile.h>
#include <utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define WALFILTER_TEST_SUBDIR "/walfilter"

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
ensure_directory(const char* dir)
{
   if (access(dir, F_OK) != 0)
   {
      if (mkdir(dir, 0700) != 0)
      {
         return 1;
      }
   }
   return 0;
}

static int
write_yaml_config(const char* yaml_path, const char* source_dir, const char* target_dir,
                  const char** operations, int op_count, const int* xids, int xid_count)
{
   FILE* f = NULL;

   if (yaml_path == NULL || source_dir == NULL || target_dir == NULL)
   {
      return 1;
   }

   f = fopen(yaml_path, "w");
   if (f == NULL)
   {
      return 1;
   }

   fprintf(f, "source_dir: %s\n", source_dir);
   fprintf(f, "target_dir: %s\n", target_dir);

   if ((op_count > 0 && operations != NULL) || (xid_count > 0 && xids != NULL))
   {
      fprintf(f, "rules:\n");

      if (op_count > 0 && operations != NULL)
      {
         fprintf(f, "  - operations:\n");
         for (int i = 0; i < op_count; i++)
         {
            fprintf(f, "    - %s\n", operations[i]);
         }
      }

      if (xid_count > 0 && xids != NULL)
      {
         fprintf(f, "  - xids:\n");
         for (int i = 0; i < xid_count; i++)
         {
            fprintf(f, "    - %d\n", xids[i]);
         }
      }
   }

   fclose(f);
   return 0;
}

MCTF_TEST(test_walfilter_cli_usage)
{
   char* output = NULL;
   int exit_code = 1;

   pgmoneta_test_setup();

   MCTF_ASSERT_INT_EQ(pgmoneta_walfilter_cli(NULL, NULL, &output, &exit_code), 0,
                      cleanup, "Failed to execute walfilter CLI");
   MCTF_ASSERT_INT_EQ(exit_code, 0, cleanup, "walfilter usage should return 0, output: %s",
                      output ? output : "<null>");
   MCTF_ASSERT(strstr(output, "pgmoneta-walfilter") != NULL, cleanup,
               "Expected 'pgmoneta-walfilter' in usage output");
   MCTF_ASSERT(strstr(output, "yaml_config_file") != NULL, cleanup,
               "Expected 'yaml_config_file' in usage output");
   MCTF_ASSERT(strstr(output, "Usage") != NULL, cleanup,
               "Expected 'Usage' in usage output");

cleanup:
   free(output);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_walfilter_cli_passthrough)
{
   char base_dir[MAX_PATH];
   char source_dir[MAX_PATH];
   char target_dir[MAX_PATH];
   char wal_path[MAX_PATH];
   char yaml_path[MAX_PATH];
   char* output = NULL;
   char* walinfo_output = NULL;
   int exit_code = 1;
   int walinfo_exit = 1;
   struct walfile* wf = NULL;

   pgmoneta_test_setup();

   memset(base_dir, 0, sizeof(base_dir));
   memset(source_dir, 0, sizeof(source_dir));
   memset(target_dir, 0, sizeof(target_dir));
   memset(wal_path, 0, sizeof(wal_path));
   memset(yaml_path, 0, sizeof(yaml_path));

   pgmoneta_snprintf(base_dir, sizeof(base_dir), "%s%s", TEST_BASE_DIR, WALFILTER_TEST_SUBDIR);
   pgmoneta_snprintf(source_dir, sizeof(source_dir), "%s/source", base_dir);
   pgmoneta_snprintf(target_dir, sizeof(target_dir), "%s/target", base_dir);
   pgmoneta_snprintf(wal_path, sizeof(wal_path), "%s%s", source_dir, RANDOM_WALFILE_NAME);
   pgmoneta_snprintf(yaml_path, sizeof(yaml_path), "%s/passthrough.yaml", base_dir);

   MCTF_ASSERT_INT_EQ(ensure_directory(base_dir), 0, cleanup, "Failed to create base directory");
   MCTF_ASSERT_INT_EQ(ensure_directory(source_dir), 0, cleanup, "Failed to create source directory");

   MCTF_ASSERT_INT_EQ(create_mock_wal_file(wal_path, &wf), 0, cleanup, "Failed to create mock WAL file");
   MCTF_ASSERT_INT_EQ(write_yaml_config(yaml_path, source_dir, target_dir, NULL, 0, NULL, 0), 0,
                      cleanup, "Failed to write YAML config");

   MCTF_ASSERT_INT_EQ(pgmoneta_walfilter_cli(yaml_path, NULL, &output, &exit_code), 0,
                      cleanup, "Failed to execute walfilter CLI");
   MCTF_ASSERT_INT_EQ(exit_code, 0, cleanup,
                      "walfilter passthrough should return 0, output: %s", output ? output : "<null>");

   MCTF_ASSERT(pgmoneta_exists(target_dir), cleanup, "Target directory was not created");

   MCTF_ASSERT_INT_EQ(pgmoneta_walinfo_cli(target_dir, NULL, &walinfo_output, &walinfo_exit), 0,
                      cleanup, "Failed to run walinfo on target directory");
   MCTF_ASSERT_INT_EQ(walinfo_exit, 0, cleanup,
                      "walinfo on target returned non-zero, output: %s", walinfo_output ? walinfo_output : "<null>");
   MCTF_ASSERT(strstr(walinfo_output, "XLOG") != NULL, cleanup,
               "Missing XLOG in filtered output");
   MCTF_ASSERT(strstr(walinfo_output, "CHECKPOINT_SHUTDOWN") != NULL, cleanup,
               "Missing CHECKPOINT_SHUTDOWN in filtered output");

cleanup:
   free(walinfo_output);
   free(output);
   if (pgmoneta_exists(target_dir))
   {
      pgmoneta_delete_directory(target_dir);
   }
   if (pgmoneta_exists(yaml_path))
   {
      unlink(yaml_path);
   }
   if (wf != NULL)
   {
      pgmoneta_destroy_walfile(wf);
   }
   if (pgmoneta_exists(wal_path))
   {
      unlink(wal_path);
   }
   if (pgmoneta_exists(source_dir))
   {
      pgmoneta_delete_directory(source_dir);
   }
   if (pgmoneta_exists(base_dir))
   {
      pgmoneta_delete_directory(base_dir);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_walfilter_cli_operation_delete)
{
   char base_dir[MAX_PATH];
   char source_dir[MAX_PATH];
   char target_dir[MAX_PATH];
   char wal_path[MAX_PATH];
   char yaml_path[MAX_PATH];
   char* output = NULL;
   char* walinfo_output = NULL;
   int exit_code = 1;
   int walinfo_exit = 1;
   struct walfile* wf = NULL;
   const char* operations[] = {"DELETE"};

   pgmoneta_test_setup();

   memset(base_dir, 0, sizeof(base_dir));
   memset(source_dir, 0, sizeof(source_dir));
   memset(target_dir, 0, sizeof(target_dir));
   memset(wal_path, 0, sizeof(wal_path));
   memset(yaml_path, 0, sizeof(yaml_path));

   pgmoneta_snprintf(base_dir, sizeof(base_dir), "%s%s", TEST_BASE_DIR, WALFILTER_TEST_SUBDIR);
   pgmoneta_snprintf(source_dir, sizeof(source_dir), "%s/source_delete", base_dir);
   pgmoneta_snprintf(target_dir, sizeof(target_dir), "%s/target_delete", base_dir);
   pgmoneta_snprintf(wal_path, sizeof(wal_path), "%s%s", source_dir, RANDOM_WALFILE_NAME);
   pgmoneta_snprintf(yaml_path, sizeof(yaml_path), "%s/delete_filter.yaml", base_dir);

   MCTF_ASSERT_INT_EQ(ensure_directory(base_dir), 0, cleanup, "Failed to create base directory");
   MCTF_ASSERT_INT_EQ(ensure_directory(source_dir), 0, cleanup, "Failed to create source directory");

   wf = pgmoneta_test_generate_mixed_heap_wal_v17();
   MCTF_ASSERT_PTR_NONNULL(wf, cleanup, "Failed to generate mixed heap WAL file");
   MCTF_ASSERT_INT_EQ(pgmoneta_write_walfile(wf, -1, wal_path), 0, cleanup, "Failed to write mixed heap WAL file");

   MCTF_ASSERT_INT_EQ(write_yaml_config(yaml_path, source_dir, target_dir, operations, 1, NULL, 0), 0,
                      cleanup, "Failed to write YAML config with DELETE operation");

   MCTF_ASSERT_INT_EQ(pgmoneta_walfilter_cli(yaml_path, NULL, &output, &exit_code), 0,
                      cleanup, "Failed to execute walfilter CLI with DELETE filter");
   MCTF_ASSERT_INT_EQ(exit_code, 0, cleanup,
                      "walfilter DELETE filter should return 0, output: %s", output ? output : "<null>");

   MCTF_ASSERT(pgmoneta_exists(target_dir), cleanup, "Target directory was not created");

   MCTF_ASSERT_INT_EQ(pgmoneta_walinfo_cli(target_dir, NULL, &walinfo_output, &walinfo_exit), 0,
                      cleanup, "Failed to run walinfo on DELETE-filtered target");
   MCTF_ASSERT_INT_EQ(walinfo_exit, 0, cleanup,
                      "walinfo on DELETE-filtered target returned non-zero, output: %s",
                      walinfo_output ? walinfo_output : "<null>");
   MCTF_ASSERT(strstr(walinfo_output, "CHECKPOINT_SHUTDOWN") != NULL, cleanup,
               "CHECKPOINT_SHUTDOWN should remain after DELETE filter");
   MCTF_ASSERT(strstr(walinfo_output, "INSERT") != NULL, cleanup,
               "HEAP INSERT should remain after DELETE filter");
   MCTF_ASSERT(strstr(walinfo_output, "DELETE") == NULL, cleanup,
               "HEAP DELETE should have been filtered out (converted to NOOP)");

cleanup:
   free(walinfo_output);
   free(output);
   if (pgmoneta_exists(target_dir))
   {
      pgmoneta_delete_directory(target_dir);
   }
   if (pgmoneta_exists(yaml_path))
   {
      unlink(yaml_path);
   }
   if (pgmoneta_exists(wal_path))
   {
      unlink(wal_path);
   }
   if (wf != NULL)
   {
      pgmoneta_destroy_walfile(wf);
   }
   if (pgmoneta_exists(source_dir))
   {
      pgmoneta_delete_directory(source_dir);
   }
   if (pgmoneta_exists(base_dir))
   {
      pgmoneta_delete_directory(base_dir);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}
