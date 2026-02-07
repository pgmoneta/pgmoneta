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
#include <achv.h>
#include <aes.h>
#include <configuration.h>
#include <mctf.h>
#include <shmem.h>
#include <tscommon.h>
#include <utils.h>
#include <zstandard_compression.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <deque.h>
#include <message.h>
#include <ev.h>

// Forward declaration - will be available in utils.h after rebase
unsigned long pgmoneta_calculate_wal_size(char* directory, char* start);

MCTF_TEST(test_resolve_path_trailing_env_var)
{
   char* resolved = NULL;
   char* env_key = "PGMONETA_TEST_PATH_KEY";
   char* env_value = "PGMONETA_TEST_PATH_VALUE";
   char* expected = "/pgmoneta/PGMONETA_TEST_PATH_VALUE";
   int result;

   setenv(env_key, env_value, 1);

   result = pgmoneta_resolve_path("/pgmoneta/$PGMONETA_TEST_PATH_KEY", &resolved);
   MCTF_ASSERT_INT_EQ(result, 0, cleanup, "resolve_path failed");
   MCTF_ASSERT_PTR_NONNULL(resolved, cleanup, "resolved path is null");
   MCTF_ASSERT_STR_EQ(resolved, expected, cleanup, "resolved path mismatch");

cleanup:
   unsetenv(env_key);
   if (resolved != NULL)
   {
      free(resolved);
      resolved = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_calculate_wal_size)
{
   char wal_dir[] = "test_wal_size_dir";
   char w1[] = "test_wal_size_dir/000000010000000000000001";
   char w2_real[] = "test_wal_size_dir/000000010000000000000002";
   char w3[] = "test_wal_size_dir/000000010000000000000003";
   FILE* f1 = NULL;
   FILE* f2 = NULL;
   FILE* f3 = NULL;
   unsigned long size = 0;

   pgmoneta_mkdir(wal_dir);

   f1 = fopen(w1, "w");
   MCTF_ASSERT_PTR_NONNULL(f1, cleanup, "failed to open w1");
   fprintf(f1, "1234567890");
   fflush(f1);
   fclose(f1); // 10 bytes
   f1 = NULL;

   f2 = fopen(w2_real, "w");
   MCTF_ASSERT_PTR_NONNULL(f2, cleanup, "failed to open w2_real");
   fprintf(f2, "12345678901234567890");
   fflush(f2);
   fclose(f2); // 20 bytes
   f2 = NULL;

   f3 = fopen(w3, "w");
   MCTF_ASSERT_PTR_NONNULL(f3, cleanup, "failed to open w3");
   fprintf(f3, "123456789012345678901234567890");
   fflush(f3);
   fclose(f3); // 30 bytes
   f3 = NULL;

   size = pgmoneta_calculate_wal_size(wal_dir, "000000010000000000000002");
   // Should include w2 and w3. 20 + 30 = 50.
   MCTF_ASSERT_INT_EQ(size, 50, cleanup, "wal size calculation mismatch");

cleanup:
   if (f1 != NULL)
   {
      fclose(f1);
      f1 = NULL;
   }
   if (f2 != NULL)
   {
      fclose(f2);
      f2 = NULL;
   }
   if (f3 != NULL)
   {
      fclose(f3);
      f3 = NULL;
   }
   pgmoneta_delete_directory(wal_dir);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_starts_with)
{
   MCTF_ASSERT(pgmoneta_starts_with("hello world", "hello"), cleanup, "starts_with positive case 1 failed");
   MCTF_ASSERT(pgmoneta_starts_with("hello", "hello"), cleanup, "starts_with positive case 2 failed");
   MCTF_ASSERT(!pgmoneta_starts_with("hello world", "world"), cleanup, "starts_with negative case 1 failed");
   MCTF_ASSERT(!pgmoneta_starts_with("hello", "hello world"), cleanup, "starts_with negative case 2 failed");
   MCTF_ASSERT(!pgmoneta_starts_with(NULL, "hello"), cleanup, "starts_with NULL case 1 failed");
   MCTF_ASSERT(!pgmoneta_starts_with("hello", NULL), cleanup, "starts_with NULL case 2 failed");
   MCTF_ASSERT(!pgmoneta_starts_with(NULL, NULL), cleanup, "starts_with NULL case 3 failed");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_ends_with)
{
   MCTF_ASSERT(pgmoneta_ends_with("hello world", "world"), cleanup, "ends_with positive case 1 failed");
   MCTF_ASSERT(pgmoneta_ends_with("world", "world"), cleanup, "ends_with positive case 2 failed");
   MCTF_ASSERT(!pgmoneta_ends_with("hello world", "hello"), cleanup, "ends_with negative case 1 failed");
   MCTF_ASSERT(!pgmoneta_ends_with("world", "hello world"), cleanup, "ends_with negative case 2 failed");
   MCTF_ASSERT(!pgmoneta_ends_with(NULL, "world"), cleanup, "ends_with NULL case 1 failed");
   MCTF_ASSERT(!pgmoneta_ends_with("world", NULL), cleanup, "ends_with NULL case 2 failed");
   MCTF_ASSERT(!pgmoneta_ends_with(NULL, NULL), cleanup, "ends_with NULL case 3 failed");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_contains)
{
   MCTF_ASSERT(pgmoneta_contains("hello world", "lo wo"), cleanup, "contains positive case 1 failed");
   MCTF_ASSERT(pgmoneta_contains("hello", "he"), cleanup, "contains positive case 2 failed");
   MCTF_ASSERT(!pgmoneta_contains("hello world", "z"), cleanup, "contains negative case 1 failed");
   MCTF_ASSERT(!pgmoneta_contains(NULL, "hello"), cleanup, "contains NULL case 1 failed");
   MCTF_ASSERT(!pgmoneta_contains("hello", NULL), cleanup, "contains NULL case 2 failed");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_compare_string)
{
   MCTF_ASSERT(pgmoneta_compare_string("abc", "abc"), cleanup, "compare_string positive case failed");
   MCTF_ASSERT(!pgmoneta_compare_string("abc", "ABC"), cleanup, "compare_string case sensitive failed");
   MCTF_ASSERT(!pgmoneta_compare_string("abc", "def"), cleanup, "compare_string negative case failed");
   MCTF_ASSERT(!pgmoneta_compare_string(NULL, "abc"), cleanup, "compare_string NULL case 1 failed");
   MCTF_ASSERT(!pgmoneta_compare_string("abc", NULL), cleanup, "compare_string NULL case 2 failed");
   MCTF_ASSERT(pgmoneta_compare_string(NULL, NULL), cleanup, "compare_string NULL case 3 failed");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_atoi)
{
   MCTF_ASSERT_INT_EQ(pgmoneta_atoi("123"), 123, cleanup, "atoi positive failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_atoi("-123"), -123, cleanup, "atoi negative failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_atoi("0"), 0, cleanup, "atoi zero failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_atoi(NULL), 0, cleanup, "atoi NULL failed");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_is_number)
{
   MCTF_ASSERT(pgmoneta_is_number("123", 10), cleanup, "is_number positive base10 failed");
   MCTF_ASSERT(pgmoneta_is_number("-123", 10), cleanup, "is_number negative base10 failed");
   MCTF_ASSERT(!pgmoneta_is_number("12a", 10), cleanup, "is_number invalid base10 failed");
   MCTF_ASSERT(!pgmoneta_is_number("abc", 10), cleanup, "is_number invalid base10 2 failed");
   MCTF_ASSERT(pgmoneta_is_number("1A", 16), cleanup, "is_number positive base16 failed");
   MCTF_ASSERT(!pgmoneta_is_number("1Z", 16), cleanup, "is_number invalid base16 failed");
   MCTF_ASSERT(!pgmoneta_is_number(NULL, 10), cleanup, "is_number NULL failed");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_base64)
{
   char* original = "hello world";
   char* encoded = NULL;
   char* decoded = NULL;
   size_t encoded_length = 0;
   size_t decoded_length = 0;
   size_t original_length = strlen(original);

   MCTF_ASSERT_INT_EQ(pgmoneta_base64_encode(original, original_length, &encoded, &encoded_length), 0, cleanup, "base64_encode failed");
   MCTF_ASSERT_PTR_NONNULL(encoded, cleanup, "encoded is null");
   MCTF_ASSERT(encoded_length > 0, cleanup, "encoded_length should be > 0");

   MCTF_ASSERT_INT_EQ(pgmoneta_base64_decode(encoded, encoded_length, (void**)&decoded, &decoded_length), 0, cleanup, "base64_decode failed");
   MCTF_ASSERT_PTR_NONNULL(decoded, cleanup, "decoded is null");
   MCTF_ASSERT_INT_EQ(decoded_length, original_length, cleanup, "decoded_length mismatch");
   MCTF_ASSERT(memcmp(decoded, original, original_length) == 0, cleanup, "decoded content mismatch");

cleanup:
   if (encoded != NULL)
   {
      free(encoded);
      encoded = NULL;
   }
   if (decoded != NULL)
   {
      free(decoded);
      decoded = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_is_incremental_path)
{
   MCTF_ASSERT(pgmoneta_is_incremental_path("/path/to/backup/INCREMENTAL.20231026120000-20231026110000"), cleanup, "is_incremental_path positive failed");
   MCTF_ASSERT(!pgmoneta_is_incremental_path("/path/to/backup/20231026120000"), cleanup, "is_incremental_path negative 1 failed");
   MCTF_ASSERT(!pgmoneta_is_incremental_path("/path/to/backup"), cleanup, "is_incremental_path negative 2 failed");
   MCTF_ASSERT(!pgmoneta_is_incremental_path(NULL), cleanup, "is_incremental_path NULL failed");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_get_parent_dir)
{
   char* parent = NULL;

   parent = pgmoneta_get_parent_dir("/a/b/c");
   MCTF_ASSERT_PTR_NONNULL(parent, cleanup, "parent dir is null");
   MCTF_ASSERT_STR_EQ(parent, "/a/b", cleanup, "parent dir mismatch 1");
   free(parent);
   parent = NULL;

   parent = pgmoneta_get_parent_dir("/a");
   MCTF_ASSERT_PTR_NONNULL(parent, cleanup, "parent dir is null");
   MCTF_ASSERT_STR_EQ(parent, "/", cleanup, "parent dir mismatch 2");
   free(parent);
   parent = NULL;

   parent = pgmoneta_get_parent_dir("/");
   MCTF_ASSERT_PTR_NONNULL(parent, cleanup, "parent dir is null");
   MCTF_ASSERT_STR_EQ(parent, "/", cleanup, "parent dir mismatch 3");
   free(parent);
   parent = NULL;

   parent = pgmoneta_get_parent_dir("a");
   MCTF_ASSERT_PTR_NONNULL(parent, cleanup, "parent dir is null");
   MCTF_ASSERT_STR_EQ(parent, ".", cleanup, "parent dir mismatch 4");
   free(parent);
   parent = NULL;

   parent = pgmoneta_get_parent_dir(NULL);
   MCTF_ASSERT_PTR_NULL(parent, cleanup, "parent dir should be null for NULL input");

cleanup:
   if (parent != NULL)
   {
      free(parent);
      parent = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_serialization)
{
   void* data = NULL;
   void* ptr = NULL;
   signed char b = 'a';
   uint8_t u8 = 10;
   int16_t i16 = -20;
   uint16_t u16 = 30;
   int32_t i32 = -400;
   uint32_t u32 = 500;
   int64_t i64 = -6000;
   uint64_t u64 = 7000;
   bool bo = true;
   char* s = "hello";

   data = malloc(1024);
   MCTF_ASSERT_PTR_NONNULL(data, cleanup, "malloc failed");
   memset(data, 0, 1024);
   ptr = data;

   pgmoneta_write_byte(ptr, b);
   ptr += 1;
   pgmoneta_write_uint8(ptr, u8);
   ptr += 1;
   pgmoneta_write_int16(ptr, i16);
   ptr += 2;
   pgmoneta_write_uint16(ptr, u16);
   ptr += 2;
   pgmoneta_write_int32(ptr, i32);
   ptr += 4;
   pgmoneta_write_uint32(ptr, u32);
   ptr += 4;
   pgmoneta_write_int64(ptr, i64);
   ptr += 8;
   pgmoneta_write_uint64(ptr, u64);
   ptr += 8;
   pgmoneta_write_bool(ptr, bo);
   ptr += 1;
   pgmoneta_write_string(ptr, s);

   ptr = data;
   MCTF_ASSERT_INT_EQ(pgmoneta_read_byte(ptr), b, cleanup, "read_byte mismatch");
   ptr += 1;
   MCTF_ASSERT_INT_EQ(pgmoneta_read_uint8(ptr), u8, cleanup, "read_uint8 mismatch");
   ptr += 1;
   MCTF_ASSERT_INT_EQ(pgmoneta_read_int16(ptr), i16, cleanup, "read_int16 mismatch");
   ptr += 2;
   MCTF_ASSERT_INT_EQ(pgmoneta_read_uint16(ptr), u16, cleanup, "read_uint16 mismatch");
   ptr += 2;
   MCTF_ASSERT_INT_EQ(pgmoneta_read_int32(ptr), i32, cleanup, "read_int32 mismatch");
   ptr += 4;
   MCTF_ASSERT_INT_EQ(pgmoneta_read_uint32(ptr), u32, cleanup, "read_uint32 mismatch");
   ptr += 4;
   MCTF_ASSERT_INT_EQ(pgmoneta_read_int64(ptr), i64, cleanup, "read_int64 mismatch");
   ptr += 8;
   MCTF_ASSERT_INT_EQ(pgmoneta_read_uint64(ptr), u64, cleanup, "read_uint64 mismatch");
   ptr += 8;
   MCTF_ASSERT(pgmoneta_read_bool(ptr) == bo, cleanup, "read_bool mismatch");
   ptr += 1;
   MCTF_ASSERT_STR_EQ(pgmoneta_read_string(ptr), s, cleanup, "read_string mismatch");

cleanup:
   if (data != NULL)
   {
      free(data);
      data = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_append)
{
   char* buffer = NULL;

   buffer = pgmoneta_append(buffer, "hello");
   MCTF_ASSERT_PTR_NONNULL(buffer, cleanup, "append failed");
   MCTF_ASSERT_STR_EQ(buffer, "hello", cleanup, "append result mismatch 1");

   buffer = pgmoneta_append_char(buffer, ' ');
   MCTF_ASSERT_PTR_NONNULL(buffer, cleanup, "append_char failed");
   MCTF_ASSERT_STR_EQ(buffer, "hello ", cleanup, "append result mismatch 2");

   buffer = pgmoneta_append_int(buffer, 123);
   MCTF_ASSERT_PTR_NONNULL(buffer, cleanup, "append_int failed");
   MCTF_ASSERT_STR_EQ(buffer, "hello 123", cleanup, "append result mismatch 3");

   buffer = pgmoneta_append(buffer, " ");
   buffer = pgmoneta_append_ulong(buffer, 456);
   MCTF_ASSERT_PTR_NONNULL(buffer, cleanup, "append_ulong failed");
   MCTF_ASSERT_STR_EQ(buffer, "hello 123 456", cleanup, "append result mismatch 4");

   buffer = pgmoneta_append(buffer, " ");
   buffer = pgmoneta_append_bool(buffer, true);
   MCTF_ASSERT_PTR_NONNULL(buffer, cleanup, "append_bool failed");
   MCTF_ASSERT_STR_EQ(buffer, "hello 123 456 true", cleanup, "append result mismatch 5");

   buffer = pgmoneta_append(buffer, " ");
   buffer = pgmoneta_append_double(buffer, 3.14);
   MCTF_ASSERT_PTR_NONNULL(buffer, cleanup, "append_double failed");
   MCTF_ASSERT_STR_EQ(buffer, "hello 123 456 true 3.140000", cleanup, "append result mismatch 6");

cleanup:
   if (buffer != NULL)
   {
      free(buffer);
      buffer = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_string_manipulation)
{
   char* s = NULL;
   char* res = NULL;

   // test remove_whitespace

   s = strdup(" a b c ");
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "strdup failed");
   res = pgmoneta_remove_whitespace(s);
   MCTF_ASSERT_PTR_NONNULL(res, cleanup, "remove_whitespace failed");
   MCTF_ASSERT_STR_EQ(res, "abc", cleanup, "remove_whitespace result mismatch");
   free(s);
   s = NULL;
   free(res);
   res = NULL;

   // test remove_prefix

   s = strdup("pre_test");
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "strdup failed");
   res = pgmoneta_remove_prefix(s, "pre_");
   MCTF_ASSERT_PTR_NONNULL(res, cleanup, "remove_prefix failed");
   MCTF_ASSERT_STR_EQ(res, "test", cleanup, "remove_prefix result mismatch");
   free(s);
   s = NULL;
   free(res);
   res = NULL;

   // test remove_suffix

   s = strdup("test.txt");
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "strdup failed");
   res = pgmoneta_remove_suffix(s, ".txt");
   MCTF_ASSERT_PTR_NONNULL(res, cleanup, "remove_suffix failed");
   MCTF_ASSERT_STR_EQ(res, "test", cleanup, "remove_suffix result mismatch");
   free(s);
   s = NULL;
   free(res);
   res = NULL;

   // test indent

   s = strdup("hello");
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "strdup failed");
   res = pgmoneta_indent(s, NULL, 2);
   MCTF_ASSERT_PTR_NONNULL(res, cleanup, "indent failed");
   MCTF_ASSERT_STR_EQ(res, "hello  ", cleanup, "indent result mismatch");
   s = NULL;
   free(res);
   res = NULL;

   // test escape_string

   s = strdup("foo'bar");
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "strdup failed");
   res = pgmoneta_escape_string(s);
   MCTF_ASSERT_PTR_NONNULL(res, cleanup, "escape_string failed");
   MCTF_ASSERT_STR_EQ(res, "foo\\'bar", cleanup, "escape_string result mismatch");
   free(s);
   s = NULL;
   free(res);
   res = NULL;

cleanup:
   if (s != NULL)
   {
      free(s);
      s = NULL;
   }
   if (res != NULL)
   {
      free(res);
      res = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_math)
{
   MCTF_ASSERT(pgmoneta_get_aligned_size(1) >= 1, cleanup, "get_aligned_size 1 failed");
   MCTF_ASSERT(pgmoneta_get_aligned_size(100) >= 100, cleanup, "get_aligned_size 100 failed");

   MCTF_ASSERT_INT_EQ(pgmoneta_swap(0x12345678), 0x78563412, cleanup, "swap failed");

   char* array[] = {"b", "a", "c"};
   pgmoneta_sort(3, array);
   MCTF_ASSERT_STR_EQ(array[0], "a", cleanup, "sort result 0 mismatch");
   MCTF_ASSERT_STR_EQ(array[1], "b", cleanup, "sort result 1 mismatch");
   MCTF_ASSERT_STR_EQ(array[2], "c", cleanup, "sort result 2 mismatch");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_version)
{
   MCTF_ASSERT_INT_EQ(pgmoneta_version_as_number(1, 2, 3), 10203, cleanup, "version_as_number failed");
   MCTF_ASSERT(pgmoneta_version_ge(0, 0, 0), cleanup, "version_ge positive failed");
   MCTF_ASSERT(!pgmoneta_version_ge(99, 99, 99), cleanup, "version_ge negative failed");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_bigendian)
{
   int n = 1;
   bool is_little = (*(char*)&n == 1);
   if (is_little)
   {
      MCTF_ASSERT(!pgmoneta_bigendian(), cleanup, "bigendian should be false on little-endian");
   }
   else
   {
      MCTF_ASSERT(pgmoneta_bigendian(), cleanup, "bigendian should be true on big-endian");
   }

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_strip_extension)
{
   char* name = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_strip_extension("file.txt", &name), 0, cleanup, "strip_extension failed 1");
   MCTF_ASSERT_PTR_NONNULL(name, cleanup, "name is null 1");
   MCTF_ASSERT_STR_EQ(name, "file", cleanup, "strip_extension result 1 mismatch");
   free(name);
   name = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_strip_extension("file", &name), 0, cleanup, "strip_extension failed 2");
   MCTF_ASSERT_PTR_NONNULL(name, cleanup, "name is null 2");
   MCTF_ASSERT_STR_EQ(name, "file", cleanup, "strip_extension result 2 mismatch");
   free(name);
   name = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_strip_extension("file.tar.gz", &name), 0, cleanup, "strip_extension failed 3");
   MCTF_ASSERT_PTR_NONNULL(name, cleanup, "name is null 3");
   MCTF_ASSERT_STR_EQ(name, "file.tar", cleanup, "strip_extension result 3 mismatch");
   free(name);
   name = NULL;

   // Hidden file case

   MCTF_ASSERT_INT_EQ(pgmoneta_strip_extension(".bashrc", &name), 0, cleanup, "strip_extension failed 4");
   MCTF_ASSERT_PTR_NONNULL(name, cleanup, "name is null 4");
   MCTF_ASSERT_STR_EQ(name, "", cleanup, "strip_extension result 4 mismatch");
   free(name);
   name = NULL;

cleanup:
   if (name != NULL)
   {
      free(name);
      name = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_file_size)
{
   char* s = NULL;

   s = pgmoneta_translate_file_size(100);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "translate_file_size 100 failed");
   MCTF_ASSERT_STR_EQ(s, "100.00B", cleanup, "translate_file_size 100 result mismatch");
   free(s);
   s = NULL;

   s = pgmoneta_translate_file_size(1024);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "translate_file_size 1024 failed");
   MCTF_ASSERT_STR_EQ(s, "1.00kB", cleanup, "translate_file_size 1024 result mismatch");
   free(s);
   s = NULL;

cleanup:
   if (s != NULL)
   {
      free(s);
      s = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_file_ops)
{
   char* path = "test_file_ops.tmp";
   char* dir = "test_dir_ops.tmp";
   FILE* f = NULL;

   f = fopen(path, "w");
   if (f)
   {
      fprintf(f, "test");
      fflush(f);
      fclose(f);
      f = NULL;
   }

   MCTF_ASSERT(pgmoneta_exists(path), cleanup, "file should exist");
   MCTF_ASSERT(pgmoneta_is_file(path), cleanup, "path should be a file");
   MCTF_ASSERT(!pgmoneta_is_directory(path), cleanup, "path should not be a directory");

   pgmoneta_mkdir(dir);
   MCTF_ASSERT(pgmoneta_exists(dir), cleanup, "directory should exist");
   MCTF_ASSERT(pgmoneta_is_directory(dir), cleanup, "dir should be a directory");
   MCTF_ASSERT(!pgmoneta_is_file(dir), cleanup, "dir should not be a file");

   remove(path);
   pgmoneta_delete_directory(dir);

   // remove doesn't check immediate effect usually but here it should be fine

   MCTF_ASSERT(!pgmoneta_exists(dir), cleanup, "directory should be deleted");

cleanup:
   if (f != NULL)
   {
      fclose(f);
      f = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_snprintf)
{
   char buf[100];

   pgmoneta_snprintf(buf, 100, "Hello %s", "World");
   MCTF_ASSERT_STR_EQ(buf, "Hello World", cleanup, "snprintf result 1 mismatch");

   pgmoneta_snprintf(buf, 5, "0123456789");
   MCTF_ASSERT_STR_EQ(buf, "0123", cleanup, "snprintf truncation failed");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_string_extras)
{
   char* s = NULL;
   char** results = NULL;
   int count = 0;

   // pgmoneta_remove_first

   s = strdup("abc");
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "strdup failed 1");
   s = pgmoneta_remove_first(s);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "remove_first returned null");
   MCTF_ASSERT_STR_EQ(s, "bc", cleanup, "remove_first result 1 mismatch");
   free(s);
   s = NULL;

   s = strdup("a");
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "strdup failed 2");
   s = pgmoneta_remove_first(s);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "remove_first returned null 2");
   MCTF_ASSERT_STR_EQ(s, "", cleanup, "remove_first result 2 mismatch");
   free(s);
   s = NULL;

   MCTF_ASSERT_PTR_NULL(pgmoneta_remove_first(NULL), cleanup, "remove_first should return NULL for NULL input");

   // pgmoneta_remove_last

   s = strdup("abc");
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "strdup failed 3");
   s = pgmoneta_remove_last(s);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "remove_last returned null");
   MCTF_ASSERT_STR_EQ(s, "ab", cleanup, "remove_last result 1 mismatch");
   free(s);
   s = NULL;

   s = strdup("a");
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "strdup failed 4");
   s = pgmoneta_remove_last(s);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "remove_last returned null 2");
   MCTF_ASSERT_STR_EQ(s, "", cleanup, "remove_last result 2 mismatch");
   free(s);
   s = NULL;

   MCTF_ASSERT_PTR_NULL(pgmoneta_remove_last(NULL), cleanup, "remove_last should return NULL for NULL input");

   // pgmoneta_bytes_to_string

   s = pgmoneta_bytes_to_string(1024);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "bytes_to_string 1024 failed");
   MCTF_ASSERT_STR_EQ(s, "1 KB", cleanup, "bytes_to_string 1024 result mismatch");
   free(s);
   s = NULL;

   s = pgmoneta_bytes_to_string(1024 * 1024);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "bytes_to_string 1MB failed");
   MCTF_ASSERT_STR_EQ(s, "1 MB", cleanup, "bytes_to_string 1MB result mismatch");
   free(s);
   s = NULL;

   s = pgmoneta_bytes_to_string(0);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "bytes_to_string 0 failed");
   MCTF_ASSERT_STR_EQ(s, "0", cleanup, "bytes_to_string 0 result mismatch");
   free(s);
   s = NULL;

   // pgmoneta_lsn_to_string / pgmoneta_string_to_lsn

   uint64_t lsn = 0x123456789ABCDEF0;
   s = pgmoneta_lsn_to_string(lsn);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "lsn_to_string failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_string_to_lsn(s), lsn, cleanup, "string_to_lsn round-trip failed");
   free(s);
   s = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_string_to_lsn(NULL), 0, cleanup, "string_to_lsn NULL should return 0");

   // pgmoneta_split

   MCTF_ASSERT_INT_EQ(pgmoneta_split("a,b,c", &results, &count, ','), 0, cleanup, "split failed");
   MCTF_ASSERT_INT_EQ(count, 3, cleanup, "split count mismatch");
   MCTF_ASSERT_PTR_NONNULL(results, cleanup, "split results is null");
   MCTF_ASSERT_STR_EQ(results[0], "a", cleanup, "split result 0 mismatch");
   MCTF_ASSERT_STR_EQ(results[1], "b", cleanup, "split result 1 mismatch");
   MCTF_ASSERT_STR_EQ(results[2], "c", cleanup, "split result 2 mismatch");
   for (int i = 0; i < count; i++)
   {
      if (results[i] != NULL)
      {
         free(results[i]);
         results[i] = NULL;
      }
   }
   free(results);
   results = NULL;

   // pgmoneta_is_substring

   MCTF_ASSERT_INT_EQ(pgmoneta_is_substring("world", "hello world"), 1, cleanup, "is_substring positive failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_is_substring("foo", "bar"), 0, cleanup, "is_substring negative failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_is_substring(NULL, "bar"), 0, cleanup, "is_substring NULL case 1 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_is_substring("foo", NULL), 0, cleanup, "is_substring NULL case 2 failed");

   // pgmoneta_format_and_append

   s = strdup("Hello");
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "strdup failed 5");
   s = pgmoneta_format_and_append(s, " %s %d", "World", 2025);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "format_and_append failed");
   MCTF_ASSERT_STR_EQ(s, "Hello World 2025", cleanup, "format_and_append result mismatch");
   free(s);
   s = NULL;

cleanup:
   if (s != NULL)
   {
      free(s);
      s = NULL;
   }
   if (results != NULL)
   {
      for (int i = 0; i < count && results[i] != NULL; i++)
      {
         free(results[i]);
         results[i] = NULL;
      }
      free(results);
      results = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_merge_string_arrays)
{
   char* list1[] = {"a", "b", NULL};
   char* list2[] = {"c", "d", NULL};
   char** lists[] = {list1, list2, NULL};
   char** out_list = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_merge_string_arrays(lists, &out_list), 0, cleanup, "merge_string_arrays failed");
   MCTF_ASSERT_PTR_NONNULL(out_list, cleanup, "out_list is null");
   MCTF_ASSERT_STR_EQ(out_list[0], "a", cleanup, "merged array[0] mismatch");
   MCTF_ASSERT_STR_EQ(out_list[1], "b", cleanup, "merged array[1] mismatch");
   MCTF_ASSERT_STR_EQ(out_list[2], "c", cleanup, "merged array[2] mismatch");
   MCTF_ASSERT_STR_EQ(out_list[3], "d", cleanup, "merged array[3] mismatch");
   MCTF_ASSERT_PTR_NULL(out_list[4], cleanup, "merged array[4] should be NULL");

   for (int i = 0; i < 4; i++)
   {
      if (out_list[i] != NULL)
      {
         free(out_list[i]);
         out_list[i] = NULL;
      }
   }
   free(out_list);
   out_list = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_merge_string_arrays(NULL, &out_list), -1, cleanup, "merge_string_arrays NULL should fail");

cleanup:
   if (out_list != NULL)
   {
      for (int i = 0; out_list[i] != NULL; i++)
      {
         free(out_list[i]);
         out_list[i] = NULL;
      }
      free(out_list);
      out_list = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_time)
{
   char short_date[SHORT_TIME_LENGTH];
   char long_date[LONG_TIME_LENGTH];
   char utc_date[UTC_TIME_LENGTH];

   MCTF_ASSERT_INT_EQ(pgmoneta_get_timestamp_ISO8601_format(short_date, long_date), 0, cleanup, "get_timestamp_ISO8601_format failed");
   MCTF_ASSERT_INT_EQ(strlen(short_date), 8, cleanup, "short_date length mismatch");
   MCTF_ASSERT_INT_EQ(strlen(long_date), 16, cleanup, "long_date length mismatch");

   MCTF_ASSERT_INT_EQ(pgmoneta_get_timestamp_UTC_format(utc_date), 0, cleanup, "get_timestamp_UTC_format failed");
   MCTF_ASSERT_INT_EQ(strlen(utc_date), 29, cleanup, "utc_date length mismatch");

   MCTF_ASSERT(pgmoneta_get_current_timestamp() > 0, cleanup, "get_current_timestamp should be > 0");
   MCTF_ASSERT(pgmoneta_get_y2000_timestamp() > 0, cleanup, "get_y2000_timestamp should be > 0");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_token_bucket)
{
   struct token_bucket* tb = NULL;

   tb = (struct token_bucket*)malloc(sizeof(struct token_bucket));
   MCTF_ASSERT_PTR_NONNULL(tb, cleanup, "malloc token_bucket failed");

   // Test initialization

   MCTF_ASSERT_INT_EQ(pgmoneta_token_bucket_init(tb, 100), 0, cleanup, "token_bucket_init failed");

   // Test consume

   MCTF_ASSERT_INT_EQ(pgmoneta_token_bucket_consume(tb, 50), 0, cleanup, "token_bucket_consume failed");

   // Test once

   MCTF_ASSERT_INT_EQ(pgmoneta_token_bucket_once(tb, 10), 0, cleanup, "token_bucket_once failed");

   // Test add (force update)

   MCTF_ASSERT_INT_EQ(pgmoneta_token_bucket_add(tb), 0, cleanup, "token_bucket_add failed");

cleanup:
   if (tb != NULL)
   {
      pgmoneta_token_bucket_destroy(tb);
      tb = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_file_dir)
{
   char base[MAX_PATH];
   char sub1[MAX_PATH];
   char* file1 = "test_dir_extras/file1.txt";
   int n_dirs = 0;
   char** dirs = NULL;
   struct deque* files = NULL;
   struct deque_iterator* it = NULL;
   FILE* f = NULL;
   bool found_sub1 = false;
   bool found_file1 = false;
   char* file2 = "test_dir_extras/file2.txt";
   char* file3 = "test_dir_extras/file3.txt";
   char* file4 = "test_dir_extras/file4.txt";

   strcpy(base, "test_dir_extras");
   strcpy(sub1, "test_dir_extras/sub1");

   pgmoneta_delete_directory(base);
   pgmoneta_mkdir(base);
   pgmoneta_mkdir(sub1);

   f = fopen(file1, "w");
   if (f)
   {
      fprintf(f, "test content");
      fflush(f);
      fclose(f);
      f = NULL;
   }

   // pgmoneta_get_directories

   MCTF_ASSERT_INT_EQ(pgmoneta_get_directories(base, &n_dirs, &dirs), 0, cleanup, "get_directories failed");
   MCTF_ASSERT(n_dirs >= 1, cleanup, "n_dirs should be >= 1");
   for (int i = 0; i < n_dirs; i++)
   {
      if (dirs[i] != NULL && pgmoneta_contains(dirs[i], "sub1"))
      {
         found_sub1 = true;
      }
      if (dirs[i] != NULL)
      {
         free(dirs[i]);
         dirs[i] = NULL;
      }
   }
   free(dirs);
   dirs = NULL;
   MCTF_ASSERT(found_sub1, cleanup, "sub1 directory not found");

   // pgmoneta_get_files

   MCTF_ASSERT_INT_EQ(pgmoneta_get_files(PGMONETA_FILE_TYPE_ALL, base, false, &files), 0, cleanup, "get_files failed");
   MCTF_ASSERT_PTR_NONNULL(files, cleanup, "files deque is null");
   MCTF_ASSERT(pgmoneta_deque_size(files) >= 1, cleanup, "files size should be >= 1");
   pgmoneta_deque_iterator_create(files, &it);
   MCTF_ASSERT_PTR_NONNULL(it, cleanup, "iterator is null");
   while (pgmoneta_deque_iterator_next(it))
   {
      if (it->value != NULL && it->value->data != 0)
      {
         char* file_path = (char*)it->value->data;
         if (file_path != NULL && pgmoneta_contains(file_path, "file1.txt"))
         {
            found_file1 = true;
         }
      }
   }
   pgmoneta_deque_iterator_destroy(it);
   it = NULL;
   pgmoneta_deque_destroy(files);
   files = NULL;
   MCTF_ASSERT(found_file1, cleanup, "file1.txt not found");

   // pgmoneta_directory_size

   MCTF_ASSERT(pgmoneta_directory_size(base) > 0, cleanup, "directory_size should be > 0");

   // pgmoneta_compare_files

   f = fopen(file2, "w");
   if (f)
   {
      fprintf(f, "test content");
      fflush(f);
      fclose(f);
      f = NULL;
   }
   MCTF_ASSERT(pgmoneta_compare_files(file1, file2), cleanup, "compare_files failed");

   // pgmoneta_copy_file

   MCTF_ASSERT_INT_EQ(pgmoneta_copy_file(file1, file3, NULL), 0, cleanup, "copy_file failed");
   MCTF_ASSERT(pgmoneta_exists(file3), cleanup, "copied file should exist");

   // pgmoneta_move_file

   MCTF_ASSERT_INT_EQ(pgmoneta_move_file(file3, file4), 0, cleanup, "move_file failed");
   MCTF_ASSERT(pgmoneta_exists(file4), cleanup, "moved file should exist");
   MCTF_ASSERT(!pgmoneta_exists(file3), cleanup, "original file should not exist after move");

cleanup:
   if (f != NULL)
   {
      fclose(f);
      f = NULL;
   }
   if (it != NULL)
   {
      pgmoneta_deque_iterator_destroy(it);
      it = NULL;
   }
   if (files != NULL)
   {
      pgmoneta_deque_destroy(files);
      files = NULL;
   }
   if (dirs != NULL)
   {
      for (int i = 0; i < n_dirs && dirs[i] != NULL; i++)
      {
         free(dirs[i]);
         dirs[i] = NULL;
      }
      free(dirs);
      dirs = NULL;
   }
   // Clean up

   pgmoneta_delete_directory(base);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_misc)
{
   char* os = NULL;
   int major, minor, patch;
   char buf[1024];
   FILE* f = NULL;

   // pgmoneta_os_kernel_version

   MCTF_ASSERT_INT_EQ(pgmoneta_os_kernel_version(&os, &major, &minor, &patch), 0, cleanup, "os_kernel_version failed");
   MCTF_ASSERT_PTR_NONNULL(os, cleanup, "os is null");
   // On linux it should be "Linux" or similar

   free(os);
   os = NULL;

   // pgmoneta_normalize_path

   f = fopen("/tmp/test.txt", "w");
   if (f)
   {
      fflush(f);
      fclose(f);
      f = NULL;
   }
   MCTF_ASSERT_INT_EQ(pgmoneta_normalize_path("/tmp", "test.txt", "/tmp/default.txt", buf, sizeof(buf)), 0, cleanup, "normalize_path failed 1");
   MCTF_ASSERT(pgmoneta_contains(buf, "/tmp/test.txt"), cleanup, "normalize_path result 1 mismatch");
   remove("/tmp/test.txt");

   // Test with default path

   f = fopen("/tmp/default.txt", "w");
   if (f)
   {
      fflush(f);
      fclose(f);
      f = NULL;
   }
   MCTF_ASSERT_INT_EQ(pgmoneta_normalize_path(NULL, "test.txt", "/tmp/default.txt", buf, sizeof(buf)), 0, cleanup, "normalize_path failed 2");
   MCTF_ASSERT_STR_EQ(buf, "/tmp/default.txt", cleanup, "normalize_path result 2 mismatch");
   remove("/tmp/default.txt");

   // pgmoneta_backtrace_string

   char* bt = NULL;
   MCTF_ASSERT_INT_EQ(pgmoneta_backtrace_string(&bt), 0, cleanup, "backtrace_string failed");
   MCTF_ASSERT_PTR_NONNULL(bt, cleanup, "backtrace is null");
   free(bt);
   bt = NULL;

cleanup:
   if (os != NULL)
   {
      free(os);
      os = NULL;
   }
   if (bt != NULL)
   {
      free(bt);
      bt = NULL;
   }
   if (f != NULL)
   {
      fclose(f);
      f = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_symlinks)
{
   char base[MAX_PATH];
   char* target = "test_symlinks/target.txt";
   char* slink = "test_symlinks/link.txt";
   FILE* f = NULL;
   char* link_target = NULL;

   strcpy(base, "test_symlinks");

   pgmoneta_delete_directory(base);
   pgmoneta_mkdir(base);

   f = fopen(target, "w");
   if (f)
   {
      fprintf(f, "target content");
      fflush(f);
      fclose(f);
      f = NULL;
   }

   MCTF_ASSERT_INT_EQ(pgmoneta_symlink_file(slink, target), 0, cleanup, "symlink_file failed");
   MCTF_ASSERT(pgmoneta_is_symlink(slink), cleanup, "is_symlink failed");
   MCTF_ASSERT(pgmoneta_is_symlink_valid(slink), cleanup, "is_symlink_valid failed");

   link_target = pgmoneta_get_symlink(slink);
   MCTF_ASSERT_PTR_NONNULL(link_target, cleanup, "get_symlink returned null");
   MCTF_ASSERT_STR_EQ(link_target, target, cleanup, "symlink target mismatch");
   free(link_target);
   link_target = NULL;

cleanup:
   if (f != NULL)
   {
      fclose(f);
      f = NULL;
   }
   if (link_target != NULL)
   {
      free(link_target);
      link_target = NULL;
   }
   pgmoneta_delete_directory(base);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_server)
{
   char* s = NULL;

   pgmoneta_test_setup();

   // server 0 is "primary" in minimal config

   s = pgmoneta_get_server(0);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "get_server(0) failed");
   MCTF_ASSERT(pgmoneta_contains(s, "primary"), cleanup, "server should contain 'primary'");
   free(s);
   s = NULL;

   s = pgmoneta_get_server_backup(0);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "get_server_backup(0) failed");
   MCTF_ASSERT(pgmoneta_contains(s, "primary/backup"), cleanup, "server backup should contain 'primary/backup'");
   free(s);
   s = NULL;

   s = pgmoneta_get_server_wal(0);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "get_server_wal(0) failed");
   MCTF_ASSERT(pgmoneta_contains(s, "primary/wal"), cleanup, "server wal should contain 'primary/wal'");
   free(s);
   s = NULL;

   // Invalid server

   MCTF_ASSERT_PTR_NULL(pgmoneta_get_server(-1), cleanup, "get_server(-1) should return NULL");
   MCTF_ASSERT_PTR_NULL(pgmoneta_get_server(100), cleanup, "get_server(100) should return NULL");

cleanup:
   if (s != NULL)
   {
      free(s);
      s = NULL;
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_utils_libev)
{
   pgmoneta_libev_engines();
   // We cannot check EVBACKEND_* constants easily unless included

   // Let's assume constants are available.

   // Test string conversion

   MCTF_ASSERT_STR_EQ(pgmoneta_libev_engine(EVBACKEND_SELECT), "select", cleanup, "libev_engine SELECT failed");
   MCTF_ASSERT_STR_EQ(pgmoneta_libev_engine(EVBACKEND_POLL), "poll", cleanup, "libev_engine POLL failed");
   MCTF_ASSERT_STR_EQ(pgmoneta_libev_engine(0xFFFFFFFF), "Unknown", cleanup, "libev_engine Unknown failed");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_extract_error)
{
   struct message* msg = NULL;
   char* p = NULL;
   char* extracted = NULL;

   msg = (struct message*)malloc(sizeof(struct message));
   MCTF_ASSERT_PTR_NONNULL(msg, cleanup, "malloc message failed");
   msg->kind = 'E';
   // Data: [Type:1][Length:4] 'S' "ERROR" \0 'C' "12345" \0 \0

   msg->data = calloc(1, 100);
   MCTF_ASSERT_PTR_NONNULL(msg->data, cleanup, "calloc msg->data failed");
   p = (char*)msg->data;

   pgmoneta_write_byte(p, 'E');
   p += 1;
   pgmoneta_write_int32(p, 0);
   p += 4;

   *p++ = 'S';
   strcpy(p, "ERROR");
   p += 6;
   *p++ = 'C';
   strcpy(p, "12345");
   p += 6;
   *p++ = 0;

   msg->length = (char*)p - (char*)msg->data;

   MCTF_ASSERT_INT_EQ(pgmoneta_extract_error_fields('S', msg, &extracted), 0, cleanup, "extract_error_fields 'S' failed");
   MCTF_ASSERT_PTR_NONNULL(extracted, cleanup, "extracted 'S' is null");
   MCTF_ASSERT_STR_EQ(extracted, "ERROR", cleanup, "extracted 'S' mismatch");
   free(extracted);
   extracted = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_extract_error_fields('C', msg, &extracted), 0, cleanup, "extract_error_fields 'C' failed");
   MCTF_ASSERT_PTR_NONNULL(extracted, cleanup, "extracted 'C' is null");
   MCTF_ASSERT_STR_EQ(extracted, "12345", cleanup, "extracted 'C' mismatch");
   free(extracted);
   extracted = NULL;

   MCTF_ASSERT(pgmoneta_extract_error_fields('X', msg, &extracted) != 0, cleanup, "extract_error_fields 'X' should fail");
   MCTF_ASSERT_PTR_NULL(extracted, cleanup, "extracted 'X' should be null");

cleanup:
   if (extracted != NULL)
   {
      free(extracted);
      extracted = NULL;
   }
   if (msg != NULL)
   {
      if (msg->data != NULL)
      {
         free(msg->data);
         msg->data = NULL;
      }
      free(msg);
      msg = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_wal_unit)
{
   char* s = NULL;

   s = pgmoneta_wal_file_name(1, 1, 16 * 1024 * 1024);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "wal_file_name failed");
   MCTF_ASSERT_STR_EQ(s, "000000010000000000000001", cleanup, "wal_file_name result mismatch");
   free(s);
   s = NULL;

cleanup:
   if (s != NULL)
   {
      free(s);
      s = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_base32)
{
   unsigned char* hex = NULL;
   // "MZXW6YTBOI======" is base32 for "foobar".
   // for (i = 0; i < base32_length; i++) sprintf(..., "%02x", base32[i]);
   // It creates a HEX representation of the INPUT BYTES directly, treating them as bytes.
   // The implementation iterates i from 0 to length, and prints base32[i] as hex.
   // So if input is "A", hex is "41".

   unsigned char input[] = "A";
   MCTF_ASSERT_INT_EQ(pgmoneta_convert_base32_to_hex(input, 1, &hex), 0, cleanup, "convert_base32_to_hex 1 failed");
   MCTF_ASSERT_PTR_NONNULL(hex, cleanup, "hex is null 1");
   MCTF_ASSERT_STR_EQ((char*)hex, "41", cleanup, "hex result 1 mismatch ('A' in hex is 41)");
   free(hex);
   hex = NULL;

   unsigned char input2[] = "\x01\x02";
   MCTF_ASSERT_INT_EQ(pgmoneta_convert_base32_to_hex(input2, 2, &hex), 0, cleanup, "convert_base32_to_hex 2 failed");
   MCTF_ASSERT_PTR_NONNULL(hex, cleanup, "hex is null 2");
   MCTF_ASSERT_STR_EQ((char*)hex, "0102", cleanup, "hex result 2 mismatch");
   free(hex);
   hex = NULL;

cleanup:
   if (hex != NULL)
   {
      free(hex);
      hex = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_enc_comp)
{
   // Is encrypted

   MCTF_ASSERT(pgmoneta_is_encrypted("file.aes"), cleanup, "is_encrypted positive failed");
   MCTF_ASSERT(!pgmoneta_is_encrypted("file.txt"), cleanup, "is_encrypted negative 1 failed");
   MCTF_ASSERT(!pgmoneta_is_encrypted(NULL), cleanup, "is_encrypted NULL failed");

   // Is compressed

   MCTF_ASSERT(pgmoneta_is_compressed("file.zstd"), cleanup, "is_compressed zstd failed");
   MCTF_ASSERT(pgmoneta_is_compressed("file.lz4"), cleanup, "is_compressed lz4 failed");
   MCTF_ASSERT(pgmoneta_is_compressed("file.bz2"), cleanup, "is_compressed bz2 failed");
   MCTF_ASSERT(pgmoneta_is_compressed("file.gz"), cleanup, "is_compressed gz failed");
   MCTF_ASSERT(!pgmoneta_is_compressed("file.txt"), cleanup, "is_compressed negative failed");
   MCTF_ASSERT(!pgmoneta_is_compressed(NULL), cleanup, "is_compressed NULL failed");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_file_type_bitmask)
{
   uint32_t type = 0;

   // Test basic WAL file
   type = pgmoneta_get_file_type("000000010000000000000001");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_WAL), cleanup, "WAL file type detection failed");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_COMPRESSED), cleanup, "WAL file should not be compressed");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_ENCRYPTED), cleanup, "WAL file should not be encrypted");

   // Test compressed WAL file .gz
   type = pgmoneta_get_file_type("000000010000000000000001.gz");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_WAL), cleanup, "Compressed WAL detection failed .gz");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_COMPRESSED), cleanup, "Compression detection failed .gz");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_GZIP), cleanup, "GZIP detection failed .gz");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_ZSTD), cleanup, "ZSTD must not be set for .gz");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_LZ4), cleanup, "LZ4 must not be set for .gz");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_BZ2), cleanup, "BZ2 must not be set for .gz");

   // Test compressed WAL file .zstd
   type = pgmoneta_get_file_type("000000010000000000000001.zstd");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_WAL), cleanup, "Compressed WAL detection failed .zstd");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_COMPRESSED), cleanup, "Compression detection failed .zstd");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_ZSTD), cleanup, "ZSTD detection failed .zstd");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_GZIP), cleanup, "GZIP must not be set for .zstd");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_LZ4), cleanup, "LZ4 must not be set for .zstd");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_BZ2), cleanup, "BZ2 must not be set for .zstd");

   // Test compressed WAL file .lz4
   type = pgmoneta_get_file_type("000000010000000000000001.lz4");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_WAL), cleanup, "Compressed WAL detection failed .lz4");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_COMPRESSED), cleanup, "Compression detection failed .lz4");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_LZ4), cleanup, "LZ4 detection failed .lz4");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_GZIP), cleanup, "GZIP must not be set for .lz4");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_ZSTD), cleanup, "ZSTD must not be set for .lz4");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_BZ2), cleanup, "BZ2 must not be set for .lz4");

   // Test compressed WAL file .bz2
   type = pgmoneta_get_file_type("000000010000000000000001.bz2");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_WAL), cleanup, "Compressed WAL detection failed .bz2");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_COMPRESSED), cleanup, "Compression detection failed .bz2");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_BZ2), cleanup, "BZ2 detection failed .bz2");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_GZIP), cleanup, "GZIP must not be set for .bz2");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_ZSTD), cleanup, "ZSTD must not be set for .bz2");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_LZ4), cleanup, "LZ4 must not be set for .bz2");

   // Test encrypted compressed WAL file
   type = pgmoneta_get_file_type("000000010000000000000001.zstd.aes");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_WAL), cleanup, "Encrypted WAL detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_COMPRESSED), cleanup, "Compression in encrypted failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_ENCRYPTED), cleanup, "Encryption detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_ZSTD), cleanup, "ZSTD detection failed in encrypted WAL");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_GZIP), cleanup, "GZIP must not be set in .zstd.aes");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_LZ4), cleanup, "LZ4 must not be set in .zstd.aes");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_BZ2), cleanup, "BZ2 must not be set in .zstd.aes");

   // Test partial file
   type = pgmoneta_get_file_type("000000010000000000000001.partial");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_WAL), cleanup, "Partial WAL detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_PARTIAL), cleanup, "Partial flag detection failed");

   // Test TAR archive
   type = pgmoneta_get_file_type("backup.tar");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_TAR), cleanup, "TAR detection failed");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_WAL), cleanup, "TAR should not be WAL");

   // Test compressed TAR archive - GZIP
   type = pgmoneta_get_file_type("backup.tar.gz");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_TAR), cleanup, "Compressed TAR detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_COMPRESSED), cleanup, "TAR compression detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_GZIP), cleanup, "TAR GZIP type detection failed");

   // Test compressed TAR archive - LZ4
   type = pgmoneta_get_file_type("backup.tar.lz4");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_TAR), cleanup, "Compressed TAR LZ4 detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_LZ4), cleanup, "TAR LZ4 type detection failed");

   // Test compressed TAR archive - ZSTD
   type = pgmoneta_get_file_type("backup.tar.zstd");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_TAR), cleanup, "Compressed TAR ZSTD detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_ZSTD), cleanup, "TAR ZSTD type detection failed");

   // Test encrypted compressed TAR archive - ZSTD + AES
   type = pgmoneta_get_file_type("backup.tar.zstd.aes");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_TAR), cleanup, "Encrypted compressed TAR detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_ZSTD), cleanup, "Encrypted TAR ZSTD type detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_COMPRESSED), cleanup, "Encrypted TAR compression flag detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_ENCRYPTED), cleanup, "Encrypted TAR encryption flag detection failed");

   // Test compressed TAR archive - BZ2
   type = pgmoneta_get_file_type("backup.tar.bz2");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_TAR), cleanup, "Compressed TAR BZ2 detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_BZ2), cleanup, "TAR BZ2 type detection failed");

   // Test .tgz shorthand
   type = pgmoneta_get_file_type("backup.tgz");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_TAR), cleanup, "TGZ TAR detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_GZIP), cleanup, "TGZ GZIP detection failed");

   // Test NULL input
   type = pgmoneta_get_file_type(NULL);
   MCTF_ASSERT(type == PGMONETA_FILE_TYPE_UNKNOWN, cleanup, "NULL should return UNKNOWN");

   // Test non-WAL file
   type = pgmoneta_get_file_type("random_file.txt");
   MCTF_ASSERT(type == PGMONETA_FILE_TYPE_UNKNOWN, cleanup, "Random file should be UNKNOWN");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_extract_layered_archive)
{
   struct main_configuration* config = NULL;
   FILE* f = NULL;
   bool own_shmem = false;
   const char* wal_name = "000000010000000000000001";
   char root[MAX_PATH];
   char src_dir[MAX_PATH];
   char out_dir[MAX_PATH];
   char home_dir[MAX_PATH];
   char hidden_dir[MAX_PATH];
   char master_key_path[MAX_PATH];
   char wal_src_path[MAX_PATH];
   char tar_path[MAX_PATH];
   char zstd_path[MAX_PATH];
   char encrypted_path[MAX_PATH];
   char extracted_path[MAX_PATH];

   memset(root, 0, sizeof(root));
   memset(src_dir, 0, sizeof(src_dir));
   memset(out_dir, 0, sizeof(out_dir));
   memset(home_dir, 0, sizeof(home_dir));
   memset(hidden_dir, 0, sizeof(hidden_dir));
   memset(master_key_path, 0, sizeof(master_key_path));
   memset(wal_src_path, 0, sizeof(wal_src_path));
   memset(tar_path, 0, sizeof(tar_path));
   memset(zstd_path, 0, sizeof(zstd_path));
   memset(encrypted_path, 0, sizeof(encrypted_path));
   memset(extracted_path, 0, sizeof(extracted_path));

   pgmoneta_snprintf(root, sizeof(root), "test_extract_layered_archive");
   pgmoneta_snprintf(src_dir, sizeof(src_dir), "%s/src", root);
   pgmoneta_snprintf(out_dir, sizeof(out_dir), "%s/out", root);
   pgmoneta_snprintf(home_dir, sizeof(home_dir), "%s/home", root);
   pgmoneta_snprintf(hidden_dir, sizeof(hidden_dir), "%s/.pgmoneta", home_dir);
   pgmoneta_snprintf(master_key_path, sizeof(master_key_path), "%s/master.key", hidden_dir);
   pgmoneta_snprintf(wal_src_path, sizeof(wal_src_path), "%s/%s", src_dir, wal_name);
   pgmoneta_snprintf(tar_path, sizeof(tar_path), "%s/wal.tar", root);
   pgmoneta_snprintf(zstd_path, sizeof(zstd_path), "%s/wal.tar.zstd", root);
   pgmoneta_snprintf(encrypted_path, sizeof(encrypted_path), "%s/wal.tar.zstd.aes", root);
   pgmoneta_snprintf(extracted_path, sizeof(extracted_path), "%s/wal_layer/%s", out_dir, wal_name);

   pgmoneta_delete_directory(root);

   MCTF_ASSERT_INT_EQ(pgmoneta_mkdir(root), 0, cleanup, "mkdir root failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_mkdir(src_dir), 0, cleanup, "mkdir src failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_mkdir(out_dir), 0, cleanup, "mkdir out failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_mkdir(home_dir), 0, cleanup, "mkdir home failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_mkdir(hidden_dir), 0, cleanup, "mkdir .pgmoneta failed");
   MCTF_ASSERT_INT_EQ(chmod(hidden_dir, 0700), 0, cleanup, "chmod .pgmoneta failed");

   f = fopen(master_key_path, "w");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "fopen master.key failed");
   fprintf(f, "dGVzdF9tYXN0ZXJfa2V5");
   fflush(f);
   fclose(f);
   f = NULL;
   MCTF_ASSERT_INT_EQ(chmod(master_key_path, 0600), 0, cleanup, "chmod master.key failed");

   f = fopen(wal_src_path, "w");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "fopen WAL source failed");
   fprintf(f, "wal test payload");
   fflush(f);
   fclose(f);
   f = NULL;

   if (shmem == NULL)
   {
      shmem = calloc(1, sizeof(struct main_configuration));
      MCTF_ASSERT_PTR_NONNULL(shmem, cleanup, "calloc shmem failed");
      own_shmem = true;
   }

   config = (struct main_configuration*)shmem;
   MCTF_ASSERT_PTR_NONNULL(config, cleanup, "config is null");

   memset(config->common.home_dir, 0, sizeof(config->common.home_dir));
   pgmoneta_snprintf(config->common.home_dir, sizeof(config->common.home_dir), "%s", home_dir);
   config->compression_level = 1;
   config->workers = 1;
   config->encryption = ENCRYPTION_AES_256_CBC;

   MCTF_ASSERT_INT_EQ(pgmoneta_tar_directory(src_dir, tar_path, "wal_layer"), 0, cleanup, "tar_directory failed");
   MCTF_ASSERT(pgmoneta_exists(tar_path), cleanup, "tar file missing");

   MCTF_ASSERT_INT_EQ(pgmoneta_zstandardc_file(tar_path, zstd_path), 0, cleanup, "zstd compression failed");
   MCTF_ASSERT(pgmoneta_exists(zstd_path), cleanup, "zstd file missing");

   MCTF_ASSERT_INT_EQ(pgmoneta_encrypt_file(zstd_path, encrypted_path), 0, cleanup, "encryption failed");
   MCTF_ASSERT(pgmoneta_exists(encrypted_path), cleanup, "encrypted file missing");

   MCTF_ASSERT_INT_EQ(pgmoneta_extract_file(encrypted_path, out_dir), 0, cleanup, "extract_file failed");
   MCTF_ASSERT(pgmoneta_exists(extracted_path), cleanup, "extracted WAL file missing");

cleanup:
   if (f != NULL)
   {
      fclose(f);
      f = NULL;
   }
   if (own_shmem)
   {
      free(shmem);
      shmem = NULL;
      own_shmem = false;
   }

   pgmoneta_delete_directory(root);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_message_parsing)
{
   struct message* msg = NULL;
   struct message* extracted = NULL;
   char* username = NULL;
   char* database = NULL;
   char* appname = NULL;
   int res = 0;
   void* p = NULL;

   msg = (struct message*)malloc(sizeof(struct message));
   MCTF_ASSERT_PTR_NONNULL(msg, cleanup, "malloc message failed 1");
   msg->kind = 0;
   msg->data = calloc(1, 1024);
   MCTF_ASSERT_PTR_NONNULL(msg->data, cleanup, "calloc msg->data failed 1");
   p = msg->data;

   // The utility expects [Length(4)][Protocol(4)][Key][Val]...

   // We write a dummy length first.

   pgmoneta_write_int32(p, 0);
   p += 4;
   pgmoneta_write_int32(p, 196608);
   p += 4;
   pgmoneta_write_string(p, "user");
   p += 5;
   pgmoneta_write_string(p, "myuser");
   p += 7;
   pgmoneta_write_string(p, "database");
   p += 9;
   pgmoneta_write_string(p, "mydb");
   p += 5;
   pgmoneta_write_string(p, "application_name");
   p += 17;
   pgmoneta_write_string(p, "myapp");
   p += 6;
   pgmoneta_write_byte(p, 0);
   p += 1;

   msg->length = (char*)p - (char*)msg->data;

   res = pgmoneta_extract_username_database(msg, &username, &database, &appname);
   MCTF_ASSERT_INT_EQ(res, 0, cleanup, "extract_username_database failed");
   MCTF_ASSERT_PTR_NONNULL(username, cleanup, "username is null");
   MCTF_ASSERT_PTR_NONNULL(database, cleanup, "database is null");
   MCTF_ASSERT_PTR_NONNULL(appname, cleanup, "appname is null");
   MCTF_ASSERT_STR_EQ(username, "myuser", cleanup, "username mismatch");
   MCTF_ASSERT_STR_EQ(database, "mydb", cleanup, "database mismatch");
   MCTF_ASSERT_STR_EQ(appname, "myapp", cleanup, "appname mismatch");

   free(username);
   username = NULL;
   free(database);
   database = NULL;
   free(appname);
   appname = NULL;
   free(msg->data);
   msg->data = NULL;
   free(msg);
   msg = NULL;

   // Test pgmoneta_extract_message (e.g. ErrorResponse 'E')

   // extract_message expects [Type][Length]... in msg->data

   msg = (struct message*)malloc(sizeof(struct message));
   MCTF_ASSERT_PTR_NONNULL(msg, cleanup, "malloc message failed 2");
   msg->kind = 'E';
   msg->data = calloc(1, 1024);
   MCTF_ASSERT_PTR_NONNULL(msg->data, cleanup, "calloc msg->data failed 2");
   p = msg->data;

   pgmoneta_write_byte(p, 'E');
   p += 1;
   pgmoneta_write_int32(p, 4);
   p += 4;
   msg->length = (char*)p - (char*)msg->data;

   res = pgmoneta_extract_message('E', msg, &extracted);
   MCTF_ASSERT_INT_EQ(res, 0, cleanup, "extract_message failed");
   MCTF_ASSERT_PTR_NONNULL(extracted, cleanup, "extracted message is null");
   MCTF_ASSERT_INT_EQ(extracted->kind, 'E', cleanup, "extracted message kind mismatch");

   pgmoneta_free_message(extracted);
   extracted = NULL;

cleanup:
   if (username != NULL)
   {
      free(username);
      username = NULL;
   }
   if (database != NULL)
   {
      free(database);
      database = NULL;
   }
   if (appname != NULL)
   {
      free(appname);
      appname = NULL;
   }
   if (extracted != NULL)
   {
      pgmoneta_free_message(extracted);
      extracted = NULL;
   }
   if (msg != NULL)
   {
      if (msg->data != NULL)
      {
         free(msg->data);
         msg->data = NULL;
      }
      free(msg);
      msg = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_permissions)
{
   char* dir = "test_perm_dir";
   char* file = "test_perm_dir/file";
   mode_t mode = 0;
   FILE* f = NULL;

   pgmoneta_delete_directory(dir);
   pgmoneta_mkdir(dir);

   f = fopen(file, "w");
   if (f)
   {
      fflush(f);
      fclose(f);
      f = NULL;
   }

   MCTF_ASSERT_INT_EQ(pgmoneta_permission_recursive(dir), 0, cleanup, "permission_recursive failed");

   mode = pgmoneta_get_permission(file);
   MCTF_ASSERT(mode > 0, cleanup, "get_permission should be > 0");

cleanup:
   if (f != NULL)
   {
      fclose(f);
      f = NULL;
   }
   pgmoneta_delete_directory(dir);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_space)
{
   unsigned long total_sp = 0;
   char* dir = "test_space_dir";
   char* file1 = "test_space_dir/small";
   char* file2 = "test_space_dir/big";
   FILE* f = NULL;
   unsigned long biggest = 0;

   total_sp = pgmoneta_total_space(".");
   MCTF_ASSERT(total_sp > 0, cleanup, "total_space should be > 0");

   pgmoneta_mkdir(dir);

   f = fopen(file1, "w");
   if (f)
   {
      fprintf(f, "a");
      fflush(f);
      fclose(f);
      f = NULL;
   }
   f = fopen(file2, "w");
   if (f)
   {
      fprintf(f, "aaaaa");
      fflush(f);
      fclose(f);
      f = NULL;
   }

   biggest = pgmoneta_biggest_file(dir);
   MCTF_ASSERT(biggest >= 5, cleanup, "biggest_file should be >= 5");

cleanup:
   if (f != NULL)
   {
      fclose(f);
      f = NULL;
   }
   pgmoneta_delete_directory(dir);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_files_advanced)
{
   char* src = NULL;
   char* dst = NULL;
   char* sub = NULL;
   char* subfile = NULL;
   char* file_src = NULL;
   char* file_dst = NULL;
   FILE* f = NULL;
   char* to_ptr = NULL;

   src = pgmoneta_append(NULL, "test_adv_src");
   MCTF_ASSERT_PTR_NONNULL(src, cleanup, "append src failed");
   dst = pgmoneta_append(NULL, "test_adv_dst");
   MCTF_ASSERT_PTR_NONNULL(dst, cleanup, "append dst failed");
   sub = pgmoneta_append(NULL, src);
   MCTF_ASSERT_PTR_NONNULL(sub, cleanup, "append sub base failed");
   sub = pgmoneta_append(sub, "/sub");
   MCTF_ASSERT_PTR_NONNULL(sub, cleanup, "append sub failed");
   subfile = pgmoneta_append(NULL, sub);
   MCTF_ASSERT_PTR_NONNULL(subfile, cleanup, "append subfile base failed");
   subfile = pgmoneta_append(subfile, "/file.txt");
   MCTF_ASSERT_PTR_NONNULL(subfile, cleanup, "append subfile failed");

   // Setup source

   pgmoneta_delete_directory(src);
   pgmoneta_delete_directory(dst);

   pgmoneta_mkdir(src);
   pgmoneta_mkdir(sub);
   f = fopen(subfile, "w");
   if (f)
   {
      fprintf(f, "data");
      fflush(f);
      fclose(f);
      f = NULL;
   }

   // Test is_wal_file

   MCTF_ASSERT(pgmoneta_is_wal_file("000000010000000000000001"), cleanup, "is_wal_file positive failed");
   MCTF_ASSERT(!pgmoneta_is_wal_file("history"), cleanup, "is_wal_file negative 1 failed");
   MCTF_ASSERT(!pgmoneta_is_wal_file("000000010000000000000001.partial"), cleanup, "is_wal_file negative 2 failed");

   // Test copy_and_extract basic

   file_src = pgmoneta_append(NULL, src);
   MCTF_ASSERT_PTR_NONNULL(file_src, cleanup, "append file_src base failed");
   file_src = pgmoneta_append(file_src, "/plain.txt");
   MCTF_ASSERT_PTR_NONNULL(file_src, cleanup, "append file_src failed");
   file_dst = pgmoneta_append(NULL, dst);
   MCTF_ASSERT_PTR_NONNULL(file_dst, cleanup, "append file_dst base failed");
   file_dst = pgmoneta_append(file_dst, "/plain.txt");
   MCTF_ASSERT_PTR_NONNULL(file_dst, cleanup, "append file_dst failed");

   pgmoneta_mkdir(dst);
   f = fopen(file_src, "w");
   if (f)
   {
      fprintf(f, "plain");
      fflush(f);
      fclose(f);
      f = NULL;
   }

   to_ptr = strdup(file_dst);
   MCTF_ASSERT_PTR_NONNULL(to_ptr, cleanup, "strdup failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_copy_and_extract_file(file_src, &to_ptr), 0, cleanup, "copy_and_extract_file failed");
   MCTF_ASSERT(pgmoneta_exists(file_dst), cleanup, "copied file should exist");
   free(to_ptr);
   to_ptr = NULL;

   // Test list_directory (just ensure it runs)

   pgmoneta_list_directory(src);

cleanup:
   if (f != NULL)
   {
      fclose(f);
      f = NULL;
   }
   if (to_ptr != NULL)
   {
      free(to_ptr);
      to_ptr = NULL;
   }
   if (src != NULL)
   {
      pgmoneta_delete_directory(src);
      free(src);
      src = NULL;
   }
   if (dst != NULL)
   {
      pgmoneta_delete_directory(dst);
      free(dst);
      dst = NULL;
   }
   if (sub != NULL)
   {
      free(sub);
      sub = NULL;
   }
   if (subfile != NULL)
   {
      free(subfile);
      subfile = NULL;
   }
   if (file_src != NULL)
   {
      free(file_src);
      file_src = NULL;
   }
   if (file_dst != NULL)
   {
      free(file_dst);
      file_dst = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_missing_basic)
{
   // Time functions

   struct timespec start = {100, 0};
   struct timespec end = {105, 500000000}; // 5.5 seconds later
   double seconds = 0;
   double duration = 0;
   char* ts_str = NULL;
   char* user = NULL;
   char* home = NULL;
   char* fpath = "test_del_file.txt";
   FILE* f = NULL;
   char* dir = "test_link_at_dir";

   duration = pgmoneta_compute_duration(start, end);
   MCTF_ASSERT(duration > 5.4 && duration < 5.6, cleanup, "compute_duration failed");

   ts_str = pgmoneta_get_timestamp_string(start, end, &seconds);
   MCTF_ASSERT_PTR_NONNULL(ts_str, cleanup, "get_timestamp_string failed");
   MCTF_ASSERT(seconds > 5.4 && seconds < 5.6, cleanup, "get_timestamp_string seconds mismatch");
   free(ts_str);
   ts_str = NULL;

   // System / User

   user = pgmoneta_get_user_name();
   MCTF_ASSERT_PTR_NONNULL(user, cleanup, "get_user_name failed");
   free(user);
   user = NULL;

   home = pgmoneta_get_home_directory();
   MCTF_ASSERT_PTR_NONNULL(home, cleanup, "get_home_directory failed");
   free(home);
   home = NULL;

   // File Extended

   f = fopen(fpath, "w");
   if (f)
   {
      fprintf(f, "12345");
      fflush(f);
      fclose(f);
      f = NULL;
   }

   MCTF_ASSERT_INT_EQ(pgmoneta_get_file_size(fpath), 5, cleanup, "get_file_size failed");

   // pgmoneta_delete_file(char* file, struct workers* workers)

   MCTF_ASSERT_INT_EQ(pgmoneta_delete_file(fpath, NULL), 0, cleanup, "delete_file failed");
   MCTF_ASSERT(!pgmoneta_exists(fpath), cleanup, "file should be deleted");

   // Create temp dir for symlink test

   pgmoneta_mkdir(dir);
   pgmoneta_delete_directory(dir);

cleanup:
   if (ts_str != NULL)
   {
      free(ts_str);
      ts_str = NULL;
   }
   if (user != NULL)
   {
      free(user);
      user = NULL;
   }
   if (home != NULL)
   {
      free(home);
      home = NULL;
   }
   if (f != NULL)
   {
      fclose(f);
      f = NULL;
   }
   if (pgmoneta_exists(dir))
   {
      pgmoneta_delete_directory(dir);
   }
   if (pgmoneta_exists(fpath))
   {
      pgmoneta_delete_file(fpath, NULL);
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_missing_server)
{
   char* s = NULL;
   int server = 0;
   char* id = "20231026120000";
   struct main_configuration* config = NULL;

   pgmoneta_test_setup();

   config = (struct main_configuration*)shmem;
   MCTF_ASSERT_PTR_NONNULL(config, cleanup, "configuration is null");

   s = pgmoneta_get_server_summary(server);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "get_server_summary failed");
   free(s);
   s = NULL;

   // Inject wal_shipping config for testing

   strcpy(config->common.servers[server].wal_shipping, "/tmp/wal_ship");

   s = pgmoneta_get_server_wal_shipping(server);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "get_server_wal_shipping failed");
   free(s);
   s = NULL;

   s = pgmoneta_get_server_wal_shipping_wal(server);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "get_server_wal_shipping_wal failed");
   free(s);
   s = NULL;

   s = pgmoneta_get_server_workspace(server);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "get_server_workspace failed");
   // Setup workspace for delete test

   pgmoneta_mkdir(s);

   // Check deletion

   MCTF_ASSERT_INT_EQ(pgmoneta_delete_server_workspace(server, NULL), 0, cleanup, "delete_server_workspace failed");
   MCTF_ASSERT(!pgmoneta_exists(s), cleanup, "workspace should be deleted");
   free(s);
   s = NULL;

   // Identifiers

   s = pgmoneta_get_server_backup_identifier(server, id);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "get_server_backup_identifier failed");
   free(s);
   s = NULL;

   s = pgmoneta_get_server_extra_identifier(server, id);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "get_server_extra_identifier failed");
   free(s);
   s = NULL;

   s = pgmoneta_get_server_backup_identifier_data(server, id);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "get_server_backup_identifier_data failed");
   free(s);
   s = NULL;

   s = pgmoneta_get_server_backup_identifier_data_wal(server, id);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "get_server_backup_identifier_data_wal failed");
   free(s);
   s = NULL;

   s = pgmoneta_get_server_backup_identifier_tablespace(server, id, "tbs");
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "get_server_backup_identifier_tablespace failed");
   free(s);
   s = NULL;

cleanup:
   if (s != NULL)
   {
      free(s);
      s = NULL;
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_utils_missing_wal)
{
   char* dir = NULL;
   char* file1 = NULL;
   char* file2 = NULL;
   char* to_dir = NULL;
   char* check_file = NULL;
   struct deque* files = NULL;
   FILE* f = NULL;

   dir = pgmoneta_append(NULL, "test_wal_dir");
   MCTF_ASSERT_PTR_NONNULL(dir, cleanup, "append dir failed");
   pgmoneta_mkdir(dir);

   // Create dummy WAL files (24 chars hex)

   file1 = pgmoneta_append(NULL, dir);
   MCTF_ASSERT_PTR_NONNULL(file1, cleanup, "append file1 base failed");
   file1 = pgmoneta_append(file1, "/000000010000000000000001");
   MCTF_ASSERT_PTR_NONNULL(file1, cleanup, "append file1 failed");
   file2 = pgmoneta_append(NULL, dir);
   MCTF_ASSERT_PTR_NONNULL(file2, cleanup, "append file2 base failed");
   file2 = pgmoneta_append(file2, "/000000010000000000000002");
   MCTF_ASSERT_PTR_NONNULL(file2, cleanup, "append file2 failed");

   f = fopen(file1, "w");
   if (f)
   {
      fflush(f);
      fclose(f);
      f = NULL;
   }
   f = fopen(file2, "w");
   if (f)
   {
      fflush(f);
      fclose(f);
      f = NULL;
   }

   MCTF_ASSERT_INT_EQ(pgmoneta_get_wal_files(dir, &files), 0, cleanup, "get_wal_files failed");
   MCTF_ASSERT_PTR_NONNULL(files, cleanup, "files deque is null");
   MCTF_ASSERT_INT_EQ(pgmoneta_deque_size(files), 2, cleanup, "files size should be 2");
   pgmoneta_deque_destroy(files);
   files = NULL;

   // number_of_wal_files

   MCTF_ASSERT_INT_EQ(pgmoneta_number_of_wal_files(dir, "000000000000000000000000", NULL), 2, cleanup, "number_of_wal_files failed");

   // copy_wal_files

   to_dir = pgmoneta_append(NULL, "test_wal_dir_copy");
   MCTF_ASSERT_PTR_NONNULL(to_dir, cleanup, "append to_dir failed");
   pgmoneta_mkdir(to_dir);

   MCTF_ASSERT_INT_EQ(pgmoneta_copy_wal_files(dir, to_dir, "000000000000000000000000", NULL), 0, cleanup, "copy_wal_files failed");
   check_file = pgmoneta_append(NULL, to_dir);
   MCTF_ASSERT_PTR_NONNULL(check_file, cleanup, "append check_file base failed");
   check_file = pgmoneta_append(check_file, "/000000010000000000000001");
   MCTF_ASSERT_PTR_NONNULL(check_file, cleanup, "append check_file failed");
   MCTF_ASSERT(pgmoneta_exists(check_file), cleanup, "copied WAL file should exist");

cleanup:
   if (f != NULL)
   {
      fclose(f);
      f = NULL;
   }
   if (files != NULL)
   {
      pgmoneta_deque_destroy(files);
      files = NULL;
   }
   if (to_dir != NULL && pgmoneta_exists(to_dir))
   {
      pgmoneta_delete_directory(to_dir);
   }
   if (dir != NULL)
   {
      pgmoneta_delete_directory(dir);
   }
   if (dir != NULL)
   {
      free(dir);
      dir = NULL;
   }
   if (file1 != NULL)
   {
      free(file1);
      file1 = NULL;
   }
   if (file2 != NULL)
   {
      free(file2);
      file2 = NULL;
   }
   if (to_dir != NULL)
   {
      free(to_dir);
      to_dir = NULL;
   }
   if (check_file != NULL)
   {
      free(check_file);
      check_file = NULL;
   }
   if (pgmoneta_exists(dir))
   {
      pgmoneta_delete_directory(dir);
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_missing_misc)
{
   // pgmoneta_extract_message_from_data
   // Construct a raw message buffer: Type (1 byte) + Length (4 bytes) + Data
   // Actually extract_message_from_data receives `data`
   // Check declaration: pgmoneta_extract_message_from_data(char type, void* data, size_t data_size, struct message** extracted)
   // Usually `data` is the payload.

   char buffer[1024];
   int len = 8; // 4 bytes for length field itself + 4 bytes content
   int nlen = 0;
   struct message* extracted = NULL;
   char arg0[32];
   char* argv[] = {arg0, NULL};
   struct main_configuration* config = NULL;

   memset(buffer, 0, 1024);
   // Format: Type(1) + Length(4) + Content
   buffer[0] = 'Q';
   nlen = htonl(len);
   memcpy(buffer + 1, &nlen, 4);
   memcpy(buffer + 1 + 4, "TEST", 4);

   MCTF_ASSERT_INT_EQ(pgmoneta_extract_message_from_data('Q', buffer, 9, &extracted), 0, cleanup, "extract_message_from_data failed");
   MCTF_ASSERT_PTR_NONNULL(extracted, cleanup, "extracted message is null");
   MCTF_ASSERT_INT_EQ(extracted->kind, 'Q', cleanup, "extracted message kind mismatch");
   MCTF_ASSERT_INT_EQ(extracted->length, 9, cleanup, "extracted message length mismatch");

   pgmoneta_free_message(extracted);
   extracted = NULL;

   pgmoneta_test_setup();
   config = (struct main_configuration*)shmem;
   if (config != NULL)
   {
      unsigned int original_update_process_title = config->update_process_title;
      config->update_process_title = UPDATE_PROCESS_TITLE_NEVER;
      strcpy(arg0, "pgmoneta");
      pgmoneta_set_proc_title(1, argv, "test", "title");
      config->update_process_title = original_update_process_title;
      config = NULL;
   }
   pgmoneta_test_teardown();

cleanup:
   if (extracted != NULL)
   {
      pgmoneta_free_message(extracted);
      extracted = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_direct_io)
{
   void* ptr = NULL;
   void* ptr2 = NULL;
   size_t block_size = 0;
   bool direct_io_result = false;

   /* Test aligned allocation with 4096 alignment */
   ptr = pgmoneta_allocate_aligned(8192, 4096);
   MCTF_ASSERT_PTR_NONNULL(ptr, cleanup, "pgmoneta_allocate_aligned(8192, 4096) failed");
   MCTF_ASSERT(((uintptr_t)ptr % 4096) == 0, cleanup, "memory not aligned to 4096");

   memset(ptr, 'A', 8192);
   MCTF_ASSERT(((char*)ptr)[0] == 'A', cleanup, "memory content mismatch at 0");
   MCTF_ASSERT(((char*)ptr)[8191] == 'A', cleanup, "memory content mismatch at end");

   pgmoneta_free_aligned(ptr);
   ptr = NULL;

   /* Test aligned allocation with 512 alignment */
   ptr2 = pgmoneta_allocate_aligned(4096, 512);
   MCTF_ASSERT_PTR_NONNULL(ptr2, cleanup, "pgmoneta_allocate_aligned(4096, 512) failed");
   MCTF_ASSERT(((uintptr_t)ptr2 % 512) == 0, cleanup, "memory not aligned to 512");

   pgmoneta_free_aligned(ptr2);
   ptr2 = NULL;

   /* Test block size detection on root */
   block_size = pgmoneta_get_block_size("/");
   MCTF_ASSERT(block_size > 0, cleanup, "block size for / is 0");

   /* Test block size detection on /tmp */
   block_size = pgmoneta_get_block_size("/tmp");
   MCTF_ASSERT(block_size > 0, cleanup, "block size for /tmp is 0");

   /* Test block size with NULL path returns default */
   block_size = pgmoneta_get_block_size(NULL);
   MCTF_ASSERT(block_size == 4096, cleanup, "block size for NULL should be 4096");

   /* Test block size on non-existent path returns default */
   block_size = pgmoneta_get_block_size("/nonexistent/path/that/does/not/exist");
   MCTF_ASSERT(block_size == 4096, cleanup, "block size for non-existent path should be 4096");

   /* Test free_aligned with NULL is safe */
   pgmoneta_free_aligned(NULL);

#if defined(__linux__)
   /* Test O_DIRECT support check - result depends on filesystem */
   direct_io_result = pgmoneta_direct_io_supported("/tmp");
   /* We don't assert the result since it depends on the filesystem,
    * but we verify the function runs without crashing */
   (void)direct_io_result;

   /* Test with NULL path should return false */
   direct_io_result = pgmoneta_direct_io_supported(NULL);
   MCTF_ASSERT(direct_io_result == false, cleanup, "direct_io_supported(NULL) should be false");

   /* Test with non-existent path should return false */
   direct_io_result = pgmoneta_direct_io_supported("/nonexistent/path");
   MCTF_ASSERT(direct_io_result == false, cleanup, "direct_io_supported on non-existent path should be false");
#endif

cleanup:
   pgmoneta_free_aligned(ptr);
   pgmoneta_free_aligned(ptr2);
   MCTF_FINISH();
}
