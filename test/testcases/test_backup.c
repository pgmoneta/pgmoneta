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
#include <logging.h>
#include <tsclient.h>
#include <tsclient_helpers.h>
#include <tscommon.h>
#include <mctf.h>
#include <utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

MCTF_TEST(test_pgmoneta_backup_full)
{
   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup failed during setup");
   }

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_backup_incremental_basic)
{
   struct json* response = NULL;
   struct json* backup0 = NULL;
   struct json* backup1 = NULL;
   struct json* backup2 = NULL;
   int num_backups = 0;

   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup_chain())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup chain failed during setup");
   }

   // Get backup list via management API
   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", "asc", &response, 0), cleanup, "failed to get backups via management API");
   MCTF_ASSERT_PTR_NONNULL(response, cleanup, "response should not be NULL");

   // Get backup count
   num_backups = pgmoneta_tsclient_get_backup_count(response);
   MCTF_ASSERT_INT_EQ(num_backups, 3, cleanup, "backup count mismatch");

   // Get individual backups (sorted ascending)
   backup0 = pgmoneta_tsclient_get_backup(response, 0);
   backup1 = pgmoneta_tsclient_get_backup(response, 1);
   backup2 = pgmoneta_tsclient_get_backup(response, 2);

   MCTF_ASSERT_PTR_NONNULL(backup0, cleanup, "backup0 should not be NULL");
   MCTF_ASSERT_PTR_NONNULL(backup1, cleanup, "backup1 should not be NULL");
   MCTF_ASSERT_PTR_NONNULL(backup2, cleanup, "backup2 should not be NULL");

   // Verify backup types
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(backup0), "FULL", cleanup, "backup 0 type mismatch");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(backup1), "INCREMENTAL", cleanup, "backup 1 type mismatch");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(backup2), "INCREMENTAL", cleanup, "backup 2 type mismatch");

   // Verify backup chain using helper function
   MCTF_ASSERT(pgmoneta_tsclient_verify_backup_chain(backup0, backup1), cleanup, "backup 1 parent mismatch");
   MCTF_ASSERT(pgmoneta_tsclient_verify_backup_chain(backup1, backup2), cleanup, "backup 2 parent mismatch");

cleanup:
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
      response = NULL;
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}
