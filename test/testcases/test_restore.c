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
 */

#include <pgmoneta.h>
#include <info.h>
#include <management.h>
#include <tsclient.h>
#include <tscommon.h>
#include <utils.h>
#include <mctf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

MCTF_TEST(test_pgmoneta_restore_full)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "current", false, 0) == 0, cleanup, "restore operation failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_restore_incremental_chain)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup_chain() == 0, cleanup, "backup chain failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "current", false, 0) == 0, cleanup, "restore operation failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_pgmoneta_restore_no_workers_copy_failure)
{
   char* backup_dir = NULL;
   char* label_file = NULL;
   int number_of_backups = 0;
   struct backup** backups = NULL;

   if (geteuid() == 0)
   {
      MCTF_SKIP("file permissions are not enforced for root");
   }

   pgmoneta_test_setup();

   /* Without workers every file is copied synchronously */
   MCTF_ASSERT(pgmoneta_tsclient_conf_set("server.primary.workers", "0", 0) == 0, cleanup, "failed to set workers = 0");

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   backup_dir = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT(pgmoneta_load_infos(backup_dir, &number_of_backups, &backups) == 0 && number_of_backups == 1, cleanup, "failed to load the backup");

   label_file = pgmoneta_get_server_backup_identifier_data(PRIMARY_SERVER, backups[0]->label);
   label_file = pgmoneta_append(label_file, "backup_label");
   MCTF_ASSERT(chmod(label_file, 0) == 0, cleanup, "failed to make backup_label unreadable");

   /* the file can't be read, so the check against backup.sha512 refuses the restore before the copy */
   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "current", false, MANAGEMENT_ERROR_RESTORE_ERROR) == 0, cleanup, "restore must fail when a file cannot be read");

cleanup:
   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);
   free(backup_dir);
   free(label_file);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

static int
file_has_line(char* path, char* expected)
{
   FILE* f = NULL;
   char buffer[2048];
   int found = 0;

   f = fopen(path, "r");
   if (f == NULL)
   {
      return 0;
   }

   while (fgets(buffer, sizeof(buffer), f) != NULL)
   {
      buffer[strcspn(buffer, "\r\n")] = '\0';
      if (strcmp(buffer, expected) == 0)
      {
         found = 1;
         break;
      }
   }

   fclose(f);

   return found;
}

MCTF_TEST(test_pgmoneta_restore_position_quote)
{
   char* backup_dir = NULL;
   char path[MAX_PATH];
   int number_of_backups = 0;
   struct backup** backups = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   /* A restore point name can hold a quote, which has to survive postgresql.auto.conf */
   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "name=before it's deployed", false, 0) == 0, cleanup, "restore operation failed");

   backup_dir = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT(pgmoneta_load_infos(backup_dir, &number_of_backups, &backups) == 0 && number_of_backups == 1, cleanup, "failed to load the backup");

   pgmoneta_snprintf(path, sizeof(path), "%s/primary-%s/postgresql.auto.conf", TEST_RESTORE_DIR, backups[0]->label);
   MCTF_ASSERT(file_has_line(path, "recovery_target_name = 'before it''s deployed'"), cleanup, "the restore point name is not quoted in %s", path);

cleanup:
   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);
   free(backup_dir);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_pgmoneta_restore_position_line_break)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   /* A line break would start a new setting in postgresql.auto.conf */
   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "name=x\nshared_preload_libraries=evil", false, MANAGEMENT_ERROR_RESTORE_ERROR) == 0, cleanup, "restore must fail when the position has a line break");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_pgmoneta_restore_position_too_long)
{
   char position[1024];

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   /* The position is parsed into fixed size buffers */
   memset(position, 0, sizeof(position));
   memcpy(position, "name=", 5);
   memset(position + 5, 'a', 700);

   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", position, false, MANAGEMENT_ERROR_RESTORE_ERROR) == 0, cleanup, "restore must fail when the position is too long");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}
