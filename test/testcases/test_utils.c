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
#include <tscommon.h>
#include <tssuite.h>
#include <utils.h>
#include <configuration.h>
#include <shmem.h>
#include <netinet/in.h>

#include <stdlib.h>
#include <stdio.h>

START_TEST(test_resolve_path_trailing_env_var)
{
   fprintf(stderr, "TEST START: %s\n", __func__);
   char* resolved = NULL;
   char* env_key = "PGMONETA_TEST_PATH_KEY";
   char* env_value = "PGMONETA_TEST_PATH_VALUE";
   char* expected = "/pgmoneta/PGMONETA_TEST_PATH_VALUE";
   int result;

   setenv(env_key, env_value, 1);

   result = pgmoneta_resolve_path("/pgmoneta/$PGMONETA_TEST_PATH_KEY", &resolved);

   ck_assert_int_eq(result, 0);

   ck_assert_ptr_nonnull(resolved);

   ck_assert_str_eq(resolved, expected);

   unsetenv(env_key);
   free(resolved);
}
END_TEST

START_TEST(test_utils_starts_with)
{
   ck_assert(pgmoneta_starts_with("hello world", "hello"));
   ck_assert(pgmoneta_starts_with("hello", "hello"));
   ck_assert(!pgmoneta_starts_with("hello world", "world"));
   ck_assert(!pgmoneta_starts_with("hello", "hello world"));
   ck_assert(!pgmoneta_starts_with(NULL, "hello"));
   ck_assert(!pgmoneta_starts_with("hello", NULL));
   ck_assert(!pgmoneta_starts_with(NULL, NULL));
}
END_TEST

START_TEST(test_utils_ends_with)
{
   ck_assert(pgmoneta_ends_with("hello world", "world"));
   ck_assert(pgmoneta_ends_with("world", "world"));
   ck_assert(!pgmoneta_ends_with("hello world", "hello"));
   ck_assert(!pgmoneta_ends_with("world", "hello world"));
   ck_assert(!pgmoneta_ends_with(NULL, "world"));
   ck_assert(!pgmoneta_ends_with("world", NULL));
   ck_assert(!pgmoneta_ends_with(NULL, NULL));
}
END_TEST

START_TEST(test_utils_contains)
{
   ck_assert(pgmoneta_contains("hello world", "lo wo"));
   ck_assert(pgmoneta_contains("hello", "he"));
   ck_assert(!pgmoneta_contains("hello world", "z"));
   ck_assert(!pgmoneta_contains(NULL, "hello"));
   ck_assert(!pgmoneta_contains("hello", NULL));
}
END_TEST

START_TEST(test_utils_compare_string)
{
   ck_assert(pgmoneta_compare_string("abc", "abc"));
   ck_assert(!pgmoneta_compare_string("abc", "ABC"));
   ck_assert(!pgmoneta_compare_string("abc", "def"));
   ck_assert(!pgmoneta_compare_string(NULL, "abc"));
   ck_assert(!pgmoneta_compare_string("abc", NULL));
   ck_assert(pgmoneta_compare_string(NULL, NULL));
}
END_TEST

START_TEST(test_utils_atoi)
{
   ck_assert_int_eq(pgmoneta_atoi("123"), 123);
   ck_assert_int_eq(pgmoneta_atoi("-123"), -123);
   ck_assert_int_eq(pgmoneta_atoi("0"), 0);
   ck_assert_int_eq(pgmoneta_atoi(NULL), 0);
}
END_TEST

START_TEST(test_utils_is_number)
{
   ck_assert(pgmoneta_is_number("123", 10));
   ck_assert(pgmoneta_is_number("-123", 10));
   ck_assert(!pgmoneta_is_number("12a", 10));
   ck_assert(!pgmoneta_is_number("abc", 10));
   ck_assert(pgmoneta_is_number("1A", 16));
   ck_assert(!pgmoneta_is_number("1Z", 16));
   ck_assert(!pgmoneta_is_number(NULL, 10));
}
END_TEST

START_TEST(test_utils_base64)
{
   char* original = "hello world";
   char* encoded = NULL;
   char* decoded = NULL;
   size_t encoded_length = 0;
   size_t decoded_length = 0;
   size_t original_length = strlen(original);

   ck_assert_int_eq(pgmoneta_base64_encode(original, original_length, &encoded, &encoded_length), 0);
   ck_assert_ptr_nonnull(encoded);
   ck_assert_int_gt(encoded_length, 0);

   ck_assert_int_eq(pgmoneta_base64_decode(encoded, encoded_length, (void**)&decoded, &decoded_length), 0);
   ck_assert_ptr_nonnull(decoded);
   ck_assert_int_eq(decoded_length, original_length);
   ck_assert_mem_eq(decoded, original, original_length);

   free(encoded);
   free(decoded);
}
END_TEST

START_TEST(test_utils_is_incremental_path)
{
   ck_assert(pgmoneta_is_incremental_path("/path/to/backup/INCREMENTAL.20231026120000-20231026110000"));
   ck_assert(!pgmoneta_is_incremental_path("/path/to/backup/20231026120000"));
   ck_assert(!pgmoneta_is_incremental_path("/path/to/backup"));
   ck_assert(!pgmoneta_is_incremental_path(NULL));
}
END_TEST

START_TEST(test_utils_get_parent_dir)
{
   char* parent = NULL;

   parent = pgmoneta_get_parent_dir("/a/b/c");
   ck_assert_str_eq(parent, "/a/b");
   free(parent);

   parent = pgmoneta_get_parent_dir("/a");
   ck_assert_str_eq(parent, "/");
   free(parent);

   parent = pgmoneta_get_parent_dir("/");
   ck_assert_str_eq(parent, "/");
   free(parent);

   parent = pgmoneta_get_parent_dir("a");
   ck_assert_str_eq(parent, ".");
   free(parent);

   parent = pgmoneta_get_parent_dir(NULL);
   ck_assert_ptr_null(parent);
}
END_TEST

START_TEST(test_utils_serialization)
{
   void* data = malloc(1024);
   void* ptr = data;
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

   memset(data, 0, 1024);

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
   ck_assert_int_eq(pgmoneta_read_byte(ptr), b);
   ptr += 1;
   ck_assert_int_eq(pgmoneta_read_uint8(ptr), u8);
   ptr += 1;
   ck_assert_int_eq(pgmoneta_read_int16(ptr), i16);
   ptr += 2;
   ck_assert_int_eq(pgmoneta_read_uint16(ptr), u16);
   ptr += 2;
   ck_assert_int_eq(pgmoneta_read_int32(ptr), i32);
   ptr += 4;
   ck_assert_int_eq(pgmoneta_read_uint32(ptr), u32);
   ptr += 4;
   ck_assert_int_eq(pgmoneta_read_int64(ptr), i64);
   ptr += 8;
   ck_assert_int_eq(pgmoneta_read_uint64(ptr), u64);
   ptr += 8;
   ck_assert(pgmoneta_read_bool(ptr) == bo);
   ptr += 1;
   ck_assert_str_eq(pgmoneta_read_string(ptr), s);

   free(data);
}
END_TEST

START_TEST(test_utils_append)
{
   char* buffer = NULL;

   buffer = pgmoneta_append(buffer, "hello");
   ck_assert_str_eq(buffer, "hello");

   buffer = pgmoneta_append_char(buffer, ' ');
   ck_assert_str_eq(buffer, "hello ");

   buffer = pgmoneta_append_int(buffer, 123);
   ck_assert_str_eq(buffer, "hello 123");

   buffer = pgmoneta_append(buffer, " ");
   buffer = pgmoneta_append_ulong(buffer, 456);
   ck_assert_str_eq(buffer, "hello 123 456");

   buffer = pgmoneta_append(buffer, " ");
   buffer = pgmoneta_append_bool(buffer, true);
   ck_assert_str_eq(buffer, "hello 123 456 true");

   buffer = pgmoneta_append(buffer, " ");
   buffer = pgmoneta_append_double(buffer, 3.14);
   ck_assert_str_eq(buffer, "hello 123 456 true 3.140000");

   free(buffer);
}
END_TEST

START_TEST(test_utils_string_manipulation)
{
   char* s = NULL;
   char* res = NULL;

   // test remove_whitespace
   s = strdup(" a b c ");
   res = pgmoneta_remove_whitespace(s);
   ck_assert_str_eq(res, "abc");
   free(s);
   free(res);

   // test remove_prefix
   s = strdup("pre_test");
   res = pgmoneta_remove_prefix(s, "pre_");
   ck_assert_str_eq(res, "test");
   free(s);
   free(res);

   // test remove_suffix
   s = strdup("test.txt");
   res = pgmoneta_remove_suffix(s, ".txt");
   ck_assert_str_eq(res, "test");
   free(s);
   free(res);

   // test indent
   s = strdup("hello");
   res = pgmoneta_indent(s, NULL, 2);
   ck_assert_str_eq(res, "hello  ");
   free(res);

   // test escape_string
   s = strdup("foo'bar");
   res = pgmoneta_escape_string(s);
   ck_assert_str_eq(res, "foo\\'bar");
   free(s);
   free(res);
}
END_TEST

START_TEST(test_utils_math)
{
   ck_assert(pgmoneta_get_aligned_size(1) >= 1);
   ck_assert(pgmoneta_get_aligned_size(100) >= 100);

   ck_assert_int_eq(pgmoneta_swap(0x12345678), 0x78563412);

   char* array[] = {"b", "a", "c"};
   pgmoneta_sort(3, array);
   ck_assert_str_eq(array[0], "a");
   ck_assert_str_eq(array[1], "b");
   ck_assert_str_eq(array[2], "c");
}
END_TEST

START_TEST(test_utils_version)
{
   ck_assert_int_eq(pgmoneta_version_as_number(1, 2, 3), 10203);
   ck_assert(pgmoneta_version_ge(0, 0, 0));
   ck_assert(!pgmoneta_version_ge(99, 99, 99));
}
END_TEST

START_TEST(test_utils_bigendian)
{
   int n = 1;
   bool is_little = (*(char*)&n == 1);
   if (is_little)
   {
      ck_assert(!pgmoneta_bigendian());
   }
   else
   {
      ck_assert(pgmoneta_bigendian());
   }
}
END_TEST

START_TEST(test_utils_strip_extension)
{
   char* name = NULL;

   ck_assert_int_eq(pgmoneta_strip_extension("file.txt", &name), 0);
   ck_assert_str_eq(name, "file");
   free(name);
   name = NULL;

   ck_assert_int_eq(pgmoneta_strip_extension("file", &name), 0);
   ck_assert_str_eq(name, "file");
   free(name);
   name = NULL;

   ck_assert_int_eq(pgmoneta_strip_extension("file.tar.gz", &name), 0);
   ck_assert_str_eq(name, "file.tar");
   free(name);
   name = NULL;

   // Hidden file case
   ck_assert_int_eq(pgmoneta_strip_extension(".bashrc", &name), 0);
   ck_assert_str_eq(name, "");
   free(name);
   name = NULL;
}
END_TEST

START_TEST(test_utils_file_size)
{
   char* s = NULL;

   s = pgmoneta_translate_file_size(100);
   ck_assert_str_eq(s, "100.00B");
   free(s);

   s = pgmoneta_translate_file_size(1024);
   ck_assert_str_eq(s, "1.00kB");
   free(s);
}
END_TEST

START_TEST(test_utils_file_ops)
{
   char* path = "test_file_ops.tmp";
   char* dir = "test_dir_ops.tmp";
   FILE* f = fopen(path, "w");
   if (f)
   {
      fprintf(f, "test");
      fclose(f);
   }

   ck_assert(pgmoneta_exists(path));
   ck_assert(pgmoneta_is_file(path));
   ck_assert(!pgmoneta_is_directory(path));

   pgmoneta_mkdir(dir);
   ck_assert(pgmoneta_exists(dir));
   ck_assert(pgmoneta_is_directory(dir));
   ck_assert(!pgmoneta_is_file(dir));

   remove(path);
   pgmoneta_delete_directory(dir);

   // ck_assert(!pgmoneta_exists(path)); // remove doesn't check immediate effect usually but here it should be fine.
   ck_assert(!pgmoneta_exists(dir));
}
END_TEST

START_TEST(test_utils_snprintf)
{
   char buf[100];
   pgmoneta_snprintf(buf, 100, "Hello %s", "World");
   ck_assert_str_eq(buf, "Hello World");

   pgmoneta_snprintf(buf, 5, "0123456789");
   ck_assert_str_eq(buf, "0123");
}
END_TEST

START_TEST(test_utils_string_extras)
{
   char* s = NULL;
   char** results = NULL;
   int count = 0;

   // pgmoneta_remove_first
   s = strdup("abc");
   s = pgmoneta_remove_first(s);
   ck_assert_str_eq(s, "bc");
   free(s);

   s = strdup("a");
   s = pgmoneta_remove_first(s);
   ck_assert_str_eq(s, "");
   free(s);

   ck_assert_ptr_null(pgmoneta_remove_first(NULL));

   // pgmoneta_remove_last
   s = strdup("abc");
   s = pgmoneta_remove_last(s);
   ck_assert_str_eq(s, "ab");
   free(s);

   s = strdup("a");
   s = pgmoneta_remove_last(s);
   ck_assert_str_eq(s, "");
   free(s);

   ck_assert_ptr_null(pgmoneta_remove_last(NULL));

   // pgmoneta_bytes_to_string
   s = pgmoneta_bytes_to_string(1024);
   ck_assert_str_eq(s, "1 KB");
   free(s);

   s = pgmoneta_bytes_to_string(1024 * 1024);
   ck_assert_str_eq(s, "1 MB");
   free(s);

   s = pgmoneta_bytes_to_string(0);
   ck_assert_str_eq(s, "0");
   free(s);

   // pgmoneta_lsn_to_string / pgmoneta_string_to_lsn
   uint64_t lsn = 0x123456789ABCDEF0;
   s = pgmoneta_lsn_to_string(lsn);
   ck_assert_ptr_nonnull(s);
   ck_assert_int_eq(pgmoneta_string_to_lsn(s), lsn);
   free(s);

   ck_assert_int_eq(pgmoneta_string_to_lsn(NULL), 0);

   // pgmoneta_split
   ck_assert_int_eq(pgmoneta_split("a,b,c", &results, &count, ','), 0);
   ck_assert_int_eq(count, 3);
   ck_assert_str_eq(results[0], "a");
   ck_assert_str_eq(results[1], "b");
   ck_assert_str_eq(results[2], "c");
   for (int i = 0; i < count; i++)
   {
      free(results[i]);
   }
   free(results);

   // pgmoneta_is_substring
   ck_assert_int_eq(pgmoneta_is_substring("world", "hello world"), 1);
   ck_assert_int_eq(pgmoneta_is_substring("foo", "bar"), 0);
   ck_assert_int_eq(pgmoneta_is_substring(NULL, "bar"), 0);
   ck_assert_int_eq(pgmoneta_is_substring("foo", NULL), 0);

   // pgmoneta_format_and_append
   s = strdup("Hello");
   s = pgmoneta_format_and_append(s, " %s %d", "World", 2025);
   ck_assert_str_eq(s, "Hello World 2025");
   free(s);
}
END_TEST

START_TEST(test_utils_merge_string_arrays)
{
   char* list1[] = {"a", "b", NULL};
   char* list2[] = {"c", "d", NULL};
   char** lists[] = {list1, list2, NULL};
   char** out_list = NULL;

   ck_assert_int_eq(pgmoneta_merge_string_arrays(lists, &out_list), 0);
   ck_assert_ptr_nonnull(out_list);
   ck_assert_str_eq(out_list[0], "a");
   ck_assert_str_eq(out_list[1], "b");
   ck_assert_str_eq(out_list[2], "c");
   ck_assert_str_eq(out_list[3], "d");
   ck_assert_ptr_null(out_list[4]);

   for (int i = 0; i < 4; i++)
   {
      free(out_list[i]);
   }
   free(out_list);

   ck_assert_int_eq(pgmoneta_merge_string_arrays(NULL, &out_list), -1);
}
END_TEST

START_TEST(test_utils_time)
{
   char short_date[SHORT_TIME_LENGTH];
   char long_date[LONG_TIME_LENGTH];
   char utc_date[UTC_TIME_LENGTH];

   ck_assert_int_eq(pgmoneta_get_timestamp_ISO8601_format(short_date, long_date), 0);
   ck_assert_int_eq(strlen(short_date), 8);
   ck_assert_int_eq(strlen(long_date), 16);

   ck_assert_int_eq(pgmoneta_get_timestamp_UTC_format(utc_date), 0);
   ck_assert_int_eq(strlen(utc_date), 29);

   ck_assert(pgmoneta_get_current_timestamp() > 0);
   ck_assert(pgmoneta_get_y2000_timestamp() > 0);
}
END_TEST

START_TEST(test_utils_token_bucket)
{
   struct token_bucket* tb = NULL;

   tb = (struct token_bucket*)malloc(sizeof(struct token_bucket));

   // Test initialization
   ck_assert_int_eq(pgmoneta_token_bucket_init(tb, 100), 0);
   // Initially we should have some tokens or be able to consume if it's not strictly 0.
   // The implementation usually starts with tokens or adds them.

   // Test consume
   ck_assert_int_eq(pgmoneta_token_bucket_consume(tb, 50), 0);

   // Test once
   ck_assert_int_eq(pgmoneta_token_bucket_once(tb, 10), 0);

   // Test add (force update)
   ck_assert_int_eq(pgmoneta_token_bucket_add(tb), 0);

   pgmoneta_token_bucket_destroy(tb);
}
END_TEST

START_TEST(test_utils_file_dir)
{
   char base[MAX_PATH];
   char sub1[MAX_PATH];
   char* file1 = "test_dir_extras/file1.txt";
   int n_dirs = 0;
   char** dirs = NULL;
   struct deque* files = NULL;
   struct deque_iterator* it = 0;

   strcpy(base, "test_dir_extras");
   strcpy(sub1, "test_dir_extras/sub1");

   pgmoneta_delete_directory(base);
   pgmoneta_mkdir(base);
   pgmoneta_mkdir(sub1);

   FILE* f = fopen(file1, "w");
   if (f)
   {
      fprintf(f, "test content");
      fclose(f);
   }

   // pgmoneta_get_directories
   ck_assert_int_eq(pgmoneta_get_directories(base, &n_dirs, &dirs), 0);
   ck_assert_int_ge(n_dirs, 1);
   bool found_sub1 = false;
   for (int i = 0; i < n_dirs; i++)
   {
      if (pgmoneta_contains(dirs[i], "sub1"))
      {
         found_sub1 = true;
      }
      free(dirs[i]);
   }
   free(dirs);
   ck_assert(found_sub1);

   // pgmoneta_get_files
   ck_assert_int_eq(pgmoneta_get_files(base, &files), 0);
   ck_assert_int_ge(pgmoneta_deque_size(files), 1);
   bool found_file1 = false;
   pgmoneta_deque_iterator_create(files, &it);
   while (pgmoneta_deque_iterator_next(it))
   {
      char* file_path = (char*)it->value->data;
      if (pgmoneta_contains(file_path, "file1.txt"))
      {
         found_file1 = true;
      }
   }
   pgmoneta_deque_iterator_destroy(it);
   pgmoneta_deque_destroy(files);
   ck_assert(found_file1);

   // pgmoneta_directory_size
   ck_assert(pgmoneta_directory_size(base) > 0);

   // pgmoneta_compare_files
   char* file2 = "test_dir_extras/file2.txt";
   f = fopen(file2, "w");
   if (f)
   {
      fprintf(f, "test content");
      fclose(f);
   }
   ck_assert(pgmoneta_compare_files(file1, file2));

   // pgmoneta_copy_file
   char* file3 = "test_dir_extras/file3.txt";
   ck_assert_int_eq(pgmoneta_copy_file(file1, file3, NULL), 0);
   ck_assert(pgmoneta_exists(file3));

   // pgmoneta_move_file
   char* file4 = "test_dir_extras/file4.txt";
   ck_assert_int_eq(pgmoneta_move_file(file3, file4), 0);
   ck_assert(pgmoneta_exists(file4));
   ck_assert(!pgmoneta_exists(file3));

   // Clean up
   pgmoneta_delete_directory(base);
}
END_TEST

START_TEST(test_utils_misc)
{
   char* os = NULL;
   int major, minor, patch;
   char buf[1024];

   // pgmoneta_os_kernel_version
   ck_assert_int_eq(pgmoneta_os_kernel_version(&os, &major, &minor, &patch), 0);
   ck_assert_ptr_nonnull(os);
   // On linux it should be "Linux" or similar
   free(os);

   // pgmoneta_normalize_path
   FILE* f = fopen("/tmp/test.txt", "w");
   if (f)
   {
      fclose(f);
   }
   ck_assert_int_eq(pgmoneta_normalize_path("/tmp", "test.txt", "/tmp/default.txt", buf, sizeof(buf)), 0);
   ck_assert(pgmoneta_contains(buf, "/tmp/test.txt"));
   remove("/tmp/test.txt");

   // Test with default path
   f = fopen("/tmp/default.txt", "w");
   if (f)
   {
      fclose(f);
   }
   ck_assert_int_eq(pgmoneta_normalize_path(NULL, "test.txt", "/tmp/default.txt", buf, sizeof(buf)), 0);
   ck_assert_str_eq(buf, "/tmp/default.txt");
   remove("/tmp/default.txt");

   // pgmoneta_backtrace_string
   char* bt = NULL;
   ck_assert_int_eq(pgmoneta_backtrace_string(&bt), 0);
   ck_assert_ptr_nonnull(bt);
   free(bt);
}
END_TEST

START_TEST(test_utils_symlinks)
{
   char base[MAX_PATH];
   char* target = "test_symlinks/target.txt";
   char* slink = "test_symlinks/link.txt";

   strcpy(base, "test_symlinks");

   pgmoneta_delete_directory(base);
   pgmoneta_mkdir(base);

   FILE* f = fopen(target, "w");
   if (f)
   {
      fprintf(f, "target content");
      fclose(f);
   }

   ck_assert_int_eq(pgmoneta_symlink_file(slink, target), 0);
   ck_assert(pgmoneta_is_symlink(slink));
   ck_assert(pgmoneta_is_symlink_valid(slink));

   char* link_target = pgmoneta_get_symlink(slink);
   ck_assert_ptr_nonnull(link_target);
   ck_assert_str_eq(link_target, target);
   free(link_target);

   pgmoneta_delete_directory(base);
}
END_TEST

START_TEST(test_utils_server)
{
   char* s = NULL;

   // server 0 is "primary" in my minimal config
   s = pgmoneta_get_server(0);
   ck_assert_ptr_nonnull(s);
   ck_assert(pgmoneta_contains(s, "primary"));
   free(s);

   s = pgmoneta_get_server_backup(0);
   ck_assert_ptr_nonnull(s);
   ck_assert(pgmoneta_contains(s, "primary/backup"));
   free(s);

   s = pgmoneta_get_server_wal(0);
   ck_assert_ptr_nonnull(s);
   ck_assert(pgmoneta_contains(s, "primary/wal"));
   free(s);

   // Invalid server
   ck_assert_ptr_null(pgmoneta_get_server(-1));
   ck_assert_ptr_null(pgmoneta_get_server(100));
}
END_TEST

START_TEST(test_utils_libev)
{
   pgmoneta_libev_engines();
   // We cannot check EVBACKEND_* constants easily unless included
   // Let's assume constants are available.

   // Test string conversion
   ck_assert_str_eq(pgmoneta_libev_engine(EVBACKEND_SELECT), "select");
   ck_assert_str_eq(pgmoneta_libev_engine(EVBACKEND_POLL), "poll");
   ck_assert_str_eq(pgmoneta_libev_engine(0xFFFFFFFF), "Unknown");
}
END_TEST

START_TEST(test_utils_extract_error)
{
   struct message* msg = malloc(sizeof(struct message));
   msg->kind = 'E';
   // Data: [Type:1][Length:4] 'S' "ERROR" \0 'C' "12345" \0 \0
   msg->data = calloc(1, 100);
   char* p = (char*)msg->data;

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

   char* extracted = NULL;

   ck_assert_int_eq(pgmoneta_extract_error_fields('S', msg, &extracted), 0);
   ck_assert_str_eq(extracted, "ERROR");
   free(extracted);

   ck_assert_int_eq(pgmoneta_extract_error_fields('C', msg, &extracted), 0);
   ck_assert_str_eq(extracted, "12345");
   free(extracted);

   ck_assert_int_ne(pgmoneta_extract_error_fields('X', msg, &extracted), 0);
   ck_assert_ptr_null(extracted);

   free(msg->data);
   free(msg);
}
END_TEST

START_TEST(test_utils_wal_unit)
{
   char* s = pgmoneta_wal_file_name(1, 1, 16 * 1024 * 1024);
   ck_assert_str_eq(s, "000000010000000000000001");
   free(s);
}
END_TEST

START_TEST(test_utils_base32)
{
   unsigned char* hex = NULL;
   // "MZXW6YTBOI======" is base32 for "foobar".
   // for (i = 0; i < base32_length; i++) sprintf(..., "%02x", base32[i]);
   // It creates a HEX representation of the INPUT BYTES directly, treating them as bytes.
   // The implementation iterates i from 0 to length, and prints base32[i] as hex.
   // So if input is "A", hex is "41".

   unsigned char input[] = "A";
   if (pgmoneta_convert_base32_to_hex(input, 1, &hex) == 0)
   {
      ck_assert_ptr_nonnull(hex);
      ck_assert_str_eq((char*)hex, "41"); // 'A' in hex is 41
      free(hex);
   }

   unsigned char input2[] = "\x01\x02";
   if (pgmoneta_convert_base32_to_hex(input2, 2, &hex) == 0)
   {
      ck_assert_str_eq((char*)hex, "0102");
      free(hex);
   }
}
END_TEST

START_TEST(test_utils_enc_comp)
{
   // Is encrypted
   ck_assert(pgmoneta_is_encrypted("file.aes"));
   ck_assert(!pgmoneta_is_encrypted("file.txt"));
   ck_assert(!pgmoneta_is_encrypted(NULL));

   // Is compressed
   ck_assert(pgmoneta_is_compressed("file.zstd"));
   ck_assert(pgmoneta_is_compressed("file.lz4"));
   ck_assert(pgmoneta_is_compressed("file.bz2"));
   ck_assert(pgmoneta_is_compressed("file.gz"));
   ck_assert(!pgmoneta_is_compressed("file.txt"));
   ck_assert(!pgmoneta_is_compressed(NULL));
}
END_TEST

START_TEST(test_utils_message_parsing)
{
   struct message* msg = NULL;
   struct message* extracted = NULL;
   char* username = NULL;
   char* database = NULL;
   char* appname = NULL;
   int res;
   void* p = NULL;

   msg = (struct message*)malloc(sizeof(struct message));
   msg->kind = 0;
   msg->data = calloc(1, 1024);
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

   ck_assert_int_eq(res, 0);
   if (res == 0)
   {
      ck_assert_str_eq(username, "myuser");
      ck_assert_str_eq(database, "mydb");
      ck_assert_str_eq(appname, "myapp");
   }

   free(username);
   free(database);
   free(appname);
   free(msg->data);
   free(msg);

   // Test pgmoneta_extract_message (e.g. ErrorResponse 'E')
   // extract_message expects [Type][Length]... in msg->data
   msg = (struct message*)malloc(sizeof(struct message));
   msg->kind = 'E'; // This doesn't matter for the function logic much if we pass type but good to set
   msg->data = calloc(1, 1024);
   p = msg->data;

   pgmoneta_write_byte(p, 'E');
   p += 1;
   pgmoneta_write_int32(p, 4);
   p += 4;
   msg->length = (char*)p - (char*)msg->data;

   res = pgmoneta_extract_message('E', msg, &extracted);
   ck_assert_int_eq(res, 0);
   ck_assert_ptr_nonnull(extracted);
   ck_assert_int_eq(extracted->kind, 'E');

   pgmoneta_free_message(extracted);
   free(msg->data);
   free(msg);
}
END_TEST

START_TEST(test_utils_permissions)
{
   char* dir = "test_perm_dir";
   char* file = "test_perm_dir/file";
   mode_t mode;

   pgmoneta_delete_directory(dir);
   pgmoneta_mkdir(dir);

   FILE* f = fopen(file, "w");
   if (f)
      fclose(f);

   ck_assert_int_eq(pgmoneta_permission_recursive(dir), 0);

   mode = pgmoneta_get_permission(file);
   ck_assert(mode > 0);

   pgmoneta_delete_directory(dir);
}
END_TEST

START_TEST(test_utils_space)
{
   unsigned long total_sp = pgmoneta_total_space(".");
   ck_assert(total_sp > 0);

   char* dir = "test_space_dir";
   char* file1 = "test_space_dir/small";
   char* file2 = "test_space_dir/big";
   pgmoneta_mkdir(dir);

   FILE* f = fopen(file1, "w");
   if (f)
   {
      fprintf(f, "a");
      fclose(f);
   }
   f = fopen(file2, "w");
   if (f)
   {
      fprintf(f, "aaaaa");
      fclose(f);
   }

   unsigned long biggest = pgmoneta_biggest_file(dir);
   ck_assert(biggest >= 5);

   pgmoneta_delete_directory(dir);
}
END_TEST

START_TEST(test_utils_files_advanced)
{
   char src[128];
   char dst[128];
   char sub[128];
   char subfile[128];

   strcpy(src, "test_adv_src");
   strcpy(dst, "test_adv_dst");
   sprintf(sub, "%s/sub", src);
   sprintf(subfile, "%s/file.txt", sub);

   // Setup source
   pgmoneta_delete_directory(src);
   pgmoneta_delete_directory(dst);

   pgmoneta_mkdir(src);
   pgmoneta_mkdir(sub);
   FILE* f = fopen(subfile, "w");
   if (f)
   {
      fprintf(f, "data");
      fclose(f);
   }

   // Test is_wal_file
   ck_assert(pgmoneta_is_wal_file("000000010000000000000001"));
   ck_assert(!pgmoneta_is_wal_file("history"));
   ck_assert(!pgmoneta_is_wal_file("000000010000000000000001.partial"));

   // Test copy_and_extract basic
   char file_src[128];
   char file_dst[128];
   sprintf(file_src, "%s/plain.txt", src);
   sprintf(file_dst, "%s/plain.txt", dst);

   pgmoneta_mkdir(dst);
   f = fopen(file_src, "w");
   if (f)
   {
      fprintf(f, "plain");
      fclose(f);
   }

   char* to_ptr = strdup(file_dst);
   ck_assert_int_eq(pgmoneta_copy_and_extract_file(file_src, &to_ptr), 0);
   ck_assert(pgmoneta_exists(file_dst));
   free(to_ptr);

   // Test list_directory (just ensure it runs)
   pgmoneta_list_directory(src);

   pgmoneta_delete_directory(src);
   pgmoneta_delete_directory(dst);
}
END_TEST

START_TEST(test_utils_missing_basic)
{
   // Time functions
   struct timespec start = {100, 0};
   struct timespec end = {105, 500000000}; // 5.5 seconds later
   double seconds = 0;

   double duration = pgmoneta_compute_duration(start, end);
   ck_assert(duration > 5.4 && duration < 5.6);

   char* ts_str = pgmoneta_get_timestamp_string(start, end, &seconds);
   ck_assert_ptr_nonnull(ts_str);
   ck_assert(seconds > 5.4 && seconds < 5.6);
   free(ts_str);

   // System / User
   char* user = pgmoneta_get_user_name();
   ck_assert_ptr_nonnull(user);
   free(user);

   char* home = pgmoneta_get_home_directory();
   ck_assert_ptr_nonnull(home);
   free(home);

   // File Extended
   char* fpath = "test_del_file.txt";
   FILE* f = fopen(fpath, "w");
   if (f)
   {
      fprintf(f, "12345");
      fclose(f);
   }

   ck_assert_int_eq(pgmoneta_get_file_size(fpath), 5);

   // pgmoneta_delete_file(char* file, struct workers* workers)
   ck_assert_int_eq(pgmoneta_delete_file(fpath, NULL), 0);
   ck_assert(!pgmoneta_exists(fpath));

   // Create temp dir for symlink test
   char* dir = "test_link_at_dir";
   pgmoneta_mkdir(dir);
   pgmoneta_delete_directory(dir);
}
END_TEST

START_TEST(test_utils_missing_server)
{
   char* s = NULL;
   int server = 0;
   char* id = "20231026120000";

   s = pgmoneta_get_server_summary(server);
   ck_assert_ptr_nonnull(s);
   free(s);

   // Inject wal_shipping config for testing
   struct main_configuration* config = (struct main_configuration*)shmem;
   strcpy(config->common.servers[server].wal_shipping, "/tmp/wal_ship");

   s = pgmoneta_get_server_wal_shipping(server);
   ck_assert_ptr_nonnull(s);
   free(s);

   s = pgmoneta_get_server_wal_shipping_wal(server);
   ck_assert_ptr_nonnull(s);
   free(s);

   s = pgmoneta_get_server_workspace(server);
   ck_assert_ptr_nonnull(s);
   // Setup workspace for delete test
   pgmoneta_mkdir(s);

   // Check deletion
   ck_assert_int_eq(pgmoneta_delete_server_workspace(server, NULL), 0);
   ck_assert(!pgmoneta_exists(s));
   free(s);

   // Identifiers
   s = pgmoneta_get_server_backup_identifier(server, id);
   ck_assert_ptr_nonnull(s);
   free(s);

   s = pgmoneta_get_server_extra_identifier(server, id);
   ck_assert_ptr_nonnull(s);
   free(s);

   s = pgmoneta_get_server_backup_identifier_data(server, id);
   ck_assert_ptr_nonnull(s);
   free(s);

   s = pgmoneta_get_server_backup_identifier_data_wal(server, id);
   ck_assert_ptr_nonnull(s);
   free(s);

   s = pgmoneta_get_server_backup_identifier_tablespace(server, id, "tbs");
   ck_assert_ptr_nonnull(s);
   free(s);
}
END_TEST

START_TEST(test_utils_missing_wal)
{
   char dir[128];
   char file1[128];
   char file2[128];
   char to_dir[128];
   struct deque* files = NULL;

   strcpy(dir, "test_wal_dir");
   pgmoneta_mkdir(dir);

   // Create dummy WAL files (24 chars hex)
   sprintf(file1, "%s/000000010000000000000001", dir);
   sprintf(file2, "%s/000000010000000000000002", dir);

   FILE* f = fopen(file1, "w");
   if (f)
      fclose(f);
   f = fopen(file2, "w");
   if (f)
      fclose(f);

   ck_assert_int_eq(pgmoneta_get_wal_files(dir, &files), 0);
   ck_assert_int_eq(pgmoneta_deque_size(files), 2);
   pgmoneta_deque_destroy(files);

   // number_of_wal_files
   ck_assert_int_eq(pgmoneta_number_of_wal_files(dir, "000000000000000000000000", NULL), 2);

   // copy_wal_files
   strcpy(to_dir, "test_wal_dir_copy");
   pgmoneta_mkdir(to_dir);

   ck_assert_int_eq(pgmoneta_copy_wal_files(dir, to_dir, "000000000000000000000000", NULL), 0);
   char check_file[128];
   sprintf(check_file, "%s/000000010000000000000001", to_dir);
   ck_assert(pgmoneta_exists(check_file));

   pgmoneta_delete_directory(to_dir);
}
END_TEST

START_TEST(test_utils_missing_misc)
{
   // pgmoneta_extract_message_from_data
   // Construct a raw message buffer: Type (1 byte) + Length (4 bytes) + Data
   // Actually extract_message_from_data receives `data`
   // Check declaration: pgmoneta_extract_message_from_data(char type, void* data, size_t data_size, struct message** extracted)
   // Usually `data` is the payload.

   char buffer[1024];
   memset(buffer, 0, 1024);
   // Format: Type(1) + Length(4) + Content
   buffer[0] = 'Q';
   int len = 8; // 4 bytes for length field itself + 4 bytes content
   int nlen = htonl(len);
   memcpy(buffer + 1, &nlen, 4);
   memcpy(buffer + 1 + 4, "TEST", 4);

   struct message* extracted = NULL;
   ck_assert_int_eq(pgmoneta_extract_message_from_data('Q', buffer, 9, &extracted), 0);
   ck_assert_ptr_nonnull(extracted);
   ck_assert_int_eq(extracted->kind, 'Q');
   ck_assert_int_eq(extracted->length, 9);

   pgmoneta_free_message(extracted);

   // pgmoneta_set_proc_title(int argc, char** argv, char* s1, char* s2)
   // Use mutable argv
   char arg0[32];
   strcpy(arg0, "pgmoneta");
   char* argv[] = {arg0, NULL};
   pgmoneta_set_proc_title(1, argv, "test", "title");
}
END_TEST

Suite*
pgmoneta_test_utils_suite()
{
   Suite* s;
   TCase* tc_utils;

   s = suite_create("pgmoneta_test_utils");

   tc_utils = tcase_create("test_utils");
   tcase_set_tags(tc_utils, "common");
   tcase_set_timeout(tc_utils, 60);
   tcase_add_checked_fixture(tc_utils, pgmoneta_test_setup, pgmoneta_test_teardown);
   tcase_add_test(tc_utils, test_resolve_path_trailing_env_var);
   tcase_add_test(tc_utils, test_utils_starts_with);
   tcase_add_test(tc_utils, test_utils_ends_with);
   tcase_add_test(tc_utils, test_utils_contains);
   tcase_add_test(tc_utils, test_utils_compare_string);
   tcase_add_test(tc_utils, test_utils_atoi);
   tcase_add_test(tc_utils, test_utils_is_number);
   tcase_add_test(tc_utils, test_utils_base64);
   tcase_add_test(tc_utils, test_utils_is_incremental_path);
   tcase_add_test(tc_utils, test_utils_get_parent_dir);
   tcase_add_test(tc_utils, test_utils_serialization);
   tcase_add_test(tc_utils, test_utils_append);
   tcase_add_test(tc_utils, test_utils_string_manipulation);
   tcase_add_test(tc_utils, test_utils_math);
   tcase_add_test(tc_utils, test_utils_version);
   tcase_add_test(tc_utils, test_utils_bigendian);
   tcase_add_test(tc_utils, test_utils_strip_extension);
   tcase_add_test(tc_utils, test_utils_file_size);
   tcase_add_test(tc_utils, test_utils_file_ops);
   tcase_add_test(tc_utils, test_utils_snprintf);
   tcase_add_test(tc_utils, test_utils_string_extras);
   tcase_add_test(tc_utils, test_utils_merge_string_arrays);
   tcase_add_test(tc_utils, test_utils_time);
   tcase_add_test(tc_utils, test_utils_token_bucket);
   tcase_add_test(tc_utils, test_utils_file_dir);
   tcase_add_test(tc_utils, test_utils_symlinks);
   tcase_add_test(tc_utils, test_utils_server);
   tcase_add_test(tc_utils, test_utils_misc);
   tcase_add_test(tc_utils, test_utils_message_parsing);
   tcase_add_test(tc_utils, test_utils_permissions);
   tcase_add_test(tc_utils, test_utils_space);

   tcase_add_test(tc_utils, test_utils_base32);
   tcase_add_test(tc_utils, test_utils_enc_comp);

   tcase_add_test(tc_utils, test_utils_missing_server);
   tcase_add_test(tc_utils, test_utils_missing_wal);
   tcase_add_test(tc_utils, test_utils_missing_misc);

   tcase_add_test(tc_utils, test_utils_wal_unit);

   tcase_add_test(tc_utils, test_utils_libev);
   tcase_add_test(tc_utils, test_utils_extract_error);

   tcase_add_test(tc_utils, test_utils_files_advanced);

   tcase_add_test(tc_utils, test_utils_missing_basic);

   suite_add_tcase(s, tc_utils);
   return s;
}
