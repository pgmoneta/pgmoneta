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

#include <pgmoneta.h>
#include <mctf.h>
#include <tscommon.h>
#include <utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MUSE_BACKUP_ID "20260101-000000F"

static char muse_dir[MAX_PATH];

static int
write_file(char* path, char* content)
{
   FILE* f = NULL;

   if (path == NULL || (f = fopen(path, "w")) == NULL)
   {
      return 1;
   }

   fputs(content, f);
   fclose(f);

   return 0;
}

/*
 * A pgBackRest repository with the directory of the backup but without its
 * backup.info, so pgmoneta-muse gets past its argument checks and then fails
 */
static int
prepare_muse(char* conf, size_t size)
{
   char path[MAX_PATH];
   char content[4 * MAX_PATH];

   pgmoneta_snprintf(muse_dir, sizeof(muse_dir), "%s/muse", TEST_BASE_DIR);
   pgmoneta_delete_directory(muse_dir);

   pgmoneta_snprintf(path, sizeof(path), "%s/source/repo/%s", muse_dir, MUSE_BACKUP_ID);
   if (pgmoneta_mkdir(path))
   {
      return 1;
   }

   pgmoneta_snprintf(path, sizeof(path), "%s/source/wal", muse_dir);
   if (pgmoneta_mkdir(path))
   {
      return 1;
   }

   pgmoneta_snprintf(path, sizeof(path), "%s/target", muse_dir);
   if (pgmoneta_mkdir(path))
   {
      return 1;
   }

   pgmoneta_snprintf(conf, size, "%s/pgmoneta_muse.conf", muse_dir);
   pgmoneta_snprintf(content, sizeof(content),
                     "log_type = file\n"
                     "log_level = info\n"
                     "log_path = %s/pgmoneta_muse.log\n"
                     "base_dir = %s/target\n"
                     "compression = none\n"
                     "encryption = none\n"
                     "source_password = wrong\n"
                     "source_tool = pgbackrest\n"
                     "source_data_dir = %s/source/repo\n"
                     "source_wal_dir = %s/source/wal\n",
                     muse_dir, muse_dir, muse_dir, muse_dir);

   return write_file(conf, content);
}

static int
run_muse(char* conf, int* exit_code)
{
   char muse[MAX_PATH];
   char command[3 * MAX_PATH];
   char* output = NULL;
   int ret = 1;

   if (pgmoneta_test_resolve_binary_path("pgmoneta-muse", muse))
   {
      return 1;
   }

   pgmoneta_snprintf(command, sizeof(command), "%s -c %s -s primary -b %s", muse, conf, MUSE_BACKUP_ID);
   ret = pgmoneta_test_exec_command(command, &output, exit_code);
   free(output);

   return ret;
}

MCTF_TEST(test_muse_failed_migration_keeps_existing_data)
{
   char conf[MAX_PATH];
   char backup[MAX_PATH];
   char wal[MAX_PATH];
   char path[MAX_PATH];
   int exit_code = 0;

   pgmoneta_test_setup();

   MCTF_ASSERT(!prepare_muse(conf, sizeof(conf)), cleanup, "could not prepare the migration");

   pgmoneta_snprintf(path, sizeof(path), "%s/target/primary/backup/20200101000000", muse_dir);
   MCTF_ASSERT(!pgmoneta_mkdir(path), cleanup, "could not create the existing backup");
   pgmoneta_snprintf(backup, sizeof(backup), "%s/backup.info", path);
   MCTF_ASSERT(!write_file(backup, "status=1\n"), cleanup, "could not create the existing backup.info");

   pgmoneta_snprintf(path, sizeof(path), "%s/target/primary/wal", muse_dir);
   MCTF_ASSERT(!pgmoneta_mkdir(path), cleanup, "could not create the existing WAL directory");
   pgmoneta_snprintf(wal, sizeof(wal), "%s/000000010000000000000001", path);
   MCTF_ASSERT(!write_file(wal, "wal"), cleanup, "could not create the existing WAL segment");

   MCTF_ASSERT(!run_muse(conf, &exit_code), cleanup, "could not run pgmoneta-muse");

   MCTF_ASSERT(pgmoneta_exists(backup), cleanup, "the failed migration removed an existing backup");
   MCTF_ASSERT(pgmoneta_exists(wal), cleanup, "the failed migration removed an existing WAL segment");
   MCTF_ASSERT(exit_code != 0, cleanup, "a failed migration exited with 0");

cleanup:
   pgmoneta_delete_directory(muse_dir);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_muse_failed_check_writes_nothing)
{
   char conf[MAX_PATH];
   char path[MAX_PATH];
   int exit_code = 0;

   pgmoneta_test_setup();

   MCTF_ASSERT(!prepare_muse(conf, sizeof(conf)), cleanup, "could not prepare the migration");

   MCTF_ASSERT(!run_muse(conf, &exit_code), cleanup, "could not run pgmoneta-muse");

   pgmoneta_snprintf(path, sizeof(path), "%s/target/primary", muse_dir);
   MCTF_ASSERT(!pgmoneta_exists(path), cleanup, "a migration that failed its checks wrote into the target");
   MCTF_ASSERT(exit_code != 0, cleanup, "a failed migration exited with 0");

cleanup:
   pgmoneta_delete_directory(muse_dir);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}
