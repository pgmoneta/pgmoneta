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
#include <configuration.h>
#include <mctf.h>
#include <shmem.h>
#include <tscommon.h>
#include <utils.h>

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

MCTF_TEST(test_resolve_path_trailing_env_var)
{
   char* resolved = NULL;
   char* env_key = "PGMONETA_TEST_PATH_KEY";
   char* env_value = "PGMONETA_TEST_PATH_VALUE";
   char* expected = "/pgmoneta/PGMONETA_TEST_PATH_VALUE";
   int result;

   setenv(env_key, env_value, 1);

   result = pgmoneta_resolve_path("/pgmoneta/$PGMONETA_TEST_PATH_KEY", &resolved);
   MCTF_ASSERT_INT_EQ(result, 0, "resolve_path failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(resolved, "resolved path is null", cleanup);
   MCTF_ASSERT_STR_EQ(resolved, expected, "resolved path mismatch", cleanup);

   mctf_errno = 0;

cleanup:
   unsetenv(env_key);
   if (resolved != NULL)
   {
      free(resolved);
      resolved = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_starts_with)
{
   MCTF_ASSERT(pgmoneta_starts_with("hello world", "hello"), "starts_with positive case 1 failed", cleanup);
   MCTF_ASSERT(pgmoneta_starts_with("hello", "hello"), "starts_with positive case 2 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_starts_with("hello world", "world"), "starts_with negative case 1 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_starts_with("hello", "hello world"), "starts_with negative case 2 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_starts_with(NULL, "hello"), "starts_with NULL case 1 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_starts_with("hello", NULL), "starts_with NULL case 2 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_starts_with(NULL, NULL), "starts_with NULL case 3 failed", cleanup);

   mctf_errno = 0;

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_ends_with)
{
   MCTF_ASSERT(pgmoneta_ends_with("hello world", "world"), "ends_with positive case 1 failed", cleanup);
   MCTF_ASSERT(pgmoneta_ends_with("world", "world"), "ends_with positive case 2 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_ends_with("hello world", "hello"), "ends_with negative case 1 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_ends_with("world", "hello world"), "ends_with negative case 2 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_ends_with(NULL, "world"), "ends_with NULL case 1 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_ends_with("world", NULL), "ends_with NULL case 2 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_ends_with(NULL, NULL), "ends_with NULL case 3 failed", cleanup);

   mctf_errno = 0;

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_contains)
{
   MCTF_ASSERT(pgmoneta_contains("hello world", "lo wo"), "contains positive case 1 failed", cleanup);
   MCTF_ASSERT(pgmoneta_contains("hello", "he"), "contains positive case 2 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_contains("hello world", "z"), "contains negative case 1 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_contains(NULL, "hello"), "contains NULL case 1 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_contains("hello", NULL), "contains NULL case 2 failed", cleanup);

   mctf_errno = 0;

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_compare_string)
{
   MCTF_ASSERT(pgmoneta_compare_string("abc", "abc"), "compare_string positive case failed", cleanup);
   MCTF_ASSERT(!pgmoneta_compare_string("abc", "ABC"), "compare_string case sensitive failed", cleanup);
   MCTF_ASSERT(!pgmoneta_compare_string("abc", "def"), "compare_string negative case failed", cleanup);
   MCTF_ASSERT(!pgmoneta_compare_string(NULL, "abc"), "compare_string NULL case 1 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_compare_string("abc", NULL), "compare_string NULL case 2 failed", cleanup);
   MCTF_ASSERT(pgmoneta_compare_string(NULL, NULL), "compare_string NULL case 3 failed", cleanup);

   mctf_errno = 0;

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_atoi)
{
   MCTF_ASSERT_INT_EQ(pgmoneta_atoi("123"), 123, "atoi positive failed", cleanup);
   MCTF_ASSERT_INT_EQ(pgmoneta_atoi("-123"), -123, "atoi negative failed", cleanup);
   MCTF_ASSERT_INT_EQ(pgmoneta_atoi("0"), 0, "atoi zero failed", cleanup);
   MCTF_ASSERT_INT_EQ(pgmoneta_atoi(NULL), 0, "atoi NULL failed", cleanup);

   mctf_errno = 0;

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_is_number)
{
   MCTF_ASSERT(pgmoneta_is_number("123", 10), "is_number positive base10 failed", cleanup);
   MCTF_ASSERT(pgmoneta_is_number("-123", 10), "is_number negative base10 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_is_number("12a", 10), "is_number invalid base10 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_is_number("abc", 10), "is_number invalid base10 2 failed", cleanup);
   MCTF_ASSERT(pgmoneta_is_number("1A", 16), "is_number positive base16 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_is_number("1Z", 16), "is_number invalid base16 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_is_number(NULL, 10), "is_number NULL failed", cleanup);

   mctf_errno = 0;

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

   MCTF_ASSERT_INT_EQ(pgmoneta_base64_encode(original, original_length, &encoded, &encoded_length), 0, "base64_encode failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(encoded, "encoded is null", cleanup);
   MCTF_ASSERT(encoded_length > 0, "encoded_length should be > 0", cleanup);

   MCTF_ASSERT_INT_EQ(pgmoneta_base64_decode(encoded, encoded_length, (void**)&decoded, &decoded_length), 0, "base64_decode failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(decoded, "decoded is null", cleanup);
   MCTF_ASSERT_INT_EQ(decoded_length, original_length, "decoded_length mismatch", cleanup);
   MCTF_ASSERT(memcmp(decoded, original, original_length) == 0, "decoded content mismatch", cleanup);

   mctf_errno = 0;

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
   MCTF_ASSERT(pgmoneta_is_incremental_path("/path/to/backup/INCREMENTAL.20231026120000-20231026110000"), "is_incremental_path positive failed", cleanup);
   MCTF_ASSERT(!pgmoneta_is_incremental_path("/path/to/backup/20231026120000"), "is_incremental_path negative 1 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_is_incremental_path("/path/to/backup"), "is_incremental_path negative 2 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_is_incremental_path(NULL), "is_incremental_path NULL failed", cleanup);

   mctf_errno = 0;

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_get_parent_dir)
{
   char* parent = NULL;

   parent = pgmoneta_get_parent_dir("/a/b/c");
   MCTF_ASSERT_PTR_NONNULL(parent, "parent dir is null", cleanup);
   MCTF_ASSERT_STR_EQ(parent, "/a/b", "parent dir mismatch 1", cleanup);
   free(parent);
   parent = NULL;

   parent = pgmoneta_get_parent_dir("/a");
   MCTF_ASSERT_PTR_NONNULL(parent, "parent dir is null", cleanup);
   MCTF_ASSERT_STR_EQ(parent, "/", "parent dir mismatch 2", cleanup);
   free(parent);
   parent = NULL;

   parent = pgmoneta_get_parent_dir("/");
   MCTF_ASSERT_PTR_NONNULL(parent, "parent dir is null", cleanup);
   MCTF_ASSERT_STR_EQ(parent, "/", "parent dir mismatch 3", cleanup);
   free(parent);
   parent = NULL;

   parent = pgmoneta_get_parent_dir("a");
   MCTF_ASSERT_PTR_NONNULL(parent, "parent dir is null", cleanup);
   MCTF_ASSERT_STR_EQ(parent, ".", "parent dir mismatch 4", cleanup);
   free(parent);
   parent = NULL;

   parent = pgmoneta_get_parent_dir(NULL);
   MCTF_ASSERT_PTR_NULL(parent, "parent dir should be null for NULL input", cleanup);

   mctf_errno = 0;

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
   MCTF_ASSERT_PTR_NONNULL(data, "malloc failed", cleanup);
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
   MCTF_ASSERT_INT_EQ(pgmoneta_read_byte(ptr), b, "read_byte mismatch", cleanup);
   ptr += 1;
   MCTF_ASSERT_INT_EQ(pgmoneta_read_uint8(ptr), u8, "read_uint8 mismatch", cleanup);
   ptr += 1;
   MCTF_ASSERT_INT_EQ(pgmoneta_read_int16(ptr), i16, "read_int16 mismatch", cleanup);
   ptr += 2;
   MCTF_ASSERT_INT_EQ(pgmoneta_read_uint16(ptr), u16, "read_uint16 mismatch", cleanup);
   ptr += 2;
   MCTF_ASSERT_INT_EQ(pgmoneta_read_int32(ptr), i32, "read_int32 mismatch", cleanup);
   ptr += 4;
   MCTF_ASSERT_INT_EQ(pgmoneta_read_uint32(ptr), u32, "read_uint32 mismatch", cleanup);
   ptr += 4;
   MCTF_ASSERT_INT_EQ(pgmoneta_read_int64(ptr), i64, "read_int64 mismatch", cleanup);
   ptr += 8;
   MCTF_ASSERT_INT_EQ(pgmoneta_read_uint64(ptr), u64, "read_uint64 mismatch", cleanup);
   ptr += 8;
   MCTF_ASSERT(pgmoneta_read_bool(ptr) == bo, "read_bool mismatch", cleanup);
   ptr += 1;
   MCTF_ASSERT_STR_EQ(pgmoneta_read_string(ptr), s, "read_string mismatch", cleanup);

   mctf_errno = 0;

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
   MCTF_ASSERT_PTR_NONNULL(buffer, "append failed", cleanup);
   MCTF_ASSERT_STR_EQ(buffer, "hello", "append result mismatch 1", cleanup);

   buffer = pgmoneta_append_char(buffer, ' ');
   MCTF_ASSERT_PTR_NONNULL(buffer, "append_char failed", cleanup);
   MCTF_ASSERT_STR_EQ(buffer, "hello ", "append result mismatch 2", cleanup);

   buffer = pgmoneta_append_int(buffer, 123);
   MCTF_ASSERT_PTR_NONNULL(buffer, "append_int failed", cleanup);
   MCTF_ASSERT_STR_EQ(buffer, "hello 123", "append result mismatch 3", cleanup);

   buffer = pgmoneta_append(buffer, " ");
   buffer = pgmoneta_append_ulong(buffer, 456);
   MCTF_ASSERT_PTR_NONNULL(buffer, "append_ulong failed", cleanup);
   MCTF_ASSERT_STR_EQ(buffer, "hello 123 456", "append result mismatch 4", cleanup);

   buffer = pgmoneta_append(buffer, " ");
   buffer = pgmoneta_append_bool(buffer, true);
   MCTF_ASSERT_PTR_NONNULL(buffer, "append_bool failed", cleanup);
   MCTF_ASSERT_STR_EQ(buffer, "hello 123 456 true", "append result mismatch 5", cleanup);

   buffer = pgmoneta_append(buffer, " ");
   buffer = pgmoneta_append_double(buffer, 3.14);
   MCTF_ASSERT_PTR_NONNULL(buffer, "append_double failed", cleanup);
   MCTF_ASSERT_STR_EQ(buffer, "hello 123 456 true 3.140000", "append result mismatch 6", cleanup);

   mctf_errno = 0;

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

   /* test remove_whitespace */
   s = strdup(" a b c ");
   MCTF_ASSERT_PTR_NONNULL(s, "strdup failed", cleanup);
   res = pgmoneta_remove_whitespace(s);
   MCTF_ASSERT_PTR_NONNULL(res, "remove_whitespace failed", cleanup);
   MCTF_ASSERT_STR_EQ(res, "abc", "remove_whitespace result mismatch", cleanup);
   free(s);
   s = NULL;
   free(res);
   res = NULL;

   /* test remove_prefix */
   s = strdup("pre_test");
   MCTF_ASSERT_PTR_NONNULL(s, "strdup failed", cleanup);
   res = pgmoneta_remove_prefix(s, "pre_");
   MCTF_ASSERT_PTR_NONNULL(res, "remove_prefix failed", cleanup);
   MCTF_ASSERT_STR_EQ(res, "test", "remove_prefix result mismatch", cleanup);
   free(s);
   s = NULL;
   free(res);
   res = NULL;

   /* test remove_suffix */
   s = strdup("test.txt");
   MCTF_ASSERT_PTR_NONNULL(s, "strdup failed", cleanup);
   res = pgmoneta_remove_suffix(s, ".txt");
   MCTF_ASSERT_PTR_NONNULL(res, "remove_suffix failed", cleanup);
   MCTF_ASSERT_STR_EQ(res, "test", "remove_suffix result mismatch", cleanup);
   free(s);
   s = NULL;
   free(res);
   res = NULL;

   /* test indent */
   s = strdup("hello");
   MCTF_ASSERT_PTR_NONNULL(s, "strdup failed", cleanup);
   res = pgmoneta_indent(s, NULL, 2);
   MCTF_ASSERT_PTR_NONNULL(res, "indent failed", cleanup);
   MCTF_ASSERT_STR_EQ(res, "hello  ", "indent result mismatch", cleanup);
   s = NULL;
   free(res);
   res = NULL;

   /* test escape_string */
   s = strdup("foo'bar");
   MCTF_ASSERT_PTR_NONNULL(s, "strdup failed", cleanup);
   res = pgmoneta_escape_string(s);
   MCTF_ASSERT_PTR_NONNULL(res, "escape_string failed", cleanup);
   MCTF_ASSERT_STR_EQ(res, "foo\\'bar", "escape_string result mismatch", cleanup);
   free(s);
   s = NULL;
   free(res);
   res = NULL;

   mctf_errno = 0;

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
   MCTF_ASSERT(pgmoneta_get_aligned_size(1) >= 1, "get_aligned_size 1 failed", cleanup);
   MCTF_ASSERT(pgmoneta_get_aligned_size(100) >= 100, "get_aligned_size 100 failed", cleanup);

   MCTF_ASSERT_INT_EQ(pgmoneta_swap(0x12345678), 0x78563412, "swap failed", cleanup);

   char* array[] = {"b", "a", "c"};
   pgmoneta_sort(3, array);
   MCTF_ASSERT_STR_EQ(array[0], "a", "sort result 0 mismatch", cleanup);
   MCTF_ASSERT_STR_EQ(array[1], "b", "sort result 1 mismatch", cleanup);
   MCTF_ASSERT_STR_EQ(array[2], "c", "sort result 2 mismatch", cleanup);

   mctf_errno = 0;

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_version)
{
   MCTF_ASSERT_INT_EQ(pgmoneta_version_as_number(1, 2, 3), 10203, "version_as_number failed", cleanup);
   MCTF_ASSERT(pgmoneta_version_ge(0, 0, 0), "version_ge positive failed", cleanup);
   MCTF_ASSERT(!pgmoneta_version_ge(99, 99, 99), "version_ge negative failed", cleanup);

   mctf_errno = 0;

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_bigendian)
{
   int n = 1;
   bool is_little = (*(char*)&n == 1);
   if (is_little)
   {
      MCTF_ASSERT(!pgmoneta_bigendian(), "bigendian should be false on little-endian", cleanup);
   }
   else
   {
      MCTF_ASSERT(pgmoneta_bigendian(), "bigendian should be true on big-endian", cleanup);
   }

   mctf_errno = 0;

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_strip_extension)
{
   char* name = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_strip_extension("file.txt", &name), 0, "strip_extension failed 1", cleanup);
   MCTF_ASSERT_PTR_NONNULL(name, "name is null 1", cleanup);
   MCTF_ASSERT_STR_EQ(name, "file", "strip_extension result 1 mismatch", cleanup);
   free(name);
   name = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_strip_extension("file", &name), 0, "strip_extension failed 2", cleanup);
   MCTF_ASSERT_PTR_NONNULL(name, "name is null 2", cleanup);
   MCTF_ASSERT_STR_EQ(name, "file", "strip_extension result 2 mismatch", cleanup);
   free(name);
   name = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_strip_extension("file.tar.gz", &name), 0, "strip_extension failed 3", cleanup);
   MCTF_ASSERT_PTR_NONNULL(name, "name is null 3", cleanup);
   MCTF_ASSERT_STR_EQ(name, "file.tar", "strip_extension result 3 mismatch", cleanup);
   free(name);
   name = NULL;

   /* Hidden file case */
   MCTF_ASSERT_INT_EQ(pgmoneta_strip_extension(".bashrc", &name), 0, "strip_extension failed 4", cleanup);
   MCTF_ASSERT_PTR_NONNULL(name, "name is null 4", cleanup);
   MCTF_ASSERT_STR_EQ(name, "", "strip_extension result 4 mismatch", cleanup);
   free(name);
   name = NULL;

   mctf_errno = 0;

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
   MCTF_ASSERT_PTR_NONNULL(s, "translate_file_size 100 failed", cleanup);
   MCTF_ASSERT_STR_EQ(s, "100.00B", "translate_file_size 100 result mismatch", cleanup);
   free(s);
   s = NULL;

   s = pgmoneta_translate_file_size(1024);
   MCTF_ASSERT_PTR_NONNULL(s, "translate_file_size 1024 failed", cleanup);
   MCTF_ASSERT_STR_EQ(s, "1.00kB", "translate_file_size 1024 result mismatch", cleanup);
   free(s);
   s = NULL;

   mctf_errno = 0;

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
      fclose(f);
      f = NULL;
   }

   MCTF_ASSERT(pgmoneta_exists(path), "file should exist", cleanup);
   MCTF_ASSERT(pgmoneta_is_file(path), "path should be a file", cleanup);
   MCTF_ASSERT(!pgmoneta_is_directory(path), "path should not be a directory", cleanup);

   pgmoneta_mkdir(dir);
   MCTF_ASSERT(pgmoneta_exists(dir), "directory should exist", cleanup);
   MCTF_ASSERT(pgmoneta_is_directory(dir), "dir should be a directory", cleanup);
   MCTF_ASSERT(!pgmoneta_is_file(dir), "dir should not be a file", cleanup);

   remove(path);
   pgmoneta_delete_directory(dir);

   /* remove doesn't check immediate effect usually but here it should be fine */
   MCTF_ASSERT(!pgmoneta_exists(dir), "directory should be deleted", cleanup);

   mctf_errno = 0;

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
   MCTF_ASSERT_STR_EQ(buf, "Hello World", "snprintf result 1 mismatch", cleanup);

   pgmoneta_snprintf(buf, 5, "0123456789");
   MCTF_ASSERT_STR_EQ(buf, "0123", "snprintf truncation failed", cleanup);

   mctf_errno = 0;

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_string_extras)
{
   char* s = NULL;
   char** results = NULL;
   int count = 0;

   /* pgmoneta_remove_first */
   s = strdup("abc");
   MCTF_ASSERT_PTR_NONNULL(s, "strdup failed 1", cleanup);
   s = pgmoneta_remove_first(s);
   MCTF_ASSERT_PTR_NONNULL(s, "remove_first returned null", cleanup);
   MCTF_ASSERT_STR_EQ(s, "bc", "remove_first result 1 mismatch", cleanup);
   free(s);
   s = NULL;

   s = strdup("a");
   MCTF_ASSERT_PTR_NONNULL(s, "strdup failed 2", cleanup);
   s = pgmoneta_remove_first(s);
   MCTF_ASSERT_PTR_NONNULL(s, "remove_first returned null 2", cleanup);
   MCTF_ASSERT_STR_EQ(s, "", "remove_first result 2 mismatch", cleanup);
   free(s);
   s = NULL;

   MCTF_ASSERT_PTR_NULL(pgmoneta_remove_first(NULL), "remove_first should return NULL for NULL input", cleanup);

   /* pgmoneta_remove_last */
   s = strdup("abc");
   MCTF_ASSERT_PTR_NONNULL(s, "strdup failed 3", cleanup);
   s = pgmoneta_remove_last(s);
   MCTF_ASSERT_PTR_NONNULL(s, "remove_last returned null", cleanup);
   MCTF_ASSERT_STR_EQ(s, "ab", "remove_last result 1 mismatch", cleanup);
   free(s);
   s = NULL;

   s = strdup("a");
   MCTF_ASSERT_PTR_NONNULL(s, "strdup failed 4", cleanup);
   s = pgmoneta_remove_last(s);
   MCTF_ASSERT_PTR_NONNULL(s, "remove_last returned null 2", cleanup);
   MCTF_ASSERT_STR_EQ(s, "", "remove_last result 2 mismatch", cleanup);
   free(s);
   s = NULL;

   MCTF_ASSERT_PTR_NULL(pgmoneta_remove_last(NULL), "remove_last should return NULL for NULL input", cleanup);

   /* pgmoneta_bytes_to_string */
   s = pgmoneta_bytes_to_string(1024);
   MCTF_ASSERT_PTR_NONNULL(s, "bytes_to_string 1024 failed", cleanup);
   MCTF_ASSERT_STR_EQ(s, "1 KB", "bytes_to_string 1024 result mismatch", cleanup);
   free(s);
   s = NULL;

   s = pgmoneta_bytes_to_string(1024 * 1024);
   MCTF_ASSERT_PTR_NONNULL(s, "bytes_to_string 1MB failed", cleanup);
   MCTF_ASSERT_STR_EQ(s, "1 MB", "bytes_to_string 1MB result mismatch", cleanup);
   free(s);
   s = NULL;

   s = pgmoneta_bytes_to_string(0);
   MCTF_ASSERT_PTR_NONNULL(s, "bytes_to_string 0 failed", cleanup);
   MCTF_ASSERT_STR_EQ(s, "0", "bytes_to_string 0 result mismatch", cleanup);
   free(s);
   s = NULL;

   /* pgmoneta_lsn_to_string / pgmoneta_string_to_lsn */
   uint64_t lsn = 0x123456789ABCDEF0;
   s = pgmoneta_lsn_to_string(lsn);
   MCTF_ASSERT_PTR_NONNULL(s, "lsn_to_string failed", cleanup);
   MCTF_ASSERT_INT_EQ(pgmoneta_string_to_lsn(s), lsn, "string_to_lsn round-trip failed", cleanup);
   free(s);
   s = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_string_to_lsn(NULL), 0, "string_to_lsn NULL should return 0", cleanup);

   /* pgmoneta_split */
   MCTF_ASSERT_INT_EQ(pgmoneta_split("a,b,c", &results, &count, ','), 0, "split failed", cleanup);
   MCTF_ASSERT_INT_EQ(count, 3, "split count mismatch", cleanup);
   MCTF_ASSERT_PTR_NONNULL(results, "split results is null", cleanup);
   MCTF_ASSERT_STR_EQ(results[0], "a", "split result 0 mismatch", cleanup);
   MCTF_ASSERT_STR_EQ(results[1], "b", "split result 1 mismatch", cleanup);
   MCTF_ASSERT_STR_EQ(results[2], "c", "split result 2 mismatch", cleanup);
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

   /* pgmoneta_is_substring */
   MCTF_ASSERT_INT_EQ(pgmoneta_is_substring("world", "hello world"), 1, "is_substring positive failed", cleanup);
   MCTF_ASSERT_INT_EQ(pgmoneta_is_substring("foo", "bar"), 0, "is_substring negative failed", cleanup);
   MCTF_ASSERT_INT_EQ(pgmoneta_is_substring(NULL, "bar"), 0, "is_substring NULL case 1 failed", cleanup);
   MCTF_ASSERT_INT_EQ(pgmoneta_is_substring("foo", NULL), 0, "is_substring NULL case 2 failed", cleanup);

   /* pgmoneta_format_and_append */
   s = strdup("Hello");
   MCTF_ASSERT_PTR_NONNULL(s, "strdup failed 5", cleanup);
   s = pgmoneta_format_and_append(s, " %s %d", "World", 2025);
   MCTF_ASSERT_PTR_NONNULL(s, "format_and_append failed", cleanup);
   MCTF_ASSERT_STR_EQ(s, "Hello World 2025", "format_and_append result mismatch", cleanup);
   free(s);
   s = NULL;

   mctf_errno = 0;

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

   MCTF_ASSERT_INT_EQ(pgmoneta_merge_string_arrays(lists, &out_list), 0, "merge_string_arrays failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(out_list, "out_list is null", cleanup);
   MCTF_ASSERT_STR_EQ(out_list[0], "a", "merged array[0] mismatch", cleanup);
   MCTF_ASSERT_STR_EQ(out_list[1], "b", "merged array[1] mismatch", cleanup);
   MCTF_ASSERT_STR_EQ(out_list[2], "c", "merged array[2] mismatch", cleanup);
   MCTF_ASSERT_STR_EQ(out_list[3], "d", "merged array[3] mismatch", cleanup);
   MCTF_ASSERT_PTR_NULL(out_list[4], "merged array[4] should be NULL", cleanup);

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

   MCTF_ASSERT_INT_EQ(pgmoneta_merge_string_arrays(NULL, &out_list), -1, "merge_string_arrays NULL should fail", cleanup);

   mctf_errno = 0;

cleanup:
   if (out_list != NULL)
   {
      for (int i = 0; out_list[i] != NULL; i++)
      {
         free(out_list[i]);
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

   MCTF_ASSERT_INT_EQ(pgmoneta_get_timestamp_ISO8601_format(short_date, long_date), 0, "get_timestamp_ISO8601_format failed", cleanup);
   MCTF_ASSERT_INT_EQ(strlen(short_date), 8, "short_date length mismatch", cleanup);
   MCTF_ASSERT_INT_EQ(strlen(long_date), 16, "long_date length mismatch", cleanup);

   MCTF_ASSERT_INT_EQ(pgmoneta_get_timestamp_UTC_format(utc_date), 0, "get_timestamp_UTC_format failed", cleanup);
   MCTF_ASSERT_INT_EQ(strlen(utc_date), 29, "utc_date length mismatch", cleanup);

   MCTF_ASSERT(pgmoneta_get_current_timestamp() > 0, "get_current_timestamp should be > 0", cleanup);
   MCTF_ASSERT(pgmoneta_get_y2000_timestamp() > 0, "get_y2000_timestamp should be > 0", cleanup);

   mctf_errno = 0;

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_token_bucket)
{
   struct token_bucket* tb = NULL;

   tb = (struct token_bucket*)malloc(sizeof(struct token_bucket));
   MCTF_ASSERT_PTR_NONNULL(tb, "malloc token_bucket failed", cleanup);

   /* Test initialization */
   MCTF_ASSERT_INT_EQ(pgmoneta_token_bucket_init(tb, 100), 0, "token_bucket_init failed", cleanup);
   /* Initially we should have some tokens or be able to consume if it's not strictly 0. */
   /* The implementation usually starts with tokens or adds them. */

   /* Test consume */
   MCTF_ASSERT_INT_EQ(pgmoneta_token_bucket_consume(tb, 50), 0, "token_bucket_consume failed", cleanup);

   /* Test once */
   MCTF_ASSERT_INT_EQ(pgmoneta_token_bucket_once(tb, 10), 0, "token_bucket_once failed", cleanup);

   /* Test add (force update) */
   MCTF_ASSERT_INT_EQ(pgmoneta_token_bucket_add(tb), 0, "token_bucket_add failed", cleanup);

   mctf_errno = 0;

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
      fclose(f);
      f = NULL;
   }

   /* pgmoneta_get_directories */
   MCTF_ASSERT_INT_EQ(pgmoneta_get_directories(base, &n_dirs, &dirs), 0, "get_directories failed", cleanup);
   MCTF_ASSERT(n_dirs >= 1, "n_dirs should be >= 1", cleanup);
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
   MCTF_ASSERT(found_sub1, "sub1 directory not found", cleanup);

   /* pgmoneta_get_files */
   MCTF_ASSERT_INT_EQ(pgmoneta_get_files(base, &files), 0, "get_files failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(files, "files deque is null", cleanup);
   MCTF_ASSERT(pgmoneta_deque_size(files) >= 1, "files size should be >= 1", cleanup);
   pgmoneta_deque_iterator_create(files, &it);
   MCTF_ASSERT_PTR_NONNULL(it, "iterator is null", cleanup);
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
   MCTF_ASSERT(found_file1, "file1.txt not found", cleanup);

   /* pgmoneta_directory_size */
   MCTF_ASSERT(pgmoneta_directory_size(base) > 0, "directory_size should be > 0", cleanup);

   /* pgmoneta_compare_files */
   f = fopen(file2, "w");
   if (f)
   {
      fprintf(f, "test content");
      fclose(f);
      f = NULL;
   }
   MCTF_ASSERT(pgmoneta_compare_files(file1, file2), "compare_files failed", cleanup);

   /* pgmoneta_copy_file */
   MCTF_ASSERT_INT_EQ(pgmoneta_copy_file(file1, file3, NULL), 0, "copy_file failed", cleanup);
   MCTF_ASSERT(pgmoneta_exists(file3), "copied file should exist", cleanup);

   /* pgmoneta_move_file */
   MCTF_ASSERT_INT_EQ(pgmoneta_move_file(file3, file4), 0, "move_file failed", cleanup);
   MCTF_ASSERT(pgmoneta_exists(file4), "moved file should exist", cleanup);
   MCTF_ASSERT(!pgmoneta_exists(file3), "original file should not exist after move", cleanup);

   mctf_errno = 0;

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
      }
      free(dirs);
      dirs = NULL;
   }
   /* Clean up */
   pgmoneta_delete_directory(base);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_misc)
{
   char* os = NULL;
   int major, minor, patch;
   char buf[1024];
   FILE* f = NULL;

   /* pgmoneta_os_kernel_version */
   MCTF_ASSERT_INT_EQ(pgmoneta_os_kernel_version(&os, &major, &minor, &patch), 0, "os_kernel_version failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(os, "os is null", cleanup);
   /* On linux it should be "Linux" or similar */
   free(os);
   os = NULL;

   /* pgmoneta_normalize_path */
   f = fopen("/tmp/test.txt", "w");
   if (f)
   {
      fclose(f);
      f = NULL;
   }
   MCTF_ASSERT_INT_EQ(pgmoneta_normalize_path("/tmp", "test.txt", "/tmp/default.txt", buf, sizeof(buf)), 0, "normalize_path failed 1", cleanup);
   MCTF_ASSERT(pgmoneta_contains(buf, "/tmp/test.txt"), "normalize_path result 1 mismatch", cleanup);
   remove("/tmp/test.txt");

   /* Test with default path */
   f = fopen("/tmp/default.txt", "w");
   if (f)
   {
      fclose(f);
      f = NULL;
   }
   MCTF_ASSERT_INT_EQ(pgmoneta_normalize_path(NULL, "test.txt", "/tmp/default.txt", buf, sizeof(buf)), 0, "normalize_path failed 2", cleanup);
   MCTF_ASSERT_STR_EQ(buf, "/tmp/default.txt", "normalize_path result 2 mismatch", cleanup);
   remove("/tmp/default.txt");

   /* pgmoneta_backtrace_string */
   char* bt = NULL;
   MCTF_ASSERT_INT_EQ(pgmoneta_backtrace_string(&bt), 0, "backtrace_string failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(bt, "backtrace is null", cleanup);
   free(bt);
   bt = NULL;

   mctf_errno = 0;

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
      fclose(f);
      f = NULL;
   }

   MCTF_ASSERT_INT_EQ(pgmoneta_symlink_file(slink, target), 0, "symlink_file failed", cleanup);
   MCTF_ASSERT(pgmoneta_is_symlink(slink), "is_symlink failed", cleanup);
   MCTF_ASSERT(pgmoneta_is_symlink_valid(slink), "is_symlink_valid failed", cleanup);

   link_target = pgmoneta_get_symlink(slink);
   MCTF_ASSERT_PTR_NONNULL(link_target, "get_symlink returned null", cleanup);
   MCTF_ASSERT_STR_EQ(link_target, target, "symlink target mismatch", cleanup);
   free(link_target);
   link_target = NULL;

   mctf_errno = 0;

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

   /* server 0 is "primary" in minimal config */
   s = pgmoneta_get_server(0);
   MCTF_ASSERT_PTR_NONNULL(s, "get_server(0) failed", cleanup);
   MCTF_ASSERT(pgmoneta_contains(s, "primary"), "server should contain 'primary'", cleanup);
   free(s);
   s = NULL;

   s = pgmoneta_get_server_backup(0);
   MCTF_ASSERT_PTR_NONNULL(s, "get_server_backup(0) failed", cleanup);
   MCTF_ASSERT(pgmoneta_contains(s, "primary/backup"), "server backup should contain 'primary/backup'", cleanup);
   free(s);
   s = NULL;

   s = pgmoneta_get_server_wal(0);
   MCTF_ASSERT_PTR_NONNULL(s, "get_server_wal(0) failed", cleanup);
   MCTF_ASSERT(pgmoneta_contains(s, "primary/wal"), "server wal should contain 'primary/wal'", cleanup);
   free(s);
   s = NULL;

   /* Invalid server */
   MCTF_ASSERT_PTR_NULL(pgmoneta_get_server(-1), "get_server(-1) should return NULL", cleanup);
   MCTF_ASSERT_PTR_NULL(pgmoneta_get_server(100), "get_server(100) should return NULL", cleanup);

   mctf_errno = 0;

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
   /* We cannot check EVBACKEND_* constants easily unless included */
   /* Let's assume constants are available. */

   /* Test string conversion */
   MCTF_ASSERT_STR_EQ(pgmoneta_libev_engine(EVBACKEND_SELECT), "select", "libev_engine SELECT failed", cleanup);
   MCTF_ASSERT_STR_EQ(pgmoneta_libev_engine(EVBACKEND_POLL), "poll", "libev_engine POLL failed", cleanup);
   MCTF_ASSERT_STR_EQ(pgmoneta_libev_engine(0xFFFFFFFF), "Unknown", "libev_engine Unknown failed", cleanup);

   mctf_errno = 0;

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_extract_error)
{
   struct message* msg = NULL;
   char* p = NULL;
   char* extracted = NULL;

   msg = (struct message*)malloc(sizeof(struct message));
   MCTF_ASSERT_PTR_NONNULL(msg, "malloc message failed", cleanup);
   msg->kind = 'E';
   /* Data: [Type:1][Length:4] 'S' "ERROR" \0 'C' "12345" \0 \0 */
   msg->data = calloc(1, 100);
   MCTF_ASSERT_PTR_NONNULL(msg->data, "calloc msg->data failed", cleanup);
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

   MCTF_ASSERT_INT_EQ(pgmoneta_extract_error_fields('S', msg, &extracted), 0, "extract_error_fields 'S' failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(extracted, "extracted 'S' is null", cleanup);
   MCTF_ASSERT_STR_EQ(extracted, "ERROR", "extracted 'S' mismatch", cleanup);
   free(extracted);
   extracted = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_extract_error_fields('C', msg, &extracted), 0, "extract_error_fields 'C' failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(extracted, "extracted 'C' is null", cleanup);
   MCTF_ASSERT_STR_EQ(extracted, "12345", "extracted 'C' mismatch", cleanup);
   free(extracted);
   extracted = NULL;

   MCTF_ASSERT(pgmoneta_extract_error_fields('X', msg, &extracted) != 0, "extract_error_fields 'X' should fail", cleanup);
   MCTF_ASSERT_PTR_NULL(extracted, "extracted 'X' should be null", cleanup);

   mctf_errno = 0;

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
   MCTF_ASSERT_PTR_NONNULL(s, "wal_file_name failed", cleanup);
   MCTF_ASSERT_STR_EQ(s, "000000010000000000000001", "wal_file_name result mismatch", cleanup);
   free(s);
   s = NULL;

   mctf_errno = 0;

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
   /* "MZXW6YTBOI======" is base32 for "foobar". */
   /* for (i = 0; i < base32_length; i++) sprintf(..., "%02x", base32[i]); */
   /* It creates a HEX representation of the INPUT BYTES directly, treating them as bytes. */
   /* The implementation iterates i from 0 to length, and prints base32[i] as hex. */
   /* So if input is "A", hex is "41". */

   unsigned char input[] = "A";
   MCTF_ASSERT_INT_EQ(pgmoneta_convert_base32_to_hex(input, 1, &hex), 0, "convert_base32_to_hex 1 failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(hex, "hex is null 1", cleanup);
   MCTF_ASSERT_STR_EQ((char*)hex, "41", "hex result 1 mismatch ('A' in hex is 41)", cleanup);
   free(hex);
   hex = NULL;

   unsigned char input2[] = "\x01\x02";
   MCTF_ASSERT_INT_EQ(pgmoneta_convert_base32_to_hex(input2, 2, &hex), 0, "convert_base32_to_hex 2 failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(hex, "hex is null 2", cleanup);
   MCTF_ASSERT_STR_EQ((char*)hex, "0102", "hex result 2 mismatch", cleanup);
   free(hex);
   hex = NULL;

   mctf_errno = 0;

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
   /* Is encrypted */
   MCTF_ASSERT(pgmoneta_is_encrypted("file.aes"), "is_encrypted positive failed", cleanup);
   MCTF_ASSERT(!pgmoneta_is_encrypted("file.txt"), "is_encrypted negative 1 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_is_encrypted(NULL), "is_encrypted NULL failed", cleanup);

   /* Is compressed */
   MCTF_ASSERT(pgmoneta_is_compressed("file.zstd"), "is_compressed zstd failed", cleanup);
   MCTF_ASSERT(pgmoneta_is_compressed("file.lz4"), "is_compressed lz4 failed", cleanup);
   MCTF_ASSERT(pgmoneta_is_compressed("file.bz2"), "is_compressed bz2 failed", cleanup);
   MCTF_ASSERT(pgmoneta_is_compressed("file.gz"), "is_compressed gz failed", cleanup);
   MCTF_ASSERT(!pgmoneta_is_compressed("file.txt"), "is_compressed negative failed", cleanup);
   MCTF_ASSERT(!pgmoneta_is_compressed(NULL), "is_compressed NULL failed", cleanup);

   mctf_errno = 0;

cleanup:
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
   MCTF_ASSERT_PTR_NONNULL(msg, "malloc message failed 1", cleanup);
   msg->kind = 0;
   msg->data = calloc(1, 1024);
   MCTF_ASSERT_PTR_NONNULL(msg->data, "calloc msg->data failed 1", cleanup);
   p = msg->data;

   /* The utility expects [Length(4)][Protocol(4)][Key][Val]... */
   /* We write a dummy length first. */
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
   MCTF_ASSERT_INT_EQ(res, 0, "extract_username_database failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(username, "username is null", cleanup);
   MCTF_ASSERT_PTR_NONNULL(database, "database is null", cleanup);
   MCTF_ASSERT_PTR_NONNULL(appname, "appname is null", cleanup);
   MCTF_ASSERT_STR_EQ(username, "myuser", "username mismatch", cleanup);
   MCTF_ASSERT_STR_EQ(database, "mydb", "database mismatch", cleanup);
   MCTF_ASSERT_STR_EQ(appname, "myapp", "appname mismatch", cleanup);

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

   /* Test pgmoneta_extract_message (e.g. ErrorResponse 'E') */
   /* extract_message expects [Type][Length]... in msg->data */
   msg = (struct message*)malloc(sizeof(struct message));
   MCTF_ASSERT_PTR_NONNULL(msg, "malloc message failed 2", cleanup);
   msg->kind = 'E'; /* This doesn't matter for the function logic much if we pass type but good to set */
   msg->data = calloc(1, 1024);
   MCTF_ASSERT_PTR_NONNULL(msg->data, "calloc msg->data failed 2", cleanup);
   p = msg->data;

   pgmoneta_write_byte(p, 'E');
   p += 1;
   pgmoneta_write_int32(p, 4);
   p += 4;
   msg->length = (char*)p - (char*)msg->data;

   res = pgmoneta_extract_message('E', msg, &extracted);
   MCTF_ASSERT_INT_EQ(res, 0, "extract_message failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(extracted, "extracted message is null", cleanup);
   MCTF_ASSERT_INT_EQ(extracted->kind, 'E', "extracted message kind mismatch", cleanup);

   pgmoneta_free_message(extracted);
   extracted = NULL;

   mctf_errno = 0;

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
      fclose(f);
      f = NULL;
   }

   MCTF_ASSERT_INT_EQ(pgmoneta_permission_recursive(dir), 0, "permission_recursive failed", cleanup);

   mode = pgmoneta_get_permission(file);
   MCTF_ASSERT(mode > 0, "get_permission should be > 0", cleanup);

   mctf_errno = 0;

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
   MCTF_ASSERT(total_sp > 0, "total_space should be > 0", cleanup);

   pgmoneta_mkdir(dir);

   f = fopen(file1, "w");
   if (f)
   {
      fprintf(f, "a");
      fclose(f);
      f = NULL;
   }
   f = fopen(file2, "w");
   if (f)
   {
      fprintf(f, "aaaaa");
      fclose(f);
      f = NULL;
   }

   biggest = pgmoneta_biggest_file(dir);
   MCTF_ASSERT(biggest >= 5, "biggest_file should be >= 5", cleanup);

   mctf_errno = 0;

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
   char src[128];
   char dst[128];
   char sub[128];
   char subfile[128];
   char file_src[128];
   char file_dst[128];
   FILE* f = NULL;
   char* to_ptr = NULL;

   strcpy(src, "test_adv_src");
   strcpy(dst, "test_adv_dst");
   sprintf(sub, "%s/sub", src);
   sprintf(subfile, "%s/file.txt", sub);

   /* Setup source */
   pgmoneta_delete_directory(src);
   pgmoneta_delete_directory(dst);

   pgmoneta_mkdir(src);
   pgmoneta_mkdir(sub);
   f = fopen(subfile, "w");
   if (f)
   {
      fprintf(f, "data");
      fclose(f);
      f = NULL;
   }

   /* Test is_wal_file */
   MCTF_ASSERT(pgmoneta_is_wal_file("000000010000000000000001"), "is_wal_file positive failed", cleanup);
   MCTF_ASSERT(!pgmoneta_is_wal_file("history"), "is_wal_file negative 1 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_is_wal_file("000000010000000000000001.partial"), "is_wal_file negative 2 failed", cleanup);

   /* Test copy_and_extract basic */
   sprintf(file_src, "%s/plain.txt", src);
   sprintf(file_dst, "%s/plain.txt", dst);

   pgmoneta_mkdir(dst);
   f = fopen(file_src, "w");
   if (f)
   {
      fprintf(f, "plain");
      fclose(f);
      f = NULL;
   }

   to_ptr = strdup(file_dst);
   MCTF_ASSERT_PTR_NONNULL(to_ptr, "strdup failed", cleanup);
   MCTF_ASSERT_INT_EQ(pgmoneta_copy_and_extract_file(file_src, &to_ptr), 0, "copy_and_extract_file failed", cleanup);
   MCTF_ASSERT(pgmoneta_exists(file_dst), "copied file should exist", cleanup);
   free(to_ptr);
   to_ptr = NULL;

   /* Test list_directory (just ensure it runs) */
   pgmoneta_list_directory(src);

   mctf_errno = 0;

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
   pgmoneta_delete_directory(src);
   pgmoneta_delete_directory(dst);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_missing_basic)
{
   /* Time functions */
   struct timespec start = {100, 0};
   struct timespec end = {105, 500000000}; /* 5.5 seconds later */
   double seconds = 0;
   double duration = 0;
   char* ts_str = NULL;
   char* user = NULL;
   char* home = NULL;
   char* fpath = "test_del_file.txt";
   FILE* f = NULL;
   char* dir = "test_link_at_dir";

   duration = pgmoneta_compute_duration(start, end);
   MCTF_ASSERT(duration > 5.4 && duration < 5.6, "compute_duration failed", cleanup);

   ts_str = pgmoneta_get_timestamp_string(start, end, &seconds);
   MCTF_ASSERT_PTR_NONNULL(ts_str, "get_timestamp_string failed", cleanup);
   MCTF_ASSERT(seconds > 5.4 && seconds < 5.6, "get_timestamp_string seconds mismatch", cleanup);
   free(ts_str);
   ts_str = NULL;

   /* System / User */
   user = pgmoneta_get_user_name();
   MCTF_ASSERT_PTR_NONNULL(user, "get_user_name failed", cleanup);
   free(user);
   user = NULL;

   home = pgmoneta_get_home_directory();
   MCTF_ASSERT_PTR_NONNULL(home, "get_home_directory failed", cleanup);
   free(home);
   home = NULL;

   /* File Extended */
   f = fopen(fpath, "w");
   if (f)
   {
      fprintf(f, "12345");
      fclose(f);
      f = NULL;
   }

   MCTF_ASSERT_INT_EQ(pgmoneta_get_file_size(fpath), 5, "get_file_size failed", cleanup);

   /* pgmoneta_delete_file(char* file, struct workers* workers) */
   MCTF_ASSERT_INT_EQ(pgmoneta_delete_file(fpath, NULL), 0, "delete_file failed", cleanup);
   MCTF_ASSERT(!pgmoneta_exists(fpath), "file should be deleted", cleanup);

   /* Create temp dir for symlink test */
   pgmoneta_mkdir(dir);
   pgmoneta_delete_directory(dir);

   mctf_errno = 0;

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
   MCTF_ASSERT_PTR_NONNULL(config, "configuration is null", cleanup);

   s = pgmoneta_get_server_summary(server);
   MCTF_ASSERT_PTR_NONNULL(s, "get_server_summary failed", cleanup);
   free(s);
   s = NULL;

   /* Inject wal_shipping config for testing */
   strcpy(config->common.servers[server].wal_shipping, "/tmp/wal_ship");

   s = pgmoneta_get_server_wal_shipping(server);
   MCTF_ASSERT_PTR_NONNULL(s, "get_server_wal_shipping failed", cleanup);
   free(s);
   s = NULL;

   s = pgmoneta_get_server_wal_shipping_wal(server);
   MCTF_ASSERT_PTR_NONNULL(s, "get_server_wal_shipping_wal failed", cleanup);
   free(s);
   s = NULL;

   s = pgmoneta_get_server_workspace(server);
   MCTF_ASSERT_PTR_NONNULL(s, "get_server_workspace failed", cleanup);
   /* Setup workspace for delete test */
   pgmoneta_mkdir(s);

   /* Check deletion */
   MCTF_ASSERT_INT_EQ(pgmoneta_delete_server_workspace(server, NULL), 0, "delete_server_workspace failed", cleanup);
   MCTF_ASSERT(!pgmoneta_exists(s), "workspace should be deleted", cleanup);
   free(s);
   s = NULL;

   /* Identifiers */
   s = pgmoneta_get_server_backup_identifier(server, id);
   MCTF_ASSERT_PTR_NONNULL(s, "get_server_backup_identifier failed", cleanup);
   free(s);
   s = NULL;

   s = pgmoneta_get_server_extra_identifier(server, id);
   MCTF_ASSERT_PTR_NONNULL(s, "get_server_extra_identifier failed", cleanup);
   free(s);
   s = NULL;

   s = pgmoneta_get_server_backup_identifier_data(server, id);
   MCTF_ASSERT_PTR_NONNULL(s, "get_server_backup_identifier_data failed", cleanup);
   free(s);
   s = NULL;

   s = pgmoneta_get_server_backup_identifier_data_wal(server, id);
   MCTF_ASSERT_PTR_NONNULL(s, "get_server_backup_identifier_data_wal failed", cleanup);
   free(s);
   s = NULL;

   s = pgmoneta_get_server_backup_identifier_tablespace(server, id, "tbs");
   MCTF_ASSERT_PTR_NONNULL(s, "get_server_backup_identifier_tablespace failed", cleanup);
   free(s);
   s = NULL;

   mctf_errno = 0;

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
   char dir[128];
   char file1[128];
   char file2[128];
   char to_dir[128];
   char check_file[128];
   struct deque* files = NULL;
   FILE* f = NULL;

   strcpy(dir, "test_wal_dir");
   pgmoneta_mkdir(dir);

   /* Create dummy WAL files (24 chars hex) */
   sprintf(file1, "%s/000000010000000000000001", dir);
   sprintf(file2, "%s/000000010000000000000002", dir);

   f = fopen(file1, "w");
   if (f)
   {
      fclose(f);
      f = NULL;
   }
   f = fopen(file2, "w");
   if (f)
   {
      fclose(f);
      f = NULL;
   }

   MCTF_ASSERT_INT_EQ(pgmoneta_get_wal_files(dir, &files), 0, "get_wal_files failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(files, "files deque is null", cleanup);
   MCTF_ASSERT_INT_EQ(pgmoneta_deque_size(files), 2, "files size should be 2", cleanup);
   pgmoneta_deque_destroy(files);
   files = NULL;

   /* number_of_wal_files */
   MCTF_ASSERT_INT_EQ(pgmoneta_number_of_wal_files(dir, "000000000000000000000000", NULL), 2, "number_of_wal_files failed", cleanup);

   /* copy_wal_files */
   strcpy(to_dir, "test_wal_dir_copy");
   pgmoneta_mkdir(to_dir);

   MCTF_ASSERT_INT_EQ(pgmoneta_copy_wal_files(dir, to_dir, "000000000000000000000000", NULL), 0, "copy_wal_files failed", cleanup);
   sprintf(check_file, "%s/000000010000000000000001", to_dir);
   MCTF_ASSERT(pgmoneta_exists(check_file), "copied WAL file should exist", cleanup);

   mctf_errno = 0;

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
   if (pgmoneta_exists(to_dir))
   {
      pgmoneta_delete_directory(to_dir);
   }
   if (pgmoneta_exists(dir))
   {
      pgmoneta_delete_directory(dir);
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_missing_misc)
{
   /* pgmoneta_extract_message_from_data */
   /* Construct a raw message buffer: Type (1 byte) + Length (4 bytes) + Data */
   /* Actually extract_message_from_data receives `data` */
   /* Check declaration: pgmoneta_extract_message_from_data(char type, void* data, size_t data_size, struct message** extracted) */
   /* Usually `data` is the payload. */

   char buffer[1024];
   int len = 8; /* 4 bytes for length field itself + 4 bytes content */
   int nlen = 0;
   struct message* extracted = NULL;
   char arg0[32];
   char* argv[] = {arg0, NULL};
   struct main_configuration* config = NULL;

   memset(buffer, 0, 1024);
   /* Format: Type(1) + Length(4) + Content */
   buffer[0] = 'Q';
   nlen = htonl(len);
   memcpy(buffer + 1, &nlen, 4);
   memcpy(buffer + 1 + 4, "TEST", 4);

   MCTF_ASSERT_INT_EQ(pgmoneta_extract_message_from_data('Q', buffer, 9, &extracted), 0, "extract_message_from_data failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(extracted, "extracted message is null", cleanup);
   MCTF_ASSERT_INT_EQ(extracted->kind, 'Q', "extracted message kind mismatch", cleanup);
   MCTF_ASSERT_INT_EQ(extracted->length, 9, "extracted message length mismatch", cleanup);

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

   mctf_errno = 0;

cleanup:
   if (extracted != NULL)
   {
      pgmoneta_free_message(extracted);
      extracted = NULL;
   }
   MCTF_FINISH();
}
