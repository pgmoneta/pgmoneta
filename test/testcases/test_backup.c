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

#include <info.h>
#include <utils.h>
#include <tsclient.h>
#include <tscommon.h>
#include <tssuite.h>

#include <stdio.h>

#include "logging.h"

// test backup
START_TEST(test_pgmoneta_backup_full)
{
   fprintf(stderr, "TEST START: %s\n", __func__);
   int ret = 0;
   ret = !pgmoneta_tsclient_backup("primary", NULL);
   ck_assert_msg(ret, "failed to add full backup");
}
END_TEST
START_TEST(test_pgmoneta_backup_incremental_basic)
{
   fprintf(stderr, "TEST START: %s\n", __func__);
   int ret = 0;
   char* d = NULL;
   int num_backups = 0;
   struct backup** backups = NULL;
   ret = !pgmoneta_tsclient_backup("primary", NULL);
   ck_assert_msg(ret, "failed to add full backup");
   ret = !pgmoneta_tsclient_backup("primary", "newest");
   ck_assert_msg(ret, "failed to add incremental backup 1");
   ret = !pgmoneta_tsclient_backup("primary", "newest");
   ck_assert_msg(ret, "failed to add incremental backup 2");

   d = pgmoneta_get_server_backup(PRIMARY_SERVER);

   pgmoneta_load_infos(d, &num_backups, &backups);
   ck_assert_int_eq(num_backups, 3);
   ck_assert_msg(backups != NULL, "backups should not be NULL");

   // sort the backups in ascending order
   pgmoneta_sort_backups(backups, num_backups, false);
   ck_assert_int_eq(backups[0]->type, TYPE_FULL);
   ck_assert_int_eq(backups[1]->type, TYPE_INCREMENTAL);
   ck_assert_int_eq(backups[2]->type, TYPE_INCREMENTAL);

   ck_assert_str_eq(backups[1]->parent_label, backups[0]->label);
   ck_assert_str_eq(backups[2]->parent_label, backups[1]->label);

   free(d);
   for (int i = 0; i < num_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);
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