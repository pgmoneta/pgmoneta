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
#include <json.h>
#include <management.h>
#include <mctf.h>
#include <tsclient.h>
#include <tsclient_helpers.h>
#include <tscommon.h>
#include <utils.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int
verify_chain_pg_version(void)
{
   char* v = getenv("TEST_PG_VERSION");

   if (v == NULL || strlen(v) == 0)
   {
      return 0;
   }

   return atoi(v);
}

/*
 * Verify must treat the integrity of an incremental backup's parent chain as a
 * precondition: an intact chain verifies, and an incremental whose parent is
 * gone must fail rather than silently attempt an impossible reconstruction.
 */
MCTF_TEST_NEGATIVE(test_verify_incremental_chain_precondition)
{
   struct json* list_response = NULL;
   struct json* b0 = NULL;
   struct json* b1 = NULL;
   char* full_label = NULL;
   char* incr_label = NULL;
   char* backup_dir = NULL;
   char* full_dir = NULL;
   char va[MAX_PATH];
   char vb[MAX_PATH];

   memset(va, 0, sizeof(va));
   memset(vb, 0, sizeof(vb));

   pgmoneta_test_setup();

   if (verify_chain_pg_version() < 17)
   {
      MCTF_SKIP("incremental backup chain verification runs on PostgreSQL 17+; TEST_PG_VERSION=%s",
                getenv("TEST_PG_VERSION") != NULL ? getenv("TEST_PG_VERSION") : "(unset)");
   }

   /* FULL -> INCREMENTAL -> INCREMENTAL */
   MCTF_ASSERT(pgmoneta_test_add_backup_chain() == 0, cleanup, "backup chain setup failed");

   MCTF_ASSERT(pgmoneta_tsclient_list_backup("primary", NULL, &list_response, 0) == 0, cleanup,
               "list backup failed");
   MCTF_ASSERT(pgmoneta_tsclient_get_backup_count(list_response) >= 2, cleanup,
               "need at least a full and an incremental backup");

   b0 = pgmoneta_tsclient_get_backup(list_response, 0);
   b1 = pgmoneta_tsclient_get_backup(list_response, 1);
   MCTF_ASSERT_PTR_NONNULL(b0, cleanup, "full backup null");
   MCTF_ASSERT_PTR_NONNULL(b1, cleanup, "incremental backup null");

   full_label = strdup(pgmoneta_tsclient_get_backup_label(b0));
   incr_label = strdup(pgmoneta_tsclient_get_backup_label(b1));
   MCTF_ASSERT_PTR_NONNULL(full_label, cleanup, "full label null");
   MCTF_ASSERT_PTR_NONNULL(incr_label, cleanup, "incremental label null");

   /* Intact chain: verifying the incremental succeeds. */
   snprintf(va, sizeof(va), "/tmp/pgmoneta-vchain-a-%d", (int)getpid());
   MCTF_ASSERT(pgmoneta_tsclient_verify("primary", incr_label, va, NULL, NULL, 0) == 0, cleanup,
               "verify of an intact incremental chain should succeed");
   pgmoneta_delete_directory(va);

   /* Break the chain by removing the full (root) backup. */
   backup_dir = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(backup_dir, cleanup, "backup directory null");
   full_dir = pgmoneta_append(full_dir, backup_dir);
   full_dir = pgmoneta_append(full_dir, full_label);
   MCTF_ASSERT_INT_EQ(pgmoneta_delete_directory(full_dir), 0, cleanup,
                      "failed to remove the full backup to break the chain");

   /* Broken chain: verifying the incremental must now fail. */
   snprintf(vb, sizeof(vb), "/tmp/pgmoneta-vchain-b-%d", (int)getpid());
   MCTF_ASSERT(pgmoneta_tsclient_verify("primary", incr_label, vb, NULL, NULL,
                                        MANAGEMENT_ERROR_VERIFY_ERROR) == 0,
               cleanup,
               "verify of an incremental whose parent is missing should fail");
   pgmoneta_delete_directory(vb);

cleanup:
   free(full_label);
   free(incr_label);
   free(backup_dir);
   free(full_dir);
   if (list_response != NULL)
   {
      pgmoneta_json_destroy(list_response);
   }

   /* This test deletes the full backup to break the chain, leaving orphaned
    * incrementals behind. Reset the base dir so later tests start from a clean,
    * intact backup store. */
   pgmoneta_test_basedir_cleanup();

   MCTF_FINISH();
}
