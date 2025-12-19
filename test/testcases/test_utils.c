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

#include <pgmoneta.h>
#include <tscommon.h>
#include <tssuite.h>
#include <utils.h>

#include <stdlib.h>
#include <stdio.h>

START_TEST(test_resolve_path_trailing_env_var)
{
   fprintf(stderr, "TEST START: %s\n", __func__);
   char* resolved = NULL;
   char* env_key = "PGMONETA_TEST_PATH_KEY";
   char* env_value = "PGMONETA_TEST_PATH_VALUE";
   char* expected = "/pgmoneta/PGMONETA_TEST_PATH_VALUE";
   int result;

   setenv(env_key, env_value, 1);

   result = pgmoneta_resolve_path("/pgmoneta/$PGMONETA_TEST_PATH_KEY", &resolved);

   ck_assert_int_eq(result, 0);

   ck_assert_ptr_nonnull(resolved);

   ck_assert_str_eq(resolved, expected);

   unsetenv(env_key);
   free(resolved);
}
END_TEST

Suite*
pgmoneta_test_utils_suite()
{
   Suite* s;
   TCase* tc_utils;

   s = suite_create("pgmoneta_test_utils");

   tc_utils = tcase_create("test_utils");
   tcase_set_tags(tc_utils, "common");
   tcase_set_timeout(tc_utils, 60);
   tcase_add_checked_fixture(tc_utils, pgmoneta_test_setup, pgmoneta_test_teardown);
   tcase_add_test(tc_utils, test_resolve_path_trailing_env_var);

   suite_add_tcase(s, tc_utils);
   return s;
}
