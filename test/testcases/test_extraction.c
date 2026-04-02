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
#include <aes.h>
#include <extraction.h>
#include <mctf.h>
#include <shmem.h>
#include <tar.h>
#include <tscommon.h>
#include <utils.h>
#include <zstandard_compression.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>

MCTF_TEST_SETUP(extraction)
{
   pgmoneta_memory_init();
}

MCTF_TEST_TEARDOWN(extraction)
{
   pgmoneta_memory_destroy();
}

MCTF_TEST(test_extraction_strip_extension)
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

   /* Hidden file: leading dot is the only dot, result is empty string */
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

MCTF_TEST(test_extraction_strip_suffix)
{
   char* base = NULL;

   /* "file.zstd.aes" with COMPRESSED | ZSTD | ENCRYPTED should become "file" */
   MCTF_ASSERT_INT_EQ(pgmoneta_extraction_strip_suffix("file.zstd.aes",
                                                       PGMONETA_FILE_TYPE_COMPRESSED | PGMONETA_FILE_TYPE_ZSTD | PGMONETA_FILE_TYPE_ENCRYPTED,
                                                       &base),
                      0, cleanup, "strip_suffix zstd.aes failed");
   MCTF_ASSERT_STR_EQ(base, "file", cleanup, "strip_suffix zstd.aes mismatch");
   free(base);
   base = NULL;

   /* "001.tar.zstd.aes" with TAR | COMPRESSED | ZSTD | ENCRYPTED should become "001" */
   MCTF_ASSERT_INT_EQ(pgmoneta_extraction_strip_suffix("001.tar.zstd.aes",
                                                       PGMONETA_FILE_TYPE_TAR | PGMONETA_FILE_TYPE_COMPRESSED | PGMONETA_FILE_TYPE_ZSTD | PGMONETA_FILE_TYPE_ENCRYPTED,
                                                       &base),
                      0, cleanup, "strip_suffix tar.zstd.aes failed");
   MCTF_ASSERT_STR_EQ(base, "001", cleanup, "strip_suffix tar.zstd.aes mismatch");
   free(base);
   base = NULL;

cleanup:
   if (base != NULL)
   {
      free(base);
      base = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_extraction_file_type_bitmask)
{
   uint32_t type = 0;

   /* Plain WAL file */
   type = pgmoneta_extraction_get_file_type("000000010000000000000001");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_WAL), cleanup, "WAL file type detection failed");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_COMPRESSED), cleanup, "WAL file should not be compressed");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_ENCRYPTED), cleanup, "WAL file should not be encrypted");

   /* WAL .gz */
   type = pgmoneta_extraction_get_file_type("000000010000000000000001.gz");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_WAL), cleanup, "Compressed WAL detection failed .gz");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_COMPRESSED), cleanup, "Compression detection failed .gz");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_GZIP), cleanup, "GZIP detection failed .gz");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_ZSTD), cleanup, "ZSTD must not be set for .gz");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_LZ4), cleanup, "LZ4 must not be set for .gz");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_BZ2), cleanup, "BZ2 must not be set for .gz");

   /* WAL .zstd */
   type = pgmoneta_extraction_get_file_type("000000010000000000000001.zstd");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_WAL), cleanup, "Compressed WAL detection failed .zstd");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_COMPRESSED), cleanup, "Compression detection failed .zstd");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_ZSTD), cleanup, "ZSTD detection failed .zstd");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_GZIP), cleanup, "GZIP must not be set for .zstd");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_LZ4), cleanup, "LZ4 must not be set for .zstd");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_BZ2), cleanup, "BZ2 must not be set for .zstd");

   /* WAL .lz4 */
   type = pgmoneta_extraction_get_file_type("000000010000000000000001.lz4");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_WAL), cleanup, "Compressed WAL detection failed .lz4");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_COMPRESSED), cleanup, "Compression detection failed .lz4");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_LZ4), cleanup, "LZ4 detection failed .lz4");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_GZIP), cleanup, "GZIP must not be set for .lz4");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_ZSTD), cleanup, "ZSTD must not be set for .lz4");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_BZ2), cleanup, "BZ2 must not be set for .lz4");

   /* WAL .bz2 */
   type = pgmoneta_extraction_get_file_type("000000010000000000000001.bz2");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_WAL), cleanup, "Compressed WAL detection failed .bz2");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_COMPRESSED), cleanup, "Compression detection failed .bz2");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_BZ2), cleanup, "BZ2 detection failed .bz2");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_GZIP), cleanup, "GZIP must not be set for .bz2");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_ZSTD), cleanup, "ZSTD must not be set for .bz2");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_LZ4), cleanup, "LZ4 must not be set for .bz2");

   /* Encrypted compressed WAL */
   type = pgmoneta_extraction_get_file_type("000000010000000000000001.zstd.aes");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_WAL), cleanup, "Encrypted WAL detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_COMPRESSED), cleanup, "Compression in encrypted failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_ENCRYPTED), cleanup, "Encryption detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_ZSTD), cleanup, "ZSTD detection failed in encrypted WAL");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_GZIP), cleanup, "GZIP must not be set in .zstd.aes");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_LZ4), cleanup, "LZ4 must not be set in .zstd.aes");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_BZ2), cleanup, "BZ2 must not be set in .zstd.aes");

   /* Partial WAL */
   type = pgmoneta_extraction_get_file_type("000000010000000000000001.partial");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_WAL), cleanup, "Partial WAL detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_PARTIAL), cleanup, "Partial flag detection failed");

   /* TAR archive */
   type = pgmoneta_extraction_get_file_type("backup.tar");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_TAR), cleanup, "TAR detection failed");
   MCTF_ASSERT(!(type & PGMONETA_FILE_TYPE_WAL), cleanup, "TAR should not be WAL");

   /* TAR + GZIP */
   type = pgmoneta_extraction_get_file_type("backup.tar.gz");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_TAR), cleanup, "Compressed TAR detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_COMPRESSED), cleanup, "TAR compression detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_GZIP), cleanup, "TAR GZIP type detection failed");

   /* TAR + LZ4 */
   type = pgmoneta_extraction_get_file_type("backup.tar.lz4");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_TAR), cleanup, "Compressed TAR LZ4 detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_LZ4), cleanup, "TAR LZ4 type detection failed");

   /* TAR + ZSTD */
   type = pgmoneta_extraction_get_file_type("backup.tar.zstd");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_TAR), cleanup, "Compressed TAR ZSTD detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_ZSTD), cleanup, "TAR ZSTD type detection failed");

   /* TAR + ZSTD + AES */
   type = pgmoneta_extraction_get_file_type("backup.tar.zstd.aes");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_TAR), cleanup, "Encrypted compressed TAR detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_ZSTD), cleanup, "Encrypted TAR ZSTD type detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_COMPRESSED), cleanup, "Encrypted TAR compression flag detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_ENCRYPTED), cleanup, "Encrypted TAR encryption flag detection failed");

   /* TAR + BZ2 */
   type = pgmoneta_extraction_get_file_type("backup.tar.bz2");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_TAR), cleanup, "Compressed TAR BZ2 detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_BZ2), cleanup, "TAR BZ2 type detection failed");

   /* .tgz shorthand */
   type = pgmoneta_extraction_get_file_type("backup.tgz");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_TAR), cleanup, "TGZ TAR detection failed");
   MCTF_ASSERT((type & PGMONETA_FILE_TYPE_GZIP), cleanup, "TGZ GZIP detection failed");

   /* NULL input */
   type = pgmoneta_extraction_get_file_type(NULL);
   MCTF_ASSERT(type == PGMONETA_FILE_TYPE_UNKNOWN, cleanup, "NULL should return UNKNOWN");

   /* Non-WAL, non-matching file */
   type = pgmoneta_extraction_get_file_type("random_file.txt");
   MCTF_ASSERT(type == PGMONETA_FILE_TYPE_UNKNOWN, cleanup, "Random file should be UNKNOWN");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_extraction_layered_archive)
{
   struct main_configuration* config = NULL;
   FILE* f = NULL;
   bool own_shmem = false;
   const char* wal_name = "000000010000000000000001";
   uint32_t file_type = 0;
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
   pgmoneta_snprintf(extracted_path, sizeof(extracted_path), "%s/src/%s", out_dir, wal_name);

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
   config->encryption = ENCRYPTION_AES_256_GCM;

   MCTF_ASSERT_INT_EQ(pgmoneta_tar(src_dir, tar_path), 0, cleanup, "tar failed");
   MCTF_ASSERT(pgmoneta_exists(tar_path), cleanup, "tar file missing");

   MCTF_ASSERT_INT_EQ(pgmoneta_zstandardc_file(tar_path, zstd_path), 0, cleanup, "zstd compression failed");
   MCTF_ASSERT(pgmoneta_exists(zstd_path), cleanup, "zstd file missing");

   MCTF_ASSERT_INT_EQ(pgmoneta_encrypt_file(zstd_path, encrypted_path, NULL), 0, cleanup, "encryption failed");
   MCTF_ASSERT(pgmoneta_exists(encrypted_path), cleanup, "encrypted file missing");

   file_type = pgmoneta_extraction_get_file_type(encrypted_path);
   {
      char* dest = out_dir;
      MCTF_ASSERT_INT_EQ(pgmoneta_extract_file(encrypted_path, file_type, false, &dest), 0, cleanup, "extract_file failed");
   }
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
