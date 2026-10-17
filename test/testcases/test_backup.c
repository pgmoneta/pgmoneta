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
#include <backup.h>
#include <delete.h>
#include <deque.h>
#include <info.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <tsclient.h>
#include <tsclient_helpers.h>
#include <tscommon.h>
#include <mctf.h>
#include <utils.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int
create_incomplete_backup(char* label, size_t size)
{
   time_t now;
   char* data = NULL;
   int ret = 1;

   now = time(NULL);
   if (strftime(label, size, "%Y%m%d%H%M%S", localtime(&now)) == 0)
   {
      goto done;
   }

   data = pgmoneta_get_server_backup_identifier_data(PRIMARY_SERVER, label);
   if (data == NULL || pgmoneta_exists(data) || pgmoneta_mkdir(data))
   {
      goto done;
   }

   ret = 0;

done:
   free(data);

   return ret;
}

static int
count_backup_directories(void)
{
   char* d = NULL;
   char** dirs = NULL;
   int n = 0;

   d = pgmoneta_get_server_backup(PRIMARY_SERVER);
   pgmoneta_get_directories(d, &n, &dirs);

   for (int i = 0; i < n; i++)
   {
      free(dirs[i]);
   }
   free(dirs);
   free(d);

   return n;
}

static int
count_incomplete_markers(void)
{
   char* d = NULL;
   int n = 0;
   struct deque* files = NULL;
   struct deque_iterator* iter = NULL;

   d = pgmoneta_get_server_backup(PRIMARY_SERVER);

   if (!pgmoneta_get_files(PGMONETA_FILE_TYPE_ALL, d, false, &files) && !pgmoneta_deque_iterator_create(files, &iter))
   {
      while (pgmoneta_deque_iterator_next(iter))
      {
         if (pgmoneta_ends_with((char*)pgmoneta_value_data(iter->value), BACKUP_INCOMPLETE_SUFFIX))
         {
            n++;
         }
      }
   }

   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_deque_destroy(files);
   free(d);

   return n;
}

static int
create_file(char* path, char* content)
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

MCTF_TEST(test_pgmoneta_backup_full)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_backup_without_backup_directory)
{
   char* server_backup = NULL;

   pgmoneta_test_setup();

   server_backup = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(server_backup, cleanup, "server backup directory null");
   MCTF_ASSERT(!pgmoneta_delete_directory(server_backup), cleanup, "could not remove the backup directory");

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed without a backup directory");

cleanup:
   free(server_backup);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_backup_incremental_basic)
{
   struct json* response = NULL;
   int num_backups = 0;
   struct json* b0 = NULL;
   struct json* b1 = NULL;
   struct json* b2 = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup_chain() == 0, cleanup, "backup chain failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), cleanup, "list backup failed");

   num_backups = pgmoneta_tsclient_get_backup_count(response);
   MCTF_ASSERT_INT_EQ(num_backups, 3, cleanup, "backup count mismatch");

   b0 = pgmoneta_tsclient_get_backup(response, 0);
   b1 = pgmoneta_tsclient_get_backup(response, 1);
   b2 = pgmoneta_tsclient_get_backup(response, 2);

   MCTF_ASSERT_PTR_NONNULL(b0, cleanup, "backup 0 null");
   MCTF_ASSERT_PTR_NONNULL(b1, cleanup, "backup 1 null");
   MCTF_ASSERT_PTR_NONNULL(b2, cleanup, "backup 2 null");

   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(b0), "FULL", cleanup, "backup 0 type mismatch");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(b1), "INCREMENTAL", cleanup, "backup 1 type mismatch");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(b2), "INCREMENTAL", cleanup, "backup 2 type mismatch");

   MCTF_ASSERT(pgmoneta_tsclient_verify_backup_chain(b0, b1), cleanup, "backup 1 parent mismatch (should be b0)");
   MCTF_ASSERT(pgmoneta_tsclient_verify_backup_chain(b1, b2), cleanup, "backup 2 parent mismatch (should be b1)");

cleanup:
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_backup_incomplete_not_listed)
{
   char label[MISC_LENGTH];
   char* complete = NULL;
   char* server_backup = NULL;
   struct backup* newest = NULL;
   struct json* response = NULL;
   struct json* backup = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), cleanup, "list backup failed");
   backup = pgmoneta_tsclient_get_backup(response, 0);
   MCTF_ASSERT_PTR_NONNULL(backup, cleanup, "backup 0 null");
   MCTF_ASSERT_PTR_NONNULL(pgmoneta_tsclient_get_backup_label(backup), cleanup, "backup 0 label null");
   complete = strdup(pgmoneta_tsclient_get_backup_label(backup));
   MCTF_ASSERT_PTR_NONNULL(complete, cleanup, "strdup failed");
   pgmoneta_json_destroy(response);
   response = NULL;

   MCTF_ASSERT(!create_incomplete_backup(label, sizeof(label)), cleanup, "could not create the incomplete backup");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), cleanup, "list backup failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_get_backup_count(response), 1, cleanup, "the incomplete backup is listed");

   server_backup = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(server_backup, cleanup, "server backup directory null");
   MCTF_ASSERT(!pgmoneta_load_info(server_backup, "newest", &newest), cleanup, "could not load the newest backup");
   MCTF_ASSERT_PTR_NONNULL(newest, cleanup, "newest backup null");
   MCTF_ASSERT_STR_EQ(newest->label, complete, cleanup, "newest is not the complete backup");

cleanup:
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }
   free(newest);
   free(server_backup);
   free(complete);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_backup_incomplete_restore_newest)
{
   char label[MISC_LENGTH];

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");
   MCTF_ASSERT(!create_incomplete_backup(label, sizeof(label)), cleanup, "could not create the incomplete backup");

   MCTF_ASSERT(!pgmoneta_tsclient_restore("primary", "newest", "current", 0), cleanup, "restore of newest failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_backup_incomplete_incremental_newest)
{
   char label[MISC_LENGTH];

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");
   MCTF_ASSERT(!create_incomplete_backup(label, sizeof(label)), cleanup, "could not create the incomplete backup");

   MCTF_ASSERT(!pgmoneta_tsclient_backup("primary", "newest", 0), cleanup, "incremental backup of newest failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_backup_without_metadata_deleted)
{
   char label[MISC_LENGTH];
   char* incomplete = NULL;
   struct json* response = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");
   MCTF_ASSERT(!create_incomplete_backup(label, sizeof(label)), cleanup, "could not create the incomplete backup");

   MCTF_ASSERT(!pgmoneta_delete_backups_without_metadata(PRIMARY_SERVER), cleanup, "could not delete the incomplete backup");

   incomplete = pgmoneta_get_server_backup_identifier(PRIMARY_SERVER, label);
   MCTF_ASSERT_PTR_NONNULL(incomplete, cleanup, "incomplete backup directory null");
   MCTF_ASSERT(!pgmoneta_exists(incomplete), cleanup, "the incomplete backup still exists");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), cleanup, "list backup failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_get_backup_count(response), 1, cleanup, "the complete backup was deleted");

cleanup:
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }
   free(incomplete);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_backup_killed)
{
   int socket = -1;
   pid_t backup = 0;
   bool throttled = false;
   struct json* response = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");
   MCTF_ASSERT(!pgmoneta_tsclient_conf_set("server.primary.max_rate", "32768", 0), cleanup, "could not slow down backups");
   throttled = true;

   MCTF_ASSERT(!pgmoneta_connect_unix_socket(config->common.unix_socket_dir, MAIN_UDS, &socket), cleanup, "could not connect");
   MCTF_ASSERT(!pgmoneta_management_request_backup(NULL, socket, "primary", MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, NULL, MANAGEMENT_OUTPUT_FORMAT_JSON),
               cleanup, "could not request a backup");

   for (int i = 0; i < 100 && backup <= 0; i++)
   {
      SLEEP(100000000L);
      if (count_backup_directories() == 2)
      {
         backup = pgmoneta_test_find_process("backup/primary");
      }
   }
   MCTF_ASSERT(backup > 0, cleanup, "the backup did not start");
   MCTF_ASSERT_INT_EQ(count_incomplete_markers(), 1, cleanup, "the backup is not marked as incomplete");

   MCTF_ASSERT(!kill(backup, SIGKILL), cleanup, "could not kill the backup");
   backup = 0;

   for (int i = 0; i < 100 && (count_backup_directories() != 1 || pgmoneta_test_find_process("cleanup/primary") > 0); i++)
   {
      SLEEP(100000000L);
   }
   MCTF_ASSERT_INT_EQ(count_backup_directories(), 1, cleanup, "the killed backup was not removed");
   MCTF_ASSERT_INT_EQ(count_incomplete_markers(), 0, cleanup, "the incomplete marker was not removed");
   MCTF_ASSERT(pgmoneta_test_find_process("cleanup/primary") == 0, cleanup, "the cleanup did not finish");

   MCTF_ASSERT(!pgmoneta_tsclient_conf_set("server.primary.max_rate", "0", 0), cleanup, "could not reset max_rate");
   throttled = false;
   MCTF_ASSERT(!pgmoneta_tsclient_backup("primary", NULL, 0), cleanup, "backup after the killed one failed");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), cleanup, "list backup failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_get_backup_count(response), 2, cleanup, "backup count mismatch");

cleanup:
   if (backup > 0)
   {
      kill(backup, SIGKILL);
   }
   if (throttled)
   {
      pgmoneta_tsclient_conf_set("server.primary.max_rate", "0", 0);
   }
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }
   pgmoneta_disconnect(socket);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_backup_marked_incomplete_deleted)
{
   char label[MISC_LENGTH];
   char* incomplete = NULL;
   char* info = NULL;
   char* server_backup = NULL;
   char* marker = NULL;
   struct json* response = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");
   MCTF_ASSERT(!create_incomplete_backup(label, sizeof(label)), cleanup, "could not create the incomplete backup");

   incomplete = pgmoneta_get_server_backup_identifier(PRIMARY_SERVER, label);
   server_backup = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT(incomplete != NULL && server_backup != NULL, cleanup, "backup directory null");
   info = pgmoneta_format_and_append(NULL, "%sbackup.info", incomplete);
   marker = pgmoneta_format_and_append(NULL, "%s%s%s", server_backup, label, BACKUP_INCOMPLETE_SUFFIX);

   /* what pgmoneta leaves when it dies together with the backup after backup.info is written */
   MCTF_ASSERT(!create_file(info, "STATUS=1\n"), cleanup, "could not create backup.info");
   MCTF_ASSERT(!create_file(marker, ""), cleanup, "could not create the incomplete marker");

   MCTF_ASSERT(!pgmoneta_delete_incomplete_backups(PRIMARY_SERVER), cleanup, "could not delete the incomplete backup");

   MCTF_ASSERT(!pgmoneta_exists(incomplete), cleanup, "the incomplete backup still exists");
   MCTF_ASSERT(!pgmoneta_exists(marker), cleanup, "the incomplete marker still exists");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), cleanup, "list backup failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_get_backup_count(response), 1, cleanup, "the complete backup was deleted");

cleanup:
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }
   free(marker);
   free(server_backup);
   free(info);
   free(incomplete);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}
