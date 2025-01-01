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

#include "pgmoneta_ext_test.h"

#define BUFFER_SIZE 8192

#define PGMONETA_EXT_VERSION_RESULT      "0.1.0"
#define PGMONETA_EXT_SWITCH_WAL_RESULT   "(f,)"
#define PGMONETA_EXT_CHECKPOINT_RESULT   "(f,)"

static int
execute_command(const char* command, char* output, size_t output_size)
{
   FILE* fp;
   fp = popen(command, "r");
   if (fp == NULL)
   {
      return -1;
   }

   if (fgets(output, output_size, fp) == NULL)
   {
      pclose(fp);
      return -1;
   }

   pclose(fp);
   return 0;
}

START_TEST(test_pgmoneta_ext_version)
{
   char output[BUFFER_SIZE];
   int result = execute_command("psql -h localhost -p 5432 -U repl -d postgres -t -c 'SELECT pgmoneta_ext_version();'", output, BUFFER_SIZE);
   ck_assert_int_eq(result, 0);
   ck_assert_msg(strstr(output, PGMONETA_EXT_VERSION_RESULT) != NULL, "Expected version string not found in output: %s", output);
}
END_TEST
START_TEST(test_pgmoneta_ext_switch_wal)
{
   char output[BUFFER_SIZE];
   int result = execute_command("psql -h localhost -p 5432 -U repl -d postgres -t -c 'SELECT pgmoneta_ext_switch_wal();'", output, BUFFER_SIZE);
   ck_assert_int_eq(result, 0);
   ck_assert_msg(strstr(output, PGMONETA_EXT_SWITCH_WAL_RESULT) != NULL, "Expected WAL switch result not found in output: %s", output);
}
END_TEST
START_TEST(test_pgmoneta_ext_checkpoint)
{
   char output[BUFFER_SIZE];
   int result = execute_command("psql -h localhost -p 5432 -U repl -d postgres -t -c 'SELECT pgmoneta_ext_checkpoint();'", output, BUFFER_SIZE);
   ck_assert_int_eq(result, 0);
   ck_assert_msg(strstr(output, PGMONETA_EXT_CHECKPOINT_RESULT) != NULL, "Expected checkpoint result not found in output: %s", output);
}
END_TEST

Suite*
pgmoneta_ext_suite(void)
{
   Suite* s;
   TCase* tc_core;

   s = suite_create("pgmoneta_ext");

   tc_core = tcase_create("Core");

   tcase_add_test(tc_core, test_pgmoneta_ext_version);
   tcase_add_test(tc_core, test_pgmoneta_ext_switch_wal);
   tcase_add_test(tc_core, test_pgmoneta_ext_checkpoint);
   suite_add_tcase(s, tc_core);

   return s;
}
