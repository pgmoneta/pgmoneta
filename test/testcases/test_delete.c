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

#include <server.h>
#include <tsclient.h>
#include <tsclient_helpers.h>
#include <tssuite.h>
#include <tscommon.h>
#include <utils.h>
#include <stdio.h>

#include "logging.h"

// test delete a single full backup
START_TEST(test_pgmoneta_delete_full)
{
   fprintf(stderr, "TEST START: %s\n", __func__);
   int found = 0;
   found = !pgmoneta_tsclient_delete("primary", "oldest", 0);
   ck_assert_msg(found, "success status not found");
}
END_TEST
// test delete the last incremental backup in the chain
START_TEST(test_pgmoneta_delete_chain_last)
{
   fprintf(stderr, "TEST START: %s\n", __func__);
   int ret = 0;
   struct json* response_before = NULL;
   struct json* response_after = NULL;
   int num_bck_before = 0;
   int num_bck_after = 0;

   // Query backups BEFORE deletion
   ret = pgmoneta_tsclient_list_backup("primary", NULL, &response_before, 0);
   ck_assert_msg(ret == 0, "failed to list backups before delete");

   // Verify we have 3 backups
   num_bck_before = pgmoneta_tsclient_get_backup_count(response_before);
   ck_assert_int_eq(num_bck_before, 3);
   pgmoneta_json_destroy(response_before);

   // Delete newest backup
   ret = !pgmoneta_tsclient_delete("primary", "newest", 0);
   ck_assert_msg(ret, "success status not found");

   // Query backups AFTER deletion
   ret = pgmoneta_tsclient_list_backup("primary", NULL, &response_after, 0);
   ck_assert_msg(ret == 0, "failed to list backups after delete");

   // Verify we now have 2 backups
   num_bck_after = pgmoneta_tsclient_get_backup_count(response_after);
   ck_assert_int_eq(num_bck_after, 2);

   pgmoneta_json_destroy(response_after);
}
END_TEST
// test delete the middle incremental backup in the chain
START_TEST(test_pgmoneta_delete_chain_middle)
{
   fprintf(stderr, "TEST START: %s\n", __func__);
   int ret = 0;
   struct json* response_before = NULL;
   struct json* response_after = NULL;
   struct json* backup_to_delete = NULL;
   struct json* backup_before_2 = NULL;
   struct json* backup_after_0 = NULL;
   struct json* backup_after_1 = NULL;
   char* label_to_delete = NULL;
   char* label_before_2 = NULL;
   char* label_after_1 = NULL;
   int num_bck_before = 0;
   int num_bck_after = 0;

   // Query backups BEFORE deletion
   ret = pgmoneta_tsclient_list_backup("primary", NULL, &response_before, 0);
   ck_assert_msg(ret == 0, "failed to list backups before delete");

   // Verify we have 3 backups
   num_bck_before = pgmoneta_tsclient_get_backup_count(response_before);
   ck_assert_int_eq(num_bck_before, 3);

   // Get label of middle backup (index 1) to delete
   backup_to_delete = pgmoneta_tsclient_get_backup(response_before, 1);
   label_to_delete = pgmoneta_tsclient_get_backup_label(backup_to_delete);
   ck_assert_msg(label_to_delete != NULL, "middle backup label should exist");

   // Get label of backup at index 2 (will become index 1 after deletion)
   backup_before_2 = pgmoneta_tsclient_get_backup(response_before, 2);
   label_before_2 = pgmoneta_tsclient_get_backup_label(backup_before_2);
   ck_assert_msg(label_before_2 != NULL, "backup 2 label should exist");
   label_before_2 = strdup(label_before_2);

   // Delete middle backup
   ret = !pgmoneta_tsclient_delete("primary", label_to_delete, 0);
   ck_assert_msg(ret, "success status not found");

   // Query backups AFTER deletion
   ret = pgmoneta_tsclient_list_backup("primary", NULL, &response_after, 0);
   ck_assert_msg(ret == 0, "failed to list backups after delete");

   // Verify we now have 2 backups
   num_bck_after = pgmoneta_tsclient_get_backup_count(response_after);
   ck_assert_int_eq(num_bck_after, 2);

   backup_after_0 = pgmoneta_tsclient_get_backup(response_after, 0);
   backup_after_1 = pgmoneta_tsclient_get_backup(response_after, 1);

   // Verify backup types
   ck_assert_str_eq(pgmoneta_tsclient_get_backup_type(backup_after_0), "FULL");
   ck_assert_str_eq(pgmoneta_tsclient_get_backup_type(backup_after_1), "INCREMENTAL");

   // Verify the backup that was at index 2 is now at index 1
   label_after_1 = pgmoneta_tsclient_get_backup_label(backup_after_1);
   ck_assert_str_eq(label_before_2, label_after_1);

   free(label_before_2);
   pgmoneta_json_destroy(response_before);
   pgmoneta_json_destroy(response_after);
}
END_TEST
// test delete the root full backup in the chain
START_TEST(test_pgmoneta_delete_chain_root)
{
   fprintf(stderr, "TEST START: %s\n", __func__);
   int ret = 0;
   struct json* response_before = NULL;
   struct json* response_after = NULL;
   struct json* backup_before_1 = NULL;
   struct json* backup_after_0 = NULL;
   struct json* backup_after_1 = NULL;
   char* label_before_1 = NULL;
   char* label_after_0 = NULL;
   int num_bck_before = 0;
   int num_bck_after = 0;

   // Query backups BEFORE deletion
   ret = pgmoneta_tsclient_list_backup("primary", NULL, &response_before, 0);
   ck_assert_msg(ret == 0, "failed to list backups before delete");

   // Verify we have 3 backups
   num_bck_before = pgmoneta_tsclient_get_backup_count(response_before);
   ck_assert_int_eq(num_bck_before, 3);

   // Get label of backup at index 1
   backup_before_1 = pgmoneta_tsclient_get_backup(response_before, 1);
   label_before_1 = pgmoneta_tsclient_get_backup_label(backup_before_1);
   ck_assert_msg(label_before_1 != NULL, "backup 1 label should exist");
   label_before_1 = strdup(label_before_1);

   // Delete oldest (root) backup
   ret = !pgmoneta_tsclient_delete("primary", "oldest", 0);
   ck_assert_msg(ret, "success status not found");

   // Query backups AFTER deletion
   ret = pgmoneta_tsclient_list_backup("primary", NULL, &response_after, 0);
   ck_assert_msg(ret == 0, "failed to list backups after delete");

   // Verify we now have 2 backups
   num_bck_after = pgmoneta_tsclient_get_backup_count(response_after);
   ck_assert_int_eq(num_bck_after, 2);

   // Get backups after deletion
   backup_after_0 = pgmoneta_tsclient_get_backup(response_after, 0);
   backup_after_1 = pgmoneta_tsclient_get_backup(response_after, 1);

   // First backup is now FULL (was INCREMENTAL)
   ck_assert_str_eq(pgmoneta_tsclient_get_backup_type(backup_after_0), "FULL");
   ck_assert_str_eq(pgmoneta_tsclient_get_backup_type(backup_after_1), "INCREMENTAL");

   // Verify the backup that was at index 1 is now at index 0
   label_after_0 = pgmoneta_tsclient_get_backup_label(backup_after_0);
   ck_assert_str_eq(label_before_1, label_after_0);

   free(label_before_1);
   pgmoneta_json_destroy(response_before);
   pgmoneta_json_destroy(response_after);
}
END_TEST

Suite*
pgmoneta_test_delete_suite()
{
   Suite* s;
   TCase* tc_delete_full;
   TCase* tc_delete_chain;

   s = suite_create("pgmoneta_test_delete");

   tc_delete_full = tcase_create("delete_full_test");
   tcase_set_tags(tc_delete_full, "common");
   tcase_set_timeout(tc_delete_full, 60);
   tcase_add_checked_fixture(tc_delete_full, pgmoneta_test_add_backup, pgmoneta_test_basedir_cleanup);
   tcase_add_test(tc_delete_full, test_pgmoneta_delete_full);
   suite_add_tcase(s, tc_delete_full);

   tc_delete_chain = tcase_create("delete_chain_test");
   tcase_set_tags(tc_delete_chain, " common");
   tcase_set_timeout(tc_delete_chain, 120);
   tcase_add_checked_fixture(tc_delete_chain, pgmoneta_test_add_backup_chain, pgmoneta_test_basedir_cleanup);
   tcase_add_test(tc_delete_chain, test_pgmoneta_delete_chain_last);
   tcase_add_test(tc_delete_chain, test_pgmoneta_delete_chain_middle);
   tcase_add_test(tc_delete_chain, test_pgmoneta_delete_chain_root);
   suite_add_tcase(s, tc_delete_chain);

   return s;
}
