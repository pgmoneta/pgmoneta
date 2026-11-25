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
#include <art.h>
#include <csv.h>
#include <manifest.h>
#include <mctf.h>
#include <tscommon.h>
#include <utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

MCTF_TEST(test_manifest_compare_basic)
{
   char old_path[MAX_PATH];
   char new_path[MAX_PATH];
   FILE* f_old = NULL;
   FILE* f_new = NULL;
   struct art* deleted = NULL;
   struct art* changed = NULL;
   struct art* added = NULL;

   pgmoneta_test_setup();

   pgmoneta_snprintf(old_path, sizeof(old_path), "%s/manifest_old_%d.csv", pgmoneta_get_tmpdir(), getpid());
   pgmoneta_snprintf(new_path, sizeof(new_path), "%s/manifest_new_%d.csv", pgmoneta_get_tmpdir(), getpid());

   f_old = fopen(old_path, "w");
   MCTF_ASSERT_PTR_NONNULL(f_old, cleanup, "Failed to create old manifest file");
   fprintf(f_old, "base/1/1234,hash_old_1\n");
   fprintf(f_old, "base/1/1235,hash_shared\n");
   fprintf(f_old, "base/1/1236,hash_to_change\n");
   fclose(f_old);
   f_old = NULL;

   f_new = fopen(new_path, "w");
   MCTF_ASSERT_PTR_NONNULL(f_new, cleanup, "Failed to create new manifest file");
   fprintf(f_new, "base/1/1235,hash_shared\n");
   fprintf(f_new, "base/1/1236,hash_changed\n");
   fprintf(f_new, "base/1/1237,hash_new\n");
   fclose(f_new);
   f_new = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_compare_manifests(old_path, new_path, &deleted, &changed, &added), 0, cleanup,
                      "pgmoneta_compare_manifests failed");

   MCTF_ASSERT_PTR_NONNULL(deleted, cleanup, "deleted tree is NULL");
   MCTF_ASSERT_PTR_NONNULL(changed, cleanup, "changed tree is NULL");
   MCTF_ASSERT_PTR_NONNULL(added, cleanup, "added tree is NULL");

   /* base/1/1234 was deleted */
   MCTF_ASSERT(pgmoneta_art_contains_key(deleted, "base/1/1234"), cleanup,
               "deleted tree missing base/1/1234");
   MCTF_ASSERT(!pgmoneta_art_contains_key(deleted, "base/1/1235"), cleanup,
               "deleted tree should not contain shared file");

   /* base/1/1236 was changed, plus backup_manifest */
   MCTF_ASSERT(pgmoneta_art_contains_key(changed, "base/1/1236"), cleanup,
               "changed tree missing base/1/1236");
   MCTF_ASSERT(pgmoneta_art_contains_key(changed, "backup_manifest"), cleanup,
               "changed tree missing backup_manifest");

   /* base/1/1237 was added */
   MCTF_ASSERT(pgmoneta_art_contains_key(added, "base/1/1237"), cleanup,
               "added tree missing base/1/1237");

cleanup:
   if (f_old != NULL)
   {
      fclose(f_old);
   }
   if (f_new != NULL)
   {
      fclose(f_new);
   }
   unlink(old_path);
   unlink(new_path);
   pgmoneta_art_destroy(deleted);
   pgmoneta_art_destroy(changed);
   pgmoneta_art_destroy(added);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_manifest_compare_identical)
{
   char old_path[MAX_PATH];
   char new_path[MAX_PATH];
   FILE* f_old = NULL;
   FILE* f_new = NULL;
   struct art* deleted = NULL;
   struct art* changed = NULL;
   struct art* added = NULL;

   pgmoneta_test_setup();

   pgmoneta_snprintf(old_path, sizeof(old_path), "%s/manifest_ident_old_%d.csv", pgmoneta_get_tmpdir(), getpid());
   pgmoneta_snprintf(new_path, sizeof(new_path), "%s/manifest_ident_new_%d.csv", pgmoneta_get_tmpdir(), getpid());

   f_old = fopen(old_path, "w");
   MCTF_ASSERT_PTR_NONNULL(f_old, cleanup, "Failed to create identical old manifest file");
   fprintf(f_old, "base/1/1235,hash_shared\n");
   fclose(f_old);
   f_old = NULL;

   f_new = fopen(new_path, "w");
   MCTF_ASSERT_PTR_NONNULL(f_new, cleanup, "Failed to create identical new manifest file");
   fprintf(f_new, "base/1/1235,hash_shared\n");
   fclose(f_new);
   f_new = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_compare_manifests(old_path, new_path, &deleted, &changed, &added), 0, cleanup,
                      "pgmoneta_compare_manifests failed on identical manifests");

   MCTF_ASSERT_PTR_NONNULL(deleted, cleanup, "deleted tree is NULL");
   MCTF_ASSERT_PTR_NONNULL(changed, cleanup, "changed tree is NULL");
   MCTF_ASSERT_PTR_NONNULL(added, cleanup, "added tree is NULL");

   MCTF_ASSERT_INT_EQ(deleted->size, 0, cleanup, "deleted size should be 0");
   MCTF_ASSERT_INT_EQ(changed->size, 0, cleanup, "changed size should be 0");
   MCTF_ASSERT_INT_EQ(added->size, 0, cleanup, "added size should be 0");

cleanup:
   if (f_old != NULL)
   {
      fclose(f_old);
   }
   if (f_new != NULL)
   {
      fclose(f_new);
   }
   unlink(old_path);
   unlink(new_path);
   pgmoneta_art_destroy(deleted);
   pgmoneta_art_destroy(changed);
   pgmoneta_art_destroy(added);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_manifest_compare_invalid_paths)
{
   struct art* deleted = NULL;
   struct art* changed = NULL;
   struct art* added = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT_INT_EQ(pgmoneta_compare_manifests("/nonexistent/manifest1.csv",
                                                 "/nonexistent/manifest2.csv",
                                                 &deleted, &changed, &added), 1, cleanup,
                      "pgmoneta_compare_manifests should fail for nonexistent paths");

   MCTF_ASSERT_PTR_NULL(deleted, cleanup, "deleted should be NULL on error");
   MCTF_ASSERT_PTR_NULL(changed, cleanup, "changed should be NULL on error");
   MCTF_ASSERT_PTR_NULL(added, cleanup, "added should be NULL on error");

cleanup:
   pgmoneta_art_destroy(deleted);
   pgmoneta_art_destroy(changed);
   pgmoneta_art_destroy(added);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_csv_reader_long_line)
{
   char path[MAX_PATH];
   char long_filename[700];
   char checksum[] = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
   FILE* f = NULL;
   struct csv_reader* reader = NULL;
   char** cols = NULL;
   int num_cols = 0;

   pgmoneta_test_setup();

   pgmoneta_snprintf(path, sizeof(path), "%s/test_long_line_%d.csv", pgmoneta_get_tmpdir(), getpid());

   memset(long_filename, 'a', sizeof(long_filename) - 1);
   long_filename[sizeof(long_filename) - 1] = '\0';
   /* Add directory slashes */
   long_filename[100] = '/';
   long_filename[200] = '/';
   long_filename[300] = '/';
   long_filename[400] = '/';
   long_filename[500] = '/';

   f = fopen(path, "w");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "Failed to create test csv file");
   fprintf(f, "%s,%s\n", long_filename, checksum);
   fclose(f);
   f = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_csv_reader_init(path, &reader), 0, cleanup, "csv reader init failed");
   MCTF_ASSERT(pgmoneta_csv_next_row(reader, &num_cols, &cols), cleanup, "csv next row failed for long line");
   MCTF_ASSERT_INT_EQ(num_cols, 2, cleanup, "num_cols should be 2");
   MCTF_ASSERT_STR_EQ(cols[0], long_filename, cleanup, "filename did not match long line");
   MCTF_ASSERT_STR_EQ(cols[1], checksum, cleanup, "checksum did not match");

   free(cols);
   cols = NULL;

cleanup:
   if (f != NULL)
   {
      fclose(f);
   }
   free(cols);
   pgmoneta_csv_reader_destroy(reader);
   unlink(path);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}
