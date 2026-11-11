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
verify_deep_pg_version(void)
{
   char* v = getenv("TEST_PG_VERSION");

   if (v == NULL || strlen(v) == 0)
   {
      return 0;
   }

   return atoi(v);
}

/*
 * Opt-in deep verify of an incremental backup should reconstruct the whole
 * chain and succeed on an intact chain. This exercises the actual combine of
 * INCREMENTAL.* files, not just the metadata precondition.
 */
MCTF_TEST(test_verify_deep_reconstructs_incremental_chain)
{
   struct json* list_response = NULL;
   struct json* b1 = NULL;
   char* incr_label = NULL;
   char vd[MAX_PATH];

   memset(vd, 0, sizeof(vd));

   pgmoneta_test_setup();

   if (verify_deep_pg_version() < 17)
   {
      MCTF_SKIP("deep incremental verify runs on PostgreSQL 17+; TEST_PG_VERSION=%s",
                getenv("TEST_PG_VERSION") != NULL ? getenv("TEST_PG_VERSION") : "(unset)");
   }

   /* FULL -> INCREMENTAL -> INCREMENTAL */
   MCTF_ASSERT(pgmoneta_test_add_backup_chain() == 0, cleanup, "backup chain setup failed");

   MCTF_ASSERT(pgmoneta_tsclient_list_backup("primary", NULL, &list_response, 0) == 0, cleanup,
               "list backup failed");
   MCTF_ASSERT(pgmoneta_tsclient_get_backup_count(list_response) >= 2, cleanup,
               "need at least a full and an incremental backup");

   b1 = pgmoneta_tsclient_get_backup(list_response, 1);
   MCTF_ASSERT_PTR_NONNULL(b1, cleanup, "incremental backup null");
   incr_label = strdup(pgmoneta_tsclient_get_backup_label(b1));
   MCTF_ASSERT_PTR_NONNULL(incr_label, cleanup, "incremental label null");

   /* Deep verify of the intact incremental must reconstruct and succeed. */
   snprintf(vd, sizeof(vd), "/tmp/pgmoneta-vdeep-%d", (int)getpid());
   MCTF_ASSERT(pgmoneta_tsclient_verify_deep("primary", incr_label, vd, NULL, NULL, 0) == 0, cleanup,
               "deep verify of an intact incremental chain should succeed");
   pgmoneta_delete_directory(vd);

cleanup:
   free(incr_label);
   if (list_response != NULL)
   {
      pgmoneta_json_destroy(list_response);
   }

   /* Leave a clean backup store for the next test. */
   pgmoneta_test_basedir_cleanup();

   MCTF_FINISH();
}
