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
 * Storage-engine integration tests: SSH via atmoz/sftp.
 *
 * The entire backend lifecycle (generate a key pair, start an sftp container,
 * configure and start a dedicated pgmoneta, tear everything down) is handled
 * by mctf_se. A test only states intent.
 */

#include <mctf.h>
#include <mctf_container.h>
#include <mctf_se.h>
#include <tscommon.h>
#include <utils.h>

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

MCTF_MODULE_SETUP(ssh)
{
   char label[256];

   memset(label, 0, sizeof(label));
   storage_status = mctf_se_up(MCTF_BACKEND_SSH);
   if (storage_status == MCTF_OK)
   {
      /* Catches silent SFTP failures where the CLI exits 0 but nothing was stored. */
      if (mctf_se_backup("primary") != 0 ||
          newest_backup_label(label, sizeof(label)) != MCTF_OK)
      {
         storage_status = MCTF_FAIL;
      }
   }
}

MCTF_MODULE_TEARDOWN(ssh)
{
   mctf_se_down();
}

/*
 * Direct proof that backup data landed on the remote: exec into the sftp
 * container and count files under the SSH base directory. At least one file
 * must be present after a successful backup.
 */
MCTF_INTEGRATION_TEST(test_ssh_remote_files_exist)
{
   const struct mctf_se* ctx = NULL;
   char* out = NULL;
   int file_count = 0;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   ctx = mctf_se_context();
   MCTF_ASSERT_PTR_NONNULL(ctx, cleanup, "no active backend context");

   /* Count all regular files uploaded to the remote SSH base directory. */
   MCTF_ASSERT(mctf_container_exec((struct mctf_container*)&ctx->container,
                                   "find /home/pgmoneta/upload -type f",
                                   &out) == 0,
               cleanup, "find on sftp container failed");

   MCTF_ASSERT_PTR_NONNULL(out, cleanup, "find returned no output");

   /* Count newlines to get the number of files. */
   for (const char* p = out; *p != '\0'; p++)
   {
      if (*p == '\n')
      {
         file_count++;
      }
   }
   MCTF_ASSERT(file_count > 0, cleanup, "no files found in SSH base directory after backup");

cleanup:
   free(out);
   MCTF_FINISH();
}

/*
 * The local backup catalog must record the SSH backup as successful.
 * STATUS=1 in backup.info is the authoritative signal that pgmoneta
 * considers the backup complete.
 */
MCTF_INTEGRATION_TEST(test_ssh_backup_info_is_valid)
{
   char backup_dir[MAX_PATH];
   char** dirs = NULL;
   int ndir = 0;
   char info_path[MAX_PATH];
   char* out = NULL;
   int best = -1;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   /* Locate the newest backup directory under the managed instance's local catalog. */
   pgmoneta_snprintf(backup_dir, sizeof(backup_dir), "%s/backup/primary/backup", mctf_se_run_dir());
   pgmoneta_get_directories(backup_dir, &ndir, &dirs);
   MCTF_ASSERT(ndir > 0 && dirs != NULL, cleanup, "no backup directories in local catalog");

   for (int i = 0; i < ndir; i++)
   {
      if (best < 0 || strcmp(dirs[i], dirs[best]) > 0)
      {
         best = i;
      }
   }

   pgmoneta_snprintf(info_path, sizeof(info_path), "%s/%s/backup.info", backup_dir, dirs[best]);
   mctf_sh(&out, "grep -c 'STATUS=1' %s", info_path);
   MCTF_ASSERT_PTR_NONNULL(out, cleanup, "could not read backup.info");
   MCTF_ASSERT(atoi(out) > 0, cleanup, "STATUS=1 not found in backup.info");

cleanup:
   free(out);
   if (dirs != NULL)
   {
      for (int i = 0; i < ndir; i++)
      {
         free(dirs[i]);
      }
      free(dirs);
   }
   MCTF_FINISH();
}

/*
 * Even though data is stored on the remote SSH server, the local backup
 * catalog (backup.info, backup.sha512, backup.manifest) must remain on disk
 * — it is the authoritative index of all backups and must survive a remote
 * backup.
 */
MCTF_INTEGRATION_TEST(test_ssh_local_metadata_retained)
{
   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(mctf_se_has_local_metadata("primary"), cleanup,
               "local metadata missing after SSH backup");

cleanup:
   MCTF_FINISH();
}

/*
 * A second backup to the same SSH server must succeed. This guards against
 * connection/session bugs where the first backup leaves the SSH session in
 * a broken state or corrupts the remote directory layout.
 */
MCTF_INTEGRATION_TEST(test_ssh_second_backup_succeeds)
{
   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(mctf_se_backup("primary") == 0, cleanup, "second SSH backup failed");

cleanup:
   MCTF_FINISH();
}
