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
#include <mctf.h>
#include <utils.h>
#include <vfile.h>

#include <stdio.h>

MCTF_TEST(test_vfile_local)
{
   char* dir = NULL;
   char* tmp_file = NULL;
   char* sample_file = NULL;
   struct vfile* reader = NULL;
   struct vfile* writer = NULL;
   char buf[128] = {0};
   size_t s = 0;
   bool last_chunk = false;
   dir = pgmoneta_append(dir, TEST_BASE_DIR);
   dir = pgmoneta_append(dir, "/vfile_local");

   sample_file = pgmoneta_append(sample_file, TEST_BASE_DIR);
   sample_file = pgmoneta_append(sample_file, "/resource/vfile_test/text.txt");

   tmp_file = pgmoneta_append(tmp_file, dir);
   tmp_file = pgmoneta_append(tmp_file, "/tmp_text.txt");

   pgmoneta_mkdir(dir);

   MCTF_ASSERT(!pgmoneta_vfile_create_local(sample_file, "r", &reader), cleanup);
   MCTF_ASSERT(!pgmoneta_vfile_create_local(tmp_file, "w", &writer), cleanup);
   MCTF_ASSERT(!reader->read(reader, buf, sizeof(buf), &s, &last_chunk), cleanup);
   MCTF_ASSERT(last_chunk, cleanup);
   MCTF_ASSERT(!writer->write(writer, buf, s, last_chunk), cleanup);

   //close writer to flush the file
   pgmoneta_vfile_destroy(writer);
   writer = NULL;

   MCTF_ASSERT(pgmoneta_compare_files(sample_file, tmp_file), cleanup);

cleanup:
   pgmoneta_delete_directory(dir);
   pgmoneta_vfile_destroy(reader);
   pgmoneta_vfile_destroy(writer);
   free(dir);
   free(tmp_file);
   free(sample_file);
   MCTF_FINISH();
}