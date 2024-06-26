/*
 * Copyright (C) 2024 The pgmoneta community
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

#include "pgmoneta_test.h"

#define BUFFER_SIZE 8192

#define PGMONETA_LOG_FILE_PATH     "/tmp/pgmoneta.log"
#define PGMONETA_BACKUP_LOG     "INFO  backup.c:140 Backup: primary/"
#define PGMONETA_RESTORE_LOG     "INFO  restore.c:106 Restore: primary/"

// test backup
START_TEST(test_pgmoneta_backup)
{
   FILE* log_file;
   char log_entry[BUFFER_SIZE];
   int found = 0;

   int result = system("su - pgmoneta -c '/pgmoneta/build/src/pgmoneta-cli -c /pgmoneta/pgmoneta.conf backup primary'");
   ck_assert_int_eq(result, 0);

   log_file = fopen(PGMONETA_LOG_FILE_PATH, "r");
   ck_assert_msg(log_file != NULL, "Log file could not be opened");

   while (fgets(log_entry, sizeof(log_entry), log_file) != NULL)
   {
      if (strstr(log_entry, PGMONETA_BACKUP_LOG) != NULL)
      {
         found = 1;
         break;
      }
   }
   fclose(log_file);

   ck_assert_msg(found, "Expected log entry not found in the log file");
}
END_TEST
// test restore
START_TEST(test_pgmoneta_restore)
{
   FILE* log_file;
   char log_entry[BUFFER_SIZE];
   int found = 0;

   int result = system("su - pgmoneta -c '/pgmoneta/build/src/pgmoneta-cli -c /pgmoneta/pgmoneta.conf restore primary newest current /pgmoneta/'");
   ck_assert_int_eq(result, 0);

   log_file = fopen(PGMONETA_LOG_FILE_PATH, "r");
   ck_assert_msg(log_file != NULL, "Log file could not be opened");

   while (fgets(log_entry, sizeof(log_entry), log_file) != NULL)
   {
      if (strstr(log_entry, PGMONETA_RESTORE_LOG) != NULL)
      {
         found = 1;
         break;
      }
   }
   fclose(log_file);

   ck_assert_msg(found, "Expected log entry not found in the log file");
}
END_TEST

Suite*
pgmoneta_suite(void)
{
   Suite* s;
   TCase* tc_core;

   s = suite_create("pgmoneta");

   tc_core = tcase_create("Core");

   tcase_add_test(tc_core, test_pgmoneta_backup);
   tcase_add_test(tc_core, test_pgmoneta_restore);
   suite_add_tcase(s, tc_core);

   return s;
}
