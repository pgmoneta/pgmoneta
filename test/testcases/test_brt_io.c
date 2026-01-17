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

#include <art.h>
#include <brt.h>
#include <info.h>
#include <tscommon.h>
#include <mctf.h>
#include <utils.h>
#include <walfile/wal_reader.h>
#include <stdio.h>

static void relation_fork_init(int spcoid, int dboid, int relnum, enum fork_number forknum, struct rel_file_locator* r, enum fork_number* frk);
static int consecutive_mark_block_modified(block_ref_table* brt, struct rel_file_locator* rlocator, enum fork_number frk, block_number blkno, int n);
static int brt_write(block_ref_table* brt);
static int brt_read(block_ref_table** brt);
static char* get_backup_summary_path();

MCTF_TEST(test_pgmoneta_write_multiple_chunks_multiple_representations)
{
   block_ref_table* brt = NULL;
   struct rel_file_locator rlocator;
   enum fork_number frk;

   pgmoneta_test_setup();

   // This test will create a block reference table with multiple chunks
   // and switch from bitmap representation to array representation
   pgmoneta_brt_create_empty(&brt);
   MCTF_ASSERT_PTR_NONNULL(brt, cleanup, "BRT creation failed");

   // Create a relation fork locator
   relation_fork_init(1663, 234, 345, MAIN_FORKNUM, &rlocator, &frk);

   MCTF_ASSERT(!consecutive_mark_block_modified(brt, &rlocator, frk, 0x123, MAX_ENTRIES_PER_CHUNK + 10), cleanup, "Mark modified failed 1");
   MCTF_ASSERT(!consecutive_mark_block_modified(brt, &rlocator, frk, 3 * BLOCKS_PER_CHUNK + 0x123, 1000), cleanup, "Mark modified failed 2");

   // Write to file to switch representation

   MCTF_ASSERT(!brt_write(brt), cleanup, "BRT write failed");

cleanup:
   pgmoneta_brt_destroy(brt);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_read_chunks)
{
   int size = 4096;
   int nblocks = 0;
   block_number start_blk = 0;
   block_number stop_blk = size;
   block_ref_table* brt = NULL;
   block_number blocks[size];
   struct rel_file_locator rlocator;
   enum fork_number frk;
   block_ref_table_entry* entry = NULL;

   pgmoneta_test_setup();

   // Create a relation fork locator
   relation_fork_init(1663, 234, 345, MAIN_FORKNUM, &rlocator, &frk);

   // Setup: Create and write the BRT (mimicking the write test) so we have something to read
   pgmoneta_brt_create_empty(&brt);
   consecutive_mark_block_modified(brt, &rlocator, frk, 0x123, MAX_ENTRIES_PER_CHUNK + 10);
   consecutive_mark_block_modified(brt, &rlocator, frk, 3 * BLOCKS_PER_CHUNK + 0x123, 1000);
   brt_write(brt);
   pgmoneta_brt_destroy(brt);
   brt = NULL;

   MCTF_ASSERT(!brt_read(&brt), cleanup, "BRT read failed");

   // Check if the blocks are correctly read
   entry = pgmoneta_brt_get_entry(brt, &rlocator, frk, NULL);
   MCTF_ASSERT_PTR_NONNULL(entry, cleanup, "Entry not found in block reference table");

   MCTF_ASSERT(!pgmoneta_brt_entry_get_blocks(entry, start_blk, stop_blk, blocks, size, &nblocks), cleanup, "Get blocks failed");
   MCTF_ASSERT(nblocks > 0, cleanup, "No blocks found in the specified range");

cleanup:
   pgmoneta_brt_destroy(brt);
   pgmoneta_test_teardown();
   MCTF_FINISH();
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

static int
consecutive_mark_block_modified(block_ref_table* brt, struct rel_file_locator* rlocator, enum fork_number frk, block_number blkno, int n)
{
   for (int i = 0; i < n; i++)
   {
      if (pgmoneta_brt_mark_block_modified(brt, rlocator, frk, blkno + i))
      {
         return 1;
      }
   }
   return 0;
}

static int
brt_write(block_ref_table* brt)
{
   char* r = NULL;
   int ret;

   r = get_backup_summary_path();
   r = pgmoneta_append(r, "tmp.summary");

   ret = pgmoneta_brt_write(brt, r);

   free(r);
   return ret;
}

static int
brt_read(block_ref_table** brt)
{
   char* r = NULL;
   int ret;

   r = get_backup_summary_path();
   r = pgmoneta_append(r, "tmp.summary");

   ret = pgmoneta_brt_read(r, brt);
   free(r);
   return ret;
}

static char*
get_backup_summary_path()
{
   return pgmoneta_get_server(PRIMARY_SERVER);
}