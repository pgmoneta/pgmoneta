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
#include <csv.h>
#include <tscommon.h>
#include <mctf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

MCTF_TEST(test_csv_special_characters)
{
   char path[] = "/tmp/test_csv_XXXXXX";
   char long_path[1024];
   char* rows[][2] = {
      {"plain/file", "abc"},
      {"pg_hba.conf,old", "def"},
      {"say \"hi\"", "ghi"},
      {"two\nlines", ""},
      {long_path, "jkl"},
   };
   int n = sizeof(rows) / sizeof(rows[0]);
   struct csv_writer* writer = NULL;
   struct csv_reader* reader = NULL;
   char** cols = NULL;
   int num_col = 0;
   int fd = -1;

   pgmoneta_test_setup();

   memset(long_path, 'x', sizeof(long_path) - 1);
   long_path[sizeof(long_path) - 1] = '\0';

   fd = mkstemp(path);
   MCTF_ASSERT(fd >= 0, cleanup, "mkstemp failed");
   close(fd);

   MCTF_ASSERT(!pgmoneta_csv_writer_init(path, &writer), cleanup, "writer init failed");
   for (int i = 0; i < n; i++)
   {
      MCTF_ASSERT(!pgmoneta_csv_write(writer, 2, rows[i]), cleanup, "write failed");
   }
   MCTF_ASSERT(!pgmoneta_csv_writer_destroy(writer), cleanup, "writer destroy failed");
   writer = NULL;

   MCTF_ASSERT(!pgmoneta_csv_reader_init(path, &reader), cleanup, "reader init failed");
   for (int i = 0; i < n; i++)
   {
      MCTF_ASSERT(pgmoneta_csv_next_row(reader, &num_col, &cols), cleanup, "missing row");
      MCTF_ASSERT_INT_EQ(num_col, 2, cleanup, "wrong column count");
      MCTF_ASSERT_STR_EQ(cols[0], rows[i][0], cleanup, "wrong path");
      MCTF_ASSERT_STR_EQ(cols[1], rows[i][1], cleanup, "wrong checksum");
      free(cols);
      cols = NULL;
   }
   MCTF_ASSERT(!pgmoneta_csv_next_row(reader, &num_col, &cols), cleanup, "unexpected extra row");

cleanup:
   free(cols);
   pgmoneta_csv_writer_destroy(writer);
   pgmoneta_csv_reader_destroy(reader);
   unlink(path);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}
