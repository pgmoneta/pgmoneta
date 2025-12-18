/*
 * Copyright (C) 2025 The pgmoneta community
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
#include <server.h>
#include <tsclient.h>
#include <tssuite.h>
#include <tscommon.h>
#include <utils.h>

// test delete a single full backup
START_TEST(test_pgmoneta_delete_full)
{
   int found = 0;
   found = !pgmoneta_tsclient_delete("primary", "oldest");
   ck_assert_msg(found, "success status not found");
}
END_TEST
// test delete the last incremental backup in the chain
START_TEST(test_pgmoneta_delete_chain_last)
{
   int found = 0;
   char* d = NULL;
   int num_bck_before = 0;
   int num_bck_after = 0;
   struct backup** bcks_before = NULL;
   struct backup** bcks_after = NULL;

   d = pgmoneta_get_server_backup(PRIMARY_SERVER);
   ck_assert_msg(d != NULL, "server backup not valid");
   pgmoneta_load_infos(d, &num_bck_before, &bcks_before);
   ck_assert_int_eq(num_bck_before, 3);

   found = !pgmoneta_tsclient_delete("primary", "newest");
   ck_assert_msg(found, "success status not found");

   pgmoneta_load_infos(d, &num_bck_after, &bcks_after);
   ck_assert_int_eq(num_bck_after, 2);

   free(d);
   for (int i = 0; i < num_bck_before; i++)
   {
      free(bcks_before[i]);
   }
   free(bcks_before);

   for (int i = 0; i < num_bck_after; i++)
   {
      free(bcks_after[i]);
   }
   free(bcks_after);
}
END_TEST
// test delete the middle incremental backup in the chain
START_TEST(test_pgmoneta_delete_chain_middle)
{
   int found = 0;
   char* d = NULL;
   int num_bck_before = 0;
   int num_bck_after = 0;
   struct backup** bcks_before = NULL;
   struct backup** bcks_after = NULL;
   d = pgmoneta_get_server_backup(PRIMARY_SERVER);
   pgmoneta_load_infos(d, &num_bck_before, &bcks_before);
   ck_assert_int_eq(num_bck_before, 3);

   found = !pgmoneta_tsclient_delete("primary", bcks_before[1]->label);
   ck_assert_msg(found, "success status not found");

   pgmoneta_load_infos(d, &num_bck_after, &bcks_after);
   ck_assert_int_eq(num_bck_after, 2);
   ck_assert_int_eq(bcks_after[0]->type, TYPE_FULL);
   ck_assert_int_eq(bcks_after[1]->type, TYPE_INCREMENTAL);
   ck_assert_str_eq(bcks_before[2]->label, bcks_after[1]->label);

   free(d);
   for (int i = 0; i < num_bck_before; i++)
   {
      free(bcks_before[i]);
   }
   free(bcks_before);

   for (int i = 0; i < num_bck_after; i++)
   {
      free(bcks_after[i]);
   }
   free(bcks_after);
}
END_TEST
// test delete the root full backup in the chain
START_TEST(test_pgmoneta_delete_chain_root)
{
   int found = 0;
   char* d = NULL;
   int num_bck_before = 0;
   int num_bck_after = 0;
   struct backup** bcks_before = NULL;
   struct backup** bcks_after = NULL;
   d = pgmoneta_get_server_backup(PRIMARY_SERVER);
   pgmoneta_load_infos(d, &num_bck_before, &bcks_before);
   ck_assert_int_eq(num_bck_before, 3);

   found = !pgmoneta_tsclient_delete("primary", "oldest");
   ck_assert_msg(found, "success status not found");

   pgmoneta_load_infos(d, &num_bck_after, &bcks_after);
   ck_assert_int_eq(num_bck_after, 2);
   ck_assert_int_eq(bcks_after[0]->type, TYPE_FULL);
   ck_assert_int_eq(bcks_after[1]->type, TYPE_INCREMENTAL);
   ck_assert_str_eq(bcks_before[1]->label, bcks_after[0]->label);

   free(d);
   for (int i = 0; i < num_bck_before; i++)
   {
      free(bcks_before[i]);
   }
   free(bcks_before);

   for (int i = 0; i < num_bck_after; i++)
   {
      free(bcks_after[i]);
   }
   free(bcks_after);
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
