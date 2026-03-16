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
         if (pgmoneta_compression_is_compressed(entry->d_name))
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
