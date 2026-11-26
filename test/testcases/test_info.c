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
#include <info.h>
#include <mctf.h>
#include <utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int
write_info_file(char* path, int tablespaces)
{
   FILE* f = fopen(path, "w");
   char longvalue[300];

   if (f == NULL)
   {
      return 1;
   }

   memset(longvalue, 'p', sizeof(longvalue));
   longvalue[sizeof(longvalue) - 1] = '\0';

   fprintf(f, "LABEL=20260101000000\n");
   fprintf(f, "TYPE=1\n");
   fprintf(f, "PARENT_LABEL=%s\n", longvalue);
   fprintf(f, "TABLESPACES=%d\n", tablespaces);
   for (int i = 0; i < tablespaces; i++)
   {
      fprintf(f, "TABLESPACE%d=ts%d\n", i, i);
      fprintf(f, "TABLESPACE_OID%d=%d\n", i, 16400 + i);
      fprintf(f, "TABLESPACE_PATH%d=/tmp/ts%d\n", i, i);
   }
   fprintf(f, "COMMENTS=ok\n");

   return fclose(f);
}

/*
 * backup.info is read back for every backup in an incremental chain, so a value
 * longer than its field, or more tablespaces than there is room for, must be
 * clamped rather than written past the struct fields.
 */
MCTF_TEST(test_info_load_clamps_oversized_values)
{
   char path[MAX_PATH];
   struct backup* bck = NULL;

   memset(path, 0, sizeof(path));
   snprintf(path, sizeof(path), "/tmp/pgmoneta-info-%d.info", (int)getpid());

   MCTF_ASSERT(write_info_file(path, 70) == 0, cleanup, "could not write backup.info");
   MCTF_ASSERT(pgmoneta_load_info_file(path, &bck) == 0, cleanup, "load of backup.info failed");
   MCTF_ASSERT_PTR_NONNULL(bck, cleanup, "backup null");

   MCTF_ASSERT(strlen(bck->parent_label) == sizeof(bck->parent_label) - 1, cleanup,
               "parent label is not clamped to its field");
   MCTF_ASSERT(strcmp(bck->label, "20260101000000") == 0, cleanup, "label damaged by oversized parent label");
   MCTF_ASSERT(strcmp(bck->comments, "ok") == 0, cleanup, "comments damaged by oversized parent label");
   MCTF_ASSERT(bck->number_of_tablespaces == MAX_NUMBER_OF_TABLESPACES, cleanup,
               "tablespace count is not clamped");
   MCTF_ASSERT(strcmp(bck->tablespaces[MAX_NUMBER_OF_TABLESPACES - 1], "ts63") == 0, cleanup,
               "last tablespace not read");

cleanup:
   free(bck);
   unlink(path);

   MCTF_FINISH();
}
