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

/*
 * Storage-engine integration tests: S3 via Garage with the local storage
 * engine enabled too (storage_engine = local, s3), so archive and incremental
 * backup/restore run on the local data that is kept after the upload.
 */

#include <mctf.h>
#include <mctf_container.h>
#include <mctf_se.h>
#include <tscommon.h>
#include <utils.h>

#include <stdlib.h>
#include <string.h>

static int storage_status = MCTF_FAIL;

/* Take an incremental backup on top of the newest backup. */
static int
incremental_backup(char* parent, char* child, size_t size, char** output)
{
   char args[MAX_PATH];

   if (mctf_se_newest_label("primary", parent, size) != MCTF_OK)
   {
      return MCTF_FAIL;
   }

   pgmoneta_snprintf(args, sizeof(args), "backup primary %s", parent);

   if (mctf_se_cli(args, output) != 0 ||
       mctf_se_newest_label("primary", child, size) != MCTF_OK ||
       strcmp(child, parent) <= 0)
   {
      return MCTF_FAIL;
   }

   return MCTF_OK;
}

MCTF_MODULE_SETUP(s3_local)
{
   storage_status = mctf_se_up_local(MCTF_BACKEND_GARAGE);
   if (storage_status == MCTF_OK && mctf_se_backup("primary") != 0)
   {
      storage_status = MCTF_FAIL;
   }
}

MCTF_MODULE_TEARDOWN(s3_local)
{
   mctf_se_down();
}

/*
 * Archive with S3 enabled: the archive is built from the kept local data
 * and written as <dir>/archive-primary-<label>.tar.zstd.
 */
MCTF_INTEGRATION_TEST(test_s3_local_archive)
{
   char label[256] = {0};
   char args[2 * MAX_PATH];
   char path[MAX_PATH] = {0};

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(mctf_se_newest_label("primary", label, sizeof(label)) == MCTF_OK, cleanup,
               "could not resolve backup label");

   pgmoneta_snprintf(args, sizeof(args), "archive primary %s %s", label, TEST_RESTORE_DIR);
   MCTF_ASSERT(mctf_se_cli(args, NULL) == 0, cleanup, "archive command failed");

   /* the CLI exits 0 even on a server error, so the archive file is the proof */
   pgmoneta_snprintf(path, sizeof(path), "%s/archive-primary-%s.tar.zstd", TEST_RESTORE_DIR, label);
   MCTF_ASSERT(pgmoneta_exists(path), cleanup, "archive file was not created");

cleanup:
   if (pgmoneta_exists(path))
   {
      pgmoneta_delete_file(path, NULL);
   }
   MCTF_FINISH();
}

/*
 * Incremental backup with S3 enabled: the backup is chained to its parent
 * and its INCREMENTAL.* files reach S3.
 */
MCTF_INTEGRATION_TEST(test_s3_local_incremental_backup)
{
   char parent[256] = {0};
   char child[256] = {0};
   char expected[MAX_PATH];
   char* out = NULL;
   char* listing = NULL;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(incremental_backup(parent, child, sizeof(parent), &out) == MCTF_OK, cleanup,
               "incremental backup failed");
   MCTF_ASSERT_PTR_NONNULL(out, cleanup, "no backup output");
   MCTF_ASSERT(strstr(out, "Incremental: true") != NULL, cleanup, "backup is not incremental");

   pgmoneta_snprintf(expected, sizeof(expected), "IncrementalParent: %s", parent);
   MCTF_ASSERT(strstr(out, expected) != NULL, cleanup, "incremental parent is not %s", parent);

   MCTF_ASSERT(mctf_se_s3_ls("primary", child, &listing) == 0, cleanup, "s3 ls failed");
   MCTF_ASSERT_PTR_NONNULL(listing, cleanup, "empty S3 listing");
   MCTF_ASSERT(strstr(listing, "INCREMENTAL.") != NULL, cleanup, "no INCREMENTAL files in S3");

cleanup:
   free(out);
   free(listing);
   MCTF_FINISH();
}

/*
 * Incremental restore with S3 enabled: the chain (of any depth, as it builds
 * on the newest backup) is combined into a full data directory, with no
 * INCREMENTAL.* files left behind.
 */
MCTF_INTEGRATION_TEST(test_s3_local_incremental_restore)
{
   char parent[256] = {0};
   char child[256] = {0};
   char dir[MAX_PATH] = {0};
   char path[MAX_PATH];
   char* out = NULL;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(incremental_backup(parent, child, sizeof(parent), NULL) == MCTF_OK, cleanup,
               "incremental backup failed");
   MCTF_ASSERT(mctf_se_restore("primary", child, NULL) == 0, cleanup, "restore command failed");

   pgmoneta_snprintf(dir, sizeof(dir), "%s/primary-%s", TEST_RESTORE_DIR, child);
   pgmoneta_snprintf(path, sizeof(path), "%s/PG_VERSION", dir);
   MCTF_ASSERT(pgmoneta_exists(path), cleanup, "restore produced no PG_VERSION");

   mctf_sh(&out, "find %s -name 'INCREMENTAL.*' | head -1", dir);
   MCTF_ASSERT(out != NULL && out[0] == '\0', cleanup, "INCREMENTAL files left after restore");

cleanup:
   if (pgmoneta_exists(dir))
   {
      pgmoneta_delete_directory(dir);
   }
   free(out);
   MCTF_FINISH();
}
