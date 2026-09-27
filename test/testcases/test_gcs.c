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
 * Storage-engine integration tests: GCS backup and restore via fake-gcs-server.
 *
 * Backup verification reads back the emulator's REST API directly. Restore
 * verification goes through the real "gcs restore" pgmoneta-cli command --
 * the module setup runs a GCS-only backup, and gcs_storage_teardown removes
 * the local data/ directory afterwards (see se_gcs.c), so every file a
 * restore produces has to have come back from the emulator via
 * pgmoneta_object_download_files() in se_object.c.
 *
 * The container lifecycle (start fake-gcs-server, provision the bucket,
 * configure and start a dedicated pgmoneta, tear everything down) is handled
 * by mctf_se. A test only states intent.
 */

#include <mctf.h>
#include <mctf_container.h>
#include <mctf_se.h>
#include <tscommon.h>
#include <deque.h>
#include <manifest.h>
#include <utils.h>

#include <stdarg.h>
#include <stdio.h>
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

MCTF_MODULE_SETUP(gcs)
{
   memset(shared_label, 0, sizeof(shared_label));
   storage_status = mctf_se_up(MCTF_BACKEND_GCS);
   if (storage_status == MCTF_OK)
   {
      if (mctf_se_backup("primary") != 0 ||
          newest_backup_label(shared_label, sizeof(shared_label)) != MCTF_OK)
      {
         storage_status = MCTF_FAIL;
      }
   }
}

MCTF_MODULE_TEARDOWN(gcs)
{
   mctf_se_down();
}

/*
 * Backup to GCS succeeds and the local catalog records it. Local metadata
 * must always be present and authoritative, even with a remote-only engine.
 */
MCTF_INTEGRATION_TEST(test_gcs_backup_keeps_local_metadata)
{
   char* listing = NULL;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(mctf_se_has_local_metadata("primary"), cleanup,
               "local metadata missing after GCS backup");

   MCTF_ASSERT(mctf_se_list_backup("primary", &listing) == 0, cleanup, "list-backup failed");
   MCTF_ASSERT_PTR_NONNULL(listing, cleanup, "empty list-backup response");

cleanup:
   free(listing);
   MCTF_FINISH();
}

/*
 * Backup uploads the actual data files to GCS, not just metadata: the bucket
 * must hold a large number of objects under the backup prefix (guards
 * against a false-positive backup that returns 0 but uploads nothing).
 */
MCTF_INTEGRATION_TEST(test_gcs_backup_uploads_data_files)
{
   char prefix[MAX_PATH];
   int count;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");
   MCTF_ASSERT(shared_label[0] != '\0', cleanup, "no backup label available");

   snprintf(prefix, sizeof(prefix), "pgmoneta/primary/backup/%s/data/", shared_label);
   count = mctf_se_gcs_object_count_prefix(prefix);

   MCTF_ASSERT(count > 0, cleanup, "no data-file objects found under %s in GCS", prefix);

cleanup:
   MCTF_FINISH();
}

/*
 * After a backup, the three mandatory metadata files (backup.info,
 * backup.sha512, backup.manifest) must each appear as exactly one object
 * in GCS -- they are the commit markers gcs_upload_files() writes last.
 * Empty-directory layout lives in backup.manifest (trailing-slash rows)
 * rather than a separate sidecar file.
 */
MCTF_INTEGRATION_TEST(test_gcs_backup_uploads_metadata_files)
{
   const char* metadata_files[] = {
      "backup.info",
      "backup.sha512",
      "backup.manifest"};
   const size_t file_count = sizeof(metadata_files) / sizeof(metadata_files[0]);
   char object_name[MAX_PATH];
   size_t i;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");
   MCTF_ASSERT(shared_label[0] != '\0', cleanup, "no backup label available");

   for (i = 0; i < file_count; i++)
   {
      int count;

      snprintf(object_name, sizeof(object_name), "pgmoneta/primary/backup/%s/%s",
               shared_label, metadata_files[i]);
      count = mctf_se_gcs_object_count_prefix(object_name);

      MCTF_ASSERT(count == 1, cleanup, "expected exactly one %s object in GCS, found %d",
                  metadata_files[i], count);
   }

cleanup:
   MCTF_FINISH();
}

/*
 * Directories are recorded in the manifest with a trailing slash. Without them a
 * restore loses every directory that holds no files.
 */
MCTF_INTEGRATION_TEST(test_gcs_manifest_has_directory_entries)
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

/*
 * Restore a backup that exists only in GCS.
 *
 * The module setup ran a GCS-only backup, and gcs_storage_teardown deletes the
 * local data/ directory afterwards (mirroring se_azure.c). So every data file the
 * restore produces has to have been downloaded from the bucket by
 * pgmoneta_object_download_files() -- there is no local copy left to fall back on.
 */
MCTF_INTEGRATION_TEST(test_gcs_restore_recovers_data)
{
   char target[MAX_PATH];
   char args[2 * MAX_PATH];

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");
   MCTF_ASSERT(shared_label[0] != '\0', cleanup, "no backup label available");

   snprintf(target, sizeof(target), "%s/restore_gcs", mctf_se_run_dir());
   MCTF_ASSERT(mctf_sh(NULL, "rm -rf %s && mkdir -p %s", target, target) == 0,
               cleanup, "could not prepare restore target directory");

   pgmoneta_snprintf(args, sizeof(args), "gcs restore primary %s %s", shared_label, target);
   MCTF_ASSERT(mctf_se_cli(args, NULL) == 0, cleanup,
               "gcs restore command failed for %s", shared_label);

   /* Record hard evidence outside the run dir, which the harness tears down */
   mctf_sh(NULL,
           "{ echo '--- restored tree ---'; find %s -type f | head -50; "
           "echo '--- file count ---'; find %s -type f | wc -l; } "
           "> /tmp/gcs-restore-evidence.txt 2>&1",
           target, target);

   /* PG_VERSION is a data file: it can only be here if the download ran */
   MCTF_ASSERT(mctf_sh(NULL, "find %s -name PG_VERSION | grep -q .", target) == 0,
               cleanup,
               "restored tree has no PG_VERSION -- nothing was downloaded from GCS");

   /* Files are staged as .tmp and renamed only on a clean transfer */
   MCTF_ASSERT(mctf_sh(NULL, "! find %s -name '*.tmp' | grep -q .", target) == 0,
               cleanup,
               "restored tree still contains .tmp files -- a transfer did not complete");

cleanup:
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
                   "pg_ctl -D %s -o '-p 5434 -c logging_collector=off' "
                   "-l %s/../restored.log -w -t 30 start",
                   pgdata, pgdata))
      {
         mctf_sh(&log, "tail -30 %s/../restored.log", pgdata);
         fprintf(stderr, "    postgres log:\n%s\n", log != NULL ? log : "(empty)");
         fflush(stderr);
         free(log);
         return 1;
      }

      if (run_step("pg_isready", "pg_isready -h /tmp -p 5434"))
      {
         mctf_sh(NULL, "pg_ctl -D %s stop -m immediate", pgdata);
         return 1;
      }

      mctf_sh(NULL, "pg_ctl -D %s stop -m immediate", pgdata);

      return 0;
   }

   if (run_step("rm", "%s exec -u root %s rm -rf /tmp/restored-gcs", engine, container) ||
       run_step("cp", "%s cp %s %s:/tmp/restored-gcs", engine, pgdata, container) ||
       run_step("chown", "%s exec -u root %s chown -R postgres:postgres /tmp/restored-gcs",
                engine, container) ||
       run_step("chmod", "%s exec -u root %s chmod 700 /tmp/restored-gcs", engine, container))
   {
      return 1;
   }

   if (run_step("pg_ctl start",
                "%s exec -u postgres %s %s/pg_ctl -D /tmp/restored-gcs "
                "-o '-p 5434 -c logging_collector=off' "
                "-l /tmp/restored-gcs.log -w -t 30 start",
                engine, container, bin))
   {
      char* log = NULL;

      mctf_sh(&log, "%s exec -u postgres %s tail -30 /tmp/restored-gcs.log", engine, container);
      fprintf(stderr, "    postgres log:\n%s\n", log != NULL ? log : "(empty)");
      fflush(stderr);
      free(log);

      return 1;
   }

   if (run_step("pg_isready", "%s exec -u postgres %s %s/pg_isready -h /tmp -p 5434",
                engine, container, bin))
   {
      mctf_sh(NULL, "%s exec -u postgres %s %s/pg_ctl -D /tmp/restored-gcs stop -m immediate",
              engine, container, bin);
      return 1;
   }

   mctf_sh(NULL, "%s exec -u postgres %s %s/pg_ctl -D /tmp/restored-gcs stop -m immediate",
           engine, container, bin);
   mctf_sh(NULL, "%s exec -u root %s rm -rf /tmp/restored-gcs", engine, container);

   return 0;
}

/*
 * These PGDATA subdirectories are normally empty, so they appear nowhere as
 * files in backup.manifest and can only return via directory rows. PostgreSQL
 * refuses to start without them, so a restore that omits them looks successful
 * but is not.
 */
MCTF_INTEGRATION_TEST(test_gcs_restore_recreates_empty_directories)
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

   snprintf(cmd, sizeof(cmd), "gcs restore primary %s %s", shared_label, TEST_RESTORE_DIR);
   MCTF_ASSERT(mctf_se_cli(cmd, NULL) == 0, cleanup, "gcs restore failed");

   for (i = 0; i < dir_count; i++)
   {
      MCTF_ASSERT(mctf_sh(NULL, "test -d %s/primary-%s/%s",
                          TEST_RESTORE_DIR, shared_label, empty_dirs[i]) == 0,
                  cleanup,
                  "restored data directory is missing %s -- PostgreSQL would refuse to start",
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
MCTF_INTEGRATION_TEST(test_gcs_restored_cluster_starts)
{
   char pgdata[MAX_PATH];
   char cmd[2 * MAX_PATH];

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");
   MCTF_ASSERT(shared_label[0] != '\0', cleanup, "no backup label available");

   snprintf(cmd, sizeof(cmd), "gcs restore primary %s %s", shared_label, TEST_RESTORE_DIR);
   MCTF_ASSERT(mctf_se_cli(cmd, NULL) == 0, cleanup, "gcs restore failed");

   snprintf(pgdata, sizeof(pgdata), "%s/primary-%s", TEST_RESTORE_DIR, shared_label);

   MCTF_ASSERT(start_restored_cluster(pgdata) == 0, cleanup,
               "PostgreSQL did not start on the restored data directory");

cleanup:
   MCTF_FINISH();
}
