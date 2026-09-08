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
 * Storage-engine integration tests: S3 via Garage.
 *
 * The entire backend lifecycle (start the Garage container, provision it,
 * configure and start a dedicated pgmoneta, tear everything down) is handled
 * by mctf_se. A test only states intent.
 */

#include <mctf.h>
#include <mctf_container.h>
#include <mctf_se.h>
#include <tscommon.h>
#include <utils.h>

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

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

MCTF_MODULE_SETUP(s3)
{
   memset(shared_label, 0, sizeof(shared_label));
   storage_status = mctf_se_up(MCTF_BACKEND_GARAGE);
   if (storage_status == MCTF_OK)
   {
      if (mctf_se_backup("primary") != 0 ||
          newest_backup_label(shared_label, sizeof(shared_label)) != MCTF_OK)
      {
         storage_status = MCTF_FAIL;
      }
   }
}

MCTF_MODULE_TEARDOWN(s3)
{
   mctf_se_down();
}

/*
 * Backup to S3 succeeds and the local catalog records it. Local metadata
 * must always be present and authoritative, even with a remote-only engine.
 */
MCTF_INTEGRATION_TEST(test_s3_backup_keeps_local_metadata)
{
   char* listing = NULL;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(mctf_se_has_local_metadata("primary"), cleanup,
               "local metadata missing after S3 backup");

   MCTF_ASSERT(mctf_se_list_backup("primary", &listing) == 0, cleanup, "list-backup failed");
   MCTF_ASSERT_PTR_NONNULL(listing, cleanup, "empty list-backup response");

cleanup:
   free(listing);
   MCTF_FINISH();
}

/*
 * Full round trip: back up to S3, then restore FROM S3 and verify the data
 * directory was genuinely reconstructed (not just an exit-0 with no files).
 */
MCTF_INTEGRATION_TEST(test_s3_backup_restore_roundtrip)
{
   char* out = NULL;
   char cmd[2 * MAX_PATH];

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   /* download from S3 */
   snprintf(cmd, sizeof(cmd), "s3 restore primary %s %s", shared_label, TEST_RESTORE_DIR);
   MCTF_ASSERT(mctf_se_cli(cmd, NULL) == 0, cleanup, "s3 restore failed");

   /* verify the cluster actually came back (guards against a false-positive
    * restore that returns 0 but writes nothing) */
   mctf_sh(&out, "find %s -name PG_VERSION | head -1 | tr -d '\\n'", TEST_RESTORE_DIR);
   MCTF_ASSERT_PTR_NONNULL(out, cleanup, "restore produced no output");
   MCTF_ASSERT(out[0] != '\0', cleanup, "restore produced no PG_VERSION (empty restore)");

cleanup:
   free(out);
   MCTF_FINISH();
}

/*
 * After a backup, the three mandatory metadata files (backup.info,
 * backup.sha512, backup.manifest) must appear as objects in S3.
 */
MCTF_INTEGRATION_TEST(test_s3_list_has_metadata_files)
{
   char* listing = NULL;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(mctf_se_s3_ls("primary", shared_label, &listing) == 0, cleanup, "s3 ls failed");
   MCTF_ASSERT_PTR_NONNULL(listing, cleanup, "empty s3 ls response");

   MCTF_ASSERT(strstr(listing, "backup.info") != NULL, cleanup,
               "backup.info not found in S3 objects");
   MCTF_ASSERT(strstr(listing, "backup.sha512") != NULL, cleanup,
               "backup.sha512 not found in S3 objects");
   MCTF_ASSERT(strstr(listing, "backup.manifest") != NULL, cleanup,
               "backup.manifest not found in S3 objects");

cleanup:
   free(listing);
   MCTF_FINISH();
}

/*
 * After delete, no S3 objects must remain for that backup label — the
 * remote store must be swept, not just the local catalog entry.
 */
MCTF_INTEGRATION_TEST(test_s3_delete_removes_objects)
{
   char label[256] = {0};
   char* listing = NULL;
   int ls_rc;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(mctf_se_backup("primary") == 0, cleanup, "backup to S3 failed");
   MCTF_ASSERT(newest_backup_label(label, sizeof(label)) == MCTF_OK, cleanup,
               "could not resolve backup label");

   MCTF_ASSERT(mctf_se_delete("primary", label) == 0, cleanup, "delete command failed");

   /* Either the label is gone from the catalog (ls fails) or the object list
    * is empty — both prove the remote store was swept. */
   ls_rc = mctf_se_s3_ls("primary", label, &listing);
   MCTF_ASSERT(ls_rc != 0 || listing == NULL || strstr(listing, "S3Key") == NULL,
               cleanup, "S3 objects remain after delete");

cleanup:
   free(listing);
   MCTF_FINISH();
}

/*
 * Directories are recorded in the manifest with a trailing slash. Without them a
 * restore loses every directory that holds no files.
 */
MCTF_INTEGRATION_TEST(test_s3_manifest_has_directory_entries)
{
   char manifest[MAX_PATH];
   char* out = NULL;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");
   MCTF_ASSERT(shared_label[0] != '\0', cleanup, "no backup label available");

   snprintf(manifest, sizeof(manifest), "%s/backup/primary/backup/%s/backup.manifest",
            mctf_se_run_dir(), shared_label);

   mctf_sh(&out, "grep -c '^pg_notify/,' %s | tr -d '\\n'", manifest);
   MCTF_ASSERT_PTR_NONNULL(out, cleanup, "could not read the manifest");
   MCTF_ASSERT(out[0] == '1', cleanup,
               "manifest has no pg_notify/ entry; empty directories will be lost");

cleanup:
   free(out);
   MCTF_FINISH();
}

static int
run_step(const char* label, const char* fmt, ...)
{
   char cmd[2048];
   char* out = NULL;
   va_list ap;
   int rc;

   va_start(ap, fmt);
   vsnprintf(cmd, sizeof(cmd), fmt, ap);
   va_end(ap);

   rc = mctf_sh(&out, "%s", cmd);
   if (rc != 0)
   {
      fprintf(stderr, "    step '%s' failed (rc=%d): %s\n", label, rc,
              out != NULL ? out : "(no output)");
      fflush(stderr);
   }

   free(out);

   return rc;
}

static int
start_restored_cluster(const char* pgdata)
{
   const char* ver = getenv("TEST_PG_VERSION");
   char engine[64] = {0};
   char container[128];
   char bin[128];
   bool use_container = true;

   if (ver == NULL || ver[0] == '\0')
   {
      ver = "17";
   }

   snprintf(bin, sizeof(bin), "/usr/pgsql-%s/bin", ver);
   snprintf(container, sizeof(container), "pgmoneta-test-postgresql%s", ver);

   /* CI runs against a local PostgreSQL; otherwise it lives in a container */
   if (mctf_container_engine(engine, sizeof(engine)) != MCTF_OK ||
       mctf_sh(NULL, "%s inspect %s >/dev/null 2>&1", engine, container) != 0)
   {
      use_container = false;
   }

   if (use_container)
   {
      mctf_sh(NULL, "%s exec -u postgres %s %s/psql -p 5432 -qtAc 'SELECT pg_switch_wal()' "
                    ">/dev/null 2>&1",
              engine, container, bin);
   }
   else
   {
      mctf_sh(NULL, "psql -h /tmp -p 5432 -U postgres -qtAc 'SELECT pg_switch_wal()' "
                    ">/dev/null 2>&1");
   }

   if (mctf_sh(NULL,
               "W=$(awk '/START WAL LOCATION/{gsub(/[()]/,\"\",$6); print $6}' %s/backup_label); "
               "A=%s/backup/primary/wal; "
               "[ -n \"$W\" ] || exit 1; "
               "for i in $(seq 1 20); do [ -f \"$A/$W.zstd\" ] && break; sleep 1; done; "
               "[ -f \"$A/$W.zstd\" ] || exit 1; "
               "zstd -d -q -f \"$A/$W.zstd\" -o \"%s/pg_wal/$W\"",
               pgdata, mctf_se_run_dir(), pgdata))
   {
      fprintf(stderr, "    could not stage the WAL segment; recovery will likely fail\n");
      fflush(stderr);
   }

   if (!use_container)
   {
      char* log = NULL;

      if (run_step("chmod", "chmod 700 %s", pgdata))
      {
         return 1;
      }

      if (run_step("pg_ctl start",
                   "pg_ctl -D %s -o '-p 5433 -c logging_collector=off' "
                   "-l %s/../restored.log -w -t 30 start",
                   pgdata, pgdata))
      {
         mctf_sh(&log, "tail -30 %s/../restored.log", pgdata);
         fprintf(stderr, "    postgres log:\n%s\n", log != NULL ? log : "(empty)");
         fflush(stderr);
         free(log);
         return 1;
      }

      if (run_step("pg_isready", "pg_isready -h /tmp -p 5433"))
      {
         mctf_sh(NULL, "pg_ctl -D %s stop -m immediate", pgdata);
         return 1;
      }

      mctf_sh(NULL, "pg_ctl -D %s stop -m immediate", pgdata);

      return 0;
   }

   if (run_step("rm", "%s exec -u root %s rm -rf /tmp/restored", engine, container) ||
       run_step("cp", "%s cp %s %s:/tmp/restored", engine, pgdata, container) ||
       run_step("chown", "%s exec -u root %s chown -R postgres:postgres /tmp/restored",
                engine, container) ||
       run_step("chmod", "%s exec -u root %s chmod 700 /tmp/restored", engine, container))
   {
      return 1;
   }

   if (run_step("pg_ctl start",
                "%s exec -u postgres %s %s/pg_ctl -D /tmp/restored "
                "-o '-p 5433 -c logging_collector=off' "
                "-l /tmp/restored.log -w -t 30 start",
                engine, container, bin))
   {
      char* log = NULL;

      mctf_sh(&log, "%s exec -u postgres %s tail -30 /tmp/restored.log", engine, container);
      fprintf(stderr, "    postgres log:\n%s\n", log != NULL ? log : "(empty)");
      fflush(stderr);
      free(log);

      return 1;
   }

   if (run_step("pg_isready", "%s exec -u postgres %s %s/pg_isready -h /tmp -p 5433",
                engine, container, bin))
   {
      mctf_sh(NULL, "%s exec -u postgres %s %s/pg_ctl -D /tmp/restored stop -m immediate",
              engine, container, bin);
      return 1;
   }

   mctf_sh(NULL, "%s exec -u postgres %s %s/pg_ctl -D /tmp/restored stop -m immediate",
           engine, container, bin);
   mctf_sh(NULL, "%s exec -u root %s rm -rf /tmp/restored", engine, container);

   return 0;
}

/*
 * These PGDATA subdirectories are normally empty, so they appear nowhere in
 * backup.manifest and can only return via backup.dirs. PostgreSQL refuses to
 * start without them, so a restore that omits them looks successful but is not.
 */
MCTF_INTEGRATION_TEST(test_s3_restore_recreates_empty_directories)
{
   const char* empty_dirs[] = {
      "pg_notify",
      "pg_stat_tmp",
      "pg_serial",
      "pg_snapshots",
      "pg_dynshmem",
      "pg_replslot",
      "pg_twophase",
      "pg_tblspc",
      "pg_subtrans",
      "pg_stat",
      "pg_logical/mappings",
      "pg_logical/snapshots"};
   const size_t dir_count = sizeof(empty_dirs) / sizeof(empty_dirs[0]);
   char cmd[2 * MAX_PATH];
   size_t i;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");
   MCTF_ASSERT(shared_label[0] != '\0', cleanup, "no backup label available");

   snprintf(cmd, sizeof(cmd), "s3 restore primary %s %s", shared_label, TEST_RESTORE_DIR);
   MCTF_ASSERT(mctf_se_cli(cmd, NULL) == 0, cleanup, "s3 restore failed");

   for (i = 0; i < dir_count; i++)
   {
      MCTF_ASSERT(mctf_sh(NULL, "test -d %s/primary-%s/%s",
                          TEST_RESTORE_DIR, shared_label, empty_dirs[i]) == 0,
                  cleanup,
                  "restored data directory is missing %s — PostgreSQL would refuse to start",
                  empty_dirs[i]);
   }

cleanup:
   MCTF_FINISH();
}

/*
 * The restore is only usable if PostgreSQL will actually run on it. Structural
 * checks can all pass on a data directory the server then refuses to open, so
 * this starts a cluster on the restored files and waits for it to accept
 * connections.
 */
MCTF_INTEGRATION_TEST(test_s3_restored_cluster_starts)
{
   char pgdata[MAX_PATH];
   char cmd[2 * MAX_PATH];

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");
   MCTF_ASSERT(shared_label[0] != '\0', cleanup, "no backup label available");

   snprintf(cmd, sizeof(cmd), "s3 restore primary %s %s", shared_label, TEST_RESTORE_DIR);
   MCTF_ASSERT(mctf_se_cli(cmd, NULL) == 0, cleanup, "s3 restore failed");

   snprintf(pgdata, sizeof(pgdata), "%s/primary-%s", TEST_RESTORE_DIR, shared_label);

   MCTF_ASSERT(start_restored_cluster(pgdata) == 0, cleanup,
               "PostgreSQL did not start on the restored data directory");

cleanup:
   MCTF_FINISH();
}
