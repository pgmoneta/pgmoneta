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

/*
 * The upload is driven by backup.manifest and runs one file per worker task,
 * so the blobs for a label must be exactly the manifest entries plus the three
 * metadata files. A count is what catches a dropped file: a worker whose
 * failure is swallowed, or a task freed before its request is sent, still
 * leaves a backup that reports STATUS=1 and a container that is not empty.
 */
static int
manifest_entry_count(const char* label)
{
   char manifest_path[MAX_PATH];
   struct deque* paths = NULL;
   int count = -1;

   snprintf(manifest_path, sizeof(manifest_path), "%s/backup/primary/backup/%s/backup.manifest",
            mctf_se_run_dir(), label);

   if (pgmoneta_manifest_get_paths(manifest_path, &paths))
   {
      return -1;
   }

   count = (int)pgmoneta_deque_size(paths);

   pgmoneta_deque_destroy(paths);

   return count;
}

MCTF_INTEGRATION_TEST(test_azure_uploads_every_manifest_file)
{
   char prefix[MAX_PATH];
   int entries;
   int blobs;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");
   MCTF_ASSERT(shared_label[0] != '\0', cleanup, "no backup label available");

   entries = manifest_entry_count(shared_label);
   MCTF_ASSERT(entries > 0, cleanup, "could not read backup.manifest");

   snprintf(prefix, sizeof(prefix), "pgmoneta/primary/backup/%s/", shared_label);
   blobs = mctf_se_azure_blob_count_prefix(prefix);

   /* backup.manifest, backup.sha512 and backup.info are uploaded after the
    * worker pool drains, and are not themselves manifest entries.
    */
   MCTF_ASSERT(blobs == entries + 3, cleanup,
               "Azurite holds %d blobs under %s (%d in the container overall) but the "
               "manifest has %d entries (expected %d) — the parallel upload dropped "
               "or duplicated files",
               blobs, prefix, mctf_se_azure_blob_count_prefix(""), entries, entries + 3);

cleanup:
   MCTF_FINISH();
}

/*
 * The three metadata files are the commit marker: they are uploaded only once
 * every data blob has landed, so a reader that sees backup.info can rely on
 * the rest being there. This pins that ordering contract.
 */
MCTF_INTEGRATION_TEST(test_azure_uploads_metadata_commit_markers)
{
   const char* metadata[] = {"backup.manifest", "backup.sha512", "backup.info"};
   char name[MAX_PATH];

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");
   MCTF_ASSERT(shared_label[0] != '\0', cleanup, "no backup label available");

   for (int i = 0; i < 3; i++)
   {
      snprintf(name, sizeof(name), "pgmoneta/primary/backup/%s/%s", shared_label, metadata[i]);
      MCTF_ASSERT(mctf_se_azure_blob_exists(name), cleanup,
                  "%s was not uploaded to Azure", metadata[i]);
   }

cleanup:
   MCTF_FINISH();
}

/*
 * Restore a backup that exists only in Azure.
 *
 * The module setup ran an azure-only backup, and test_azure_backup_removes_local_data
 * pins that the local data/ directory is deleted afterwards. So every data file the
 * restore produces has to have been downloaded from the container by
 * azure_download_files — there is no local copy left to fall back on.
 */
MCTF_INTEGRATION_TEST(test_azure_restore_recovers_data)
{
   char target[MAX_PATH];
   char args[2 * MAX_PATH];

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");
   MCTF_ASSERT(shared_label[0] != '\0', cleanup, "no backup label available");

   snprintf(target, sizeof(target), "%s/restore_azure", mctf_se_run_dir());
   MCTF_ASSERT(mctf_sh(NULL, "rm -rf %s && mkdir -p %s", target, target) == 0,
               cleanup, "could not prepare restore target directory");

   pgmoneta_snprintf(args, sizeof(args), "azure restore primary %s %s", shared_label, target);
   MCTF_ASSERT(mctf_se_cli(args, NULL) == 0, cleanup,
               "azure restore command failed for %s", shared_label);

   /* Record hard evidence outside the run dir, which the harness tears down */
   mctf_sh(NULL,
           "{ echo '--- restored tree ---'; find %s -type f | head -50; "
           "echo '--- file count ---'; find %s -type f | wc -l; } "
           "> /tmp/azure-restore-evidence.txt 2>&1",
           target, target);

   /* PG_VERSION is a data file: it can only be here if the download ran */
   MCTF_ASSERT(mctf_sh(NULL, "find %s -name PG_VERSION | grep -q .", target) == 0,
               cleanup,
               "restored tree has no PG_VERSION — nothing was downloaded from Azure");

   /* Files are staged as .tmp and renamed only on a clean transfer */
   MCTF_ASSERT(mctf_sh(NULL, "! find %s -name '*.tmp' | grep -q .", target) == 0,
               cleanup,
               "restored tree still contains .tmp files — a transfer did not complete");

cleanup:
   MCTF_FINISH();
}

/*
 * Directories are recorded in the manifest with a trailing slash. Without them a
 * restore loses every directory that holds no files.
 */
MCTF_INTEGRATION_TEST(test_azure_manifest_has_directory_entries)
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
 * These PGDATA subdirectories are normally empty, so they appear nowhere as
 * files in backup.manifest and can only return via directory rows. PostgreSQL
 * refuses to start without them, so a restore that omits them looks successful
 * but is not.
 */
MCTF_INTEGRATION_TEST(test_azure_restore_recreates_empty_directories)
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

   snprintf(cmd, sizeof(cmd), "azure restore primary %s %s", shared_label, TEST_RESTORE_DIR);
   MCTF_ASSERT(mctf_se_cli(cmd, NULL) == 0, cleanup, "azure restore failed");

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
MCTF_INTEGRATION_TEST(test_azure_restored_cluster_starts)
{
   char pgdata[MAX_PATH];
   char cmd[2 * MAX_PATH];

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");
   MCTF_ASSERT(shared_label[0] != '\0', cleanup, "no backup label available");

   snprintf(cmd, sizeof(cmd), "azure restore primary %s %s", shared_label, TEST_RESTORE_DIR);
   MCTF_ASSERT(mctf_se_cli(cmd, NULL) == 0, cleanup, "azure restore failed");

   snprintf(pgdata, sizeof(pgdata), "%s/primary-%s", TEST_RESTORE_DIR, shared_label);

   MCTF_ASSERT(start_restored_cluster(pgdata) == 0, cleanup,
               "PostgreSQL did not start on the restored data directory");

cleanup:
   MCTF_FINISH();
}
