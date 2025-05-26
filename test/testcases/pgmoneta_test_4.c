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
#include <tsclient.h>
#include <wal.h>
#include <walfile/wal_reader.h>

#include "pgmoneta_test_4.h"

START_TEST(test_pgmoneta_write_multiple_chunks_multiple_representations)
{
   int found = 0;
   block_ref_table* brt;
   // This test will create a block reference table with multiple chunks
   // and switch from bitmap representation to array representation
   pgmoneta_brt_create_empty(&brt);
   if (brt == NULL)
   {
      goto done;
   }
   // Create a relation fork locator
   struct rel_file_locator rlocator;
   enum fork_number frk;
   pgmoneta_tsclient_relation_fork_init(1663, 234, 345, MAIN_FORKNUM, &rlocator, &frk);

   if (pgmoneta_tsclient_execute_consecutive_mark_block_modified(brt, &rlocator, frk, 0x123, MAX_ENTRIES_PER_CHUNK + 10))
   {
      goto done;
   }
   if (pgmoneta_tsclient_execute_consecutive_mark_block_modified(brt, &rlocator, frk, 3 * BLOCKS_PER_CHUNK + 0x123, 1000))
   {
      goto done;
   }
   /* Write to file to switch representation */
   if (pgmoneta_tsclient_write(brt))
   {
      goto done;
   }

   found = 1;
done:
   pgmoneta_brt_destroy(brt);
   ck_assert_msg(found, "success status not found");
}
END_TEST
START_TEST(test_pgmoneta_read_summary_get_blocks)
{
   int found = 0;
   int size = 4096;
   int nblocks;
   block_number start_blk = 0;
   block_number stop_blk = size;
   block_ref_table* brt = NULL;

   block_number blocks[size];

   if (pgmoneta_tsclient_read(&brt))
   {
      goto done;
   }

   // Create a relation fork locator
   struct rel_file_locator rlocator;
   enum fork_number frk;
   pgmoneta_tsclient_relation_fork_init(1663, 234, 345, MAIN_FORKNUM, &rlocator, &frk);
   // Check if the blocks are correctly read
   block_ref_table_entry* entry = pgmoneta_brt_get_entry(brt, &rlocator, frk, NULL);
   ck_assert_msg(entry != NULL, "Entry not found in block reference table");

   if (pgmoneta_brt_entry_get_blocks(entry, start_blk, stop_blk, blocks, size, &nblocks))
   {
      goto done;
   }
   ck_assert_msg(nblocks > 0, "No blocks found in the specified range");

   found = 1;

done:
   pgmoneta_brt_destroy(brt);
   ck_assert_msg(found, "success status not found");
}
END_TEST

Suite*
pgmoneta_test4_suite()
{
   Suite* s;
   TCase* tc_core;
   s = suite_create("pgmoneta_test4");

   tc_core = tcase_create("Core");

   tcase_set_timeout(tc_core, 60);
   tcase_add_test(tc_core, test_pgmoneta_write_multiple_chunks_multiple_representations);
   tcase_add_test(tc_core, test_pgmoneta_read_summary_get_blocks);
   suite_add_tcase(s, tc_core);

   return s;
}