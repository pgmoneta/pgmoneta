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
#include <manifest.h>

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
      return MCTF_FAIL;

   for (int i = 0; i < ndir; i++)
   {
      if (best < 0 || strcmp(dirs[i], dirs[best]) > 0)
         best = i;
   }
   pgmoneta_snprintf(out, size, "%s", dirs[best]);

   for (int i = 0; i < ndir; i++)
      free(dirs[i]);
   free(dirs);

   return out[0] != '\0' ? MCTF_OK : MCTF_FAIL;
}
/*
 * Count the S3 objects reported by an s3 ls response.
 * Each object is represented by one S3Key entry.
 */
static int
count_s3_objects(const char* listing)
{
   const char* p = listing;
   const char* key = "\"S3Key\"";
   int count = 0;

   if (listing == NULL)
   {
      return 0;
   }

   while ((p = strstr(p, key)) != NULL)
   {
      count++;
      p += strlen(key);
   }

   return count;
}

/*
 * Take a backup on the given server and resolve its label in one call.
 * The new label must differ from the newest label that existed before
 * the backup. This prevents a failed backup from being mistaken for
 * an older successful backup.
 */
static int
take_backup_and_get_label(const char* server, char* out, size_t size)
{
   char previous_label[256] = {0};

   if (newest_backup_label(previous_label, sizeof(previous_label)) != MCTF_OK)
   {
      return MCTF_FAIL;
   }

   if (mctf_se_backup(server) != 0)
   {
      return MCTF_FAIL;
   }

   if (newest_backup_label(out, size) != MCTF_OK)
   {
      return MCTF_FAIL;
   }

   if (strcmp(previous_label, out) == 0)
   {
      return MCTF_FAIL;
   }

   return MCTF_OK;
}

/*
 * Verify that a backup label has no objects left in S3. Checks the command
 * succeeded, the response reports Status: true, and no S3Key entries remain.
 */
static int
assert_no_s3_objects(const char* server, const char* label, char** out_listing)
{
   char* listing = NULL;
   int result = MCTF_FAIL;

   if (mctf_se_s3_ls(server, label, &listing) != 0)
   {
      goto cleanup;
   }
   if (listing == NULL)
   {
      goto cleanup;
   }
   if (strstr(listing, "\"Status\": true") == NULL)
   {
      goto cleanup;
   }
   if (strstr(listing, "S3Key") != NULL)
   {
      goto cleanup;
   }

   result = MCTF_OK;

cleanup:
   *out_listing = listing;
   return result;
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
 * list-backup must also report this specific backup, not just any response.
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

   MCTF_ASSERT(strstr(listing, shared_label) != NULL, cleanup,
               "S3 backup label missing from list-backup output");

cleanup:
   free(listing);
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
   MCTF_ASSERT(strstr(listing, "\"Status\": true") != NULL, cleanup,
               "s3 ls did not report success");

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
   char* backup_list = NULL;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(take_backup_and_get_label("primary", label, sizeof(label)) == MCTF_OK, cleanup,
               "could not create backup and resolve its label");

   MCTF_ASSERT(mctf_se_delete("primary", label) == 0, cleanup, "delete command failed");

   MCTF_ASSERT(assert_no_s3_objects("primary", label, &listing) == MCTF_OK, cleanup,
               "S3 objects remain after delete, or s3 ls did not report success");

   MCTF_ASSERT(mctf_se_list_backup("primary", &backup_list) == 0, cleanup,
               "list-backup failed after delete");
   MCTF_ASSERT_PTR_NONNULL(backup_list, cleanup,
                           "empty list-backup response after delete");

   MCTF_ASSERT(strstr(backup_list, label) == NULL, cleanup,
               "deleted backup is still present in list-backup output");

   MCTF_ASSERT(strstr(backup_list, "\"Status\": true") != NULL, cleanup,
               "list-backup did not report success after delete");

cleanup:
   free(listing);
   free(backup_list);
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

   pgmoneta_snprintf(manifest, sizeof(manifest), "%s/backup/primary/backup/%s/backup.manifest",
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

   pgmoneta_snprintf(bin, sizeof(bin), "/usr/pgsql-%s/bin", ver);
   pgmoneta_snprintf(container, sizeof(container), "pgmoneta-test-postgresql%s", ver);

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

   pgmoneta_snprintf(cmd, sizeof(cmd), "s3 restore primary %s %s", shared_label, TEST_RESTORE_DIR);
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

   pgmoneta_snprintf(cmd, sizeof(cmd), "s3 restore primary %s %s", shared_label, TEST_RESTORE_DIR);
   MCTF_ASSERT(mctf_se_cli(cmd, NULL) == 0, cleanup, "s3 restore failed");

   pgmoneta_snprintf(pgdata, sizeof(pgdata), "%s/primary-%s", TEST_RESTORE_DIR, shared_label);

   MCTF_ASSERT(start_restored_cluster(pgdata) == 0, cleanup,
               "PostgreSQL did not start on the restored data directory");

cleanup:
   MCTF_FINISH();
}

/*
 * Verify that a backup label has no objects in S3.
 * Checks that the listing succeeds and contains no S3 objects.
 */
MCTF_INTEGRATION_TEST(test_s3_list_nonexistent_backup_is_empty)
{
   char* listing = NULL;
   char invalid_label[256];

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   pgmoneta_snprintf(invalid_label, sizeof(invalid_label), "%s-invalid", shared_label);

   MCTF_ASSERT(assert_no_s3_objects("primary", invalid_label, &listing) == MCTF_OK, cleanup,
               "s3 ls failed, or returned objects, for a non-existent backup");

cleanup:
   free(listing);
   MCTF_FINISH();
}

/*
 * Restoring from S3 must preserve file contents, not just file names.
 * Compares the SHA-512 checksums of selected files against the checksums
 * recorded in backup.manifest. This provides content validation for
 * representative files, not the entire backup.
 */
MCTF_INTEGRATION_TEST(test_s3_backup_content_validation)
{
   char label[256] = {0};
   char manifest_path[MAX_PATH];
   char restored_file[MAX_PATH];
   char command[2 * MAX_PATH];
   char* checksum = NULL;
   struct deque* paths = NULL;

   const char* files[] =
      {
         "PG_VERSION",
         "postgresql.auto.conf",
         "base/1/PG_VERSION"};

   const int file_count = sizeof(files) / sizeof(files[0]);

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(take_backup_and_get_label("primary", label, sizeof(label)) == MCTF_OK, cleanup,
               "could not create backup and resolve its label");

   pgmoneta_snprintf(manifest_path, sizeof(manifest_path),
                     "%s/backup/primary/backup/%s/backup.manifest",
                     mctf_se_run_dir(), label);

   MCTF_ASSERT(pgmoneta_manifest_get_paths(manifest_path, &paths) == 0, cleanup,
               "could not read backup.manifest");
   MCTF_ASSERT_PTR_NONNULL(paths, cleanup, "backup.manifest produced no entries");

   pgmoneta_snprintf(command, sizeof(command), "s3 restore primary %s %s", label, TEST_RESTORE_DIR);
   MCTF_ASSERT(mctf_se_cli(command, NULL) == 0, cleanup, "S3 restore failed");

   for (int i = 0; i < file_count; i++)
   {
      const char* expected_checksum = NULL;

      MCTF_ASSERT(pgmoneta_deque_exists(paths, (char*)files[i]), cleanup,
                  "file is missing from backup.manifest: %s", files[i]);

      expected_checksum = (const char*)pgmoneta_deque_get(paths, (char*)files[i]);
      MCTF_ASSERT_PTR_NONNULL(expected_checksum, cleanup,
                              "file checksum is missing from manifest: %s", files[i]);

      pgmoneta_snprintf(restored_file, sizeof(restored_file), "%s/primary-%s/%s",
                        TEST_RESTORE_DIR, label, files[i]);

      MCTF_ASSERT(mctf_sh(NULL, "test -f '%s'", restored_file) == 0, cleanup,
                  "restored file does not exist: %s", files[i]);

      MCTF_ASSERT(mctf_sh(&checksum, "sha512sum '%s' | awk '{print $1}' | tr -d '\\n'",
                          restored_file) == 0,
                  cleanup,
                  "could not calculate restored file checksum: %s", files[i]);
      MCTF_ASSERT_PTR_NONNULL(checksum, cleanup,
                              "restored file checksum is missing: %s", files[i]);
      MCTF_ASSERT(checksum[0] != '\0', cleanup,
                  "restored file checksum is empty: %s", files[i]);
      MCTF_ASSERT(strcmp(checksum, expected_checksum) == 0, cleanup,
                  "restored file checksum does not match backup.manifest: %s", files[i]);

      free(checksum);
      checksum = NULL;
   }

cleanup:
   free(checksum);

   if (paths != NULL)
   {
      pgmoneta_deque_destroy(paths);
   }

   MCTF_FINISH();
}

/*
 * s3 restore, delete and retain must all refuse a label that does not exist,
 * and report the failure explicitly rather than silently doing nothing.
 * restore needs an extra target directory argument that the other two
 * commands do not take.
  * retain and delete must also fail with their specific "no backup" error codes.
 */
MCTF_INTEGRATION_TEST(test_s3_commands_report_failure_for_invalid_label)
{
   const char* commands[] = {"restore", "delete", "retain"};
   const int command_count = sizeof(commands) / sizeof(commands[0]);
   char invalid_label[256];
   char cmd[2 * MAX_PATH];
   char* output = NULL;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   pgmoneta_snprintf(invalid_label, sizeof(invalid_label), "%s-invalid", shared_label);

   for (int i = 0; i < command_count; i++)
   {
      if (strcmp(commands[i], "restore") == 0)
      {
         pgmoneta_snprintf(cmd, sizeof(cmd), "s3 restore primary %s %s", invalid_label, TEST_RESTORE_DIR);
      }
      else
      {
         pgmoneta_snprintf(cmd, sizeof(cmd), "%s primary %s", commands[i], invalid_label);
      }

      mctf_se_cli(cmd, &output);

      MCTF_ASSERT_PTR_NONNULL(output, cleanup, "no output from %s command", commands[i]);
      MCTF_ASSERT(strstr(output, "Status: false") != NULL, cleanup,
                  "%s did not report failure for a non-existent label", commands[i]);

      if (strcmp(commands[i], "retain") == 0)
      {
         MCTF_ASSERT(strstr(output, "Error: 1200") != NULL, cleanup,
                     "%s returned an unexpected error code", commands[i]);
      }
      else if (strcmp(commands[i], "delete") == 0)
      {
         MCTF_ASSERT(strstr(output, "Error: 505") != NULL, cleanup,
                     "%s returned an unexpected error code", commands[i]);
      }

      free(output);
      output = NULL;
   }

cleanup:
   free(output);
   MCTF_FINISH();
}

/*
 * retain and expunge must actually flip the Keep flag on the backup, not
 * just return success while leaving the metadata unchanged.
 */
MCTF_INTEGRATION_TEST(test_s3_retain_then_expunge_toggles_keep)
{
   char label[256] = {0};
   char cmd[512];
   char* output = NULL;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(take_backup_and_get_label("primary", label, sizeof(label)) == MCTF_OK, cleanup,
               "could not create backup and resolve its label");

   pgmoneta_snprintf(cmd, sizeof(cmd), "retain primary %s", label);
   MCTF_ASSERT(mctf_se_cli(cmd, NULL) == 0, cleanup, "retain failed");

   pgmoneta_snprintf(cmd, sizeof(cmd), "-F json info primary %s", label);
   MCTF_ASSERT(mctf_se_cli(cmd, &output) == 0, cleanup, "info after retain failed");
   MCTF_ASSERT_PTR_NONNULL(output, cleanup, "no info output after retain");
   MCTF_ASSERT(strstr(output, "\"Keep\": true") != NULL,
               cleanup, "Keep flag not set after retain");
   free(output);
   output = NULL;

   pgmoneta_snprintf(cmd, sizeof(cmd), "expunge primary %s", label);
   MCTF_ASSERT(mctf_se_cli(cmd, NULL) == 0, cleanup, "expunge failed");

   pgmoneta_snprintf(cmd, sizeof(cmd), "-F json info primary %s", label);
   MCTF_ASSERT(mctf_se_cli(cmd, &output) == 0, cleanup, "info after expunge failed");
   MCTF_ASSERT_PTR_NONNULL(output, cleanup, "no info output after expunge");
   MCTF_ASSERT(strstr(output, "\"Keep\": false") != NULL,
               cleanup, "Keep flag not cleared after expunge");

cleanup:
   free(output);
   MCTF_FINISH();
}

/*
 * A retained backup must be protected from delete: the command must be
 * refused and the S3 objects must remain untouched. Once expunged, delete
 * must work normally and the objects must be removed.
 */
MCTF_INTEGRATION_TEST(test_s3_retain_blocks_delete_until_expunged)
{
   char label[256] = {0};
   char cmd[512];
   char* output = NULL;
   char* listing = NULL;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(take_backup_and_get_label("primary", label, sizeof(label)) == MCTF_OK, cleanup,
               "could not create backup and resolve its label");

   pgmoneta_snprintf(cmd, sizeof(cmd), "retain primary %s", label);
   MCTF_ASSERT(mctf_se_cli(cmd, NULL) == 0, cleanup, "retain failed");

   pgmoneta_snprintf(cmd, sizeof(cmd), "-F json delete primary %s", label);
   mctf_se_cli(cmd, &output);
   MCTF_ASSERT_PTR_NONNULL(output, cleanup, "no output from delete command");
   MCTF_ASSERT(strstr(output, "\"Status\": false") != NULL, cleanup,
               "delete of a retained backup did not report failure status");
   MCTF_ASSERT(strstr(output, "\"Error\": 506") != NULL, cleanup,
               "delete did not fail with the retained-backup error code");
   free(output);
   output = NULL;

   MCTF_ASSERT(mctf_se_s3_ls("primary", label, &listing) == 0, cleanup,
               "s3 ls failed after refused delete");
   MCTF_ASSERT_PTR_NONNULL(listing, cleanup, "empty s3 ls response");
   MCTF_ASSERT(strstr(listing, "\"Status\": true") != NULL, cleanup,
               "s3 ls did not report success after the refused delete");
   MCTF_ASSERT(strstr(listing, "S3Key") != NULL, cleanup,
               "retained backup objects vanished from S3 after refused delete");
   free(listing);
   listing = NULL;

   pgmoneta_snprintf(cmd, sizeof(cmd), "expunge primary %s", label);
   MCTF_ASSERT(mctf_se_cli(cmd, NULL) == 0, cleanup, "expunge failed");

   MCTF_ASSERT(mctf_se_delete("primary", label) == 0, cleanup, "delete failed after expunge");

   MCTF_ASSERT(assert_no_s3_objects("primary", label, &listing) == MCTF_OK, cleanup,
               "S3 objects remain after delete of expunged backup, or s3 ls did not report success");

cleanup:
   free(output);
   free(listing);
   MCTF_FINISH();
}

/*
 * Deleting one backup must not affect any other backup: its objects must
 * remain fully intact in S3 after the delete.
 */
MCTF_INTEGRATION_TEST(test_s3_delete_one_backup_keeps_the_other)
{
   char first_label[256] = {0};
   char second_label[256] = {0};
   char* first_listing = NULL;
   char* second_listing = NULL;
   char second_prefix[512];
   int second_object_count = 0;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(take_backup_and_get_label("primary", first_label, sizeof(first_label)) == MCTF_OK,
               cleanup, "could not create first backup and resolve its label");

   MCTF_ASSERT(take_backup_and_get_label("primary", second_label, sizeof(second_label)) == MCTF_OK,
               cleanup, "could not create second backup and resolve its label");

   MCTF_ASSERT(strcmp(first_label, second_label) != 0, cleanup,
               "two backups have the same label");

   /*
    * Record the number of objects belonging to the second backup
    * before deleting the first backup.
    */
   MCTF_ASSERT(mctf_se_s3_ls("primary", second_label, &second_listing) == 0, cleanup,
               "listing second backup before delete failed");
   MCTF_ASSERT_PTR_NONNULL(second_listing, cleanup, "empty s3 ls for second backup");
   MCTF_ASSERT(strstr(second_listing, "\"Status\": true") != NULL, cleanup,
               "s3 ls did not report success for the second backup before delete");

   second_object_count = count_s3_objects(second_listing);
   MCTF_ASSERT(second_object_count > 0, cleanup,
               "second backup has no S3 objects before delete");

   free(second_listing);
   second_listing = NULL;

   MCTF_ASSERT(mctf_se_delete("primary", first_label) == 0, cleanup,
               "delete of first backup failed");

   MCTF_ASSERT(assert_no_s3_objects("primary", first_label, &first_listing) == MCTF_OK, cleanup,
               "first backup objects remain after delete, or s3 ls did not report success");

   /*
    * The second backup must still contain exactly the same number of S3
    * objects after the first backup was deleted.
    */
   MCTF_ASSERT(mctf_se_s3_ls("primary", second_label, &second_listing) == 0, cleanup,
               "listing second backup after delete failed");
   MCTF_ASSERT_PTR_NONNULL(second_listing, cleanup, "empty s3 ls for second backup");
   MCTF_ASSERT(strstr(second_listing, "\"Status\": true") != NULL, cleanup,
               "s3 ls did not report success for the second backup after delete");

   MCTF_ASSERT(count_s3_objects(second_listing) == second_object_count, cleanup,
               "deleting the first backup changed the number of objects in the second backup");

   pgmoneta_snprintf(second_prefix, sizeof(second_prefix),
                     "\"S3Key\": \"primary/backup/%s/", second_label);
   MCTF_ASSERT(strstr(second_listing, second_prefix) != NULL, cleanup,
               "second backup objects were removed by deleting the first");

cleanup:
   free(first_listing);
   free(second_listing);
   MCTF_FINISH();
}