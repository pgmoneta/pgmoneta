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

#include <tssuite.h>
#include <tsclient.h>
#include <tscommon.h>

// test restore
START_TEST(test_pgmoneta_restore)
{
   int found = 0;
   found = !pgmoneta_tsclient_restore("primary", "newest", "current");
   ck_assert_msg(found, "success status not found");
}
END_TEST

Suite*
pgmoneta_test_restore_suite()
{
   Suite* s;
   TCase* tc_restore_full;
   TCase* tc_restore_incremental;

   s = suite_create("pgmoneta_test_restore");

   tc_restore_full = tcase_create("full_restore_test");
   tcase_set_timeout(tc_restore_full, 60);
   tcase_add_checked_fixture(tc_restore_full, pgmoneta_test_add_backup, pgmoneta_test_basedir_cleanup);
   tcase_add_test(tc_restore_full, test_pgmoneta_restore);
   suite_add_tcase(s, tc_restore_full);

   tc_restore_incremental = tcase_create("incremental_restore_test");
   tcase_set_timeout(tc_restore_incremental, 60);
   tcase_add_checked_fixture(tc_restore_incremental, pgmoneta_test_add_backup_chain, pgmoneta_test_basedir_cleanup);
   tcase_add_test(tc_restore_incremental, test_pgmoneta_restore);
   suite_add_tcase(s, tc_restore_incremental);

   return s;
}