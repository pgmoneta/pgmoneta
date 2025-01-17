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

#include "pgmoneta_test_2.h"
#include "common.h"

// test backup
START_TEST(test_pgmoneta_backup)
{
   int found = 0;
   char command[BUFFER_SIZE];
   char* executable_path = NULL;
   char* configuration_path = NULL;
   char* log_path = NULL;
   char command_output[BUFFER_SIZE];
   FILE* fp;

   executable_path = get_executable_path();
   configuration_path = get_configuration_path();
   log_path = get_log_path();

   snprintf(command, sizeof(command), "%s -c %s backup primary", executable_path, configuration_path);

   fp = popen(command, "r");
   if (fp == NULL)
   {
      ck_assert_msg(0, "couldn't execute the command");
   }

   fread(command_output, sizeof(char), BUFFER_SIZE - 1, fp);

   pclose(fp);

   if (strstr(command_output, SUCCESS_STATUS) != NULL)
   {
      found = 1;
   }
   ck_assert_msg(found, "success status not found");

done:
   free(executable_path);
   free(configuration_path);
   free(log_path);
}
END_TEST
// test restore
START_TEST(test_pgmoneta_restore)
{
   int found = 0;
   char command[BUFFER_SIZE];
   char* executable_path = NULL;
   char* configuration_path = NULL;
   char* restore_path = NULL;
   char* log_path = NULL;
   FILE* fp;
   char command_output[BUFFER_SIZE];

   executable_path = get_executable_path();
   configuration_path = get_configuration_path();
   restore_path = get_restore_path();
   log_path = get_log_path();

   snprintf(command, sizeof(command), "%s -c %s restore primary newest current %s", executable_path, configuration_path, restore_path);

   fp = popen(command, "r");
   if (fp == NULL)
   {
      ck_assert_msg(0, "couldn't execute the command");
   }

   fread(command_output, sizeof(char), BUFFER_SIZE - 1, fp);

   pclose(fp);

done:
   free(executable_path);
   free(configuration_path);
   free(restore_path);
   free(log_path);
}
END_TEST
// test delete
START_TEST(test_pgmoneta_delete)
{
   FILE* fp;
   char command[BUFFER_SIZE];
   char* executable_path = NULL;
   char* configuration_path = NULL;
   char* log_path = NULL;
   char command_output[BUFFER_SIZE];

   int found = 0;
   executable_path = get_executable_path();
   configuration_path = get_configuration_path();
   log_path = get_log_path();

   snprintf(command, sizeof(command), "%s -c %s delete primary oldest", executable_path, configuration_path);

   fp = popen(command, "r");
   if (fp == NULL)
   {
      ck_assert_msg(0, "couldn't execute the command");
   }

   fread(command_output, sizeof(char), BUFFER_SIZE - 1, fp);

   pclose(fp);

done:
   free(executable_path);
   free(configuration_path);
   free(log_path);
}
END_TEST

Suite*
pgmoneta_test2_suite(char* dir)
{
   Suite* s;
   TCase* tc_core;

   memset(project_directory, 0, sizeof(project_directory));
   memcpy(project_directory, dir, strlen(dir));

   s = suite_create("pgmoneta_test2");

   tc_core = tcase_create("Core");

   tcase_set_timeout(tc_core, 60);
   tcase_add_test(tc_core, test_pgmoneta_backup);
   tcase_add_test(tc_core, test_pgmoneta_backup);
   tcase_add_test(tc_core, test_pgmoneta_delete);
   tcase_add_test(tc_core, test_pgmoneta_restore);
   suite_add_tcase(s, tc_core);

   return s;
}