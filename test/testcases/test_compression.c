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
#include <bzip2_compression.h>
#include <gzip_compression.h>
#include <lz4_compression.h>
#include <zstandard_compression.h>
#include <configuration.h>
#include <stream.h>
#include <tsclient.h>
#include <tscommon.h>
#include <mctf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <signal.h>
#include <sys/resource.h>
#include <utils.h>
#include <tsclient_helpers.h>

#include <lz4frame.h>

/* Server-Side Compression Tests */

#define SERVER_ZSTD_AES "\ncompression = server-zstd\nencryption = aes\n"

static int
inject_pgmoneta_compression_encryption(const char* path, const char* injection)
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
   MCTF_ASSERT(inject_pgmoneta_compression_encryption(config->common.configuration_path, SERVER_ZSTD_AES) == 0,
               cleanup,
               "failed to update pgmoneta.conf for server-zstd + aes: %s",
               config->common.configuration_path);

   MCTF_ASSERT(pgmoneta_tsclient_reload(0) == 0, cleanup,
               "failed to reload pgmoneta after updating pgmoneta.conf");

   /* Set server online before backup */
   MCTF_ASSERT(pgmoneta_tsclient_mode("primary", "online", 0) == 0,
               cleanup, "failed to set server online before backup");

   /* Full backup with server-zstd + AES */
   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, false, NULL, 0) == 0,
               cleanup, "backup with server-zstd compression and AES encryption failed");

   /* Restore newest backup */
   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "current", false, 0) == 0,
               cleanup, "restore of server-zstd compressed backup failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

static int
server_lz4_backup_restore(const char* injection, bool incremental)
{
   struct main_configuration* config = (struct main_configuration*)shmem;

   if (inject_pgmoneta_compression_encryption(config->common.configuration_path, injection))
   {
      return 1;
   }

   if (pgmoneta_tsclient_reload(0) ||
       pgmoneta_tsclient_mode("primary", "online", 0) ||
       pgmoneta_tsclient_backup("primary", NULL, false, NULL, 0))
   {
      return 1;
   }

   if (incremental && pgmoneta_tsclient_backup("primary", "newest", false, NULL, 0))
   {
      return 1;
   }

   return pgmoneta_tsclient_restore("primary", "newest", "current", false, 0);
}

static int
test_pg_version(void)
{
   char* pg_version_str = getenv("TEST_PG_VERSION");

   if (pg_version_str == NULL || strlen(pg_version_str) == 0)
   {
      return 0;
   }

   return atoi(pg_version_str);
}

MCTF_TEST(test_pgmoneta_server_lz4_backup_restore)
{
   pgmoneta_test_setup();

   if (test_pg_version() < 15)
   {
      MCTF_SKIP("server-side backup compression requires PostgreSQL 15+");
   }

   MCTF_ASSERT(server_lz4_backup_restore("\ncompression = server-lz4\n", false) == 0,
               cleanup, "backup and restore with server-lz4 failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_server_lz4_aes_backup_restore)
{
   pgmoneta_test_setup();

   if (test_pg_version() < 15)
   {
      MCTF_SKIP("server-side backup compression requires PostgreSQL 15+");
   }

   MCTF_ASSERT(server_lz4_backup_restore("\ncompression = server-lz4\nencryption = aes\n", false) == 0,
               cleanup, "backup and restore with server-lz4 and aes failed");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_server_lz4_incremental_backup_restore)
{
   struct json* response = NULL;
   struct json* b1 = NULL;

   pgmoneta_test_setup();

   /* PostgreSQL 14-16 incrementals are read through pgmoneta_ext, not received as a server archive */
   if (test_pg_version() < 17)
   {
      MCTF_SKIP("server-side incremental archives require PostgreSQL 17+");
   }

   MCTF_ASSERT(server_lz4_backup_restore("\ncompression = server-lz4\n", true) == 0,
               cleanup, "incremental backup and restore with server-lz4 failed");

   MCTF_ASSERT(!pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), cleanup, "list backup failed");
   b1 = pgmoneta_tsclient_get_backup(response, 1);
   MCTF_ASSERT_PTR_NONNULL(b1, cleanup, "backup 1 null");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(b1), "INCREMENTAL", cleanup, "backup 1 type mismatch");

cleanup:
   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }
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
   MCTF_ASSERT(inject_pgmoneta_compression_encryption(config->common.configuration_path, SERVER_ZSTD_AES) == 0,
               cleanup,
               "failed to update pgmoneta.conf for server-zstd + aes: %s",
               config->common.configuration_path);

   MCTF_ASSERT(pgmoneta_tsclient_reload(0) == 0, cleanup,
               "failed to reload pgmoneta after updating pgmoneta.conf");

   /* Set server online before backup */
   MCTF_ASSERT(pgmoneta_tsclient_mode("primary", "online", 0) == 0,
               cleanup, "failed to set server online before backup");

   /* Full backup with server-zstd + AES */
   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, false, NULL, 0) == 0,
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
   MCTF_ASSERT(inject_pgmoneta_compression_encryption(config->common.configuration_path, SERVER_ZSTD_AES) == 0,
               cleanup,
               "failed to update pgmoneta.conf for server-zstd + aes: %s",
               config->common.configuration_path);

   MCTF_ASSERT(pgmoneta_tsclient_reload(0) == 0, cleanup,
               "failed to reload pgmoneta after updating pgmoneta.conf");

   /* Set server online before backup */
   MCTF_ASSERT(pgmoneta_tsclient_mode("primary", "online", 0) == 0,
               cleanup, "failed to set server online before backup");

   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, false, NULL, 0) == 0,
               cleanup, "backup with server-zstd compression and AES encryption failed");
   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", "newest", false, NULL, 0) == 0,
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
   MCTF_ASSERT(inject_pgmoneta_compression_encryption(config->common.configuration_path, SERVER_ZSTD_AES) == 0,
               cleanup,
               "failed to update pgmoneta.conf for server-zstd + aes: %s",
               config->common.configuration_path);

   MCTF_ASSERT(pgmoneta_tsclient_reload(0) == 0, cleanup,
               "failed to reload pgmoneta after updating pgmoneta.conf");

   /* Set server online before backup */
   MCTF_ASSERT(pgmoneta_tsclient_mode("primary", "online", 0) == 0,
               cleanup, "failed to set server online before backup");

   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, false, NULL, 0) == 0,
               cleanup, "backup with server-zstd compression and AES encryption failed");
   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", "newest", false, NULL, 0) == 0,
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

MCTF_TEST(test_compression_lz4_empty_block)
{
   /* what a backup stores for an empty file: block size 1, then an empty LZ4 block */
   unsigned char empty_file[] = {0x01, 0x00, 0x00, 0x00, 0x00};
   char* lz4 = "test_lz4_empty_block.lz4";
   char* path = "test_lz4_empty_block";
   FILE* f = NULL;

   f = fopen(lz4, "wb");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "could not create %s", lz4);
   MCTF_ASSERT(fwrite(empty_file, 1, sizeof(empty_file), f) == sizeof(empty_file), cleanup, "write failed");
   fclose(f);
   f = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_lz4d_file(lz4, path), 0, cleanup, "decompressing an empty block failed");
   MCTF_ASSERT(pgmoneta_exists(path), cleanup, "decompressed file missing");
   MCTF_ASSERT_INT_EQ(pgmoneta_get_file_size(path), 0, cleanup, "decompressed file should be empty");

cleanup:
   if (f != NULL)
   {
      fclose(f);
   }
   remove(path);
   remove(lz4);
   MCTF_FINISH();
}

static int
write_test_input(char* path, char* data, size_t size)
{
   FILE* f = fopen(path, "wb");

   if (f == NULL)
   {
      return 1;
   }
   if (fwrite(data, 1, size, f) != size)
   {
      fclose(f);
      return 1;
   }
   return fclose(f) != 0;
}

MCTF_TEST_NEGATIVE(test_compression_failed_final_write)
{
   struct
   {
      char* name;
      int (*compress)(char* from, char* to);
   } methods[] = {
      {"lz4", pgmoneta_lz4c_file},
      {"zstd", pgmoneta_zstandardc_file},
      {"bzip2", pgmoneta_bzip2_file},
      {"gzip", pgmoneta_gzip_file},
   };
   char* input = "test_failed_final_write";
   char* output = "test_failed_final_write.out";
   static char data[30000];
   struct rlimit orig;
   struct rlimit limit;
   void (*old_handler)(int) = SIG_ERR;
   size_t size = 0;
   int ret = 0;

   /* incompressible input, so the output spans several stdio buffers */
   for (size_t i = 0; i < sizeof(data); i++)
   {
      data[i] = (char)(rand() & 0xff);
   }

   MCTF_ASSERT_INT_EQ(getrlimit(RLIMIT_FSIZE, &orig), 0, cleanup, "getrlimit failed");
   old_handler = signal(SIGXFSZ, SIG_IGN);

   for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); i++)
   {
      MCTF_ASSERT_INT_EQ(write_test_input(input, data, sizeof(data)), 0, cleanup, "could not write %s", input);
      MCTF_ASSERT_INT_EQ(methods[i].compress(input, output), 0, cleanup, "%s: compression failed", methods[i].name);
      size = pgmoneta_get_file_size(output);
      remove(output);

      /* everything but the last, partial buffer fits, so only the final write fails */
      limit = orig;
      limit.rlim_cur = size / 4096 * 4096;
      if (limit.rlim_cur == size)
      {
         limit.rlim_cur = size - 1;
      }

      MCTF_ASSERT_INT_EQ(write_test_input(input, data, sizeof(data)), 0, cleanup, "could not write %s", input);
      MCTF_ASSERT_INT_EQ(setrlimit(RLIMIT_FSIZE, &limit), 0, cleanup, "setrlimit failed");
      ret = methods[i].compress(input, output);
      setrlimit(RLIMIT_FSIZE, &orig);

      MCTF_ASSERT(ret != 0, cleanup, "%s: a failed final write was reported as success", methods[i].name);
      MCTF_ASSERT(pgmoneta_exists(input), cleanup, "%s: the source was deleted", methods[i].name);
      MCTF_ASSERT(!pgmoneta_exists(output), cleanup, "%s: a truncated output was kept", methods[i].name);
      remove(input);
   }

cleanup:
   setrlimit(RLIMIT_FSIZE, &orig);
   if (old_handler != SIG_ERR)
   {
      signal(SIGXFSZ, old_handler);
   }
   remove(input);
   remove(output);
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_compression_failed_final_write_decompress)
{
   struct
   {
      char* name;
      char* compressed;
      int (*compress)(char* from, char* to);
      int (*decompress)(char* from, char* to);
   } methods[] = {
      {"lz4", "test_failed_final_write.lz4", pgmoneta_lz4c_file, pgmoneta_lz4d_file},
      {"zstd", "test_failed_final_write.zstd", pgmoneta_zstandardc_file, pgmoneta_zstandardd_file},
      {"bzip2", "test_failed_final_write.bz2", pgmoneta_bzip2_file, pgmoneta_bunzip2_file},
      {"gzip", "test_failed_final_write.gz", pgmoneta_gzip_file, pgmoneta_gunzip_file},
   };
   char* input = "test_failed_final_write";
   char* output = "test_failed_final_write.out";
   char* compressed = NULL;
   static char data[30000];
   struct rlimit orig;
   struct rlimit limit;
   void (*old_handler)(int) = SIG_ERR;
   int ret = 0;

   for (size_t i = 0; i < sizeof(data); i++)
   {
      data[i] = (char)(rand() & 0xff);
   }

   MCTF_ASSERT_INT_EQ(getrlimit(RLIMIT_FSIZE, &orig), 0, cleanup, "getrlimit failed");
   old_handler = signal(SIGXFSZ, SIG_IGN);

   for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); i++)
   {
      compressed = methods[i].compressed;
      MCTF_ASSERT_INT_EQ(write_test_input(input, data, sizeof(data)), 0, cleanup, "could not write %s", input);
      MCTF_ASSERT_INT_EQ(methods[i].compress(input, compressed), 0, cleanup, "%s: compression failed", methods[i].name);

      /* everything but the last, partial buffer of the decompressed output fits */
      limit = orig;
      limit.rlim_cur = sizeof(data) / 4096 * 4096;

      MCTF_ASSERT_INT_EQ(setrlimit(RLIMIT_FSIZE, &limit), 0, cleanup, "setrlimit failed");
      ret = methods[i].decompress(compressed, output);
      setrlimit(RLIMIT_FSIZE, &orig);

      MCTF_ASSERT(ret != 0, cleanup, "%s: a failed final write was reported as success", methods[i].name);
      MCTF_ASSERT(pgmoneta_exists(compressed), cleanup, "%s: the compressed file was deleted", methods[i].name);
      MCTF_ASSERT(!pgmoneta_exists(output), cleanup, "%s: a truncated output was kept", methods[i].name);
      remove(compressed);
      compressed = NULL;
   }

cleanup:
   setrlimit(RLIMIT_FSIZE, &orig);
   if (old_handler != SIG_ERR)
   {
      signal(SIGXFSZ, old_handler);
   }
   if (compressed != NULL)
   {
      remove(compressed);
   }
   remove(input);
   remove(output);
   MCTF_FINISH();
}

#define LZ4_FRAME_DECODED  0
#define LZ4_FRAME_ERROR    1
#define LZ4_FRAME_MISMATCH 2

static unsigned char*
lz4_frame_test_data(size_t size)
{
   unsigned char* data = malloc(size > 0 ? size : 1);
   uint32_t seed = 1399;

   if (data == NULL)
   {
      return NULL;
   }

   /* alternate runs that do and don't compress, so the frame has both kinds of block */
   for (size_t i = 0; i < size; i++)
   {
      seed = seed * 1103515245 + 12345;
      data[i] = (i / 4096) % 2 == 0 ? (unsigned char)(seed >> 16) : 'p';
   }

   return data;
}

static char*
lz4_frames(unsigned char* data, size_t size, LZ4F_preferences_t* prefs, int count, size_t* frames_size)
{
   size_t bound = LZ4F_compressFrameBound(size, prefs);
   size_t frame_size = 0;
   char* frames = malloc(bound * count);

   if (frames == NULL)
   {
      return NULL;
   }

   frame_size = LZ4F_compressFrame(frames, bound, data, size, prefs);
   if (LZ4F_isError(frame_size))
   {
      free(frames);
      return NULL;
   }

   for (int i = 1; i < count; i++)
   {
      memcpy(frames + i * frame_size, frames, frame_size);
   }

   *frames_size = frame_size * count;
   return frames;
}

/* drives the decompressor like restore_stream_cb() does, with chunk bytes of input per call */
static int
lz4_frame_decode(char* src, size_t src_size, size_t chunk, size_t out_capacity, unsigned char* expected, size_t expected_size)
{
   struct compressor* decompressor = NULL;
   char* out = NULL;
   size_t pos = 0;
   size_t total = 0;
   size_t n = 0;
   size_t produced = 0;
   bool last = false;
   bool finished = false;
   int ret = LZ4_FRAME_ERROR;

   out = malloc(out_capacity);
   if (out == NULL || pgmoneta_lz4_frame_decompressor_create(&decompressor))
   {
      goto done;
   }

   do
   {
      n = MIN(chunk, src_size - pos);
      last = pos + n == src_size;
      pgmoneta_compressor_prepare(decompressor, src + pos, n, last);
      finished = false;
      while (!finished)
      {
         if (decompressor->decompress(decompressor, out, out_capacity, &produced, &finished))
         {
            goto done;
         }
         if (total + produced > expected_size || memcmp(out, expected + total, produced) != 0)
         {
            ret = LZ4_FRAME_MISMATCH;
            goto done;
         }
         total += produced;
      }
      pos += n;
   }
   while (!last);

   ret = total == expected_size ? LZ4_FRAME_DECODED : LZ4_FRAME_MISMATCH;

done:
   pgmoneta_compressor_destroy(decompressor);
   free(out);
   return ret;
}

MCTF_TEST(test_compression_lz4_frame_decode)
{
   size_t size = 4 * 1024 * 1024 + 123;
   unsigned char* data = NULL;
   unsigned char* twice = NULL;
   char* frames = NULL;
   size_t frames_size = 0;

   data = lz4_frame_test_data(size);
   MCTF_ASSERT_PTR_NONNULL(data, cleanup, "allocation failed");

   frames = lz4_frames(data, size, NULL, 1, &frames_size);
   MCTF_ASSERT_PTR_NONNULL(frames, cleanup, "could not create an LZ4 frame");

   MCTF_ASSERT_INT_EQ(lz4_frame_decode(frames, frames_size, BUFFER_SIZE, BUFFER_SIZE, data, size), LZ4_FRAME_DECODED,
                      cleanup, "decoding in streamer sized pieces failed");
   MCTF_ASSERT_INT_EQ(lz4_frame_decode(frames, frames_size, 4093, 65536, data, size), LZ4_FRAME_DECODED,
                      cleanup, "decoding in 4093 byte pieces failed");
   MCTF_ASSERT_INT_EQ(lz4_frame_decode(frames, frames_size, 4093, 1000, data, size), LZ4_FRAME_DECODED,
                      cleanup, "decoding into a small output buffer failed");
   free(frames);
   frames = NULL;

   /* frames back to back are decoded one after the other, like lz4 -d does */
   twice = malloc(2 * size);
   MCTF_ASSERT_PTR_NONNULL(twice, cleanup, "allocation failed");
   memcpy(twice, data, size);
   memcpy(twice + size, data, size);
   frames = lz4_frames(data, size, NULL, 2, &frames_size);
   MCTF_ASSERT_PTR_NONNULL(frames, cleanup, "could not create LZ4 frames");
   MCTF_ASSERT_INT_EQ(lz4_frame_decode(frames, frames_size, BUFFER_SIZE, BUFFER_SIZE, twice, 2 * size), LZ4_FRAME_DECODED,
                      cleanup, "decoding two frames failed");

cleanup:
   free(frames);
   free(twice);
   free(data);
   MCTF_FINISH();
}

MCTF_TEST(test_compression_lz4_frame_decode_checksums)
{
   size_t size = 300 * 1024;
   unsigned char* data = NULL;
   char* frames = NULL;
   size_t frames_size = 0;
   LZ4F_preferences_t prefs;

   data = lz4_frame_test_data(size);
   MCTF_ASSERT_PTR_NONNULL(data, cleanup, "allocation failed");

   memset(&prefs, 0, sizeof(prefs));
   prefs.frameInfo.blockSizeID = LZ4F_max256KB;
   prefs.frameInfo.blockMode = LZ4F_blockIndependent;
   prefs.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;
   prefs.frameInfo.blockChecksumFlag = LZ4F_blockChecksumEnabled;
   prefs.frameInfo.contentSize = size;
   prefs.compressionLevel = 12;

   frames = lz4_frames(data, size, &prefs, 1, &frames_size);
   MCTF_ASSERT_PTR_NONNULL(frames, cleanup, "could not create an LZ4 frame");

   MCTF_ASSERT_INT_EQ(lz4_frame_decode(frames, frames_size, 1, 65536, data, size), LZ4_FRAME_DECODED,
                      cleanup, "decoding one byte at a time failed");

cleanup:
   free(frames);
   free(data);
   MCTF_FINISH();
}

MCTF_TEST(test_compression_lz4_frame_decode_empty)
{
   unsigned char data[1] = {0};
   char* frames = NULL;
   size_t frames_size = 0;

   frames = lz4_frames(data, 0, NULL, 1, &frames_size);
   MCTF_ASSERT_PTR_NONNULL(frames, cleanup, "could not create an empty LZ4 frame");

   MCTF_ASSERT_INT_EQ(lz4_frame_decode(frames, frames_size, BUFFER_SIZE, BUFFER_SIZE, data, 0), LZ4_FRAME_DECODED,
                      cleanup, "decoding an empty frame failed");

cleanup:
   free(frames);
   MCTF_FINISH();
}

MCTF_TEST(test_compression_lz4_frame_decode_fills_output)
{
   /* the frame ends exactly when the output buffer is full, so the caller asks once more without input */
   unsigned char data[65536];
   char* frames = NULL;
   size_t frames_size = 0;

   memset(data, 'p', sizeof(data));

   frames = lz4_frames(data, sizeof(data), NULL, 1, &frames_size);
   MCTF_ASSERT_PTR_NONNULL(frames, cleanup, "could not create an LZ4 frame");

   MCTF_ASSERT_INT_EQ(lz4_frame_decode(frames, frames_size, frames_size, sizeof(data), data, sizeof(data)), LZ4_FRAME_DECODED,
                      cleanup, "decoding a frame that fills the output buffer failed");

cleanup:
   free(frames);
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_compression_lz4_frame_decode_invalid)
{
   size_t size = 300 * 1024;
   unsigned char* data = NULL;
   char* frames = NULL;
   size_t frames_size = 0;
   LZ4F_preferences_t prefs;
   /* an empty file in pgmoneta's own lz4 block format */
   char block_format[] = {0x01, 0x00, 0x00, 0x00, 0x00};

   data = lz4_frame_test_data(size);
   MCTF_ASSERT_PTR_NONNULL(data, cleanup, "allocation failed");

   frames = lz4_frames(data, size, NULL, 1, &frames_size);
   MCTF_ASSERT_PTR_NONNULL(frames, cleanup, "could not create an LZ4 frame");

   MCTF_ASSERT_INT_EQ(lz4_frame_decode(frames, frames_size - 4, BUFFER_SIZE, BUFFER_SIZE, data, size), LZ4_FRAME_ERROR,
                      cleanup, "a frame without its end mark was accepted");
   MCTF_ASSERT_INT_EQ(lz4_frame_decode(frames, frames_size / 2, BUFFER_SIZE, BUFFER_SIZE, data, size), LZ4_FRAME_ERROR,
                      cleanup, "a frame cut in a block was accepted");
   MCTF_ASSERT_INT_EQ(lz4_frame_decode(frames, 0, BUFFER_SIZE, BUFFER_SIZE, data, size), LZ4_FRAME_ERROR,
                      cleanup, "input without a frame was accepted");
   MCTF_ASSERT_INT_EQ(lz4_frame_decode(block_format, sizeof(block_format), BUFFER_SIZE, BUFFER_SIZE, data, 0), LZ4_FRAME_ERROR,
                      cleanup, "pgmoneta's lz4 block format was accepted as a frame");
   free(frames);
   frames = NULL;

   memset(&prefs, 0, sizeof(prefs));
   prefs.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;
   frames = lz4_frames(data, size, &prefs, 1, &frames_size);
   MCTF_ASSERT_PTR_NONNULL(frames, cleanup, "could not create an LZ4 frame");
   frames[frames_size - 2] ^= 0xFF;
   MCTF_ASSERT_INT_EQ(lz4_frame_decode(frames, frames_size, BUFFER_SIZE, BUFFER_SIZE, data, size), LZ4_FRAME_ERROR,
                      cleanup, "a frame with a wrong content checksum was accepted");

cleanup:
   free(frames);
   free(data);
   MCTF_FINISH();
}
