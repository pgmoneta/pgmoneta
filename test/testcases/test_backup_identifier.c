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
 */

#include <pgmoneta.h>
#include <art.h>
#include <backup.h>
#include <info.h>
#include <utils.h>
#include <mctf.h>
#include <tsclient.h>
#include <tscommon.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

static void
create_mock_backup(char* label, char* lsn, int timeline, int status)
{
   char* server_path = pgmoneta_get_server_backup(PRIMARY_SERVER);
   char path[MAX_PATH];
   char info_path[MAX_PATH];
   FILE* file;

   snprintf(path, sizeof(path), "%s/%s", server_path, label);
   pgmoneta_mkdir(path);

   snprintf(info_path, sizeof(info_path), "%s/backup.info", path);
   file = fopen(info_path, "w");
   if (file)
   {
      fprintf(file, "LABEL=%s\n", label);
      fprintf(file, "STATUS=%d\n", status);
      fprintf(file, "START_WALPOS=%s\n", lsn);
      fprintf(file, "START_TIMELINE=%d\n", timeline);
      fprintf(file, "PGMONETA_VERSION=0.20.0\n");
      fclose(file);
   }
   free(server_path);
}

MCTF_TEST(test_backup_identifier_lsn)
{
   struct art* nodes = NULL;
   char label[MAX_PATH];
   char identifier[MAX_PATH];
   char* base = NULL;

   // Initialize config/shmem FIRST
   pgmoneta_test_setup();

   pgmoneta_art_create(&nodes);

   // Cleanup first (using initialized config/paths)
   base = getenv("PGMONETA_TEST_BASE_DIR");
   if (base != NULL)
   {
      pgmoneta_delete_directory(base);
      pgmoneta_mkdir(base);
   }

   // Setup (now safe): Create 2 backups
   // Backup 1: 20250101000000, LSN 0/1000
   create_mock_backup("20250101000000", "0/1000", 1, 1);
   // Backup 2: 20250101010000, LSN 0/2000
   create_mock_backup("20250101010000", "0/2000", 1, 1);

   // Test 1: Target LSN between backups (0/1500) -> Should pick Backup 1 (0/1000)
   snprintf(identifier, sizeof(identifier), "target-lsn:0/1500");
   memset(label, 0, MAX_PATH);
   int ret = pgmoneta_get_backup_identifier(PRIMARY_SERVER, identifier, nodes, label);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "ret mismatch 1");
   MCTF_ASSERT_STR_EQ(label, "20250101000000", cleanup, "label mismatch 1");

   // Test 2: Target LSN after Backup 2 (0/3000) -> Should pick Backup 2 (0/2000)
   snprintf(identifier, sizeof(identifier), "target-lsn:0/3000");
   memset(label, 0, MAX_PATH);
   ret = pgmoneta_get_backup_identifier(PRIMARY_SERVER, identifier, nodes, label);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "ret mismatch 2");
   MCTF_ASSERT_STR_EQ(label, "20250101010000", cleanup, "label mismatch 2");

   // Test 3: Target LSN before Backup 1 (0/500) -> Should fail
   snprintf(identifier, sizeof(identifier), "target-lsn:0/500");
   memset(label, 0, MAX_PATH);
   ret = pgmoneta_get_backup_identifier(PRIMARY_SERVER, identifier, nodes, label);
   MCTF_ASSERT(ret != 0, cleanup, "ret should adhere to logic");

cleanup:
   if (nodes != NULL)
   {
      pgmoneta_art_destroy(nodes);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_backup_identifier_time)
{
   struct art* nodes = NULL;
   char label[MAX_PATH];
   char identifier[MAX_PATH];
   char* base = NULL;

   pgmoneta_test_setup();
   pgmoneta_art_create(&nodes);

   base = getenv("PGMONETA_TEST_BASE_DIR");
   if (base != NULL)
   {
      pgmoneta_delete_directory(base);
      pgmoneta_mkdir(base);
   }

   // Backup 1: 20230101000000
   create_mock_backup("20230101000000", "0/1000", 1, 1);
   // Backup 2: 20230101020000
   create_mock_backup("20230101020000", "0/2000", 1, 1);

   // Target Time: 2023-01-01 01:00:00 -> Should pick Backup 1
   snprintf(identifier, sizeof(identifier), "target-time:2023-01-01 01:00:00");
   memset(label, 0, MAX_PATH);
   int ret = pgmoneta_get_backup_identifier(PRIMARY_SERVER, identifier, nodes, label);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "ret mismatch");
   MCTF_ASSERT_STR_EQ(label, "20230101000000", cleanup, "label mismatch"); // 2023...0000 is older than 01:00:00

cleanup:
   if (nodes != NULL)
   {
      pgmoneta_art_destroy(nodes);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_backup_identifier_tli)
{
   struct art* nodes = NULL;
   char label[MAX_PATH];
   char identifier[MAX_PATH];
   char* base = NULL;

   pgmoneta_test_setup();
   pgmoneta_art_create(&nodes);

   base = getenv("PGMONETA_TEST_BASE_DIR");
   if (base != NULL)
   {
      pgmoneta_delete_directory(base);
      pgmoneta_mkdir(base);
   }

   // Backup 1: TLI 1
   create_mock_backup("20230101000000", "0/1000", 1, 1);
   // Backup 2: TLI 2
   create_mock_backup("20230101010000", "0/2000", 2, 1);

   // Target TLI: 1
   snprintf(identifier, sizeof(identifier), "target-tli:1");
   memset(label, 0, MAX_PATH);
   int ret = pgmoneta_get_backup_identifier(PRIMARY_SERVER, identifier, nodes, label);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "ret mismatch");
   MCTF_ASSERT_STR_EQ(label, "20230101000000", cleanup, "label mismatch");

cleanup:
   if (nodes != NULL)
   {
      pgmoneta_art_destroy(nodes);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}
