/*
 * Copyright (C) 2025 The pgmoneta community
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

#include <art.h>
#include <brt.h>
#include <info.h>
#include <tscommon.h>
#include <tssuite.h>
#include <utils.h>
#include <walfile/wal_reader.h>

static void relation_fork_init(int spcoid, int dboid, int relnum, enum fork_number forknum, struct rel_file_locator* r, enum fork_number* frk);
static void consecutive_mark_block_modified(block_ref_table* brt, struct rel_file_locator* rlocator, enum fork_number frk, block_number blkno, int n);
static void brt_write(block_ref_table* brt);
static void brt_read(block_ref_table** brt);
static char* get_backup_summary_path();

START_TEST(test_pgmoneta_write_multiple_chunks_multiple_representations)
{
   block_ref_table* brt;
   // This test will create a block reference table with multiple chunks
   // and switch from bitmap representation to array representation
   pgmoneta_brt_create_empty(&brt);
   ck_assert_ptr_nonnull(brt);
   // Create a relation fork locator
   struct rel_file_locator rlocator;
   enum fork_number frk;
   relation_fork_init(1663, 234, 345, MAIN_FORKNUM, &rlocator, &frk);

   consecutive_mark_block_modified(brt, &rlocator, frk, 0x123, MAX_ENTRIES_PER_CHUNK + 10);
   consecutive_mark_block_modified(brt, &rlocator, frk, 3 * BLOCKS_PER_CHUNK + 0x123, 1000);
   /* Write to file to switch representation */
   brt_write(brt);
   pgmoneta_brt_destroy(brt);
}
END_TEST
START_TEST(test_pgmoneta_read_chunks)
{
   int size = 4096;
   int nblocks;
   block_number start_blk = 0;
   block_number stop_blk = size;
   block_ref_table* brt = NULL;

   block_number blocks[size];

   brt_read(&brt);

   // Create a relation fork locator
   struct rel_file_locator rlocator;
   enum fork_number frk;
   relation_fork_init(1663, 234, 345, MAIN_FORKNUM, &rlocator, &frk);
   // Check if the blocks are correctly read
   block_ref_table_entry* entry = pgmoneta_brt_get_entry(brt, &rlocator, frk, NULL);
   ck_assert_msg(entry != NULL, "Entry not found in block reference table");

   ck_assert(!pgmoneta_brt_entry_get_blocks(entry, start_blk, stop_blk, blocks, size, &nblocks));
   ck_assert_msg(nblocks > 0, "No blocks found in the specified range");

   pgmoneta_brt_destroy(brt);
}
END_TEST

Suite*
pgmoneta_test_brt_io_suite()
{
   Suite* s;
   TCase* tc_brt_io;
   s = suite_create("pgmoneta_test_brt_io");

   tc_brt_io = tcase_create("test_brt_io");

   tcase_set_timeout(tc_brt_io, 60);
   tcase_add_checked_fixture(tc_brt_io, pgmoneta_test_setup, pgmoneta_test_teardown);
   tcase_add_test(tc_brt_io, test_pgmoneta_write_multiple_chunks_multiple_representations);
   tcase_add_test(tc_brt_io, test_pgmoneta_read_chunks);
   suite_add_tcase(s, tc_brt_io);

   return s;
}

static void
relation_fork_init(int spcoid, int dboid, int relnum, enum fork_number forknum, struct rel_file_locator* r, enum fork_number* frk)
{
   struct rel_file_locator rlocator;
   rlocator.spcOid = spcoid;
   rlocator.dbOid = dboid;
   rlocator.relNumber = relnum;

   *r = rlocator;
   *frk = forknum;
}

static void
consecutive_mark_block_modified(block_ref_table* brt, struct rel_file_locator* rlocator, enum fork_number frk, block_number blkno, int n)
{
   for (int i = 0; i < n; i++)
   {
      ck_assert(!pgmoneta_brt_mark_block_modified(brt, rlocator, frk, blkno + i));
   }
}

static void
brt_write(block_ref_table* brt)
{
   char* r = NULL;
   r = get_backup_summary_path();

   r = pgmoneta_append(r, "tmp.summary");
   ck_assert(!pgmoneta_brt_write(brt, r));

   free(r);
}

static void
brt_read(block_ref_table** brt)
{
   char* r = NULL;

   r = get_backup_summary_path();
   r = pgmoneta_append(r, "tmp.summary");

   ck_assert(!pgmoneta_brt_read(r, brt));
   free(r);
}

static char*
get_backup_summary_path()
{
   return pgmoneta_get_server(PRIMARY_SERVER);
}