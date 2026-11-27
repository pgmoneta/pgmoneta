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
#include <info.h>
#include <manifest.h>
#include <shmem.h>
#include <storage.h>
#include <utils.h>
#include <verify.h>

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int storage_status = MCTF_FAIL;
static char shared_label[256];

MCTF_MODULE_SETUP(azure)
{
   memset(shared_label, 0, sizeof(shared_label));
   storage_status = mctf_se_up(MCTF_BACKEND_AZURITE);
   if (storage_status == MCTF_OK)
   {
      if (mctf_se_backup("primary") != 0 ||
          mctf_se_newest_label("primary", shared_label, sizeof(shared_label)) != MCTF_OK)
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

/*
 * Run scheduled verification once in a child process, pointed at the managed
 * instance's local metadata and Azurite connection.
 *
 * pgmoneta_sha512_verification() exits with its result, so it cannot run in
 * the test process. The child opts out of the global backend teardown first.
 *
 * @return the verification exit code, or -1 if it could not be run
 */
static int
run_verification_once(void)
{
   const struct mctf_se* ctx = NULL;
   struct main_configuration* config = NULL;
   char saved_base_dir[MAX_PATH];
   char saved_azure_storage_account[MISC_LENGTH];
   char saved_azure_container[MISC_LENGTH];
   char saved_azure_shared_key[MISC_LENGTH];
   char saved_azure_base_dir[MAX_PATH];
   char saved_azure_endpoint[MISC_LENGTH];
   int saved_azure_port = 0;
   bool saved_azure_use_tls = false;
   int saved_storage_engine = 0;
   bool saved_online[NUMBER_OF_SERVERS];
   bool saved = false;
   pid_t pid;
   int status = 0;
   int rc = -1;

   ctx = mctf_se_context();
   config = (struct main_configuration*)shmem;

   if (ctx == NULL || config == NULL)
   {
      return -1;
   }

   memset(saved_online, 0, sizeof(saved_online));

   snprintf(saved_base_dir, sizeof(saved_base_dir), "%s", config->base_dir);
   snprintf(saved_azure_storage_account, sizeof(saved_azure_storage_account), "%s", config->azure_storage_account);
   snprintf(saved_azure_container, sizeof(saved_azure_container), "%s", config->azure_container);
   snprintf(saved_azure_shared_key, sizeof(saved_azure_shared_key), "%s", config->azure_shared_key);
   snprintf(saved_azure_base_dir, sizeof(saved_azure_base_dir), "%s", config->azure_base_dir);
   snprintf(saved_azure_endpoint, sizeof(saved_azure_endpoint), "%s", config->azure_endpoint);
   saved_azure_port = config->azure_port;
   saved_azure_use_tls = config->azure_use_tls;
   saved_storage_engine = config->storage_engine;
   for (int i = 0; i < config->common.number_of_servers; i++)
   {
      saved_online[i] = config->common.servers[i].online;
   }
   saved = true;

   snprintf(config->base_dir, sizeof(config->base_dir), "%s/backup", mctf_se_run_dir());
   snprintf(config->azure_storage_account, sizeof(config->azure_storage_account), "%s", ctx->access_key);
   snprintf(config->azure_container, sizeof(config->azure_container), "%s", ctx->bucket);
   snprintf(config->azure_shared_key, sizeof(config->azure_shared_key), "%s", ctx->secret_key);
   snprintf(config->azure_base_dir, sizeof(config->azure_base_dir), "pgmoneta");
   snprintf(config->azure_endpoint, sizeof(config->azure_endpoint), "%s", ctx->endpoint);
   config->azure_port = ctx->port;
   config->azure_use_tls = ctx->use_tls;
   config->storage_engine = STORAGE_ENGINE_AZURE;
   for (int i = 0; i < config->common.number_of_servers; i++)
   {
      config->common.servers[i].online = true;
   }

   pid = fork();
   if (pid < 0)
   {
      goto cleanup;
   }

   if (pid == 0)
   {
      char* argv[] = {"pgmoneta-test", NULL};

      /* The verification job exits the process; keep the parent's daemon and
       * Azurite container alive by opting out of the global teardown. It also
       * rewrites the process title, but argv here is a local array, not the
       * real command line, so leave the title alone in the child. */
      mctf_se_disable_atexit_cleanup();
      config->update_process_title = UPDATE_PROCESS_TITLE_NEVER;
      pgmoneta_sha512_verification(argv);
      _exit(1);
   }

   if (waitpid(pid, &status, 0) != pid)
   {
      goto cleanup;
   }

   if (WIFEXITED(status))
   {
      rc = WEXITSTATUS(status);
   }

cleanup:
   if (saved)
   {
      snprintf(config->base_dir, sizeof(config->base_dir), "%s", saved_base_dir);
      snprintf(config->azure_storage_account, sizeof(config->azure_storage_account), "%s", saved_azure_storage_account);
      snprintf(config->azure_container, sizeof(config->azure_container), "%s", saved_azure_container);
      snprintf(config->azure_shared_key, sizeof(config->azure_shared_key), "%s", saved_azure_shared_key);
      snprintf(config->azure_base_dir, sizeof(config->azure_base_dir), "%s", saved_azure_base_dir);
      snprintf(config->azure_endpoint, sizeof(config->azure_endpoint), "%s", saved_azure_endpoint);
      config->azure_port = saved_azure_port;
      config->azure_use_tls = saved_azure_use_tls;
      config->storage_engine = saved_storage_engine;
      for (int i = 0; i < config->common.number_of_servers; i++)
      {
         config->common.servers[i].online = saved_online[i];
      }
   }

   return rc;
}

/*
 * A healthy Azure-only backup must stay valid after scheduled verification.
 * The local data/ directory is gone, so the job has to check the blobs in
 * Azurite instead of hashing local files.
 */
MCTF_INTEGRATION_TEST(test_azure_scheduled_verification_keeps_backup_valid)
{
   char manifest_path[MAX_PATH];
   int rc;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");
   MCTF_ASSERT(shared_label[0] != '\0', cleanup, "no backup label available");

   /* Local metadata is always retained, even when the data lives only in
    * Azure. Verification must read it locally rather than fetching it again. */
   MCTF_ASSERT(mctf_se_has_local_metadata("primary"), cleanup,
               "local metadata for the Azure-only backup is missing");
   snprintf(manifest_path, sizeof(manifest_path),
            "%s/backup/primary/backup/%s/backup.manifest",
            mctf_se_run_dir(), shared_label);
   MCTF_ASSERT(access(manifest_path, F_OK) == 0, cleanup,
               "local backup.manifest is missing for %s", shared_label);

   rc = run_verification_once();
   MCTF_ASSERT_INT_EQ(rc, 0, cleanup,
                      "scheduled verification failed for %s", shared_label);

   MCTF_ASSERT(
      mctf_sh(NULL, "grep -q 'STATUS=1' %s/backup/primary/backup/%s/backup.info",
              mctf_se_run_dir(), shared_label) == 0,
      cleanup, "scheduled verification marked the Azure-only backup invalid");

cleanup:
   MCTF_FINISH();
}

/*
 * A backup whose blobs are missing from the container must be marked invalid.
 * Create a local copy of the metadata under a label that was never uploaded,
 * so every HEAD request returns 404.
 */
MCTF_INTEGRATION_TEST_NEGATIVE(test_azure_verification_detects_missing_remote_blobs)
{
   char backup_root[MAX_PATH] = {0};
   char fake_label[256] = {0};
   char fake_root[MAX_PATH] = {0};
   char real_manifest[MAX_PATH];
   char fake_manifest[MAX_PATH];
   char real_sha512[MAX_PATH];
   char fake_sha512[MAX_PATH];
   struct backup* info = NULL;
   int rc;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");
   MCTF_ASSERT(shared_label[0] != '\0', cleanup, "no backup label available");

   snprintf(backup_root, sizeof(backup_root), "%s/backup/primary/backup/", mctf_se_run_dir());
   snprintf(fake_label, sizeof(fake_label), "verify-missing-%s", shared_label);
   snprintf(fake_root, sizeof(fake_root), "%s%s", backup_root, fake_label);

   MCTF_ASSERT(pgmoneta_load_info(backup_root, shared_label, &info) == 0, cleanup,
               "could not load the real backup metadata");
   MCTF_ASSERT_PTR_NONNULL(info, cleanup, "real backup metadata is NULL");

   snprintf(info->label, sizeof(info->label), "%s", fake_label);

   MCTF_ASSERT(pgmoneta_mkdir(fake_root) == 0, cleanup,
               "could not create %s", fake_root);
   MCTF_ASSERT(pgmoneta_save_info(backup_root, info) == 0, cleanup,
               "could not save the fake backup.info");

   snprintf(real_manifest, sizeof(real_manifest), "%s%s/backup.manifest",
            backup_root, shared_label);
   snprintf(fake_manifest, sizeof(fake_manifest), "%s/backup.manifest", fake_root);
   snprintf(real_sha512, sizeof(real_sha512), "%s%s/backup.sha512",
            backup_root, shared_label);
   snprintf(fake_sha512, sizeof(fake_sha512), "%s/backup.sha512", fake_root);

   MCTF_ASSERT(pgmoneta_copy_file(real_manifest, fake_manifest, NULL) == 0, cleanup,
               "could not copy backup.manifest to %s", fake_root);
   MCTF_ASSERT(pgmoneta_copy_file(real_sha512, fake_sha512, NULL) == 0, cleanup,
               "could not copy backup.sha512 to %s", fake_root);

   rc = run_verification_once();
   MCTF_ASSERT_INT_EQ(rc, 1, cleanup,
                      "verification did not fail for a backup without remote blobs");

   MCTF_ASSERT(
      mctf_sh(NULL, "grep -q 'STATUS=0' %s%s/backup.info", backup_root, fake_label) == 0,
      cleanup, "verification did not mark the backup without remote blobs invalid");

cleanup:
   if (fake_root[0] != '\0' && pgmoneta_exists(fake_root))
   {
      pgmoneta_delete_directory(fake_root);
   }
   free(info);
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
   MCTF_ASSERT(mctf_se_newest_label("primary", label2, sizeof(label2)) == MCTF_OK, cleanup,
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

static int
take_backup_and_get_label(const char* server, char* out, size_t size)
{
   char previous_label[256] = {0};

   if (mctf_se_newest_label(server, previous_label, sizeof(previous_label)) != MCTF_OK)
   {
      return MCTF_FAIL;
   }

   if (mctf_se_backup(server) != 0)
   {
      return MCTF_FAIL;
   }

   if (mctf_se_newest_label(server, out, size) != MCTF_OK)
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
 * Verify that a backup label has no blobs left in Azure. This is the Azure
 * equivalent of S3's assert_no_s3_objects, scoped by prefix since
 * mctf_se_azure_blob_count_prefix queries Azurite directly rather than
 * going through a "azure ls" CLI command (which does not exist).
 */
static int
assert_no_azure_blobs(const char* server, const char* label)
{
   char prefix[MAX_PATH];

   snprintf(prefix, sizeof(prefix), "pgmoneta/%s/backup/%s/", server, label);

   if (mctf_se_azure_blob_count_prefix(prefix) != 0)
   {
      return MCTF_FAIL;
   }

   return MCTF_OK;
}

extern int xml_parse_azure_next_marker(char* xml, char** next_marker);

MCTF_TEST(test_azure_next_marker_self_closing_means_no_more_pages)
{
   char* next_marker = NULL;
   const char* no_more_pages =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<EnumerationResults><Prefix>pgmoneta/</Prefix><Blobs>"
      "<Blob><Name>pgmoneta/a</Name></Blob></Blobs>"
      "<NextMarker/></EnumerationResults>";

   MCTF_ASSERT(xml_parse_azure_next_marker((char*)no_more_pages, &next_marker) == 0, cleanup,
               "parse failed on a self-closing <NextMarker/>");
   MCTF_ASSERT(next_marker == NULL, cleanup,
               "self-closing <NextMarker/> must mean there is no next page");

cleanup:
   free(next_marker);
   MCTF_FINISH();
}

MCTF_TEST(test_azure_next_marker_open_tag_means_more_pages)
{
   char* next_marker = NULL;
   const char* more_pages =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<EnumerationResults><Prefix>pgmoneta/</Prefix><Blobs>"
      "<Blob><Name>pgmoneta/a</Name></Blob></Blobs>"
      "<NextMarker>2!100!token==</NextMarker></EnumerationResults>";

   MCTF_ASSERT(xml_parse_azure_next_marker((char*)more_pages, &next_marker) == 0, cleanup,
               "parse failed on an open <NextMarker>...</NextMarker>");
   MCTF_ASSERT_PTR_NONNULL(next_marker, cleanup,
                           "an open <NextMarker> must produce a non-NULL token");
   MCTF_ASSERT(strcmp(next_marker, "2!100!token==") == 0, cleanup,
               "next marker token did not match the XML content");

cleanup:
   free(next_marker);
   MCTF_FINISH();
}

MCTF_TEST(test_azure_next_marker_absent_means_no_more_pages)
{
   char* next_marker = NULL;
   const char* no_tag_at_all =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<EnumerationResults><Prefix>pgmoneta/</Prefix><Blobs>"
      "<Blob><Name>pgmoneta/a</Name></Blob></Blobs></EnumerationResults>";

   MCTF_ASSERT(xml_parse_azure_next_marker((char*)no_tag_at_all, &next_marker) == 0, cleanup,
               "parse failed when there is no <NextMarker> element at all");
   MCTF_ASSERT(next_marker == NULL, cleanup,
               "a response with no <NextMarker> element must mean there is no next page");

cleanup:
   free(next_marker);
   MCTF_FINISH();
}

MCTF_INTEGRATION_TEST(test_azure_delete_removes_blobs)
{
   char label[256] = {0};
   char prefix[MAX_PATH];
   char* backup_list = NULL;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(take_backup_and_get_label("primary", label, sizeof(label)) == MCTF_OK, cleanup,
               "could not create backup and resolve its label");

   snprintf(prefix, sizeof(prefix), "pgmoneta/primary/backup/%s/", label);
   MCTF_ASSERT(mctf_se_azure_blob_count_prefix(prefix) > 0, cleanup,
               "no blobs found for the backup before delete");

   MCTF_ASSERT(mctf_se_delete("primary", label) == 0, cleanup, "delete command failed");

   MCTF_ASSERT(mctf_se_azure_blob_count_prefix(prefix) == 0, cleanup,
               "Azure blobs remain after delete");

   MCTF_ASSERT(mctf_se_list_backup("primary", &backup_list) == 0, cleanup,
               "list-backup failed after delete");
   MCTF_ASSERT_PTR_NONNULL(backup_list, cleanup,
                           "empty list-backup response after delete");

   MCTF_ASSERT(strstr(backup_list, label) == NULL, cleanup,
               "deleted backup is still present in list-backup output");

   MCTF_ASSERT(strstr(backup_list, "\"Status\": true") != NULL, cleanup,
               "list-backup did not report success after delete");

cleanup:
   free(backup_list);
   MCTF_FINISH();
}

MCTF_INTEGRATION_TEST(test_azure_delete_one_backup_keeps_the_other)
{
   char first_label[256] = {0};
   char second_label[256] = {0};
   char first_prefix[MAX_PATH];
   char second_prefix[MAX_PATH];
   int second_blob_count = 0;

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

   snprintf(first_prefix, sizeof(first_prefix), "pgmoneta/primary/backup/%s/", first_label);
   snprintf(second_prefix, sizeof(second_prefix), "pgmoneta/primary/backup/%s/", second_label);

   second_blob_count = mctf_se_azure_blob_count_prefix(second_prefix);
   MCTF_ASSERT(second_blob_count > 0, cleanup,
               "second backup has no Azure blobs before delete");

   MCTF_ASSERT(mctf_se_delete("primary", first_label) == 0, cleanup,
               "delete of first backup failed");

   MCTF_ASSERT(mctf_se_azure_blob_count_prefix(first_prefix) == 0, cleanup,
               "first backup blobs remain after delete");

   MCTF_ASSERT(mctf_se_azure_blob_count_prefix(second_prefix) == second_blob_count, cleanup,
               "deleting the first backup changed the number of blobs in the second backup");

cleanup:
   MCTF_FINISH();
}

/*
 * Verify that a backup label which never existed has no blobs in Azure.
 */
MCTF_INTEGRATION_TEST(test_azure_list_nonexistent_backup_is_empty)
{
   char invalid_label[256];

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   pgmoneta_snprintf(invalid_label, sizeof(invalid_label), "%s-invalid", shared_label);

   MCTF_ASSERT(assert_no_azure_blobs("primary", invalid_label) == MCTF_OK, cleanup,
               "a non-existent backup has blobs in Azure");

cleanup:
   MCTF_FINISH();
}

/*
 * azure restore, delete and retain must all refuse a label that does not
 * exist, and report the failure explicitly rather than silently doing
 * nothing. restore needs an extra target directory argument that the other
 * two commands do not take. retain and delete must also fail with their
 * specific "no backup" error codes.
 */
MCTF_INTEGRATION_TEST(test_azure_commands_report_failure_for_invalid_label)
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
         pgmoneta_snprintf(cmd, sizeof(cmd), "azure restore primary %s %s", invalid_label, TEST_RESTORE_DIR);
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

   MCTF_ASSERT(mctf_sh(NULL, "! ls -A %s/workspace | grep -q '^\\.tmp_primary_'", mctf_se_run_dir()) == 0,
               cleanup, "failed azure restore left its staging directory in the workspace");

cleanup:
   free(output);
   MCTF_FINISH();
}

/*
 * retain and expunge must actually flip the Keep flag on the backup, not
 * just return success while leaving the metadata unchanged.
 */
MCTF_INTEGRATION_TEST(test_azure_retain_then_expunge_toggles_keep)
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
 * refused and the Azure blobs must remain untouched. Once expunged, delete
 * must work normally and the blobs must be removed.
 */
MCTF_INTEGRATION_TEST(test_azure_retain_blocks_delete_until_expunged)
{
   char label[256] = {0};
   char prefix[MAX_PATH];
   char cmd[512];
   char* output = NULL;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   MCTF_ASSERT(take_backup_and_get_label("primary", label, sizeof(label)) == MCTF_OK, cleanup,
               "could not create backup and resolve its label");

   snprintf(prefix, sizeof(prefix), "pgmoneta/primary/backup/%s/", label);

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

   MCTF_ASSERT(mctf_se_azure_blob_count_prefix(prefix) > 0, cleanup,
               "retained backup blobs vanished from Azure after refused delete");

   pgmoneta_snprintf(cmd, sizeof(cmd), "expunge primary %s", label);
   MCTF_ASSERT(mctf_se_cli(cmd, NULL) == 0, cleanup, "expunge failed");

   MCTF_ASSERT(mctf_se_delete("primary", label) == 0, cleanup, "delete failed after expunge");

   MCTF_ASSERT(assert_no_azure_blobs("primary", label) == MCTF_OK, cleanup,
               "Azure blobs remain after delete of expunged backup");

cleanup:
   free(output);
   MCTF_FINISH();
}

/*
 * A backup that dies after backup.info is written can already be uploaded,
 * so removing it has to remove it from Azure too.
 */
MCTF_INTEGRATION_TEST(test_azure_incomplete_backup_removed)
{
   char label[256] = {0};
   char newest[256] = {0};
   char backup_dir[MAX_PATH];
   pid_t backup = 0;
   bool throttled = false;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   pgmoneta_snprintf(backup_dir, sizeof(backup_dir), "%s/backup/primary/backup", mctf_se_run_dir());

   MCTF_ASSERT(take_backup_and_get_label("primary", label, sizeof(label)) == MCTF_OK, cleanup,
               "could not create backup and resolve its label");
   MCTF_ASSERT(mctf_sh(NULL, "touch %s/%s.incomplete", backup_dir, label) == 0, cleanup,
               "could not mark the backup as incomplete");

   /* incomplete backups are removed when a backup process dies */
   MCTF_ASSERT(mctf_se_cli("conf set server.primary.max_rate 32768", NULL) == 0, cleanup, "could not slow down backups");
   throttled = true;
   MCTF_ASSERT(mctf_se_cli("backup primary >/dev/null 2>&1 &", NULL) == 0, cleanup, "could not start a backup");

   for (int i = 0; i < 100 && backup <= 0; i++)
   {
      SLEEP(100000000L);
      if (mctf_se_newest_label("primary", newest, sizeof(newest)) == MCTF_OK && strcmp(newest, label) > 0)
      {
         backup = pgmoneta_test_find_process("backup/primary");
      }
   }
   MCTF_ASSERT(backup > 0, cleanup, "the backup did not start");

   MCTF_ASSERT(!kill(backup, SIGKILL), cleanup, "could not kill the backup");
   backup = 0;

   for (int i = 0; i < 300 && (mctf_sh(NULL, "test -e %s/%s", backup_dir, label) == 0 || pgmoneta_test_find_process("cleanup/primary") > 0); i++)
   {
      SLEEP(100000000L);
   }
   MCTF_ASSERT(mctf_sh(NULL, "test -e %s/%s", backup_dir, label) != 0, cleanup, "the incomplete backup is still on disk");
   MCTF_ASSERT(assert_no_azure_blobs("primary", label) == MCTF_OK, cleanup,
               "the incomplete backup is still in Azure");

cleanup:
   if (backup > 0)
   {
      kill(backup, SIGKILL);
   }
   if (throttled)
   {
      mctf_se_cli("conf set server.primary.max_rate 0", NULL);
   }
   MCTF_FINISH();
}

/*
 * Startup removes what a crash of pgmoneta left behind: a backup that was
 * still being taken, a backup whose cleanup had already deleted backup.info,
 * both also from Azure, and a backup directory without backup.info.
 */
MCTF_INTEGRATION_TEST(test_azure_restart_removes_incomplete_backups)
{
   char label[256] = {0};
   char cleaned[256] = {0};
   char backup_dir[MAX_PATH];

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   pgmoneta_snprintf(backup_dir, sizeof(backup_dir), "%s/backup/primary/backup", mctf_se_run_dir());

   MCTF_ASSERT(take_backup_and_get_label("primary", label, sizeof(label)) == MCTF_OK, cleanup,
               "could not create backup and resolve its label");
   MCTF_ASSERT(take_backup_and_get_label("primary", cleaned, sizeof(cleaned)) == MCTF_OK, cleanup,
               "could not create backup and resolve its label");
   MCTF_ASSERT(mctf_sh(NULL, "touch %s/%s.incomplete && mkdir -p %s/20000101000000/data", backup_dir, label, backup_dir) == 0,
               cleanup, "could not create the leftovers of a crash");
   MCTF_ASSERT(mctf_sh(NULL, "echo remote > %s/%s.incomplete && rm %s/%s/backup.info", backup_dir, cleaned, backup_dir, cleaned) == 0,
               cleanup, "could not create the leftovers of an interrupted cleanup");

   MCTF_ASSERT(mctf_se_restart() == MCTF_OK, cleanup, "could not restart pgmoneta");

   MCTF_ASSERT(mctf_sh(NULL, "test -e %s/%s", backup_dir, label) != 0, cleanup, "the incomplete backup is still on disk");
   MCTF_ASSERT(mctf_sh(NULL, "test -e %s/%s.incomplete", backup_dir, label) != 0, cleanup, "the incomplete marker is still on disk");
   MCTF_ASSERT(mctf_sh(NULL, "test -e %s/20000101000000", backup_dir) != 0, cleanup,
               "the backup directory without backup.info is still on disk");
   MCTF_ASSERT(assert_no_azure_blobs("primary", label) == MCTF_OK, cleanup,
               "the incomplete backup is still in Azure");
   MCTF_ASSERT(mctf_sh(NULL, "test -e %s/%s", backup_dir, cleaned) != 0, cleanup, "the cleaned up backup is still on disk");
   MCTF_ASSERT(mctf_sh(NULL, "test -e %s/%s.incomplete", backup_dir, cleaned) != 0, cleanup, "the cleaned up marker is still on disk");
   MCTF_ASSERT(assert_no_azure_blobs("primary", cleaned) == MCTF_OK, cleanup,
               "the cleaned up backup is still in Azure");
   MCTF_ASSERT(mctf_se_has_local_metadata("primary"), cleanup, "a complete backup was removed");

cleanup:
   MCTF_FINISH();
}

MCTF_INTEGRATION_TEST(test_azure_retention_removes_incomplete_backups)
{
   char label[256] = {0};
   char complete[256] = {0};
   char backup_dir[MAX_PATH];
   char conf[MAX_PATH];
   bool changed = false;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");

   pgmoneta_snprintf(backup_dir, sizeof(backup_dir), "%s/backup/primary/backup", mctf_se_run_dir());
   pgmoneta_snprintf(conf, sizeof(conf), "%s/pgmoneta.conf", mctf_se_run_dir());

   MCTF_ASSERT(take_backup_and_get_label("primary", complete, sizeof(complete)) == MCTF_OK, cleanup,
               "could not create backup and resolve its label");
   MCTF_ASSERT(take_backup_and_get_label("primary", label, sizeof(label)) == MCTF_OK, cleanup,
               "could not create backup and resolve its label");

   MCTF_ASSERT(mctf_sh(NULL, "sed -i 's/^\\[pgmoneta\\]$/[pgmoneta]\\nretention_interval = 1/' %s", conf) == 0, cleanup,
               "could not set retention_interval");
   changed = true;
   MCTF_ASSERT(mctf_se_restart() == MCTF_OK, cleanup, "could not restart pgmoneta");

   /* pgmoneta keeps running, so only retention can retry the cleanup */
   MCTF_ASSERT(mctf_sh(NULL, "echo remote > %s/%s.incomplete && rm %s/%s/backup.info", backup_dir, label, backup_dir, label) == 0,
               cleanup, "could not create the leftovers of an interrupted cleanup");
   MCTF_ASSERT(mctf_sh(NULL, "mkdir -p %s/20000101000000/data", backup_dir) == 0, cleanup,
               "could not create a backup directory without backup.info");

   for (int i = 0; i < 30 && mctf_sh(NULL, "test -e %s/%s.incomplete", backup_dir, label) == 0; i++)
   {
      sleep(1);
   }

   MCTF_ASSERT(mctf_sh(NULL, "test -e %s/%s.incomplete", backup_dir, label) != 0, cleanup, "retention did not remove the incomplete marker");
   MCTF_ASSERT(mctf_sh(NULL, "test -e %s/%s", backup_dir, label) != 0, cleanup, "retention did not remove the incomplete backup");
   MCTF_ASSERT(mctf_sh(NULL, "test -e %s/20000101000000", backup_dir) != 0, cleanup,
               "retention did not remove the backup directory without backup.info");
   MCTF_ASSERT(assert_no_azure_blobs("primary", label) == MCTF_OK, cleanup,
               "the incomplete backup is still in Azure");
   MCTF_ASSERT(mctf_sh(NULL, "test -e %s/%s/backup.info", backup_dir, complete) == 0, cleanup, "a complete backup was removed");

cleanup:
   if (changed)
   {
      mctf_sh(NULL, "sed -i '/^retention_interval = 1$/d' %s", conf);
      mctf_se_restart();
   }
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

/*
 * Deleting a backup must remove its blobs from Azure too, or retention leaves
 * them behind for good. A fresh backup is used so the shared one stays intact,
 * and the shared prefix is checked to pin that only the deleted label goes.
 */
MCTF_INTEGRATION_TEST(test_azure_delete_removes_remote_blobs)
{
   char label[256];
   char prefix[MAX_PATH];
   char shared_prefix[MAX_PATH];
   int before;

   if (storage_status == MCTF_SKIPPED)
   {
      MCTF_SKIP("no container engine / test environment");
   }
   MCTF_ASSERT(storage_status == MCTF_OK, cleanup, "storage backend setup failed");
   MCTF_ASSERT(shared_label[0] != '\0', cleanup, "no backup label available");

   MCTF_ASSERT(mctf_se_backup("primary") == 0, cleanup, "backup to Azure failed");
   MCTF_ASSERT(mctf_se_newest_label("primary", label, sizeof(label)) == MCTF_OK, cleanup,
               "no label for the new backup");
   MCTF_ASSERT(strcmp(label, shared_label) != 0, cleanup, "the new backup reused label %s", label);

   snprintf(prefix, sizeof(prefix), "pgmoneta/primary/backup/%s/", label);
   snprintf(shared_prefix, sizeof(shared_prefix), "pgmoneta/primary/backup/%s/", shared_label);

   before = mctf_se_azure_blob_count_prefix(prefix);
   MCTF_ASSERT(before > 0, cleanup, "no blobs under %s after the backup", prefix);

   MCTF_ASSERT(mctf_se_delete("primary", label) == 0, cleanup, "delete of %s failed", label);

   MCTF_ASSERT_INT_EQ(mctf_se_azure_blob_count_prefix(prefix), 0, cleanup,
                      "%s still holds blobs after deleting %s (%d before)", prefix, label, before);
   MCTF_ASSERT(mctf_se_azure_blob_count_prefix(shared_prefix) > 0, cleanup,
               "deleting %s also removed the blobs of %s", label, shared_label);

cleanup:
   MCTF_FINISH();
}
