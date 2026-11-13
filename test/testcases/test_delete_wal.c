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
#include <delete.h>
#include <info.h>
#include <utils.h>
#include <mctf.h>
#include <tscommon.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WAL_1       "000000010000000000000001"
#define WAL_2       "000000010000000000000002"
#define WAL_3       "000000010000000000000003"
#define WAL_4       "000000010000000000000004"
#define WAL_PARTIAL "000000010000000000000005.partial"

static char tmp_base[MAX_PATH];
static char saved_base_dir[MAX_PATH];
static char saved_wal_shipping[MAX_PATH];
static bool swapped = false;

static void
touch(char* dir, char* file)
{
   char path[MAX_PATH];
   FILE* f = NULL;

   pgmoneta_snprintf(path, sizeof(path), "%s%s", dir, file);
   f = fopen(path, "w");
   if (f != NULL)
   {
      fclose(f);
   }
}

static bool
wal_exists(char* dir, char* file)
{
   char path[MAX_PATH];

   pgmoneta_snprintf(path, sizeof(path), "%s%s", dir, file);
   return pgmoneta_exists(path);
}

static void
create_wal(void)
{
   char* wal = pgmoneta_get_server_wal(PRIMARY_SERVER);
   char* ws = pgmoneta_get_server_wal_shipping_wal(PRIMARY_SERVER);

   pgmoneta_mkdir(wal);
   pgmoneta_mkdir(ws);

   touch(wal, WAL_1);
   touch(wal, WAL_2);
   touch(wal, WAL_3);
   touch(wal, WAL_4);
   touch(wal, WAL_PARTIAL);
   touch(ws, WAL_1);
   touch(ws, WAL_2);
   touch(ws, WAL_3);
   touch(ws, WAL_4);

   free(wal);
   free(ws);
}

static void
create_backup(char* label, int status, int keep, char* first_wal)
{
   char* dir = NULL;
   char* pg_wal = NULL;
   char info[MAX_PATH];
   FILE* f = NULL;

   dir = pgmoneta_get_server_backup_identifier(PRIMARY_SERVER, label);
   pgmoneta_mkdir(dir);

   pgmoneta_snprintf(info, sizeof(info), "%sbackup.info", dir);
   f = fopen(info, "w");
   if (f != NULL)
   {
      fprintf(f, "LABEL=%s\n", label);
      fprintf(f, "STATUS=%d\n", status);
      fprintf(f, "KEEP=%d\n", keep);
      fclose(f);
   }

   pg_wal = pgmoneta_get_server_backup_identifier_data_wal(PRIMARY_SERVER, label);
   pgmoneta_mkdir(pg_wal);
   if (first_wal != NULL)
   {
      touch(pg_wal, first_wal);
   }

   free(pg_wal);
   free(dir);
}

static void
count_wal(int* wal_count, int* ws_count)
{
   char* files[] = {WAL_1, WAL_2, WAL_3, WAL_4};
   char* wal = pgmoneta_get_server_wal(PRIMARY_SERVER);
   char* ws = pgmoneta_get_server_wal_shipping_wal(PRIMARY_SERVER);

   *wal_count = 0;
   *ws_count = 0;

   for (int i = 0; i < 4; i++)
   {
      if (wal_exists(wal, files[i]))
      {
         (*wal_count)++;
      }
      if (wal_exists(ws, files[i]))
      {
         (*ws_count)++;
      }
   }

   free(wal);
   free(ws);
}

MCTF_TEST_SETUP(delete_wal)
{
   struct main_configuration* config = NULL;
   char ws[MAX_PATH];

   pgmoneta_test_setup();

   config = (struct main_configuration*)shmem;

   memset(tmp_base, 0, sizeof(tmp_base));
   pgmoneta_snprintf(tmp_base, sizeof(tmp_base), "%s/delete_wal", TEST_BASE_DIR);
   pgmoneta_delete_directory(tmp_base);
   if (pgmoneta_mkdir(tmp_base))
   {
      return;
   }

   memcpy(saved_base_dir, config->base_dir, sizeof(saved_base_dir));
   memcpy(saved_wal_shipping, config->common.servers[PRIMARY_SERVER].wal_shipping, sizeof(saved_wal_shipping));

   memset(config->base_dir, 0, sizeof(config->base_dir));
   pgmoneta_snprintf(config->base_dir, sizeof(config->base_dir), "%s/base", tmp_base);

   pgmoneta_snprintf(ws, sizeof(ws), "%s/shipping", tmp_base);
   memset(config->common.servers[PRIMARY_SERVER].wal_shipping, 0, sizeof(config->common.servers[PRIMARY_SERVER].wal_shipping));
   memcpy(config->common.servers[PRIMARY_SERVER].wal_shipping, ws, strlen(ws));

   swapped = true;
}

MCTF_TEST_TEARDOWN(delete_wal)
{
   struct main_configuration* config = (struct main_configuration*)shmem;

   if (swapped)
   {
      memcpy(config->base_dir, saved_base_dir, sizeof(saved_base_dir));
      memcpy(config->common.servers[PRIMARY_SERVER].wal_shipping, saved_wal_shipping, sizeof(saved_wal_shipping));
      pgmoneta_delete_directory(tmp_base);
      swapped = false;
   }

   pgmoneta_test_teardown();
}

MCTF_TEST(test_delete_wal_no_backups)
{
   int wal = -1;
   int ws = -1;
   char* wal_dir = NULL;

   MCTF_ASSERT(swapped, cleanup, "failed to create temporary base directory");

   create_wal();

   MCTF_ASSERT_INT_EQ(pgmoneta_delete_wal(PRIMARY_SERVER), 0, cleanup, "delete_wal must succeed without backups");

   count_wal(&wal, &ws);
   MCTF_ASSERT_INT_EQ(wal, 0, cleanup, "wal/ must be empty when there are no backups");
   MCTF_ASSERT_INT_EQ(ws, 0, cleanup, "wal_shipping must be empty when there are no backups");

   wal_dir = pgmoneta_get_server_wal(PRIMARY_SERVER);
   MCTF_ASSERT(wal_exists(wal_dir, WAL_PARTIAL), cleanup, "the segment being streamed (.partial) must be kept");

cleanup:
   free(wal_dir);
   MCTF_FINISH();
}

MCTF_TEST(test_delete_wal_older_than_oldest_backup)
{
   char* wal = NULL;
   char* ws = NULL;

   MCTF_ASSERT(swapped, cleanup, "failed to create temporary base directory");

   create_wal();
   create_backup("20260101000000", 1, 0, WAL_3);
   create_backup("20260102000000", 1, 0, WAL_4);

   MCTF_ASSERT_INT_EQ(pgmoneta_delete_wal(PRIMARY_SERVER), 0, cleanup, "delete_wal failed");

   wal = pgmoneta_get_server_wal(PRIMARY_SERVER);
   ws = pgmoneta_get_server_wal_shipping_wal(PRIMARY_SERVER);

   MCTF_ASSERT(!wal_exists(wal, WAL_1), cleanup, "wal/ " WAL_1 " must be deleted");
   MCTF_ASSERT(!wal_exists(wal, WAL_2), cleanup, "wal/ " WAL_2 " must be deleted");
   MCTF_ASSERT(wal_exists(wal, WAL_3), cleanup, "wal/ " WAL_3 " must be kept");
   MCTF_ASSERT(wal_exists(wal, WAL_4), cleanup, "wal/ " WAL_4 " must be kept");

   MCTF_ASSERT(!wal_exists(ws, WAL_1), cleanup, "wal_shipping " WAL_1 " must be deleted");
   MCTF_ASSERT(!wal_exists(ws, WAL_2), cleanup, "wal_shipping " WAL_2 " must be deleted");
   MCTF_ASSERT(wal_exists(ws, WAL_3), cleanup, "wal_shipping " WAL_3 " must be kept");
   MCTF_ASSERT(wal_exists(ws, WAL_4), cleanup, "wal_shipping " WAL_4 " must be kept");

cleanup:
   free(wal);
   free(ws);
   MCTF_FINISH();
}

MCTF_TEST(test_delete_wal_oldest_backup_kept)
{
   int wal = -1;
   int ws = -1;

   MCTF_ASSERT(swapped, cleanup, "failed to create temporary base directory");

   create_wal();
   create_backup("20260101000000", 1, 1, WAL_3);

   MCTF_ASSERT_INT_EQ(pgmoneta_delete_wal(PRIMARY_SERVER), 0, cleanup, "delete_wal failed");

   count_wal(&wal, &ws);
   MCTF_ASSERT_INT_EQ(wal, 4, cleanup, "no WAL may be deleted when the oldest backup is retained");
   MCTF_ASSERT_INT_EQ(ws, 4, cleanup, "no shipped WAL may be deleted when the oldest backup is retained");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_delete_wal_oldest_backup_invalid)
{
   int wal = -1;
   int ws = -1;

   MCTF_ASSERT(swapped, cleanup, "failed to create temporary base directory");

   create_wal();
   create_backup("20260101000000", 0, 0, WAL_3);

   MCTF_ASSERT_INT_EQ(pgmoneta_delete_wal(PRIMARY_SERVER), 0, cleanup, "delete_wal failed");

   count_wal(&wal, &ws);
   MCTF_ASSERT_INT_EQ(wal, 4, cleanup, "no WAL may be deleted when the oldest backup is invalid");
   MCTF_ASSERT_INT_EQ(ws, 4, cleanup, "no shipped WAL may be deleted when the oldest backup is invalid");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_delete_wal_oldest_backup_without_wal)
{
   int wal = -1;
   int ws = -1;

   MCTF_ASSERT(swapped, cleanup, "failed to create temporary base directory");

   create_wal();
   create_backup("20260101000000", 1, 0, NULL);

   MCTF_ASSERT_INT_EQ(pgmoneta_delete_wal(PRIMARY_SERVER), 0, cleanup, "delete_wal failed");

   count_wal(&wal, &ws);
   MCTF_ASSERT_INT_EQ(wal, 4, cleanup, "no WAL may be deleted when the oldest backup has no WAL");
   MCTF_ASSERT_INT_EQ(ws, 4, cleanup, "no shipped WAL may be deleted when the oldest backup has no WAL");

cleanup:
   MCTF_FINISH();
}
