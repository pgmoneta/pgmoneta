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

/* Integration tests for the unified remote fan-out (SSH + S3 + Azure). */

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

   pgmoneta_snprintf(backup_dir, sizeof(backup_dir), "%s/backup/primary/backup", mctf_se_run_dir());
   pgmoneta_get_directories(backup_dir, &ndir, &dirs);
   if (ndir <= 0 || dirs == NULL)
   {
      return MCTF_FAIL;
   }

   for (int i = 0; i < ndir; i++)
   {
      if (best < 0 || strcmp(dirs[i], dirs[best]) > 0)
      {
         best = i;
      }
   }
   pgmoneta_snprintf(out, size, "%s", dirs[best]);

   for (int i = 0; i < ndir; i++)
   {
      free(dirs[i]);
   }
   free(dirs);

   return out[0] != '\0' ? MCTF_OK : MCTF_FAIL;
}

/* Read one REMOTE_*_ELAPSED value from the newest backup.info; -1 on error. */
static double
elapsed_from_backup_info(const char* key)
{
   char* out = NULL;
   double v = -1.0;

   mctf_sh(&out, "grep '^%s=' %s/backup/primary/backup/%s/backup.info | cut -d= -f2 | tr -d '\\n'",
           key, mctf_se_run_dir(), shared_label);
   if (out != NULL && out[0] != '\0')
   {
      v = atof(out);
   }
   free(out);

   return v;
}

MCTF_MODULE_SETUP(remote)
{
   memset(shared_label, 0, sizeof(shared_label));
   storage_status = mctf_se_up_all();
   if (storage_status == MCTF_OK)
   {
      if (mctf_se_backup("primary") != 0 ||
          newest_backup_label(shared_label, sizeof(shared_label)) != MCTF_OK)
      {
         storage_status = MCTF_FAIL;
      }
   }
}

MCTF_MODULE_TEARDOWN(remote)
{
   mctf_se_down();
}

/* One backup must land on every configured backend. */
MCTF_INTEGRATION_TEST(test_remote_backup_lands_on_all_backends)
{
   const struct mctf_se* ssh_ctx = NULL;
   char* s3_listing = NULL;
   char* ssh_out = NULL;
   int blob_count = 0;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   /* S3 (Garage) */
   MCTF_ASSERT(mctf_se_s3_ls("primary", shared_label, &s3_listing) == 0, cleanup, "s3 ls failed");
   MCTF_ASSERT_PTR_NONNULL(s3_listing, cleanup, "empty s3 ls response");
   MCTF_ASSERT(strstr(s3_listing, "backup.info") != NULL, cleanup,
               "backup.info not found in S3 objects");

   /* Azure (Azurite) */
   blob_count = mctf_se_azure_blob_count();
   MCTF_ASSERT(blob_count > 0, cleanup, "no blobs found in Azurite after backup");

   /* SSH (sftp) */
   ssh_ctx = mctf_se_context_for(MCTF_BACKEND_SSH);
   MCTF_ASSERT_PTR_NONNULL(ssh_ctx, cleanup, "no SSH backend context");
   MCTF_ASSERT(mctf_container_exec((struct mctf_container*)&ssh_ctx->container,
                                   "find /home/pgmoneta/upload -type f",
                                   &ssh_out) == 0,
               cleanup, "find on sftp container failed");
   MCTF_ASSERT(ssh_out != NULL && strchr(ssh_out, '\n') != NULL, cleanup,
               "no files found on sftp container after backup");

cleanup:
   free(s3_listing);
   free(ssh_out);
   MCTF_FINISH();
}

/* At least two uploads must be in flight before the first one finishes. */
MCTF_INTEGRATION_TEST(test_remote_uploads_run_in_parallel)
{
   char* out = NULL;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   mctf_sh(&out,
           "awk '/remote_upload: .* started/{s++} /remote_upload: .* finished/{print s; exit}' %s/pgmoneta.log",
           mctf_se_run_dir());
   MCTF_ASSERT(out != NULL && out[0] != '\0', cleanup, "no remote_upload markers in managed log");
   MCTF_ASSERT(atoi(out) >= 2, cleanup,
               "fewer than 2 uploads in flight before the first finished (serial upload?)");

cleanup:
   free(out);
   MCTF_FINISH();
}

/* All three REMOTE_*_ELAPSED keys must appear in backup.info. */
MCTF_INTEGRATION_TEST(test_remote_all_timings_recorded)
{
   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(elapsed_from_backup_info("REMOTE_SSH_ELAPSED") > 0.0, cleanup,
               "REMOTE_SSH_ELAPSED not recorded in backup.info");
   MCTF_ASSERT(elapsed_from_backup_info("REMOTE_S3_ELAPSED") > 0.0, cleanup,
               "REMOTE_S3_ELAPSED not recorded in backup.info");
   MCTF_ASSERT(elapsed_from_backup_info("REMOTE_AZURE_ELAPSED") > 0.0, cleanup,
               "REMOTE_AZURE_ELAPSED not recorded in backup.info");

cleanup:
   MCTF_FINISH();
}

/* Local data/ is removed after remote-only backup; catalog stays. */
MCTF_INTEGRATION_TEST(test_remote_local_data_removed_metadata_kept)
{
   char data_dir[MAX_PATH];

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   pgmoneta_snprintf(data_dir, sizeof(data_dir), "%s/backup/primary/backup/%s/data",
                     mctf_se_run_dir(), shared_label);
   MCTF_ASSERT(access(data_dir, F_OK) != 0, cleanup,
               "local data directory still present after remote-only backup");

   MCTF_ASSERT(mctf_se_has_local_metadata("primary"), cleanup,
               "local metadata missing after remote backup");

cleanup:
   MCTF_FINISH();
}

/* A second backup must succeed to guard against per-backend state corruption. */
MCTF_INTEGRATION_TEST(test_remote_second_backup_succeeds)
{
   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(mctf_se_backup("primary") == 0, cleanup, "second remote backup failed");

cleanup:
   MCTF_FINISH();
}
