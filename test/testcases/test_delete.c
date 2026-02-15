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

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(pgmoneta_tsclient_delete("primary", "oldest", 0) == 0, cleanup, "delete failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_delete_retained_backup)
{
   struct json* response = NULL;
   int num_backups = 0;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(!pgmoneta_tsclient_retain("primary", "oldest", false, 0), cleanup, "failed to retain backup");
   MCTF_ASSERT(pgmoneta_tsclient_delete("primary", "oldest", 0) != 0, cleanup, "delete should fail for retained backup");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), cleanup, "list backup failed");
   num_backups = pgmoneta_tsclient_get_backup_count(response);
   MCTF_ASSERT_INT_EQ(num_backups, 1, cleanup, "expected 1 backup after retain");
   pgmoneta_json_destroy(response);
   response = NULL;

   MCTF_ASSERT(!pgmoneta_tsclient_expunge("primary", "oldest", false, 0), cleanup, "failed to expunge backup");
   MCTF_ASSERT(!pgmoneta_tsclient_delete("primary", "oldest", 0), cleanup, "failed to delete after expunge");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), cleanup, "list backup failed after delete");
   num_backups = pgmoneta_tsclient_get_backup_count(response);
   MCTF_ASSERT_INT_EQ(num_backups, 0, cleanup, "expected 0 backups after delete");
   pgmoneta_json_destroy(response);
   response = NULL;

cleanup:
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_delete_force_retained_backup)
{
   struct json* response = NULL;
   int num_backups = 0;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(!pgmoneta_tsclient_retain("primary", "oldest", false, 0), cleanup, "failed to retain backup");
   MCTF_ASSERT(pgmoneta_tsclient_delete("primary", "oldest", 0) != 0, cleanup, "delete should fail for retained backup");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), cleanup, "list backup failed");
   num_backups = pgmoneta_tsclient_get_backup_count(response);
   MCTF_ASSERT_INT_EQ(num_backups, 1, cleanup, "expected 1 backup after retain");
   pgmoneta_json_destroy(response);
   response = NULL;

   MCTF_ASSERT(!pgmoneta_tsclient_force_delete("primary", "oldest", 0), cleanup, "failed to force delete retained backup");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), cleanup, "list backup failed after force delete");
   num_backups = pgmoneta_tsclient_get_backup_count(response);
   MCTF_ASSERT_INT_EQ(num_backups, 0, cleanup, "expected 0 backups after force delete");
   pgmoneta_json_destroy(response);
   response = NULL;

cleanup:
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_delete_chain_last)
{
   struct json* response_before = NULL;
   struct json* response_after = NULL;
   int num_bck_before = 0;
   int num_bck_after = 0;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup_chain() == 0, cleanup, "backup chain failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response_before, 0), cleanup, "list backup before failed");
   num_bck_before = pgmoneta_tsclient_get_backup_count(response_before);
   MCTF_ASSERT_INT_EQ(num_bck_before, 3, cleanup, "expected 3 backups before deletion");
   pgmoneta_json_destroy(response_before);
   response_before = NULL;

   MCTF_ASSERT(pgmoneta_tsclient_delete("primary", "newest", 0) == 0, cleanup, "delete operation failed");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response_after, 0), cleanup, "list backup after failed");
   num_bck_after = pgmoneta_tsclient_get_backup_count(response_after);
   MCTF_ASSERT_INT_EQ(num_bck_after, 2, cleanup, "expected 2 backups after deletion");
   pgmoneta_json_destroy(response_after);
   response_after = NULL;

cleanup:
   if (response_before != NULL)
   {
      pgmoneta_json_destroy(response_before);
   }
   if (response_after != NULL)
   {
      pgmoneta_json_destroy(response_after);
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_delete_chain_middle)
{
   struct json* response_before = NULL;
   struct json* response_after = NULL;
   struct json* backup_target = NULL;
   char* label_to_delete = NULL;
   int num_bck_before = 0;
   int num_bck_after = 0;
   struct json* b_after_0 = NULL;
   struct json* b_after_1 = NULL;
   struct json* b_before_2 = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup_chain() == 0, cleanup, "backup chain failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response_before, 0), cleanup, "list backup before failed");
   num_bck_before = pgmoneta_tsclient_get_backup_count(response_before);
   MCTF_ASSERT_INT_EQ(num_bck_before, 3, cleanup, "expected 3 backups before deletion");

   backup_target = pgmoneta_tsclient_get_backup(response_before, 1);
   MCTF_ASSERT_PTR_NONNULL(backup_target, cleanup, "backup[1] not found");
   label_to_delete = pgmoneta_tsclient_get_backup_label(backup_target);
   MCTF_ASSERT_PTR_NONNULL(label_to_delete, cleanup, "label is null");

   MCTF_ASSERT(pgmoneta_tsclient_delete("primary", label_to_delete, 0) == 0, cleanup, "delete operation failed");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response_after, 0), cleanup, "list backup after failed");
   num_bck_after = pgmoneta_tsclient_get_backup_count(response_after);
   MCTF_ASSERT_INT_EQ(num_bck_after, 2, cleanup, "expected 2 backups after deletion");

   b_after_0 = pgmoneta_tsclient_get_backup(response_after, 0);
   b_after_1 = pgmoneta_tsclient_get_backup(response_after, 1);

   MCTF_ASSERT_PTR_NONNULL(b_after_0, cleanup, "backup[0] after null");
   MCTF_ASSERT_PTR_NONNULL(b_after_1, cleanup, "backup[1] after null");

   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(b_after_0), "FULL", cleanup, "expected FULL");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(b_after_1), "INCREMENTAL", cleanup, "expected INCREMENTAL");

   b_before_2 = pgmoneta_tsclient_get_backup(response_before, 2);
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_label(b_before_2), pgmoneta_tsclient_get_backup_label(b_after_1), cleanup, "label mismatch");

cleanup:
   if (response_before != NULL)
   {
      pgmoneta_json_destroy(response_before);
   }
   if (response_after != NULL)
   {
      pgmoneta_json_destroy(response_after);
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_delete_chain_root)
{
   struct json* response_before = NULL;
   struct json* response_after = NULL;
   int num_bck_before = 0;
   int num_bck_after = 0;
   struct json* b_after_0 = NULL;
   struct json* b_after_1 = NULL;
   struct json* b_before_1 = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup_chain() == 0, cleanup, "backup chain failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response_before, 0), cleanup, "list backup before failed");
   num_bck_before = pgmoneta_tsclient_get_backup_count(response_before);
   MCTF_ASSERT_INT_EQ(num_bck_before, 3, cleanup, "expected 3 backups before deletion");

   MCTF_ASSERT(pgmoneta_tsclient_delete("primary", "oldest", 0) == 0, cleanup, "delete operation failed");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response_after, 0), cleanup, "list backup after failed");
   num_bck_after = pgmoneta_tsclient_get_backup_count(response_after);
   MCTF_ASSERT_INT_EQ(num_bck_after, 2, cleanup, "expected 2 backups after deletion");

   b_after_0 = pgmoneta_tsclient_get_backup(response_after, 0);
   b_after_1 = pgmoneta_tsclient_get_backup(response_after, 1);

   MCTF_ASSERT_PTR_NONNULL(b_after_0, cleanup, "backup[0] after null");
   MCTF_ASSERT_PTR_NONNULL(b_after_1, cleanup, "backup[1] after null");

   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(b_after_0), "FULL", cleanup, "expected FULL");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(b_after_1), "INCREMENTAL", cleanup, "expected INCREMENTAL");

   b_before_1 = pgmoneta_tsclient_get_backup(response_before, 1);
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_label(b_before_1), pgmoneta_tsclient_get_backup_label(b_after_0), cleanup, "label mismatch");

cleanup:
   if (response_before != NULL)
   {
      pgmoneta_json_destroy(response_before);
   }
   if (response_after != NULL)
   {
      pgmoneta_json_destroy(response_after);
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}