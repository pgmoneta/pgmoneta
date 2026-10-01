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
#include <mctf.h>
#include <pagechecksum.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BLOCK_SIZE         8192
#define PD_CHECKSUM_OFFSET 8

/* A page whose contents do not depend on the machine, so the checksums below
 * are the same everywhere. 251 is prime, which keeps the bytes from lining up
 * with the 32 parallel sums. */
static void
fill_page(char* page)
{
   for (int i = 0; i < BLOCK_SIZE; i++)
   {
      page[i] = (char)(i % 251);
   }
}

static void
store_checksum(char* page, uint16_t checksum)
{
   memcpy(page + PD_CHECKSUM_OFFSET, &checksum, sizeof(checksum));
}

/* The values here come from PostgreSQL itself: this implementation was run
 * against a cluster created with initdb -k and agreed with pg_checksums on
 * every page, including the checksum each reported for a page corrupted on
 * purpose. */
MCTF_TEST(test_pagechecksum_known_values)
{
   char page[BLOCK_SIZE];

   fill_page(page);

   MCTF_ASSERT_INT_EQ(pgmoneta_page_checksum(page, BLOCK_SIZE, 0), 0x1de0, cleanup,
                      "checksum of the reference page at block 0 changed");

cleanup:
   MCTF_FINISH();
}

/* The block number is part of the checksum, so the same bytes at a different
 * block do not verify. This is what catches a page written to the wrong place. */
MCTF_TEST(test_pagechecksum_block_number_matters)
{
   char page[BLOCK_SIZE];

   fill_page(page);

   MCTF_ASSERT_INT_EQ(pgmoneta_page_checksum(page, BLOCK_SIZE, 1), 0x1ddf, cleanup,
                      "checksum of the reference page at block 1 changed");
   MCTF_ASSERT(pgmoneta_page_checksum(page, BLOCK_SIZE, 0) !=
                  pgmoneta_page_checksum(page, BLOCK_SIZE, 1),
               cleanup,
               "the block number is not part of the checksum");

cleanup:
   MCTF_FINISH();
}

/* A relation past the first segment continues the block numbering rather than
 * restarting it, so the first page of segment 1 is block relseg_size. Using the
 * offset within the file instead reports every page of every later segment as
 * corrupt. */
MCTF_TEST(test_pagechecksum_segment_numbering)
{
   char page[BLOCK_SIZE];
   uint32_t relseg_size = 131072;

   fill_page(page);

   MCTF_ASSERT_INT_EQ(pgmoneta_page_checksum(page, BLOCK_SIZE, relseg_size), 0x1dde, cleanup,
                      "checksum of the reference page at the start of segment 1 changed");
   MCTF_ASSERT(pgmoneta_page_checksum(page, BLOCK_SIZE, 0) !=
                  pgmoneta_page_checksum(page, BLOCK_SIZE, relseg_size),
               cleanup,
               "segment 1 block 0 and segment 0 block 0 have the same checksum");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_pagechecksum_verify_roundtrip)
{
   char page[BLOCK_SIZE];
   uint16_t computed = 0;
   uint16_t stored = 0;

   fill_page(page);
   store_checksum(page, pgmoneta_page_checksum(page, BLOCK_SIZE, 7));

   MCTF_ASSERT(pgmoneta_page_verify(page, BLOCK_SIZE, 7, &computed, &stored), cleanup,
               "a page carrying its own checksum did not verify");
   MCTF_ASSERT_INT_EQ(computed, stored, cleanup, "computed and stored differ after verifying");

   /* The same page read as a different block must not verify. */
   MCTF_ASSERT(!pgmoneta_page_verify(page, BLOCK_SIZE, 8, &computed, &stored), cleanup,
               "a page verified against the wrong block number");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_pagechecksum_detects_corruption)
{
   char page[BLOCK_SIZE];
   uint16_t computed = 0;
   uint16_t stored = 0;

   fill_page(page);
   store_checksum(page, pgmoneta_page_checksum(page, BLOCK_SIZE, 0));

   MCTF_ASSERT(pgmoneta_page_verify(page, BLOCK_SIZE, 0, &computed, &stored), cleanup,
               "the page did not verify before being corrupted");

   /* One bit, in the data rather than the header. */
   page[4000] = (char)(page[4000] ^ 0x01);

   MCTF_ASSERT(!pgmoneta_page_verify(page, BLOCK_SIZE, 0, &computed, &stored), cleanup,
               "a corrupted page verified");
   MCTF_ASSERT(computed != stored, cleanup, "corruption did not change the checksum");

cleanup:
   MCTF_FINISH();
}

/* PostgreSQL leaves a page all zero until it is used and does not checksum it.
 * Such a page is not corrupt. */
MCTF_TEST(test_pagechecksum_new_page)
{
   char page[BLOCK_SIZE];

   memset(page, 0, sizeof(page));

   MCTF_ASSERT(pgmoneta_page_is_new(page, BLOCK_SIZE), cleanup, "an all zero page was not new");
   MCTF_ASSERT(pgmoneta_page_verify(page, BLOCK_SIZE, 0, NULL, NULL), cleanup,
               "an empty page was reported corrupt");

   page[100] = 1;
   MCTF_ASSERT(!pgmoneta_page_is_new(page, BLOCK_SIZE), cleanup,
               "a page with data in it was reported new");

cleanup:
   MCTF_FINISH();
}

/* A page that has never been checksummed stores zero, which the algorithm can
 * never produce, so it is always distinguishable from a real checksum. */
MCTF_TEST(test_pagechecksum_never_zero)
{
   char page[BLOCK_SIZE];

   fill_page(page);

   for (uint32_t blockno = 0; blockno < 512; blockno++)
   {
      MCTF_ASSERT(pgmoneta_page_checksum(page, BLOCK_SIZE, blockno) != PAGE_CHECKSUM_NONE,
                  cleanup, "the checksum came out zero");
   }

cleanup:
   MCTF_FINISH();
}

/* Computing a checksum must not disturb the page, or verifying a file would
 * change what is being verified. */
MCTF_TEST(test_pagechecksum_does_not_modify_page)
{
   char page[BLOCK_SIZE];
   char before[BLOCK_SIZE];

   fill_page(page);
   store_checksum(page, 0x1234);
   memcpy(before, page, sizeof(page));

   pgmoneta_page_checksum(page, BLOCK_SIZE, 0);

   MCTF_ASSERT(memcmp(before, page, sizeof(page)) == 0, cleanup,
               "the page changed while its checksum was computed");
   MCTF_ASSERT_INT_EQ(pgmoneta_page_stored_checksum(page), 0x1234, cleanup,
                      "the stored checksum was not put back");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_pagechecksum_checksummable)
{
   uint32_t segno = 99;

   /* Relation forks, with and without a segment suffix. */
   MCTF_ASSERT(pgmoneta_page_checksummable("data/base/16384/16385", &segno), cleanup,
               "a relation file was not checksummable");
   MCTF_ASSERT_INT_EQ(segno, 0, cleanup, "the first segment was not 0");

   MCTF_ASSERT(pgmoneta_page_checksummable("data/base/16384/16385.3", &segno), cleanup,
               "a later segment was not checksummable");
   MCTF_ASSERT_INT_EQ(segno, 3, cleanup, "the segment number was not read");

   MCTF_ASSERT(pgmoneta_page_checksummable("data/global/1262", &segno), cleanup,
               "a file under global was not checksummable");
   MCTF_ASSERT(pgmoneta_page_checksummable("data/pg_tblspc/16400/PG_17_202406281/5/16385", &segno),
               cleanup, "a file under pg_tblspc was not checksummable");

   /* The free space and visibility maps are pages too. */
   MCTF_ASSERT(pgmoneta_page_checksummable("data/base/16384/16385_fsm", &segno), cleanup,
               "the free space map was not checksummable");
   MCTF_ASSERT(pgmoneta_page_checksummable("data/base/16384/16385_vm", &segno), cleanup,
               "the visibility map was not checksummable");

   /* What PostgreSQL leaves out of its own checksumming. */
   MCTF_ASSERT(!pgmoneta_page_checksummable("data/global/pg_control", &segno), cleanup,
               "pg_control was checksummable");
   MCTF_ASSERT(!pgmoneta_page_checksummable("data/base/16384/pg_filenode.map", &segno), cleanup,
               "pg_filenode.map was checksummable");
   MCTF_ASSERT(!pgmoneta_page_checksummable("data/base/16384/PG_VERSION", &segno), cleanup,
               "PG_VERSION was checksummable");
   MCTF_ASSERT(!pgmoneta_page_checksummable("data/base/16384/pg_internal.init.123", &segno),
               cleanup, "pg_internal.init was checksummable");

   /* Nothing outside the relation directories has pages. */
   MCTF_ASSERT(!pgmoneta_page_checksummable("data/pg_wal/000000010000000000000001", &segno),
               cleanup, "a WAL segment was checksummable");
   MCTF_ASSERT(!pgmoneta_page_checksummable("data/postgresql.conf", &segno), cleanup,
               "a config file was checksummable");
   MCTF_ASSERT(!pgmoneta_page_checksummable("data/backup_label", &segno), cleanup,
               "backup_label was checksummable");

   /* A dot that is not a segment number. */
   MCTF_ASSERT(!pgmoneta_page_checksummable("data/base/16384/16385.tmp", &segno), cleanup,
               "a non numeric suffix was read as a segment");

   /* An incremental backup stores only the changed blocks behind a header, so
    * the file is not a run of pages at their own block numbers. Reading one as
    * if it were takes the relfilenode after the dot for a segment number and
    * fails every page in it. */
   MCTF_ASSERT(!pgmoneta_page_checksummable("data/base/16384/INCREMENTAL.16385", &segno),
               cleanup, "an incremental file was checksummable");
   MCTF_ASSERT(!pgmoneta_page_checksummable("data/base/16384/INCREMENTAL.16385.2", &segno),
               cleanup, "an incremental segment was checksummable");

   /* Temporary relations, and the files a Mac leaves behind in a backup. */
   MCTF_ASSERT(!pgmoneta_page_checksummable("data/base/16384/pgsql_tmp123.0", &segno), cleanup,
               "a temporary relation was checksummable");
   MCTF_ASSERT(!pgmoneta_page_checksummable("data/base/16384/.DS_Store", &segno), cleanup,
               ".DS_Store was checksummable");

   /* A segment number that does not fit is not a segment number. Truncating it
      would give a plausible looking one, and since the segment is part of every
      block number, that fails every page of a file that is fine. */
   MCTF_ASSERT(!pgmoneta_page_checksummable("data/base/16384/16385.4294967296", &segno),
               cleanup, "an out of range segment number was accepted");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_pagechecksum_verify_file)
{
   char path[] = "/tmp/pgmoneta_pagecheck_XXXXXX";
   char page[BLOCK_SIZE];
   uint32_t blockno = 0;
   uint16_t computed = 0;
   uint16_t stored = 0;
   int number_of_bad = -1;
   int fd = -1;
   FILE* f = NULL;

   fd = mkstemp(path);
   MCTF_ASSERT(fd >= 0, cleanup, "could not create a temporary file");
   f = fdopen(fd, "wb");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "could not open the temporary file");

   /* Three blocks of a relation whose second segment starts at block 4, so the
    * block numbers are 4, 5 and 6 rather than 0, 1 and 2. */
   for (uint32_t b = 0; b < 3; b++)
   {
      fill_page(page);
      page[16] = (char)b;
      store_checksum(page, pgmoneta_page_checksum(page, BLOCK_SIZE, b + 1 * 4));
      MCTF_ASSERT_INT_EQ(fwrite(page, 1, BLOCK_SIZE, f), BLOCK_SIZE, cleanup,
                         "could not write a page");
   }
   fclose(f);
   f = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_page_verify_file(path, BLOCK_SIZE, 4, 1,
                                                &blockno, &computed, &stored, &number_of_bad),
                      0, cleanup, "a good file did not verify");
   MCTF_ASSERT_INT_EQ(number_of_bad, 0, cleanup, "a good file reported bad blocks");

   /* Reading the same file as the first segment makes every block number wrong,
    * which is what happens if the segment is not taken into account. */
   MCTF_ASSERT_INT_EQ(pgmoneta_page_verify_file(path, BLOCK_SIZE, 4, 0,
                                                &blockno, &computed, &stored, &number_of_bad),
                      1, cleanup, "the file verified against the wrong segment");
   MCTF_ASSERT_INT_EQ(number_of_bad, 3, cleanup, "the wrong segment did not fail every block");

   /* A file that cannot be read is reported apart from one whose pages failed:
    * both return 1, but this one has no bad blocks to report. */
   MCTF_ASSERT_INT_EQ(pgmoneta_page_verify_file("/tmp/pgmoneta_does_not_exist", BLOCK_SIZE, 4, 0,
                                                &blockno, &computed, &stored, &number_of_bad),
                      1, cleanup, "a missing file did not fail");
   MCTF_ASSERT_INT_EQ(number_of_bad, 0, cleanup, "a missing file reported bad blocks");

cleanup:
   if (f != NULL)
   {
      fclose(f);
   }
   unlink(path);
   MCTF_FINISH();
}
