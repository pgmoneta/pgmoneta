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

#include <pgmoneta.h>
#include <aes.h>
#include <art.h>
#include <compression.h>
#include <configuration.h>
#include <deque.h>
#include <mctf.h>
#include <progress.h>
#include <shmem.h>
#include <storage.h>
#include <tscommon.h>
#include <utils.h>
#include <workflow.h>
#include <workflow_funcs.h>

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool shmem_allocated = false;

MCTF_MODULE_SETUP(progress)
{
   if (shmem == NULL)
   {
      pgmoneta_create_shared_memory(sizeof(struct main_configuration), HUGEPAGE_OFF, &shmem);
      memset(shmem, 0, sizeof(struct main_configuration));
      shmem_allocated = true;
   }
}

MCTF_MODULE_TEARDOWN(progress)
{
   if (shmem_allocated && shmem != NULL)
   {
      pgmoneta_destroy_shared_memory(shmem, sizeof(struct main_configuration));
      shmem = NULL;
      shmem_allocated = false;
   }
}

MCTF_TEST_SETUP(progress)
{
   pgmoneta_test_config_save();
   pgmoneta_memory_init();
}

MCTF_TEST_TEARDOWN(progress)
{
   pgmoneta_memory_destroy();
   pgmoneta_test_config_restore();
}

MCTF_TEST(test_progress_is_enabled)
{
   struct main_configuration* config;

   /* conf/01: server-level progress = on */
   MCTF_ASSERT_INT_EQ(pgmoneta_test_load_conf(TEST_CONF_DIR "/progress/01.conf"), 0,
                      cleanup, "failed to read 01.conf");
   MCTF_ASSERT(pgmoneta_is_progress_enabled(0) == true,
               cleanup, "01.conf: server progress=on should be enabled");

   /* conf/02: server-level progress = off */
   MCTF_ASSERT_INT_EQ(pgmoneta_test_load_conf(TEST_CONF_DIR "/progress/02.conf"), 0,
                      cleanup, "failed to read 02.conf");
   MCTF_ASSERT(pgmoneta_is_progress_enabled(0) == false,
               cleanup, "02.conf: server progress=off should be disabled");

   /* conf/03: global progress = on, server inherits (-1) */
   MCTF_ASSERT_INT_EQ(pgmoneta_test_load_conf(TEST_CONF_DIR "/progress/03.conf"), 0,
                      cleanup, "failed to read 03.conf");
   config = (struct main_configuration*)shmem;
   MCTF_ASSERT_INT_EQ(config->common.servers[0].progress_enabled, -1,
                      cleanup, "03.conf: server progress should be -1 (inherit)");
   MCTF_ASSERT(pgmoneta_is_progress_enabled(0) == true,
               cleanup, "03.conf: global progress=on should be inherited");

   /* conf/04: global progress = off, server inherits (-1) */
   MCTF_ASSERT_INT_EQ(pgmoneta_test_load_conf(TEST_CONF_DIR "/progress/04.conf"), 0,
                      cleanup, "failed to read 04.conf");
   config = (struct main_configuration*)shmem;
   MCTF_ASSERT_INT_EQ(config->common.servers[0].progress_enabled, -1,
                      cleanup, "04.conf: server progress should be -1 (inherit)");
   MCTF_ASSERT(pgmoneta_is_progress_enabled(0) == false,
               cleanup, "04.conf: global progress=off should be inherited");

   /* conf/05: global progress = off, server progress = on (server overrides global) */
   MCTF_ASSERT_INT_EQ(pgmoneta_test_load_conf(TEST_CONF_DIR "/progress/05.conf"), 0,
                      cleanup, "failed to read 05.conf");
   config = (struct main_configuration*)shmem;
   MCTF_ASSERT_INT_EQ(config->progress, false,
                      cleanup, "05.conf: global progress should be off");
   MCTF_ASSERT_INT_EQ(config->common.servers[0].progress_enabled, 1,
                      cleanup, "05.conf: server progress should be 1");
   MCTF_ASSERT(pgmoneta_is_progress_enabled(0) == true,
               cleanup, "05.conf: server progress=on should override global off");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_progress_next_phase)
{
   struct progress* p;
   struct main_configuration* config;
   struct art* nodes = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_test_load_conf(TEST_CONF_DIR "/progress/01.conf"), 0,
                      cleanup, "failed to read 01.conf");

   config = (struct main_configuration*)shmem;
   p = &config->common.servers[0].progress;

   pgmoneta_art_create(&nodes);
   pgmoneta_art_insert(nodes, NODE_PROGRESS_LIMIT_COMPRESSION, (uintptr_t)42, ValueInt32);
   pgmoneta_art_insert(nodes, NODE_PROGRESS_LIMIT_ENCRYPTION, (uintptr_t)85, ValueInt32);

   MCTF_ASSERT(pgmoneta_is_progress_enabled(0) == true,
               cleanup, "progress should be enabled");

   atomic_store(&p->state, WORKFLOW_PROGRESS_RUNNING);
   atomic_store(&p->percentage, 0);
   atomic_store(&p->current_phase, PHASE_NONE);

   pgmoneta_progress_next_phase(0, PHASE_COMPRESSION, nodes);

   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->phase_limit), 42,
                      cleanup, "phase_limit should be 42 after update");
   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->current_phase), PHASE_COMPRESSION,
                      cleanup, "phase should be COMPRESSION after update");

   atomic_store(&p->percentage, 42); // Simulate pct reached 42
   pgmoneta_progress_next_phase(0, PHASE_ENCRYPTION, nodes);

   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->phase_limit), 85,
                      cleanup, "phase_limit should be 85 after second update");
   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->current_phase), PHASE_ENCRYPTION,
                      cleanup, "phase should be ENCRYPTION after second update");

   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->state), WORKFLOW_PROGRESS_RUNNING,
                      cleanup, "state should remain RUNNING across updates");

cleanup:
   if (nodes != NULL)
   {
      pgmoneta_art_destroy(nodes);
   }
   MCTF_FINISH();
}

MCTF_TEST(test_progress_teardown)
{
   struct main_configuration* config;
   struct progress* p;

   MCTF_ASSERT_INT_EQ(pgmoneta_test_load_conf(TEST_CONF_DIR "/progress/01.conf"), 0,
                      cleanup, "failed to read 01.conf");

   config = (struct main_configuration*)shmem;
   p = &config->common.servers[0].progress;

   atomic_store(&p->state, WORKFLOW_PROGRESS_RUNNING);
   atomic_store(&p->percentage, 55);
   atomic_store(&p->current_phase, PHASE_SHA512);
   atomic_store(&p->done, 100);
   atomic_store(&p->total, 200);
   atomic_store(&p->prev_phase_limit, 10);
   atomic_store(&p->phase_limit, 80);

   pgmoneta_progress_teardown(0);

   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->state), WORKFLOW_PROGRESS_NONE,
                      cleanup, "state should be NONE after reset");
   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->percentage), 0,
                      cleanup, "percentage should be 0 after reset");
   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->current_phase), PHASE_NONE,
                      cleanup, "phase should be NONE after reset");
   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->done), 0,
                      cleanup, "done should be 0 after reset");
   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->total), 0,
                      cleanup, "total should be 0 after reset");
   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->prev_phase_limit), 0,
                      cleanup, "prev_phase_limit should be 0 after reset");
   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->phase_limit), 0,
                      cleanup, "phase_limit should be 0 after reset");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_progress_setup_disabled)
{
   struct main_configuration* config;
   struct art* nodes = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_test_load_conf(TEST_CONF_DIR "/progress/02.conf"), 0,
                      cleanup, "failed to read 02.conf");

   config = (struct main_configuration*)shmem;

   MCTF_ASSERT(pgmoneta_is_progress_enabled(0) == false,
               cleanup, "progress should be disabled from 02.conf");

   atomic_store(&config->common.servers[0].progress.state, WORKFLOW_PROGRESS_NONE);
   atomic_store(&config->common.servers[0].progress.percentage, 0);

   pgmoneta_art_create(&nodes);

   pgmoneta_progress_setup(0, NULL, nodes, WORKFLOW_TYPE_BACKUP);

   MCTF_ASSERT_INT_EQ((int)atomic_load(&config->common.servers[0].progress.state),
                      WORKFLOW_PROGRESS_NONE, cleanup, "state should remain NONE when progress disabled");
   MCTF_ASSERT_INT_EQ((int)atomic_load(&config->common.servers[0].progress.percentage),
                      0, cleanup, "percentage should remain 0 when progress disabled");

cleanup:
   pgmoneta_art_destroy(nodes);
   MCTF_FINISH();
}

MCTF_TEST(test_progress_compression_end_to_end)
{
   char template[] = "/tmp/pgmoneta_test_compress_XXXXXX";
   char* tmpdir = mkdtemp(template);
   char filepath[256];
   struct main_configuration* config;
   struct progress* p;
   struct deque* excludes = NULL;
   int file_count;
   int final_done;
   int final_pct;

   MCTF_ASSERT_INT_EQ(pgmoneta_test_load_conf(TEST_CONF_DIR "/progress/01.conf"), 0,
                      cleanup, "failed to read 01.conf");

   config = (struct main_configuration*)shmem;
   p = &config->common.servers[0].progress;

   MCTF_ASSERT(pgmoneta_is_progress_enabled(0) == true,
               cleanup, "progress should be enabled from 01.conf");

   MCTF_ASSERT_PTR_NONNULL(tmpdir, cleanup, "mkdtemp failed");

   for (int i = 0; i < 4; i++)
   {
      FILE* f;
      pgmoneta_snprintf(filepath, sizeof(filepath), "%s/data%d.txt", tmpdir, i);
      f = fopen(filepath, "w");
      MCTF_ASSERT_PTR_NONNULL(f, cleanup, "fopen failed");
      for (int j = 0; j < 100; j++)
      {
         fprintf(f, "line %d of file %d - padding to make compressible content\n", j, i);
      }
      fclose(f);
   }

   file_count = pgmoneta_count_files(tmpdir);
   MCTF_ASSERT_INT_EQ(file_count, 4, cleanup, "should have 4 files");

   atomic_store(&p->state, WORKFLOW_PROGRESS_RUNNING);
   atomic_store(&p->done, 0);
   atomic_store(&p->total, file_count);
   atomic_store(&p->prev_phase_limit, 20);
   atomic_store(&p->phase_limit, 60);
   atomic_store(&p->percentage, 20);

   pgmoneta_deque_create(true, &excludes);

   MCTF_ASSERT_INT_EQ(
      pgmoneta_compress_directory(0, tmpdir, COMPRESSION_SERVER_GZIP, NULL, excludes),
      0, cleanup, "compress_directory failed");

   final_done = (int)atomic_load(&p->done);
   final_pct = (int)atomic_load(&p->percentage);

   MCTF_ASSERT_INT_EQ(final_done, 4, cleanup, "done should equal file count after compression");

   /* 20 + 4/4 * (60 - 20) = 60 */
   MCTF_ASSERT_INT_EQ(final_pct, 60, cleanup, "percentage should reach phase_limit after full compression");

   MCTF_ASSERT(final_pct >= 20, cleanup, "percentage must not go below prev_phase_limit");
   MCTF_ASSERT(final_pct <= 60, cleanup, "percentage must not exceed phase_limit");

cleanup:
   pgmoneta_deque_destroy(excludes);
   if (tmpdir != NULL)
   {
      pgmoneta_delete_directory(tmpdir);
   }
   MCTF_FINISH();
}

MCTF_TEST(test_progress_encryption_end_to_end)
{
   struct test_encryption_env enc_env;
   char template[] = "/tmp/pgmoneta_test_encrypt_XXXXXX";
   char* tmpdir = mkdtemp(template);
   char filepath[256];
   char aes_path[256];
   struct main_configuration* config;
   struct progress* p;
   struct deque* excludes = NULL;
   int file_count;
   int final_done;
   int final_pct;

   MCTF_ASSERT_INT_EQ(pgmoneta_test_load_conf(TEST_CONF_DIR "/progress/01.conf"), 0,
                      cleanup, "failed to read 01.conf");

   config = (struct main_configuration*)shmem;
   p = &config->common.servers[0].progress;

   MCTF_ASSERT_INT_EQ(pgmoneta_test_setup_encryption_env(&enc_env),
                      0, cleanup, "failed to set up encryption env");

   MCTF_ASSERT(pgmoneta_is_progress_enabled(0) == true,
               cleanup, "progress should be enabled from 01.conf");

   MCTF_ASSERT_PTR_NONNULL(tmpdir, cleanup, "mkdtemp failed");

   for (int i = 0; i < 4; i++)
   {
      FILE* f;
      pgmoneta_snprintf(filepath, sizeof(filepath), "%s/data%d.txt", tmpdir, i);
      f = fopen(filepath, "w");
      MCTF_ASSERT_PTR_NONNULL(f, cleanup, "fopen failed");
      for (int j = 0; j < 100; j++)
      {
         fprintf(f, "line %d of file %d - content for encryption test\n", j, i);
      }
      fclose(f);
   }

   file_count = pgmoneta_count_files(tmpdir);
   MCTF_ASSERT_INT_EQ(file_count, 4, cleanup, "should have 4 files");

   atomic_store(&p->state, WORKFLOW_PROGRESS_RUNNING);
   atomic_store(&p->done, 0);
   atomic_store(&p->total, file_count);
   atomic_store(&p->prev_phase_limit, 60);
   atomic_store(&p->phase_limit, 80);
   atomic_store(&p->percentage, 60);

   pgmoneta_deque_create(true, &excludes);

   MCTF_ASSERT_INT_EQ(
      pgmoneta_encrypt_directory(0, tmpdir, NULL, excludes),
      0, cleanup, "encrypt_directory failed");

   final_done = (int)atomic_load(&p->done);
   final_pct = (int)atomic_load(&p->percentage);

   MCTF_ASSERT_INT_EQ(final_done, 4, cleanup, "done should equal file count after encryption");

   /* 60 + 4/4 * (80 - 60) = 80 */
   MCTF_ASSERT_INT_EQ(final_pct, 80, cleanup, "percentage should reach phase_limit after full encryption");

   MCTF_ASSERT(final_pct >= 60, cleanup, "percentage must not go below prev_phase_limit");
   MCTF_ASSERT(final_pct <= 80, cleanup, "percentage must not exceed phase_limit");

   for (int i = 0; i < 4; i++)
   {
      pgmoneta_snprintf(aes_path, sizeof(aes_path), "%s/data%d.txt.aes", tmpdir, i);
      MCTF_ASSERT(pgmoneta_exists(aes_path), cleanup, ".aes file should exist after encryption");
   }

cleanup:
   pgmoneta_deque_destroy(excludes);
   pgmoneta_test_teardown_encryption_env(&enc_env);
   if (tmpdir != NULL)
   {
      pgmoneta_delete_directory(tmpdir);
   }
   MCTF_FINISH();
}

MCTF_TEST(test_progress_setup)
{
   struct art* nodes = NULL;
   struct main_configuration* config;
   struct workflow* head = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_test_load_conf(TEST_CONF_DIR "/progress/02.conf"), 0,
                      cleanup, "failed to read 02.conf");

   config = (struct main_configuration*)shmem;

   /* Enable progress explicitly for this test */
   config->common.servers[0].progress_enabled = true;

   pgmoneta_art_create(&nodes);

   MCTF_ASSERT(pgmoneta_is_progress_enabled(0) == true,
               cleanup, "progress was not enabled properly");

   head = pgmoneta_create_basebackup();
   struct workflow* current = head;

   current->next = pgmoneta_create_manifest();
   current = current->next;

   current->next = pgmoneta_create_link();
   current = current->next;

   current->next = pgmoneta_create_sha512();

   /* Add required node for ART assertions */
   pgmoneta_art_insert(nodes, NODE_SERVER_BACKUP, (uintptr_t)"mock_backup", ValueString);

   /* Test fallback weights for WORKFLOW_TYPE_BACKUP */
   pgmoneta_progress_setup(0, head, nodes, WORKFLOW_TYPE_BACKUP);

   /* Full backup weights: BASEBACKUP(77), MANIFEST(7), LINKING(2), SHA512(14) */
   MCTF_ASSERT_INT_EQ((int)(uintptr_t)pgmoneta_art_search(nodes, NODE_PROGRESS_LIMIT_BACKUP), 77,
                      cleanup, "backup limit should be 77");
   MCTF_ASSERT_INT_EQ((int)(uintptr_t)pgmoneta_art_search(nodes, NODE_PROGRESS_LIMIT_MANIFEST), 84,
                      cleanup, "manifest limit should be 84");
   MCTF_ASSERT_INT_EQ((int)(uintptr_t)pgmoneta_art_search(nodes, NODE_PROGRESS_LIMIT_LINK), 86,
                      cleanup, "link limit should be 86");
   MCTF_ASSERT_INT_EQ((int)(uintptr_t)pgmoneta_art_search(nodes, NODE_PROGRESS_LIMIT_SHA512), 100,
                      cleanup, "sha512 limit should be 100");

cleanup:
   if (head != NULL)
   {
      pgmoneta_workflow_destroy(head);
   }
   pgmoneta_art_destroy(nodes);
   MCTF_FINISH();
}

MCTF_TEST(test_progress_phase_mapping_for_s3)
{
   MCTF_ASSERT_INT_EQ(pgmoneta_progress_phase_from_workflow_name(PHASE_NAME_INFO), PHASE_INFO,
                      cleanup, "info phase name should map to PHASE_INFO");
   MCTF_ASSERT_INT_EQ(pgmoneta_progress_phase_from_workflow_name(PHASE_NAME_DELETE), PHASE_DELETE,
                      cleanup, "delete phase name should map to PHASE_DELETE");
   MCTF_ASSERT_INT_EQ(pgmoneta_progress_phase_from_workflow_name(PHASE_NAME_RESTORE), PHASE_RESTORE,
                      cleanup, "restore phase name should map to PHASE_RESTORE");
   MCTF_ASSERT_INT_EQ(pgmoneta_progress_phase_from_workflow_name(PHASE_NAME_VERIFY), PHASE_VERIFY,
                      cleanup, "verify phase name should map to PHASE_VERIFY");

   MCTF_ASSERT_STR_EQ(pgmoneta_progress_limit_node_key(PHASE_INFO), NODE_PROGRESS_LIMIT_INFO,
                      cleanup, "info phase should use the info node key");
   MCTF_ASSERT_STR_EQ(pgmoneta_progress_limit_node_key(PHASE_DELETE), NODE_PROGRESS_LIMIT_DELETE,
                      cleanup, "delete phase should use the delete node key");
   MCTF_ASSERT_STR_EQ(pgmoneta_progress_limit_node_key(PHASE_RESTORE), NODE_PROGRESS_LIMIT_RESTORE,
                      cleanup, "restore phase should use the restore node key");
   MCTF_ASSERT_STR_EQ(pgmoneta_progress_limit_node_key(PHASE_VERIFY), NODE_PROGRESS_LIMIT_VERIFY,
                      cleanup, "verify phase should use the verify node key");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_progress_setup_s3_workflows)
{
   struct art* nodes = NULL;
   struct main_configuration* config;
   struct workflow* workflow = NULL;
   struct progress* p;

   MCTF_ASSERT_INT_EQ(pgmoneta_test_load_conf(TEST_CONF_DIR "/progress/02.conf"), 0,
                      cleanup, "failed to read 02.conf");

   config = (struct main_configuration*)shmem;
   config->common.servers[0].progress_enabled = true;
   config->storage_engine = STORAGE_ENGINE_S3;
   p = &config->common.servers[0].progress;

   MCTF_ASSERT_INT_EQ(pgmoneta_art_create(&nodes), 0,
                      cleanup, "failed to create art");

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_S3_LIST, NULL);
   MCTF_ASSERT_PTR_NONNULL(workflow, cleanup, "failed to create s3 list workflow");
   pgmoneta_progress_setup(0, workflow, nodes, WORKFLOW_TYPE_S3_LIST);

   MCTF_ASSERT_INT_EQ((int)(uintptr_t)pgmoneta_art_search(nodes, NODE_PROGRESS_LIMIT_INFO), 100,
                      cleanup, "s3 list should reserve 100 percent for info");
   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->current_phase), PHASE_INFO,
                      cleanup, "s3 list should start in info phase");
   pgmoneta_progress_teardown(0);
   pgmoneta_workflow_destroy(workflow);
   workflow = NULL;
   pgmoneta_art_destroy(nodes);
   nodes = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_art_create(&nodes), 0,
                      cleanup, "failed to recreate art");
   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_S3_DELETE, NULL);
   MCTF_ASSERT_PTR_NONNULL(workflow, cleanup, "failed to create s3 delete workflow");
   pgmoneta_progress_setup(0, workflow, nodes, WORKFLOW_TYPE_S3_DELETE);

   MCTF_ASSERT_INT_EQ((int)(uintptr_t)pgmoneta_art_search(nodes, NODE_PROGRESS_LIMIT_DELETE), 100,
                      cleanup, "s3 delete should reserve 100 percent for delete");
   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->current_phase), PHASE_DELETE,
                      cleanup, "s3 delete should start in delete phase");
   pgmoneta_progress_teardown(0);
   pgmoneta_workflow_destroy(workflow);
   workflow = NULL;
   pgmoneta_art_destroy(nodes);
   nodes = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_art_create(&nodes), 0,
                      cleanup, "failed to recreate art again");
   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_S3_RESTORE, NULL);
   MCTF_ASSERT_PTR_NONNULL(workflow, cleanup, "failed to create s3 restore workflow");
   pgmoneta_progress_setup(0, workflow, nodes, WORKFLOW_TYPE_S3_RESTORE);

   MCTF_ASSERT_INT_EQ((int)(uintptr_t)pgmoneta_art_search(nodes, NODE_PROGRESS_LIMIT_RESTORE), 100,
                      cleanup, "s3 restore should reserve 100 percent for restore");
   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->current_phase), PHASE_RESTORE,
                      cleanup, "s3 restore should start in restore phase");

cleanup:
   if (workflow != NULL)
   {
      pgmoneta_workflow_destroy(workflow);
   }
   if (nodes != NULL)
   {
      pgmoneta_art_destroy(nodes);
   }
   MCTF_FINISH();
}

MCTF_TEST(test_progress_increment)
{
   struct main_configuration* config;
   struct progress* p;

   MCTF_ASSERT_INT_EQ(pgmoneta_test_load_conf(TEST_CONF_DIR "/progress/01.conf"), 0,
                      cleanup, "failed to read 01.conf");

   config = (struct main_configuration*)shmem;
   p = &config->common.servers[0].progress;

   atomic_store(&p->state, WORKFLOW_PROGRESS_RUNNING);
   atomic_store(&p->done, 10);
   atomic_store(&p->total, 100);
   atomic_store(&p->prev_phase_limit, 0);
   atomic_store(&p->phase_limit, 50);

   /* Test pgmoneta_progress_increment */
   pgmoneta_progress_increment(0, 5);

   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->done), 15,
                      cleanup, "done should be incremented to 15");

   /* 0 + 15/100 * (50 - 0) = 7.5 -> 7 */
   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->percentage), 7,
                      cleanup, "percentage should be 7 after increment");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_progress_report)
{
   struct main_configuration* config;
   struct progress* p;

   MCTF_ASSERT_INT_EQ(pgmoneta_test_load_conf(TEST_CONF_DIR "/progress/01.conf"), 0,
                      cleanup, "failed to read 01.conf");

   config = (struct main_configuration*)shmem;
   p = &config->common.servers[0].progress;

   atomic_store(&p->state, WORKFLOW_PROGRESS_RUNNING);
   atomic_store(&p->done, 50);
   atomic_store(&p->total, 100);
   atomic_store(&p->prev_phase_limit, 0);
   atomic_store(&p->phase_limit, 50);

   /* Test pgmoneta_progress_report individually */
   pgmoneta_progress_report(0);

   /* 0 + 50/100 * (50 - 0) = 25 */
   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->percentage), 25,
                      cleanup, "percentage should be 25 after report");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_progress_set_total)
{
   struct main_configuration* config;
   struct progress* p;

   MCTF_ASSERT_INT_EQ(pgmoneta_test_load_conf(TEST_CONF_DIR "/progress/01.conf"), 0,
                      cleanup, "failed to read 01.conf");

   config = (struct main_configuration*)shmem;
   p = &config->common.servers[0].progress;

   atomic_store(&p->state, WORKFLOW_PROGRESS_RUNNING);
   atomic_store(&p->total, 0);

   pgmoneta_progress_set_total(0, 1024);

   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->total), 1024,
                      cleanup, "total should be 1024");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_progress_get_total)
{
   struct main_configuration* config;
   struct progress* p;

   MCTF_ASSERT_INT_EQ(pgmoneta_test_load_conf(TEST_CONF_DIR "/progress/01.conf"), 0,
                      cleanup, "failed to read 01.conf");

   config = (struct main_configuration*)shmem;
   p = &config->common.servers[0].progress;

   atomic_store(&p->state, WORKFLOW_PROGRESS_RUNNING);
   atomic_store(&p->total, 2048);

   int64_t total = pgmoneta_progress_get_total(0);
   MCTF_ASSERT_INT_EQ((int)total, 2048,
                      cleanup, "get_total should return 2048");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_progress_update_done)
{
   struct main_configuration* config;
   struct progress* p;

   MCTF_ASSERT_INT_EQ(pgmoneta_test_load_conf(TEST_CONF_DIR "/progress/01.conf"), 0,
                      cleanup, "failed to read 01.conf");

   config = (struct main_configuration*)shmem;
   p = &config->common.servers[0].progress;

   atomic_store(&p->state, WORKFLOW_PROGRESS_RUNNING);
   atomic_store(&p->total, 200);
   atomic_store(&p->done, 0);
   atomic_store(&p->prev_phase_limit, 50);
   atomic_store(&p->phase_limit, 100);

   pgmoneta_progress_update_done(0, 100);

   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->done), 100,
                      cleanup, "done should be updated to 100");

   /* 50 + 100/200 * (100 - 50) = 50 + 25 = 75 */
   MCTF_ASSERT_INT_EQ((int)atomic_load(&p->percentage), 75,
                      cleanup, "percentage should be 75 after update_done");

cleanup:
   MCTF_FINISH();
}
