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
#include <logging.h>
#include <tscommon.h>
#include <mctf.h>
#include <utils.h>
#include <stream.h>

#include <stdio.h>
#include <stdlib.h>

int compression_methods[] = {COMPRESSION_NONE, COMPRESSION_CLIENT_ZSTD, COMPRESSION_CLIENT_GZIP, COMPRESSION_CLIENT_BZIP2, COMPRESSION_CLIENT_LZ4};
int encryption_methods[] = {ENCRYPTION_NONE, ENCRYPTION_AES_128_CBC};

static char* translate_compression(int compression);
static char* translate_encryption(int encryption);

MCTF_TEST(test_streamer)
{
   char* dir = NULL;
   char* bigfile = NULL;
   char cmd[256] = {0};
   char retrospect_dir[MAX_PATH] = {0};
   dir = pgmoneta_append(dir, TEST_BASE_DIR);
   dir = pgmoneta_append(dir, "/streamer");
   snprintf(retrospect_dir, sizeof(retrospect_dir), "%s/%s", TEST_RETROSPECT_DIR, "streamer");
   pgmoneta_mkdir(retrospect_dir);
   bigfile = pgmoneta_append(bigfile, dir);
   bigfile = pgmoneta_append(bigfile, "/bigfile.txt");
   char buf[256] = {0};
   char backup_dest[MAX_PATH];
   char restore_dest[MAX_PATH];
   struct vfile* reader = NULL;
   struct vfile* writer = NULL;
   struct streamer* backup_streamer = NULL;
   struct streamer* restore_streamer = NULL;
   bool last_chunk = false;
   bool same = false;
   size_t num_read = 0;

   // generate a large file for testing
   snprintf(cmd, sizeof(cmd), "tr -dc \"A-Za-z0-9\" < /dev/urandom | fold -w100 | head -n 100000 > %s", bigfile);
   pgmoneta_mkdir(dir);
   system(cmd);

   MCTF_ASSERT(pgmoneta_exists(bigfile), cleanup, "Failed to create %s", bigfile);
   for (int i = 0; i < sizeof(compression_methods) / sizeof(COMPRESSION_NONE); i++)
   {
      int compression = compression_methods[i];
      for (int j = 0; j < sizeof(encryption_methods) / sizeof(ENCRYPTION_NONE); j++)
      {
         int encryption = encryption_methods[j];
         memset(backup_dest, 0, sizeof(backup_dest));
         memset(restore_dest, 0, sizeof(restore_dest));
         snprintf(backup_dest, sizeof(backup_dest), "%s/bigfile_backup_%s_%s", dir, translate_compression(compression), translate_encryption(encryption));
         snprintf(restore_dest, sizeof(restore_dest), "%s/bigfile_restore_%s_%s", dir, translate_compression(compression), translate_encryption(encryption));
         //backup
         MCTF_ASSERT(!pgmoneta_vfile_create_local(bigfile, "r", &reader), cleanup);
         MCTF_ASSERT(!pgmoneta_vfile_create_local(backup_dest, "wb", &writer), cleanup);
         MCTF_ASSERT(!pgmoneta_streamer_create(STREAMER_MODE_BACKUP, encryption, compression, &backup_streamer), cleanup);
         pgmoneta_streamer_add_destination(backup_streamer, writer);

         do
         {
            MCTF_ASSERT(!reader->read(reader, buf, sizeof(buf), &num_read, &last_chunk), cleanup);
            MCTF_ASSERT(!pgmoneta_streamer_write(backup_streamer, buf, num_read, last_chunk), cleanup);
         }
         while (!last_chunk);

         pgmoneta_streamer_destroy(backup_streamer);
         pgmoneta_vfile_destroy(reader);
         reader = NULL;
         backup_streamer = NULL;

         //restore
         MCTF_ASSERT(!pgmoneta_vfile_create_local(backup_dest, "r", &reader), cleanup);
         MCTF_ASSERT(!pgmoneta_vfile_create_local(restore_dest, "wb", &writer), cleanup);
         MCTF_ASSERT(!pgmoneta_streamer_create(STREAMER_MODE_RESTORE, encryption, compression, &restore_streamer), cleanup);
         pgmoneta_streamer_add_destination(restore_streamer, writer);

         do
         {
            MCTF_ASSERT(!reader->read(reader, buf, sizeof(buf), &num_read, &last_chunk), cleanup);
            MCTF_ASSERT(!pgmoneta_streamer_write(restore_streamer, buf, num_read, last_chunk), cleanup);
         }
         while (!last_chunk);

         pgmoneta_streamer_destroy(restore_streamer);
         pgmoneta_vfile_destroy(reader);
         reader = NULL;
         restore_streamer = NULL;

         same = pgmoneta_compare_files(bigfile, restore_dest);
         if (!same)
         {
            // save the test input to retrospect/ for inspection
            pgmoneta_copy_directory(dir, retrospect_dir, NULL, NULL);
         }

         MCTF_ASSERT(same, cleanup, "Mismatch original file %s and retored file %s", bigfile, restore_dest);
         pgmoneta_delete_file(restore_dest, NULL);
         pgmoneta_delete_file(backup_dest, NULL);
      }
   }

cleanup:
   pgmoneta_delete_directory(dir);
   free(dir);
   free(bigfile);
   pgmoneta_vfile_destroy(reader);
   pgmoneta_streamer_destroy(backup_streamer);
   pgmoneta_streamer_destroy(restore_streamer);
   MCTF_FINISH();
}

static char*
translate_compression(int compression)
{
   switch (compression)
   {
      case COMPRESSION_CLIENT_GZIP:
         return "gzip";
      case COMPRESSION_CLIENT_ZSTD:
         return "zstd";
      case COMPRESSION_CLIENT_LZ4:
         return "lz4";
      case COMPRESSION_CLIENT_BZIP2:
         return "bzip2";
      case COMPRESSION_NONE:
         return "none";
      default:
         return "unknown";
   }
}

static char*
translate_encryption(int encryption)
{
   switch (encryption)
   {
      case ENCRYPTION_AES_256_CBC:
         return "aes-256-cbc";
      case ENCRYPTION_AES_192_CBC:
         return "aes-192-cbc";
      case ENCRYPTION_AES_128_CBC:
         return "aes-128-cbc";
      case ENCRYPTION_AES_256_CTR:
         return "aes-256-ctr";
      case ENCRYPTION_AES_192_CTR:
         return "aes-192-ctr";
      case ENCRYPTION_AES_128_CTR:
         return "aes-128-ctr";
      case ENCRYPTION_NONE:
         return "none";
      default:
         return "unknown";
   }
}