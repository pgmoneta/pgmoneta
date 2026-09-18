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
 * Storage-engine integration tests: local,s3 via Garage.
 *
 * With storage_engine = local,s3, "s3 delete" removes the S3 copy while the
 * local backup (data + metadata) stays intact and restorable. This is the
 * complement of the s3-only metadata cleanup covered in test_s3.c.
 */

#include <mctf.h>
#include <mctf_container.h>
#include <mctf_se.h>
#include <tscommon.h>
#include <utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int storage_status = MCTF_FAIL;

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

MCTF_MODULE_SETUP(s3_local)
{
   storage_status = mctf_se_up_with_engine(MCTF_BACKEND_GARAGE, "local, s3", "garage-local-s3");
   if (storage_status == MCTF_OK)
   {
      if (mctf_se_backup("primary") != 0)
      {
         storage_status = MCTF_FAIL;
      }
   }
}

MCTF_MODULE_TEARDOWN(s3_local)
{
   mctf_se_down();
}

/*
 * A leaf s3 delete sweeps the S3 copy but keeps the local backup fully
 * intact: metadata, data directory, catalog entry, and local restorability.
 */
MCTF_INTEGRATION_TEST(test_s3_delete_leaf_keeps_local_backup)
{
   char label[256] = {0};
   char cmd[512];
   char path[MAX_PATH];
   char* listing = NULL;
   char* backups = NULL;
   char* out = NULL;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(mctf_se_backup("primary") == 0, cleanup, "backup failed");
   MCTF_ASSERT(newest_backup_label(label, sizeof(label)) == MCTF_OK, cleanup,
               "could not resolve backup label");

   /* prove the S3 copy exists before deleting (no false-positive sweep) */
   MCTF_ASSERT(mctf_se_s3_ls("primary", label, &listing) == 0, cleanup, "s3 ls failed");
   MCTF_ASSERT_PTR_NONNULL(listing, cleanup, "empty s3 ls response");
   MCTF_ASSERT(strstr(listing, "S3Key") != NULL, cleanup, "no S3 objects after backup");
   free(listing);
   listing = NULL;

   snprintf(cmd, sizeof(cmd), "s3 delete primary %s", label);
   MCTF_ASSERT(mctf_se_cli(cmd, NULL) == 0, cleanup, "s3 delete failed");

   /* S3 copy gone */
   MCTF_ASSERT(mctf_se_s3_ls("primary", label, &listing) != 0 ||
                  listing == NULL ||
                  strstr(listing, "S3Key") == NULL,
               cleanup, "S3 objects remain after s3 delete");

   /* local metadata kept */
   snprintf(path, sizeof(path), "%s/backup/primary/backup/%s/backup.info",
            mctf_se_run_dir(), label);
   MCTF_ASSERT(mctf_sh(NULL, "test -f %s", path) == 0, cleanup,
               "local backup.info missing after s3 delete");

   /* local data kept (local engine enabled, no data pruning) */
   snprintf(path, sizeof(path), "%s/backup/primary/backup/%s/data",
            mctf_se_run_dir(), label);
   MCTF_ASSERT(mctf_sh(NULL, "test -d %s", path) == 0, cleanup,
               "local data missing after s3 delete");

   /* catalog still lists the label */
   MCTF_ASSERT(mctf_se_list_backup("primary", &backups) == 0, cleanup, "list-backup failed");
   MCTF_ASSERT_PTR_NONNULL(backups, cleanup, "empty list-backup response");
   MCTF_ASSERT(strstr(backups, label) != NULL, cleanup,
               "local backup missing from catalog after s3 delete");

   /* local backup still restores */
   snprintf(cmd, sizeof(cmd), "restore primary %s %s", label, TEST_RESTORE_DIR);
   MCTF_ASSERT(mctf_se_cli(cmd, NULL) == 0, cleanup, "local restore failed");

   mctf_sh(&out, "find %s -name PG_VERSION | head -1 | tr -d '\\n'", TEST_RESTORE_DIR);
   MCTF_ASSERT_PTR_NONNULL(out, cleanup, "restore produced no output");
   MCTF_ASSERT(out[0] != '\0', cleanup, "restore produced no PG_VERSION (empty restore)");

cleanup:
   free(listing);
   free(backups);
   free(out);
   MCTF_FINISH();
}

/*
 * S3 delete is leaf-only: deleting a backup that has a dependent live
 * incremental child must be refused (Outcome.Error 3404,
 * MANAGEMENT_ERROR_DELETE_S3_HAS_CHILD) with the S3 objects and the local
 * backup untouched. Chain-aware removal is owned by normal 'delete'.
 *
 * This runs here (not in test_s3.c) because a live incremental needs the
 * parent's local data, which only persists when the local engine is
 * enabled. The CLI exit code only reflects transport success, so the
 * refusal is asserted on the JSON outcome, not on the exit code.
 */
MCTF_INTEGRATION_TEST(test_s3_delete_refuses_parent_with_live_child)
{
   char parent[256] = {0};
   char child[256] = {0};
   char cmd[2048];
   char path[MAX_PATH];
   char* out = NULL;
   char* listing = NULL;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   /* full backup -> parent */
   MCTF_ASSERT(mctf_se_backup("primary") == 0, cleanup, "full backup failed");
   MCTF_ASSERT(newest_backup_label(parent, sizeof(parent)) == MCTF_OK, cleanup,
               "could not resolve parent label");

   /* labels have 1s resolution: wait out the parent's second so the child
    * cannot collide with it */
   MCTF_ASSERT(mctf_sh(NULL, "sleep 2") == 0, cleanup, "sleep failed");

   /* incremental child of the newest (full) backup */
   MCTF_ASSERT(mctf_se_cli("backup primary newest", NULL) == 0, cleanup,
               "incremental backup failed");
   MCTF_ASSERT(newest_backup_label(child, sizeof(child)) == MCTF_OK, cleanup,
               "could not resolve child label");
   MCTF_ASSERT(strcmp(parent, child) != 0, cleanup,
               "incremental backup did not create a new label");

   /* prove the chain linkage the guard depends on (backup.info PARENT key) */
   snprintf(path, sizeof(path), "%s/backup/primary/backup/%s/backup.info",
            mctf_se_run_dir(), child);
   MCTF_ASSERT(mctf_sh(NULL, "grep -q 'PARENT=%s' %s", parent, path) == 0, cleanup,
               "child backup is not linked to the parent");

   /* refusal must surface as error 3404 in the JSON response */
   snprintf(cmd, sizeof(cmd), "-F json s3 delete primary %s", parent);
   MCTF_ASSERT(mctf_se_cli(cmd, &out) != 0, cleanup, "s3 delete unexpectedly succeeded");
   MCTF_ASSERT_PTR_NONNULL(out, cleanup, "empty s3 delete response");
   MCTF_ASSERT(strstr(out, "3404") != NULL, cleanup,
               "s3 delete of a parent with a live child was not refused (expected error 3404)");
   free(out);
   out = NULL;

   /* the parent's S3 objects must be untouched */
   MCTF_ASSERT(mctf_se_s3_ls("primary", parent, &listing) == 0, cleanup, "s3 ls failed");
   MCTF_ASSERT_PTR_NONNULL(listing, cleanup, "empty s3 ls response");
   MCTF_ASSERT(strstr(listing, "S3Key") != NULL, cleanup,
               "parent S3 objects were deleted despite refusal");

   /* the local parent backup must be untouched as well */
   snprintf(path, sizeof(path), "%s/backup/primary/backup/%s/backup.info",
            mctf_se_run_dir(), parent);
   MCTF_ASSERT(mctf_sh(NULL, "test -f %s", path) == 0, cleanup,
               "local parent metadata missing after refused s3 delete");

cleanup:
   free(out);
   free(listing);
   MCTF_FINISH();
}

/*
 * Fail-closed guard: when the local catalog cannot be read, s3 delete must
 * be refused (Outcome.Error 3405, MANAGEMENT_ERROR_DELETE_S3_UNVERIFIED)
 * instead of deleting blind. The test poisons a backup.info with a malformed
 * line (bytes saved for restore), asserts the refusal with S3 objects intact,
 * then restores the file byte-for-byte so no corruption leaks into other tests.
 * Lives in this module (not test_s3.c) because the guard only applies with
 * the local engine enabled. Placed last: it temporarily poisons the shared
 * catalog.
 */
MCTF_INTEGRATION_TEST(test_s3_delete_refuses_when_catalog_unreadable)
{
   char label[256] = {0};
   char cmd[2048];
   char info[MAX_PATH];
   char* out = NULL;
   char* listing = NULL;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(mctf_se_backup("primary") == 0, cleanup, "backup failed");
   MCTF_ASSERT(newest_backup_label(label, sizeof(label)) == MCTF_OK, cleanup,
               "could not resolve backup label");

   /* prove the S3 copy exists before deleting (no false-positive sweep) */
   MCTF_ASSERT(mctf_se_s3_ls("primary", label, &listing) == 0, cleanup, "s3 ls failed");
   MCTF_ASSERT_PTR_NONNULL(listing, cleanup, "empty s3 ls response");
   MCTF_ASSERT(strstr(listing, "S3Key") != NULL, cleanup, "no S3 objects after backup");
   free(listing);
   listing = NULL;

   /* poison the local catalog entry with a malformed line (no '='):
    * the backup.info parser aborts the whole catalog load on it, which is
    * exactly the unreadable-catalog path (bytes saved for restore below).
    * Note: merely truncating the file would parse cleanly into a blank
    * entry and would NOT trigger the guard. */
   snprintf(info, sizeof(info), "%s/backup/primary/backup/%s/backup.info",
            mctf_se_run_dir(), label);
   MCTF_ASSERT(mctf_sh(NULL, "cp %s %s.guardbak", info, info) == 0, cleanup,
               "could not back up backup.info");
   MCTF_ASSERT(mctf_sh(NULL, "printf 'CORRUPT-NO-EQUALS\\n' > %s", info) == 0, cleanup,
               "could not poison backup.info");

   /* refusal must surface as error 3405 in the JSON response */
   snprintf(cmd, sizeof(cmd), "-F json s3 delete primary %s", label);
   MCTF_ASSERT(mctf_se_cli(cmd, &out) != 0, cleanup, "s3 delete unexpectedly succeeded");
   MCTF_ASSERT_PTR_NONNULL(out, cleanup, "empty s3 delete response");
   MCTF_ASSERT(strstr(out, "3405") != NULL, cleanup,
               "s3 delete with unreadable catalog was not refused (expected error 3405)");
   free(out);
   out = NULL;

   /* the S3 objects must be untouched (s3 ls reads S3 only, no catalog) */
   MCTF_ASSERT(mctf_se_s3_ls("primary", label, &listing) == 0, cleanup, "s3 ls failed");
   MCTF_ASSERT_PTR_NONNULL(listing, cleanup, "empty s3 ls response");
   MCTF_ASSERT(strstr(listing, "S3Key") != NULL, cleanup,
               "S3 objects were deleted despite refusal");

   /* the local backup must be untouched as well */
   MCTF_ASSERT(mctf_sh(NULL, "test -f %s.guardbak", info) == 0, cleanup,
               "backup of backup.info went missing");

cleanup:
   /* restore the catalog entry byte-for-byte; never leak corruption */
   if (label[0] != '\0')
   {
      snprintf(info, sizeof(info), "%s/backup/primary/backup/%s/backup.info",
               mctf_se_run_dir(), label);
      mctf_sh(NULL, "mv %s.guardbak %s 2>/dev/null", info, info);
   }
   free(out);
   free(listing);
   MCTF_FINISH();
}
