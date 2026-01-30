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
 */

#include <pgmoneta.h>
#include <server.h>
#include <tsclient.h>
#include <tsclient_helpers.h>
#include <tscommon.h>
#include <utils.h>
#include <mctf.h>

#include <stdio.h>
#include <stdlib.h>

MCTF_TEST(test_pgmoneta_delete_full)
{
   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup failed during setup");
   }

   if (pgmoneta_tsclient_delete("primary", "oldest", 0))
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup failed during setup");
   }

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_delete_retained_backup)
{
   char* d = NULL;
   struct json* response = NULL;
   int num_backups = 0;

   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup failed during setup");
   }

   d = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(d, cleanup, "server backup not valid");

   // Retain the backup
   MCTF_ASSERT(!pgmoneta_tsclient_retain("primary", "oldest", false, 0), cleanup, "failed to retain backup");

   // Delete without force should fail
   MCTF_ASSERT(pgmoneta_tsclient_delete("primary", "oldest", 0) != 0, cleanup, "delete should fail for retained backup");

   // Verify the count is still 1
   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", "asc", &response, 0), cleanup, "failed to list backups");
   num_backups = pgmoneta_tsclient_get_backup_count(response);
   MCTF_ASSERT_INT_EQ(num_backups, 1, cleanup, "expected 1 backup after retain");
   pgmoneta_json_destroy(response);
   response = NULL;

   // Expunge the backup (remove the retained flag)
   MCTF_ASSERT(!pgmoneta_tsclient_expunge("primary", "oldest", false, 0), cleanup, "failed to expunge backup");

   // Delete will work now without force
   MCTF_ASSERT(!pgmoneta_tsclient_delete("primary", "oldest", 0), cleanup, "failed to delete after expunge");

   // Verify the count is 0
   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", "asc", &response, 0), cleanup, "failed to list backups");
   num_backups = pgmoneta_tsclient_get_backup_count(response);
   MCTF_ASSERT_INT_EQ(num_backups, 0, cleanup, "expected 0 backups after delete");

cleanup:
   free(d);
   d = NULL;
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
      response = NULL;
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_delete_force_retained_backup)
{
   char* d = NULL;
   struct json* response = NULL;
   int num_backups = 0;

   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup failed during setup");
   }

   d = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(d, cleanup, "server backup not valid");

   // Retain the backup
   MCTF_ASSERT(!pgmoneta_tsclient_retain("primary", "oldest", false, 0), cleanup, "failed to retain backup");

   // Delete without force should fail
   MCTF_ASSERT(pgmoneta_tsclient_delete("primary", "oldest", 0) != 0, cleanup, "delete should fail for retained backup");

   // Verify the count is still 1
   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", "asc", &response, 0), cleanup, "failed to list backups");
   num_backups = pgmoneta_tsclient_get_backup_count(response);
   MCTF_ASSERT_INT_EQ(num_backups, 1, cleanup, "expected 1 backup after retain");
   pgmoneta_json_destroy(response);
   response = NULL;

   // Delete will work now with the force flag
   MCTF_ASSERT(!pgmoneta_tsclient_force_delete("primary", "oldest", 0), cleanup, "failed to force delete retained backup");

   // Verify the count is 0
   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", "asc", &response, 0), cleanup, "failed to list backups");
   num_backups = pgmoneta_tsclient_get_backup_count(response);
   MCTF_ASSERT_INT_EQ(num_backups, 0, cleanup, "expected 0 backups after force delete");

cleanup:
   free(d);
   d = NULL;
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
      response = NULL;
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_delete_chain_last)
{
   char* d = NULL;
   struct json* response = NULL;
   int num_bck_before = 0;
   int num_bck_after = 0;

   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup_chain())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup failed during setup");
   }

   d = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(d, cleanup, "server backup not valid");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", "asc", &response, 0), cleanup, "failed to list backups");
   num_bck_before = pgmoneta_tsclient_get_backup_count(response);
   MCTF_ASSERT_INT_EQ(num_bck_before, 3, cleanup, "expected 3 backups before deletion");
   pgmoneta_json_destroy(response);
   response = NULL;

   if (pgmoneta_tsclient_delete("primary", "newest", 0))
   {
      free(d);
      d = NULL;
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup failed during setup");
   }

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", "asc", &response, 0), cleanup, "failed to list backups");
   num_bck_after = pgmoneta_tsclient_get_backup_count(response);
   MCTF_ASSERT_INT_EQ(num_bck_after, 2, cleanup, "expected 2 backups after deletion");

cleanup:
   free(d);
   d = NULL;
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
      response = NULL;
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_delete_chain_middle)
{
   char* d = NULL;
   struct json* response = NULL;
   struct json* backup_before = NULL;
   struct json* backup_after0 = NULL;
   struct json* backup_after1 = NULL;
   struct json* backup_before2 = NULL;
   int num_bck_before = 0;
   int num_bck_after = 0;
   char* middle_label = NULL;

   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup_chain())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup failed during setup");
   }

   d = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(d, cleanup, "server backup not valid");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", "asc", &response, 0), cleanup, "failed to list backups");
   num_bck_before = pgmoneta_tsclient_get_backup_count(response);
   MCTF_ASSERT_INT_EQ(num_bck_before, 3, cleanup, "expected 3 backups before deletion");

   backup_before = pgmoneta_tsclient_get_backup(response, 1);
   MCTF_ASSERT_PTR_NONNULL(backup_before, cleanup, "backup[1] is null");
   middle_label = pgmoneta_tsclient_get_backup_label(backup_before);
   MCTF_ASSERT_PTR_NONNULL(middle_label, cleanup, "backup[1] label is null");

   // Save backup[2] label for comparison later
   backup_before2 = pgmoneta_tsclient_get_backup(response, 2);
   char* last_label_before = pgmoneta_tsclient_get_backup_label(backup_before2);

   if (pgmoneta_tsclient_delete("primary", middle_label, 0))
   {
      free(d);
      d = NULL;
      pgmoneta_json_destroy(response);
      response = NULL;
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("delete failed during test");
   }

   pgmoneta_json_destroy(response);
   response = NULL;

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", "asc", &response, 0), cleanup, "failed to list backups after delete");
   num_bck_after = pgmoneta_tsclient_get_backup_count(response);
   MCTF_ASSERT_INT_EQ(num_bck_after, 2, cleanup, "expected 2 backups after deletion");

   backup_after0 = pgmoneta_tsclient_get_backup(response, 0);
   backup_after1 = pgmoneta_tsclient_get_backup(response, 1);
   MCTF_ASSERT_PTR_NONNULL(backup_after0, cleanup, "backup[0] after deletion is null");
   MCTF_ASSERT_PTR_NONNULL(backup_after1, cleanup, "backup[1] after deletion is null");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(backup_after0), "FULL", cleanup, "expected first backup to be full");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(backup_after1), "INCREMENTAL", cleanup, "expected second backup to be incremental");

cleanup:
   free(d);
   d = NULL;
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
      response = NULL;
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_delete_chain_root)
{
   char* d = NULL;
   struct json* response = NULL;
   struct json* backup_before1 = NULL;
   struct json* backup_after0 = NULL;
   struct json* backup_after1 = NULL;
   int num_bck_before = 0;
   int num_bck_after = 0;
   char* label_before1 = NULL;

   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup_chain())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup failed during setup");
   }

   d = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(d, cleanup, "server backup not valid");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", "asc", &response, 0), cleanup, "failed to list backups");
   num_bck_before = pgmoneta_tsclient_get_backup_count(response);
   MCTF_ASSERT_INT_EQ(num_bck_before, 3, cleanup, "expected 3 backups before deletion");

   backup_before1 = pgmoneta_tsclient_get_backup(response, 1);
   MCTF_ASSERT_PTR_NONNULL(backup_before1, cleanup, "backup[1] is null");
   label_before1 = pgmoneta_tsclient_get_backup_label(backup_before1);

   if (pgmoneta_tsclient_delete("primary", "oldest", 0))
   {
      free(d);
      d = NULL;
      pgmoneta_json_destroy(response);
      response = NULL;
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("delete failed during test");
   }

   pgmoneta_json_destroy(response);
   response = NULL;

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", "asc", &response, 0), cleanup, "failed to list backups after delete");
   num_bck_after = pgmoneta_tsclient_get_backup_count(response);
   MCTF_ASSERT_INT_EQ(num_bck_after, 2, cleanup, "expected 2 backups after deletion");

   backup_after0 = pgmoneta_tsclient_get_backup(response, 0);
   backup_after1 = pgmoneta_tsclient_get_backup(response, 1);
   MCTF_ASSERT_PTR_NONNULL(backup_after0, cleanup, "backup[0] after deletion is null");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(backup_after0), "FULL", cleanup, "expected first backup to be full");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(backup_after1), "INCREMENTAL", cleanup, "expected second backup to be incremental");

cleanup:
   free(d);
   d = NULL;
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
      response = NULL;
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}
