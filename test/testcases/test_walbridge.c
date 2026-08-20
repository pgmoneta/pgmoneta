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

/* pgmoneta */
#include <pgmoneta.h>
#include <deque.h>
#include <logging.h>
#include <shmem.h>
#include <utils.h>
#include <walfile.h>
#include <walfile/wal_reader.h>
#include <walfile/rmgr.h>
#include <walfile/rm_heap.h>
#include <walfile/rm_mxact.h>
#include <walfile/rm_gist.h>
#include <walfile/pg_control.h>
#include <walbridge/lsn_map.h>
#include <walbridge/migration_engine.h>
#include <walbridge/wal_store.h>
#include <mctf.h>
#include <tscommon.h>

/* system */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* PG19 visibility map flags (defined locally; also in migration_engine.c) */
#define XLHP_VM_ALL_VISIBLE (1 << 8)
#define XLHP_VM_ALL_FROZEN  (1 << 9)
/* PG18 visibility map flags */
#define VM_ALL_VISIBLE      0x01
#define VM_ALL_FROZEN       0x02
#define VM_XLOG_CATALOG_REL 0x04

/* PG19 CheckPoint wire layout (mirrors struct check_point_v19 in
 * migration_engine.c) */
struct walbridge_checkpoint_v19
{
   xlog_rec_ptr redo;
   timeline_id this_timeline_id;
   timeline_id prev_timeline_id;
   bool full_page_writes;
   int wal_level;
   bool logical_decoding_enabled;
   struct full_transaction_id next_xid;
   oid next_oid;
   multi_xact_id next_multi;
   uint64_t next_multi_offset;
   transaction_id oldest_xid;
   oid oldest_xid_db;
   multi_xact_id oldest_multi;
   oid oldest_multi_db;
   pg_time_t time;
   transaction_id oldest_commit_ts_xid;
   transaction_id newest_commit_ts_xid;
   transaction_id oldest_active_xid;
   uint32_t data_checksum_state;
};

static bool test_shmem_allocated = false;

MCTF_MODULE_SETUP(walbridge)
{
   if (shmem == NULL)
   {
      pgmoneta_create_shared_memory(sizeof(struct main_configuration), HUGEPAGE_OFF, &shmem);
      memset(shmem, 0, sizeof(struct main_configuration));
      test_shmem_allocated = true;
   }
}

MCTF_MODULE_TEARDOWN(walbridge)
{
   if (test_shmem_allocated && shmem != NULL)
   {
      pgmoneta_destroy_shared_memory(shmem, sizeof(struct main_configuration));
      shmem = NULL;
      test_shmem_allocated = false;
   }
}

static struct decoded_xlog_record*
create_decoded_record(uint8_t rmid, uint8_t info, uint64_t lsn, void* main_data, uint32_t main_data_len)
{
   struct decoded_xlog_record* rec = calloc(1, sizeof(struct decoded_xlog_record));
   if (rec == NULL)
   {
      return NULL;
   }

   rec->header.xl_rmid = rmid;
   rec->header.xl_info = info;
   rec->header.xl_xid = 1;
   rec->lsn = lsn;
   rec->max_block_id = -1;

   if (main_data_len > 0)
   {
      rec->main_data = malloc(main_data_len);
      if (rec->main_data == NULL)
      {
         free(rec);
         return NULL;
      }
      memcpy(rec->main_data, main_data, main_data_len);
      rec->main_data_len = main_data_len;
   }

   return rec;
}

static void
destroy_decoded_record(struct decoded_xlog_record* rec)
{
   if (rec == NULL)
   {
      return;
   }
   for (int i = 0; i <= rec->max_block_id; i++)
   {
      if (rec->blocks[i].data)
      {
         free(rec->blocks[i].data);
      }
      if (rec->blocks[i].bkp_image)
      {
         free(rec->blocks[i].bkp_image);
      }
   }
   free(rec->main_data);
   free(rec);
}

MCTF_TEST(test_walbridge_lsn_map_at_or_before)
{
   struct lsn_map* map = NULL;
   char path[PATH_MAX];
   uint64_t up = 0;
   uint64_t down = 0;

   pgmoneta_snprintf(path, sizeof(path), "%s", "/tmp/walbridge_lsnmap_test.map");
   unlink(path);

   MCTF_ASSERT_INT_EQ(lsn_map_create(path, &map), 0, cleanup, "lsn_map_create failed");

   MCTF_ASSERT_INT_EQ(lsn_map_put(map, 0x1000, 40), 0, cleanup, "put 1 failed");
   MCTF_ASSERT_INT_EQ(lsn_map_put(map, 0x2000, 72), 0, cleanup, "put 2 failed");

   /* exact match */
   MCTF_ASSERT_INT_EQ(lsn_map_get_downstream(map, 0x2000, &down), 0, cleanup, "exact lookup failed");
   MCTF_ASSERT_INT_EQ(down, 72, cleanup, "exact lookup returned wrong downstream");

   /* at-or-before */
   MCTF_ASSERT_INT_EQ(lsn_map_get_downstream_at_or_before(map, 0x2500, &up, &down), 0, cleanup, "at-or-before failed");
   MCTF_ASSERT_INT_EQ(up, 0x2000, cleanup, "at-or-before returned wrong upstream");
   MCTF_ASSERT_INT_EQ(down, 72, cleanup, "at-or-before returned wrong downstream");

   MCTF_ASSERT_INT_EQ(lsn_map_get_downstream_at_or_before(map, 0x0800, &up, &down), 1, cleanup, "no entry before requested should fail");

cleanup:
   lsn_map_destroy(map);
   unlink(path);
   MCTF_FINISH();
}

MCTF_TEST(test_walbridge_migration_heap2_prune)
{
   struct decoded_xlog_record* rec = NULL;
   uint8_t data[2] = {0xAA, 0xBB};

   rec = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x1000, data, sizeof(data));
   MCTF_ASSERT_PTR_NONNULL(rec, cleanup, "create record failed");

   MCTF_ASSERT_INT_EQ(migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL), 0, cleanup, "translate failed");

   MCTF_ASSERT_INT_EQ(rec->main_data_len, 2, cleanup, "prune record size must be unchanged");
   MCTF_ASSERT_INT_EQ(((uint8_t*)rec->main_data)[0], 0xBB, cleanup, "flags byte must move to byte 0");
   MCTF_ASSERT_INT_EQ(((uint8_t*)rec->main_data)[1], 0x00, cleanup, "reason byte must become zero");

cleanup:
   destroy_decoded_record(rec);
   MCTF_FINISH();
}

MCTF_TEST(test_walbridge_migration_heap2_visible)
{
   struct decoded_xlog_record* rec = NULL;
   transaction_id conflict = 5;
   uint8_t data[5];
   uint16_t flags = 0;

   memcpy(data, &conflict, sizeof(transaction_id));
   data[4] = VM_ALL_FROZEN | VM_XLOG_CATALOG_REL;

   rec = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_VISIBLE, 0x1000, data, sizeof(data));
   MCTF_ASSERT_PTR_NONNULL(rec, cleanup, "create record failed");

   /* block 0 = visibility map, block 1 = heap page (PG18 layout) */
   rec->max_block_id = 1;
   rec->blocks[0].in_use = true;
   rec->blocks[0].forknum = VISIBILITYMAP_FORKNUM;
   rec->blocks[0].blkno = 7;
   rec->blocks[1].in_use = true;
   rec->blocks[1].forknum = MAIN_FORKNUM;
   rec->blocks[1].blkno = 42;
   rec->blocks[1].flags = BKPBLOCK_SAME_REL | MAIN_FORKNUM;

   MCTF_ASSERT_INT_EQ(migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL), 0, cleanup, "translate failed");

   /* becomes a vacuum cleanup prune record with the heap as block 0 */
   MCTF_ASSERT_INT_EQ((int)(rec->header.xl_info & ~XLR_INFO_MASK), XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, cleanup, "info not rewritten to prune vacuum cleanup");
   MCTF_ASSERT_INT_EQ((int)rec->blocks[0].forknum, (int)MAIN_FORKNUM, cleanup, "block 0 must be the heap page");
   MCTF_ASSERT_INT_EQ((int)(rec->blocks[0].flags & BKPBLOCK_SAME_REL), 0, cleanup, "block 0 must not use SAME_REL");
   MCTF_ASSERT_INT_EQ((int)rec->blocks[1].forknum, (int)VISIBILITYMAP_FORKNUM, cleanup, "block 1 must be the visibility map");
   MCTF_ASSERT_INT_EQ((int)rec->blocks[0].blkno, 42, cleanup, "heap block number lost");
   MCTF_ASSERT_INT_EQ((int)rec->blocks[1].blkno, 7, cleanup, "vm block number lost");

   /* flags(2) + conflict horizon(4) */
   MCTF_ASSERT_INT_EQ(rec->main_data_len, 6, cleanup, "visible -> prune main data size wrong");
   memcpy(&flags, rec->main_data, sizeof(uint16_t));
   MCTF_ASSERT_INT_EQ((int)(flags & (XLHP_VM_ALL_VISIBLE | XLHP_VM_ALL_FROZEN | XLHP_IS_CATALOG_REL | XLHP_HAS_CONFLICT_HORIZON)),
                      (int)(XLHP_VM_ALL_VISIBLE | XLHP_VM_ALL_FROZEN | XLHP_IS_CATALOG_REL | XLHP_HAS_CONFLICT_HORIZON),
                      cleanup, "synthesized prune flags wrong");

cleanup:
   destroy_decoded_record(rec);
   MCTF_FINISH();
}

MCTF_TEST(test_walbridge_migration_checkpoint)
{
   struct decoded_xlog_record* rec = NULL;
   struct check_point_v17 v17;
   struct walbridge_checkpoint_v19 v19;

   memset(&v17, 0, sizeof(v17));
   v17.redo = 0x123456789ABCDEF0ULL;
   v17.this_timeline_id = 1;
   v17.prev_timeline_id = 1;
   v17.full_page_writes = true;
   v17.wal_level = 2; /* WAL_LEVEL_REPLICA */
   v17.next_multi = 200;
   v17.next_multi_offset = 0x12345678;
   v17.oldest_xid = 400;

   rec = create_decoded_record(RM_XLOG_ID, XLOG_CHECKPOINT_ONLINE, 0x1000, &v17, sizeof(v17));
   MCTF_ASSERT_PTR_NONNULL(rec, cleanup, "create record failed");

   MCTF_ASSERT_INT_EQ(migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL), 0, cleanup, "translate failed");

   MCTF_ASSERT_INT_EQ(rec->main_data_len, 96, cleanup, "checkpoint must grow from 80 to 96 bytes");

   memcpy(&v19, rec->main_data, sizeof(v19));
   MCTF_ASSERT_INT_EQ((int)v19.redo, (int)v17.redo, cleanup, "redo not preserved");
   MCTF_ASSERT_INT_EQ(v19.next_multi, v17.next_multi, cleanup, "next_multi not preserved");
   MCTF_ASSERT_INT_EQ(v19.next_multi_offset, (uint64_t)v17.next_multi_offset, cleanup, "next_multi_offset not preserved");
   MCTF_ASSERT_INT_EQ(v19.data_checksum_state, 0, cleanup, "data checksum state must be off for PoC");

cleanup:
   destroy_decoded_record(rec);
   MCTF_FINISH();
}

MCTF_TEST(test_walbridge_migration_multixact_create)
{
   struct decoded_xlog_record* rec = NULL;
   uint8_t data[20];
   uint32_t mid = 77;
   uint32_t moff32 = 0x87654321;
   int32_t nmembers = 1;
   uint64_t moff64 = 0;
   uint64_t member = 0x1122334455667788ULL;

   memset(data, 0, sizeof(data));
   memcpy(data, &mid, 4);
   memcpy(data + 4, &moff32, 4);
   memcpy(data + 8, &nmembers, 4);
   memcpy(data + 12, &member, 8);

   rec = create_decoded_record(RM_MULTIXACT_ID, XLOG_MULTIXACT_CREATE_ID, 0x1000, data, sizeof(data));
   MCTF_ASSERT_PTR_NONNULL(rec, cleanup, "create record failed");

   MCTF_ASSERT_INT_EQ(migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL), 0, cleanup, "translate failed");

   /* +4 pad +4 widened offset */
   MCTF_ASSERT_INT_EQ(rec->main_data_len, 28, cleanup, "multixact create must grow by 8 bytes");

   memcpy(&mid, rec->main_data, 4);
   memcpy(&moff64, rec->main_data + 8, 8);
   memcpy(&member, rec->main_data + 20, 8);

   MCTF_ASSERT_INT_EQ(mid, 77, cleanup, "mid not preserved");
   MCTF_ASSERT_INT_EQ(moff64, (uint64_t)moff32, cleanup, "offset must widen 32 -> 64 bit");
   MCTF_ASSERT_INT_EQ(member, 0x1122334455667788ULL, cleanup, "member data not preserved");

cleanup:
   destroy_decoded_record(rec);
   MCTF_FINISH();
}

MCTF_TEST(test_walbridge_migration_multixact_truncate)
{
   struct decoded_xlog_record* rec = NULL;
   uint8_t data[20];
   uint32_t db = 5;
   uint32_t start_off = 100;
   uint32_t end_off = 200;
   uint32_t start_memb = 1000;
   uint32_t end_memb = 2000;
   uint32_t out_db = 0;
   uint32_t out_multi = 0;
   uint64_t out_offset = 0;

   memset(data, 0, sizeof(data));
   memcpy(data, &db, 4);
   memcpy(data + 4, &start_off, 4);
   memcpy(data + 8, &end_off, 4);
   memcpy(data + 12, &start_memb, 4);
   memcpy(data + 16, &end_memb, 4);

   rec = create_decoded_record(RM_MULTIXACT_ID, XLOG_MULTIXACT_TRUNCATE_ID, 0x1000, data, sizeof(data));
   MCTF_ASSERT_PTR_NONNULL(rec, cleanup, "create record failed");

   MCTF_ASSERT_INT_EQ(migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL), 0, cleanup, "translate failed");

   MCTF_ASSERT_INT_EQ(rec->main_data_len, 16, cleanup, "multixact truncate must shrink from 20 to 16 bytes");

   memcpy(&out_db, rec->main_data, 4);
   memcpy(&out_multi, rec->main_data + 4, 4);
   memcpy(&out_offset, rec->main_data + 8, 8);

   MCTF_ASSERT_INT_EQ(out_db, 5, cleanup, "oldest multi db not preserved");
   MCTF_ASSERT_INT_EQ(out_multi, 200, cleanup, "truncation offset not preserved");
   MCTF_ASSERT_INT_EQ(out_offset, (uint64_t)2000, cleanup, "truncation member offset not preserved");

cleanup:
   destroy_decoded_record(rec);
   MCTF_FINISH();
}

MCTF_TEST(test_walbridge_migration_gist)
{
   struct decoded_xlog_record* rec = NULL;
   uint32_t dummy = 0;

   rec = create_decoded_record(RM_GIST_ID, XLOG_GIST_ASSIGN_LSN, 0x1000, &dummy, sizeof(dummy));
   MCTF_ASSERT_PTR_NONNULL(rec, cleanup, "create record failed");

   MCTF_ASSERT_INT_EQ(migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL), 0, cleanup, "translate failed");

   MCTF_ASSERT_INT_EQ((int)rec->header.xl_rmid, (int)RM_XLOG_ID, cleanup, "gist assign lsn must become RM_XLOG_ID");
   MCTF_ASSERT_INT_EQ((int)(rec->header.xl_info & ~XLR_INFO_MASK), XLOG_ASSIGN_LSN, cleanup, "gist assign lsn must become XLOG_ASSIGN_LSN");

cleanup:
   destroy_decoded_record(rec);
   MCTF_FINISH();
}

MCTF_TEST(test_walbridge_store_roundtrip)
{
   struct lsn_map* map = NULL;
   struct wal_store* store = NULL;
   struct decoded_xlog_record* rec1 = NULL;
   struct decoded_xlog_record* rec2 = NULL;
   struct walfile* wf = NULL;
   struct deque_iterator* iter = NULL;
   char base[PATH_MAX];
   char map_path[PATH_MAX];
   char downstream_dir[PATH_MAX];
   char seg_path[PATH_MAX] = "";
   uint64_t down1 = 0;
   uint64_t down2 = 0;
   uint8_t prune[2] = {0x01, 0x07};
   int wal_level = 2;
   int count = 0;

   pgmoneta_snprintf(base, sizeof(base), "%s", "/tmp/walbridge_test_store");
   pgmoneta_mkdir(base);
   pgmoneta_snprintf(map_path, sizeof(map_path), "%s/lsn.map", base);
   pgmoneta_snprintf(downstream_dir, sizeof(downstream_dir), "%s/wal", base);

   unlink(map_path);
   pgmoneta_snprintf(downstream_dir, sizeof(downstream_dir), "%s/wal", base);

   MCTF_ASSERT_INT_EQ(lsn_map_create(map_path, &map), 0, cleanup, "lsn_map_create failed");
   MCTF_ASSERT_INT_EQ(wal_store_create(downstream_dir, map, 123456789, DEFAULT_WAL_SEGZ_BYTES, 8192, 1, &store), 0, cleanup, "wal_store_create failed");

   /* heap prune record: { reason, flags } -> { flags, 0 } */
   rec1 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x10000000, prune, sizeof(prune));
   MCTF_ASSERT_PTR_NONNULL(rec1, cleanup, "create rec1 failed");
   MCTF_ASSERT_INT_EQ(migration_engine_translate(rec1, WAL_MAGIC_V18, WAL_MAGIC_V19, map), 0, cleanup, "translate rec1 failed");
   MCTF_ASSERT_INT_EQ(wal_store_write_record(store, rec1), 0, cleanup, "write rec1 failed");
   MCTF_ASSERT_INT_EQ(wal_store_sync_partial_page(store), 0, cleanup, "sync rec1 failed");

   /* checkpoint redo record: { wal_level } -> { wal_level, checksum_version } */
   rec2 = create_decoded_record(RM_XLOG_ID, XLOG_CHECKPOINT_REDO, 0x20000000, &wal_level, sizeof(wal_level));
   MCTF_ASSERT_PTR_NONNULL(rec2, cleanup, "create rec2 failed");
   MCTF_ASSERT_INT_EQ(migration_engine_translate(rec2, WAL_MAGIC_V18, WAL_MAGIC_V19, map), 0, cleanup, "translate rec2 failed");
   MCTF_ASSERT_INT_EQ(wal_store_write_record(store, rec2), 0, cleanup, "write rec2 failed");

   MCTF_ASSERT_INT_EQ(wal_store_flush(store), 0, cleanup, "flush failed");

   /* first record must start after the 40-byte long page header */
   MCTF_ASSERT_INT_EQ(lsn_map_get_downstream(map, 0x10000000, &down1), 0, cleanup, "map lookup 1 failed");
   MCTF_ASSERT_INT_EQ(down1, 40, cleanup, "first record must start at LSN 40");
   MCTF_ASSERT_INT_EQ(lsn_map_get_downstream(map, 0x20000000, &down2), 0, cleanup, "map lookup 2 failed");
   MCTF_ASSERT_INT_EQ(down2, down1 + MAXALIGN(28), cleanup, "second record must follow aligned first record");

   /* parse the downstream segment back */
   pgmoneta_snprintf(seg_path, sizeof(seg_path), "%s/%s", downstream_dir, "000000010000000000000000");

   wf = calloc(1, sizeof(*wf));
   MCTF_ASSERT_PTR_NONNULL(wf, cleanup, "calloc wf failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_deque_create(false, &wf->records), 0, cleanup, "create records deque failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_deque_create(false, &wf->page_headers), 0, cleanup, "create page_headers deque failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_parse_wal_file(seg_path, -1, wf), 0, cleanup, "could not parse downstream segment");

   MCTF_ASSERT_INT_EQ(pgmoneta_deque_iterator_create(wf->records, &iter), 0, cleanup, "iterator create failed");
   while (pgmoneta_deque_iterator_next(iter))
   {
      struct decoded_xlog_record* rec = (struct decoded_xlog_record*)iter->value->data;
      char* encoded = NULL;
      uint32_t total_len;

      if (!rec || rec->partial)
      {
         continue;
      }
      count++;

      encoded = pgmoneta_wal_encode_xlog_record(rec, WAL_MAGIC_V19, NULL);
      MCTF_ASSERT_PTR_NONNULL(encoded, cleanup, "encode parsed record failed");
      total_len = ((struct xlog_record*)encoded)->xl_tot_len;

      /* the CRC stored in the segment must match a fresh computation */
      MCTF_ASSERT_INT_EQ(rec->header.xl_crc, wal_store_compute_crc(encoded, total_len), cleanup, "stored record CRC mismatch");
      free(encoded);

      if (count == 1)
      {
         MCTF_ASSERT_INT_EQ(rec->lsn, down1, cleanup, "parsed rec1 lsn mismatch");
         MCTF_ASSERT_INT_EQ(rec->header.xl_prev, 0, cleanup, "first record xl_prev must be 0");
         MCTF_ASSERT_INT_EQ(((uint8_t*)rec->main_data)[0], 0x07, cleanup, "rec1 flags not translated");
      }
      else if (count == 2)
      {
         MCTF_ASSERT_INT_EQ(rec->lsn, down2, cleanup, "parsed rec2 lsn mismatch");
         MCTF_ASSERT_INT_EQ(rec->header.xl_prev, down1, cleanup, "second record must chain to first");
         MCTF_ASSERT_INT_EQ(rec->main_data_len, 8, cleanup, "checkpoint redo main data must be 8 bytes");
      }
   }
   pgmoneta_deque_iterator_destroy(iter);
   iter = NULL;

   MCTF_ASSERT_INT_EQ(count, 2, cleanup, "expected 2 records in downstream segment");

cleanup:
   if (iter)
   {
      pgmoneta_deque_iterator_destroy(iter);
   }
   if (wf)
   {
      pgmoneta_destroy_walfile(wf);
      wf = NULL;
   }
   destroy_decoded_record(rec1);
   destroy_decoded_record(rec2);
   if (store)
   {
      wal_store_destroy(store);
   }
   lsn_map_destroy(map);
   unlink(map_path);
   if (seg_path[0])
   {
      unlink(seg_path);
   }
   rmdir(downstream_dir);
   rmdir(base);
   MCTF_FINISH();
}

MCTF_TEST(test_walbridge_store_header_crossing)
{
   struct lsn_map* map = NULL;
   struct wal_store* store = NULL;
   struct decoded_xlog_record* rec1 = NULL;
   struct decoded_xlog_record* rec2 = NULL;
   struct decoded_xlog_record* rec3 = NULL;
   struct walfile* wf = NULL;
   struct deque_iterator* iter = NULL;
   char base[PATH_MAX];
   char map_path[PATH_MAX];
   char downstream_dir[PATH_MAX];
   char seg_path[PATH_MAX] = "";
   uint8_t prune[2] = {0x01, 0x07};
   uint8_t* big = NULL;
   uint32_t big_len = 8111;
   uint64_t down1 = 0;
   uint64_t down2 = 0;
   uint64_t down3 = 0;
   int count = 0;

   pgmoneta_snprintf(base, sizeof(base), "%s", "/tmp/walbridge_test_cross");
   pgmoneta_mkdir(base);
   pgmoneta_snprintf(map_path, sizeof(map_path), "%s/lsn.map", base);
   pgmoneta_snprintf(downstream_dir, sizeof(downstream_dir), "%s/wal", base);

   unlink(map_path);

   big = malloc(big_len);
   MCTF_ASSERT_PTR_NONNULL(big, cleanup, "alloc big payload failed");
   memset(big, 0x5A, big_len);

   MCTF_ASSERT_INT_EQ(lsn_map_create(map_path, &map), 0, cleanup, "lsn_map_create failed");
   MCTF_ASSERT_INT_EQ(wal_store_create(downstream_dir, map, 123456789, DEFAULT_WAL_SEGZ_BYTES, 8192, 1, &store), 0, cleanup, "wal_store_create failed");

   rec1 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x10000000, big, big_len);
   MCTF_ASSERT_PTR_NONNULL(rec1, cleanup, "create rec1 failed");
   MCTF_ASSERT_INT_EQ(migration_engine_translate(rec1, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL), 0, cleanup, "translate rec1 failed");
   MCTF_ASSERT_INT_EQ(wal_store_write_record(store, rec1), 0, cleanup, "write rec1 failed");
   MCTF_ASSERT_INT_EQ(wal_store_sync_partial_page(store), 0, cleanup, "sync rec1 failed");

   /* 26-byte record at 8184: only 8 bytes fit on page 0, header splits */
   rec2 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x20000000, prune, sizeof(prune));
   MCTF_ASSERT_PTR_NONNULL(rec2, cleanup, "create rec2 failed");
   MCTF_ASSERT_INT_EQ(migration_engine_translate(rec2, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL), 0, cleanup, "translate rec2 failed");
   MCTF_ASSERT_INT_EQ(wal_store_write_record(store, rec2), 0, cleanup, "write rec2 failed");

   /* third record must start right after the continuation page data */
   rec3 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x30000000, prune, sizeof(prune));
   MCTF_ASSERT_PTR_NONNULL(rec3, cleanup, "create rec3 failed");
   MCTF_ASSERT_INT_EQ(migration_engine_translate(rec3, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL), 0, cleanup, "translate rec3 failed");
   MCTF_ASSERT_INT_EQ(wal_store_write_record(store, rec3), 0, cleanup, "write rec3 failed");

   MCTF_ASSERT_INT_EQ(wal_store_flush(store), 0, cleanup, "flush failed");

   MCTF_ASSERT_INT_EQ(lsn_map_get_downstream(map, 0x10000000, &down1), 0, cleanup, "map lookup 1 failed");
   MCTF_ASSERT_INT_EQ(down1, 40, cleanup, "first record must start at LSN 40");
   MCTF_ASSERT_INT_EQ(lsn_map_get_downstream(map, 0x20000000, &down2), 0, cleanup, "map lookup 2 failed");
   MCTF_ASSERT_INT_EQ(down2, 8184, cleanup, "second record must start 8 bytes before page end");
   MCTF_ASSERT_INT_EQ(lsn_map_get_downstream(map, 0x30000000, &down3), 0, cleanup, "map lookup 3 failed");
   MCTF_ASSERT_INT_EQ(down3, 8192 + 48, cleanup, "third record must start after the continuation (0x2030)");

   /* parse the downstream segment back; all three records must be found */
   pgmoneta_snprintf(seg_path, sizeof(seg_path), "%s/%s", downstream_dir, "000000010000000000000000");

   wf = calloc(1, sizeof(*wf));
   MCTF_ASSERT_PTR_NONNULL(wf, cleanup, "calloc wf failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_deque_create(false, &wf->records), 0, cleanup, "create records deque failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_deque_create(false, &wf->page_headers), 0, cleanup, "create page_headers deque failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_parse_wal_file(seg_path, -1, wf), 0, cleanup, "could not parse downstream segment");

   MCTF_ASSERT_INT_EQ(pgmoneta_deque_iterator_create(wf->records, &iter), 0, cleanup, "iterator create failed");
   while (pgmoneta_deque_iterator_next(iter))
   {
      struct decoded_xlog_record* rec = (struct decoded_xlog_record*)iter->value->data;
      char* encoded = NULL;
      uint32_t total_len;

      if (!rec || rec->partial)
      {
         continue;
      }
      count++;

      encoded = pgmoneta_wal_encode_xlog_record(rec, WAL_MAGIC_V19, NULL);
      MCTF_ASSERT_PTR_NONNULL(encoded, cleanup, "encode parsed record failed");
      total_len = ((struct xlog_record*)encoded)->xl_tot_len;

      MCTF_ASSERT_INT_EQ(rec->header.xl_crc, wal_store_compute_crc(encoded, total_len), cleanup, "stored record CRC mismatch");
      free(encoded);

      if (count == 1)
      {
         MCTF_ASSERT_INT_EQ(rec->lsn, down1, cleanup, "parsed rec1 lsn mismatch");
         MCTF_ASSERT_INT_EQ(rec->header.xl_prev, 0, cleanup, "first record xl_prev must be 0");
         MCTF_ASSERT_INT_EQ(rec->next_lsn, down2, cleanup, "rec1 next_lsn mismatch");
      }
      else if (count == 2)
      {
         MCTF_ASSERT_INT_EQ(rec->lsn, down2, cleanup, "parsed rec2 (header-crossing) lsn mismatch");
         MCTF_ASSERT_INT_EQ(rec->header.xl_prev, down1, cleanup, "second record must chain to first");
         MCTF_ASSERT_INT_EQ(rec->next_lsn, down3, cleanup, "rec2 next_lsn mismatch");
      }
      else if (count == 3)
      {
         MCTF_ASSERT_INT_EQ(rec->lsn, down3, cleanup, "parsed rec3 lsn mismatch");
         MCTF_ASSERT_INT_EQ(rec->header.xl_prev, down2, cleanup, "third record must chain to the cross-page record");
      }
   }
   pgmoneta_deque_iterator_destroy(iter);
   iter = NULL;

   MCTF_ASSERT_INT_EQ(count, 3, cleanup, "expected 3 records in downstream segment");

cleanup:
   if (iter)
   {
      pgmoneta_deque_iterator_destroy(iter);
   }
   if (wf)
   {
      pgmoneta_destroy_walfile(wf);
      wf = NULL;
   }
   destroy_decoded_record(rec1);
   destroy_decoded_record(rec2);
   destroy_decoded_record(rec3);
   free(big);
   if (store)
   {
      wal_store_destroy(store);
   }
   lsn_map_destroy(map);
   unlink(map_path);
   if (seg_path[0])
   {
      unlink(seg_path);
   }
   rmdir(downstream_dir);
   rmdir(base);
   MCTF_FINISH();
}

MCTF_TEST(test_walbridge_store_page_boundary)
{
   struct lsn_map* map = NULL;
   struct wal_store* store = NULL;
   struct decoded_xlog_record* rec1 = NULL;
   struct decoded_xlog_record* rec2 = NULL;
   struct decoded_xlog_record* rec3 = NULL;
   struct walfile* wf = NULL;
   struct deque_iterator* iter = NULL;
   char base[PATH_MAX];
   char map_path[PATH_MAX];
   char downstream_dir[PATH_MAX];
   char seg_path[PATH_MAX] = "";
   uint8_t prune[2] = {0x01, 0x07};
   uint8_t* big = NULL;
   uint32_t big_len = 8119;
   uint64_t down1 = 0;
   uint64_t down2 = 0;
   uint64_t down3 = 0;
   int count = 0;

   pgmoneta_snprintf(base, sizeof(base), "%s", "/tmp/walbridge_test_boundary");
   pgmoneta_mkdir(base);
   pgmoneta_snprintf(map_path, sizeof(map_path), "%s/lsn.map", base);
   pgmoneta_snprintf(downstream_dir, sizeof(downstream_dir), "%s/wal", base);

   unlink(map_path);

   big = malloc(big_len);
   MCTF_ASSERT_PTR_NONNULL(big, cleanup, "alloc big payload failed");
   memset(big, 0x5A, big_len);

   MCTF_ASSERT_INT_EQ(lsn_map_create(map_path, &map), 0, cleanup, "lsn_map_create failed");
   MCTF_ASSERT_INT_EQ(wal_store_create(downstream_dir, map, 123456789, DEFAULT_WAL_SEGZ_BYTES, 8192, 1, &store), 0, cleanup, "wal_store_create failed");

   rec1 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x10000000, big, big_len);
   MCTF_ASSERT_PTR_NONNULL(rec1, cleanup, "create rec1 failed");
   MCTF_ASSERT_INT_EQ(migration_engine_translate(rec1, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL), 0, cleanup, "translate rec1 failed");
   MCTF_ASSERT_INT_EQ(wal_store_write_record(store, rec1), 0, cleanup, "write rec1 failed");
   MCTF_ASSERT_INT_EQ(wal_store_sync_partial_page(store), 0, cleanup, "sync rec1 failed");

   rec2 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x20000000, prune, sizeof(prune));
   MCTF_ASSERT_PTR_NONNULL(rec2, cleanup, "create rec2 failed");
   MCTF_ASSERT_INT_EQ(migration_engine_translate(rec2, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL), 0, cleanup, "translate rec2 failed");
   MCTF_ASSERT_INT_EQ(wal_store_write_record(store, rec2), 0, cleanup, "write rec2 failed");

   rec3 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x30000000, prune, sizeof(prune));
   MCTF_ASSERT_PTR_NONNULL(rec3, cleanup, "create rec3 failed");
   MCTF_ASSERT_INT_EQ(migration_engine_translate(rec3, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL), 0, cleanup, "translate rec3 failed");
   MCTF_ASSERT_INT_EQ(wal_store_write_record(store, rec3), 0, cleanup, "write rec3 failed");

   MCTF_ASSERT_INT_EQ(wal_store_flush(store), 0, cleanup, "flush failed");

   MCTF_ASSERT_INT_EQ(lsn_map_get_downstream(map, 0x10000000, &down1), 0, cleanup, "map lookup 1 failed");
   MCTF_ASSERT_INT_EQ(down1, 40, cleanup, "first record must start at LSN 40");
   MCTF_ASSERT_INT_EQ(lsn_map_get_downstream(map, 0x20000000, &down2), 0, cleanup, "map lookup 2 failed");
   MCTF_ASSERT_INT_EQ(down2, 8192 + 24, cleanup, "second record must start after the page header (0x2018)");
   MCTF_ASSERT_INT_EQ(lsn_map_get_downstream(map, 0x30000000, &down3), 0, cleanup, "map lookup 3 failed");
   MCTF_ASSERT_INT_EQ(down3, 8192 + 24 + MAXALIGN(28), cleanup, "third record must follow the second");

   /* parse the downstream segment back; all three records must be found */
   pgmoneta_snprintf(seg_path, sizeof(seg_path), "%s/%s", downstream_dir, "000000010000000000000000");

   wf = calloc(1, sizeof(*wf));
   MCTF_ASSERT_PTR_NONNULL(wf, cleanup, "calloc wf failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_deque_create(false, &wf->records), 0, cleanup, "create records deque failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_deque_create(false, &wf->page_headers), 0, cleanup, "create page_headers deque failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_parse_wal_file(seg_path, -1, wf), 0, cleanup, "could not parse downstream segment");

   MCTF_ASSERT_INT_EQ(pgmoneta_deque_iterator_create(wf->records, &iter), 0, cleanup, "iterator create failed");
   while (pgmoneta_deque_iterator_next(iter))
   {
      struct decoded_xlog_record* rec = (struct decoded_xlog_record*)iter->value->data;
      char* encoded = NULL;
      uint32_t total_len;

      if (!rec || rec->partial)
      {
         continue;
      }
      count++;

      encoded = pgmoneta_wal_encode_xlog_record(rec, WAL_MAGIC_V19, NULL);
      MCTF_ASSERT_PTR_NONNULL(encoded, cleanup, "encode parsed record failed");
      total_len = ((struct xlog_record*)encoded)->xl_tot_len;

      MCTF_ASSERT_INT_EQ(rec->header.xl_crc, wal_store_compute_crc(encoded, total_len), cleanup, "stored record CRC mismatch");
      free(encoded);

      if (count == 1)
      {
         MCTF_ASSERT_INT_EQ(rec->lsn, down1, cleanup, "parsed rec1 lsn mismatch");
         MCTF_ASSERT_INT_EQ(rec->header.xl_prev, 0, cleanup, "first record xl_prev must be 0");
         MCTF_ASSERT_INT_EQ(rec->next_lsn, down2, cleanup, "rec1 next_lsn mismatch");
      }
      else if (count == 2)
      {
         MCTF_ASSERT_INT_EQ(rec->lsn, down2, cleanup, "parsed rec2 (page-boundary) lsn mismatch");
         MCTF_ASSERT_INT_EQ(rec->header.xl_prev, down1, cleanup, "second record must chain to first");
         MCTF_ASSERT_INT_EQ(rec->next_lsn, down3, cleanup, "rec2 next_lsn mismatch");
      }
      else if (count == 3)
      {
         MCTF_ASSERT_INT_EQ(rec->lsn, down3, cleanup, "parsed rec3 lsn mismatch");
         MCTF_ASSERT_INT_EQ(rec->header.xl_prev, down2, cleanup, "third record must chain to the page-boundary record");
      }
   }
   pgmoneta_deque_iterator_destroy(iter);
   iter = NULL;

   MCTF_ASSERT_INT_EQ(count, 3, cleanup, "expected 3 records in downstream segment");

cleanup:
   if (iter)
   {
      pgmoneta_deque_iterator_destroy(iter);
   }
   if (wf)
   {
      pgmoneta_destroy_walfile(wf);
      wf = NULL;
   }
   destroy_decoded_record(rec1);
   destroy_decoded_record(rec2);
   destroy_decoded_record(rec3);
   free(big);
   if (store)
   {
      wal_store_destroy(store);
   }
   lsn_map_destroy(map);
   unlink(map_path);
   if (seg_path[0])
   {
      unlink(seg_path);
   }
   rmdir(downstream_dir);
   rmdir(base);
   MCTF_FINISH();
}
