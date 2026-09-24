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
#include <compression.h>
#include <configuration.h>
#include <tsclient.h>
#include <tscommon.h>
#include <mctf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <utils.h>
#include <tsclient_helpers.h>

/* Server-Side Compression Tests */

static int
inject_pgmoneta_compression_encryption(const char* path)
{
   FILE* in = NULL;
   FILE* out = NULL;
   char* content = NULL;
   char* new_content = NULL;
   char* compression_line = NULL;
   char* compression_end = NULL;
   long size = 0;
   size_t prefix_len = 0;
   size_t suffix_len = 0;
   const char* needle = "\ncompression = zstd\n";
   const char* injection = "\ncompression = server-zstd\nencryption = aes\n";
   int ret = 1;

   in = fopen(path, "r");
   if (in == NULL)
   {
      goto cleanup;
   }

   if (fseek(in, 0, SEEK_END) != 0)
   {
      goto cleanup;
   }

   size = ftell(in);
   if (size < 0)
   {
      goto cleanup;
   }

   if (fseek(in, 0, SEEK_SET) != 0)
   {
      goto cleanup;
   }

   content = calloc((size_t)size + 1, 1);
   if (content == NULL)
   {
      goto cleanup;
   }

   if (fread(content, 1, (size_t)size, in) != (size_t)size)
   {
      goto cleanup;
   }

   compression_line = strstr(content, needle);
   if (compression_line == NULL)
   {
      goto cleanup;
   }

   compression_end = compression_line + strlen(needle);

   prefix_len = (size_t)(compression_line - content);
   suffix_len = (size_t)size - (size_t)(compression_end - content);

   new_content = calloc((size_t)size + strlen(injection) + 1, 1);
   if (new_content == NULL)
   {
      goto cleanup;
   }

   memcpy(new_content, content, prefix_len);
   memcpy(new_content + prefix_len, injection, strlen(injection));
   memcpy(new_content + prefix_len + strlen(injection), compression_end, suffix_len);

   out = fopen(path, "w");
   if (out == NULL)
   {
      goto cleanup;
   }

   if (fwrite(new_content, 1, prefix_len + strlen(injection) + suffix_len, out) !=
       prefix_len + strlen(injection) + suffix_len)
   {
      goto cleanup;
   }

   ret = 0;

cleanup:
   if (out != NULL)
   {
      fclose(out);
   }
   if (in != NULL)
   {
      fclose(in);
   }
   free(new_content);
   free(content);
   return ret;
}

static bool
has_repeated_zstd_compression_or_encryption_suffix(const char* file_name)
{
   char stripped[1024];
   int zstd_count = 0;
   int aes_count = 0;

   if (file_name == NULL)
   {
      return false;
   }

   size_t len = strlen(file_name);
   if (len >= sizeof(stripped))
   {
      len = sizeof(stripped) - 1;
   }
   memcpy(stripped, file_name, len);
   stripped[len] = '\0';

   while (pgmoneta_ends_with(stripped, ".zstd") || pgmoneta_ends_with(stripped, ".aes"))
   {
      if (pgmoneta_ends_with(stripped, ".aes"))
      {
         char* dot = strrchr(stripped, '.');
         if (dot)
         {
            *dot = '\0';
         }
         aes_count++;
      }
      else if (pgmoneta_ends_with(stripped, ".zstd"))
      {
         char* dot = strrchr(stripped, '.');
         if (dot)
         {
            *dot = '\0';
         }
         zstd_count++;
      }

      if (zstd_count > 1 || aes_count > 1)
      {
         return true;
      }
   }

   return false;
}

static int
check_no_double_compression(const char* path)
{
   DIR* dir = NULL;
   struct dirent* entry = NULL;
   char entry_path[MAX_PATH];

   if (!(dir = opendir(path)))
   {
      return 1;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (pgmoneta_compare_string(entry->d_name, ".") || pgmoneta_compare_string(entry->d_name, ".."))
      {
         continue;
      }

      if (entry->d_type != DT_REG)
      {
         continue;
      }

      pgmoneta_snprintf(entry_path, sizeof(entry_path), "%s/%s", path, entry->d_name);

      if (has_repeated_zstd_compression_or_encryption_suffix(entry->d_name))
      {
         goto error;
      }
   }

   closedir(dir);
   return 0;

error:
   if (dir != NULL)
   {
      closedir(dir);
   }
   return 1;
}

MCTF_TEST(test_pgmoneta_server_zstd_backup_restore)
{
   char* pg_version_str = NULL;
   int pg_version = 0;
   struct main_configuration* config = NULL;

   pgmoneta_test_setup();

   /* wal_compression = zstd was introduced in PostgreSQL 15. */
   pg_version_str = getenv("TEST_PG_VERSION");
   if (pg_version_str != NULL && strlen(pg_version_str) > 0)
   {
      pg_version = atoi(pg_version_str);
   }
   if (pg_version < 15)
   {
      MCTF_SKIP("server-zstd WAL compression requires PostgreSQL 15+; TEST_PG_VERSION=%s",
                pg_version_str != NULL ? pg_version_str : "(unset)");
   }

   config = (struct main_configuration*)shmem;

   /* Inject settings in the [pgmoneta] section to avoid [primary] parse errors. */
   MCTF_ASSERT(inject_pgmoneta_compression_encryption(config->common.configuration_path) == 0,
               cleanup,
               "failed to update pgmoneta.conf for server-zstd + aes: %s",
               config->common.configuration_path);

   MCTF_ASSERT(pgmoneta_tsclient_reload(0) == 0, cleanup,
               "failed to reload pgmoneta after updating pgmoneta.conf");

   /* Set server online before backup */
   MCTF_ASSERT(pgmoneta_tsclient_mode("primary", "online", 0) == 0,
               cleanup, "failed to set server online before backup");

   /* Full backup with server-zstd + AES */
   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, 0) == 0,
               cleanup, "backup with server-zstd compression and AES encryption failed");

   /* Restore newest backup */
   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "current", 0) == 0,
               cleanup, "restore of server-zstd compressed backup failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_compression_is_compressed)
{
   MCTF_ASSERT(pgmoneta_is_compressed("file.zstd"), cleanup, "is_compressed zstd failed");
   MCTF_ASSERT(pgmoneta_is_compressed("file.lz4"), cleanup, "is_compressed lz4 failed");
   MCTF_ASSERT(pgmoneta_is_compressed("file.bz2"), cleanup, "is_compressed bz2 failed");
   MCTF_ASSERT(pgmoneta_is_compressed("file.gz"), cleanup, "is_compressed gz failed");
   MCTF_ASSERT(!pgmoneta_is_compressed("file.txt"), cleanup, "is_compressed negative failed");
   MCTF_ASSERT(!pgmoneta_is_compressed(NULL), cleanup, "is_compressed NULL failed");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_not_double_compression_full_backup)
{
   char* pg_version_str = NULL;
   int pg_version = 0;
   struct main_configuration* config = NULL;
   struct json* response = NULL;
   struct json* b0 = NULL;
   char* label = NULL;
   char* data_dir = NULL;

   pgmoneta_test_setup();

   /* wal_compression = zstd was introduced in PostgreSQL 15. */
   pg_version_str = getenv("TEST_PG_VERSION");
   if (pg_version_str != NULL && strlen(pg_version_str) > 0)
   {
      pg_version = atoi(pg_version_str);
   }
   if (pg_version < 15)
   {
      MCTF_SKIP("server-zstd WAL compression requires PostgreSQL 15+; TEST_PG_VERSION=%s",
                pg_version_str != NULL ? pg_version_str : "(unset)");
   }

   config = (struct main_configuration*)shmem;

   /* Inject settings in the [pgmoneta] section to avoid [primary] parse errors. */
   MCTF_ASSERT(inject_pgmoneta_compression_encryption(config->common.configuration_path) == 0,
               cleanup,
               "failed to update pgmoneta.conf for server-zstd + aes: %s",
               config->common.configuration_path);

   MCTF_ASSERT(pgmoneta_tsclient_reload(0) == 0, cleanup,
               "failed to reload pgmoneta after updating pgmoneta.conf");

   /* Set server online before backup */
   MCTF_ASSERT(pgmoneta_tsclient_mode("primary", "online", 0) == 0,
               cleanup, "failed to set server online before backup");

   /* Full backup with server-zstd + AES */
   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, 0) == 0,
               cleanup, "backup with server-zstd compression and AES encryption failed");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), cleanup, "list backup failed");
   b0 = pgmoneta_tsclient_get_backup(response, 0);
   MCTF_ASSERT_PTR_NONNULL(b0, cleanup, "backup 0 null");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(b0), "FULL", cleanup, "backup 0 type mismatch");

   label = pgmoneta_tsclient_get_backup_label(b0);
   MCTF_ASSERT_PTR_NONNULL(label, cleanup, "label null");

   data_dir = pgmoneta_get_server_backup_identifier_data(PRIMARY_SERVER, label);
   MCTF_ASSERT_PTR_NONNULL(data_dir, cleanup, "data_dir null");

   MCTF_ASSERT(check_no_double_compression(data_dir) == 0,
               cleanup, "Double compression detected in %s", data_dir);

cleanup:
   free(data_dir);
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_not_double_compression_incremental_backup)
{
   char* pg_version_str = NULL;
   int pg_version = 0;
   struct main_configuration* config = NULL;
   struct json* response = NULL;
   struct json* b1 = NULL;
   char* label = NULL;
   char* data_dir = NULL;

   pgmoneta_test_setup();

   /* wal_compression = zstd was introduced in PostgreSQL 15. */
   pg_version_str = getenv("TEST_PG_VERSION");
   if (pg_version_str != NULL && strlen(pg_version_str) > 0)
   {
      pg_version = atoi(pg_version_str);
   }
   if (pg_version < 15)
   {
      MCTF_SKIP("server-zstd WAL compression requires PostgreSQL 15+; TEST_PG_VERSION=%s",
                pg_version_str != NULL ? pg_version_str : "(unset)");
   }

   config = (struct main_configuration*)shmem;

   /* Inject settings in the [pgmoneta] section to avoid [primary] parse errors. */
   MCTF_ASSERT(inject_pgmoneta_compression_encryption(config->common.configuration_path) == 0,
               cleanup,
               "failed to update pgmoneta.conf for server-zstd + aes: %s",
               config->common.configuration_path);

   MCTF_ASSERT(pgmoneta_tsclient_reload(0) == 0, cleanup,
               "failed to reload pgmoneta after updating pgmoneta.conf");

   /* Set server online before backup */
   MCTF_ASSERT(pgmoneta_tsclient_mode("primary", "online", 0) == 0,
               cleanup, "failed to set server online before backup");

   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, 0) == 0,
               cleanup, "backup with server-zstd compression and AES encryption failed");
   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", "newest", 0) == 0,
               cleanup, "backup with server-zstd compression and AES encryption failed");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), cleanup, "list backup failed");
   b1 = pgmoneta_tsclient_get_backup(response, 1);
   MCTF_ASSERT_PTR_NONNULL(b1, cleanup, "backup 1 null");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(b1), "INCREMENTAL", cleanup, "backup 1 type mismatch");

   label = pgmoneta_tsclient_get_backup_label(b1);
   MCTF_ASSERT_PTR_NONNULL(label, cleanup, "label null");

   data_dir = pgmoneta_get_server_backup_identifier_data(PRIMARY_SERVER, label);
   MCTF_ASSERT_PTR_NONNULL(data_dir, cleanup, "data_dir null");

   MCTF_ASSERT(check_no_double_compression(data_dir) == 0,
               cleanup, "Double compression detected in %s", data_dir);

cleanup:
   free(data_dir);
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_not_double_compression_wal_files)
{
   char* pg_version_str = NULL;
   int pg_version = 0;
   struct main_configuration* config = NULL;
   char* wal_dir = NULL;

   pgmoneta_test_setup();

   /* wal_compression = zstd was introduced in PostgreSQL 15. */
   pg_version_str = getenv("TEST_PG_VERSION");
   if (pg_version_str != NULL && strlen(pg_version_str) > 0)
   {
      pg_version = atoi(pg_version_str);
   }
   if (pg_version < 15)
   {
      MCTF_SKIP("server-zstd WAL compression requires PostgreSQL 15+; TEST_PG_VERSION=%s",
                pg_version_str != NULL ? pg_version_str : "(unset)");
   }

   config = (struct main_configuration*)shmem;

   /* Inject settings in the [pgmoneta] section to avoid [primary] parse errors. */
   MCTF_ASSERT(inject_pgmoneta_compression_encryption(config->common.configuration_path) == 0,
               cleanup,
               "failed to update pgmoneta.conf for server-zstd + aes: %s",
               config->common.configuration_path);

   MCTF_ASSERT(pgmoneta_tsclient_reload(0) == 0, cleanup,
               "failed to reload pgmoneta after updating pgmoneta.conf");

   /* Set server online before backup */
   MCTF_ASSERT(pgmoneta_tsclient_mode("primary", "online", 0) == 0,
               cleanup, "failed to set server online before backup");

   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, 0) == 0,
               cleanup, "backup with server-zstd compression and AES encryption failed");
   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", "newest", 0) == 0,
               cleanup, "backup with server-zstd compression and AES encryption failed");

   wal_dir = pgmoneta_get_server_wal(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(wal_dir, cleanup, "wal_dir null");

   MCTF_ASSERT(check_no_double_compression(wal_dir) == 0,
               cleanup, "Double compression detected in %s", wal_dir);

cleanup:
   free(wal_dir);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}