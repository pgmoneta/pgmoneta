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

#include <utils.h>
#include <tsclient.h>
#include <tsclient_helpers.h>
#include <tscommon.h>
#include <tssuite.h>

#include <stdio.h>

#include "logging.h"

// test backup
START_TEST(test_pgmoneta_backup_full)
{
   fprintf(stderr, "TEST START: %s\n", __func__);
   int ret = 0;
   struct json* response = NULL;
   int num_backups_before = 0;
   int num_backups_after = 0;

   // Query initial backup count via LIST_BACKUP
   ret = pgmoneta_tsclient_list_backup("primary", &response);
   ck_assert_msg(ret == 0, "failed to list backups");
   num_backups_before = pgmoneta_tsclient_get_backup_count(response);
   pgmoneta_json_destroy(response);
   response = NULL;

   // Create full backup
   ret = !pgmoneta_tsclient_backup("primary", NULL);
   ck_assert_msg(ret, "failed to add full backup");

   // Query backup count after creation
   ret = pgmoneta_tsclient_list_backup("primary", &response);
   ck_assert_msg(ret == 0, "failed to list backups after backup");
   num_backups_after = pgmoneta_tsclient_get_backup_count(response);
   pgmoneta_json_destroy(response);

   // Verify one backup was created
   ck_assert_int_eq(num_backups_after, num_backups_before + 1);
}
END_TEST
START_TEST(test_pgmoneta_backup_incremental_basic)
{
   fprintf(stderr, "TEST START: %s\n", __func__);
   int ret = 0;
   struct json* response = NULL;
   int num_backups = 0;
   struct json* backup0 = NULL;
   struct json* backup1 = NULL;
   struct json* backup2 = NULL;

   // Create full backup
   ret = !pgmoneta_tsclient_backup("primary", NULL);
   ck_assert_msg(ret, "failed to add full backup");

   // Create incremental backup 1
   ret = !pgmoneta_tsclient_backup("primary", "newest");
   ck_assert_msg(ret, "failed to add incremental backup 1");

   // Create incremental backup 2
   ret = !pgmoneta_tsclient_backup("primary", "newest");
   ck_assert_msg(ret, "failed to add incremental backup 2");

   // Query backups
   ret = pgmoneta_tsclient_list_backup("primary", &response);
   ck_assert_msg(ret == 0, "failed to get backup list from management protocol");

   // Verify we have exactly 3 backups
   num_backups = pgmoneta_tsclient_get_backup_count(response);
   ck_assert_int_eq(num_backups, 3);

   // Get each backup from response
   backup0 = pgmoneta_tsclient_get_backup(response, 0);
   backup1 = pgmoneta_tsclient_get_backup(response, 1);
   backup2 = pgmoneta_tsclient_get_backup(response, 2);

   ck_assert_msg(backup0 != NULL, "backup 0 should exist");
   ck_assert_msg(backup1 != NULL, "backup 1 should exist");
   ck_assert_msg(backup2 != NULL, "backup 2 should exist");

   // Verify backup types
   ck_assert_str_eq(pgmoneta_tsclient_get_backup_type(backup0), "FULL");
   ck_assert_str_eq(pgmoneta_tsclient_get_backup_type(backup1), "INCREMENTAL");
   ck_assert_str_eq(pgmoneta_tsclient_get_backup_type(backup2), "INCREMENTAL");

   // Verify chain relationships
   ck_assert_msg(pgmoneta_tsclient_verify_backup_chain(backup0, backup1),
                 "backup1 should be child of backup0");
   ck_assert_msg(pgmoneta_tsclient_verify_backup_chain(backup1, backup2),
                 "backup2 should be child of backup1");

   pgmoneta_json_destroy(response);
}
END_TEST

Suite*
pgmoneta_test_backup_suite()
{
   Suite* s;
   TCase* tc_backup_basic;

   s = suite_create("pgmoneta_test_backup");

   tc_backup_basic = tcase_create("backup_basic_test");
   tcase_set_tags(tc_backup_basic, "common");
   tcase_set_timeout(tc_backup_basic, 60);
   tcase_add_checked_fixture(tc_backup_basic, pgmoneta_test_setup, pgmoneta_test_basedir_cleanup);
   tcase_add_test(tc_backup_basic, test_pgmoneta_backup_full);
   tcase_add_test(tc_backup_basic, test_pgmoneta_backup_incremental_basic);
   suite_add_tcase(s, tc_backup_basic);

   return s;
}