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
 * Storage-engine integration tests: Azure Blob Storage via Azurite.
 *
 * The entire backend lifecycle (start the Azurite container, provision the
 * blob container, configure and start a dedicated pgmoneta, tear everything
 * down) is handled by mctf_se. A test only states intent.
 *
 * Blob-side verification uses a direct Azurite REST query (python3 stdlib,
 * SharedKey auth) to count blobs in the container — equivalent to what
 * mctf_se_s3_ls does for S3. Indirect signals are also checked:
 *   - STATUS=1 in backup.info confirms the upload completed successfully
 *   - the local data/ subdirectory is removed by azure_storage_teardown when
 *     STORAGE_ENGINE_LOCAL is not set — this proves the teardown ran
 */

#include <mctf.h>
#include <mctf_container.h>
#include <mctf_se.h>
#include <tscommon.h>
#include <utils.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int storage_status = MCTF_FAIL;
static char shared_label[256];

/* Return the lexicographically largest (newest) backup label for primary. */
static int
newest_backup_label(char* out, size_t size)
{
   char backup_dir[MAX_PATH];
   char** dirs = NULL;
   int ndir = 0;
   int best = -1;

   snprintf(backup_dir, sizeof(backup_dir), "%s/backup/primary/backup", mctf_se_run_dir());
   pgmoneta_get_directories(backup_dir, &ndir, &dirs);
   if (ndir <= 0 || dirs == NULL)
      return MCTF_FAIL;

   for (int i = 0; i < ndir; i++)
   {
      if (best < 0 || strcmp(dirs[i], dirs[best]) > 0)
         best = i;
   }
   snprintf(out, size, "%s", dirs[best]);

   for (int i = 0; i < ndir; i++)
      free(dirs[i]);
   free(dirs);

   return out[0] != '\0' ? MCTF_OK : MCTF_FAIL;
}

MCTF_MODULE_SETUP(azure)
{
   memset(shared_label, 0, sizeof(shared_label));
   storage_status = mctf_se_up(MCTF_BACKEND_AZURITE);
   if (storage_status == MCTF_OK)
   {
      if (mctf_se_backup("primary") != 0 ||
          newest_backup_label(shared_label, sizeof(shared_label)) != MCTF_OK)
      {
         storage_status = MCTF_FAIL;
      }
   }
}

MCTF_MODULE_TEARDOWN(azure)
{
   mctf_se_down();
}

/* Verify blobs physically exist in Azurite by querying its list-blobs REST API. */
MCTF_INTEGRATION_TEST(test_azure_blobs_exist_in_container)
{
   int blob_count;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   blob_count = mctf_se_azure_blob_count();
   MCTF_ASSERT(blob_count > 0, cleanup,
               "Azurite container is empty after backup — se_azure.c did not upload any blobs");

cleanup:
   MCTF_FINISH();
}

/* Verify backup.info contains STATUS=1, confirming the upload completed without error. */
MCTF_INTEGRATION_TEST(test_azure_backup_info_is_valid)
{
   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");
   MCTF_ASSERT(shared_label[0] != '\0', cleanup, "no backup label available");

   MCTF_ASSERT(
      mctf_sh(NULL, "grep -q 'STATUS=1' %s/backup/primary/backup/%s/backup.info",
              mctf_se_run_dir(), shared_label) == 0,
      cleanup, "backup.info does not contain STATUS=1 (upload may have failed)");

cleanup:
   MCTF_FINISH();
}

/* Verify the local data/ directory is removed after upload, confirming teardown ran. */
MCTF_INTEGRATION_TEST(test_azure_backup_removes_local_data)
{
   char data_path[MAX_PATH];

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");
   MCTF_ASSERT(shared_label[0] != '\0', cleanup, "no backup label available");

   snprintf(data_path, sizeof(data_path), "%s/backup/primary/backup/%s/data",
            mctf_se_run_dir(), shared_label);

   MCTF_ASSERT(access(data_path, F_OK) != 0, cleanup,
               "local data/ directory still exists after azure-only backup (teardown failed)");

cleanup:
   MCTF_FINISH();
}

/* Verify a second consecutive backup also succeeds, guarding against WAL or connection regressions. */
MCTF_INTEGRATION_TEST(test_azure_second_backup_succeeds)
{
   char label2[256] = {0};

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(mctf_se_backup("primary") == 0, cleanup, "second backup to Azure failed");
   MCTF_ASSERT(newest_backup_label(label2, sizeof(label2)) == MCTF_OK, cleanup,
               "could not resolve second backup label");

   /* The new label must be different from (and lexicographically after) the first. */
   MCTF_ASSERT(strcmp(label2, shared_label) > 0, cleanup,
               "second backup label is not newer than first");

   MCTF_ASSERT(
      mctf_sh(NULL, "grep -q 'STATUS=1' %s/backup/primary/backup/%s/backup.info",
              mctf_se_run_dir(), label2) == 0,
      cleanup, "second backup.info does not contain STATUS=1");

cleanup:
   MCTF_FINISH();
}
