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

#include <tsclient.h>

#include "pgmoneta_test_3.h"

START_TEST(test_pgmoneta_http)
{
   int found = 0;
   found = !pgmoneta_tsclient_execute_http();
   ck_assert_msg(found, "success status not found");
}
END_TEST
START_TEST(test_pgmoneta_https)
{
   int found = 0;
   found = !pgmoneta_tsclient_execute_https();
   ck_assert_msg(found, "success status not found");
}
END_TEST
START_TEST(test_pgmoneta_http_post)
{
   int found = 0;
   found = !pgmoneta_tsclient_execute_http_post();
   ck_assert_msg(found, "success status not found");
}
END_TEST
START_TEST(test_pgmoneta_http_put)
{
   int found = 0;
   found = !pgmoneta_tsclient_execute_http_put();
   ck_assert_msg(found, "success status not found");
}
END_TEST
START_TEST(test_pgmoneta_http_put_file)
{
   int found = 0;
   found = !pgmoneta_tsclient_execute_http_put_file();
   ck_assert_msg(found, "success status not found");
}
END_TEST

Suite*
pgmoneta_test3_suite()
{
   Suite* s;
   TCase* tc_core;
   s = suite_create("pgmoneta_test3");

   tc_core = tcase_create("Core");

   tcase_set_timeout(tc_core, 60);
   tcase_add_test(tc_core, test_pgmoneta_http);
   tcase_add_test(tc_core, test_pgmoneta_https);
   tcase_add_test(tc_core, test_pgmoneta_http_post);
   tcase_add_test(tc_core, test_pgmoneta_http_put);
   tcase_add_test(tc_core, test_pgmoneta_http_put_file);
   suite_add_tcase(s, tc_core);

   return s;
}