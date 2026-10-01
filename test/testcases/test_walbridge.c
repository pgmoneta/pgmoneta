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
#include <walbridge/wal_encode.h>
#include <mctf.h>
#include <tscommon.h>

/* system */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* system: sockets for the wire-protocol sender tests */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

/* pgmoneta: sender harness + SCRAM client */
#include <security.h>
#include <walbridge/wal_sender.h>

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

   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_create(path, &map), 0, cleanup, "pgmoneta_lsn_map_create failed");

   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_put(map, 0x1000, 40), 0, cleanup, "put 1 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_put(map, 0x2000, 72), 0, cleanup, "put 2 failed");

   /* exact match */
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_downstream(map, 0x2000, &down), 0, cleanup, "exact lookup failed");
   MCTF_ASSERT_INT_EQ(down, 72, cleanup, "exact lookup returned wrong downstream");

   /* at-or-before */
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_downstream_at_or_before(map, 0x2500, &up, &down), 0, cleanup, "at-or-before failed");
   MCTF_ASSERT_INT_EQ(up, 0x2000, cleanup, "at-or-before returned wrong upstream");
   MCTF_ASSERT_INT_EQ(down, 72, cleanup, "at-or-before returned wrong downstream");

   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_downstream_at_or_before(map, 0x0800, &up, &down), 1, cleanup, "no entry before requested should fail");

cleanup:
   pgmoneta_lsn_map_destroy(map);
   unlink(path);
   MCTF_FINISH();
}

MCTF_TEST(test_walbridge_migration_heap2_prune)
{
   struct decoded_xlog_record* rec = NULL;
   uint8_t data[2] = {0xAA, 0xBB};

   rec = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x1000, data, sizeof(data));
   MCTF_ASSERT_PTR_NONNULL(rec, cleanup, "create record failed");

   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate failed");

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

   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate failed");

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

   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL, true), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate failed");

   MCTF_ASSERT_INT_EQ(rec->main_data_len, 96, cleanup, "checkpoint must grow from 80 to 96 bytes");

   memcpy(&v19, rec->main_data, sizeof(v19));
   MCTF_ASSERT_INT_EQ((int)v19.redo, (int)v17.redo, cleanup, "redo not preserved");
   MCTF_ASSERT_INT_EQ(v19.next_multi, v17.next_multi, cleanup, "next_multi not preserved");
   MCTF_ASSERT_INT_EQ(v19.next_multi_offset, (uint64_t)v17.next_multi_offset, cleanup, "next_multi_offset not preserved");
   MCTF_ASSERT_INT_EQ((int)v19.data_checksum_state, 1, cleanup, "data checksum state must be on when request");

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

   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate failed");

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

   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate failed");

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

   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL, false),
                      PGMONETA_MIGRATION_DROP, cleanup, "gist assign lsn must be dropped");

cleanup:
   destroy_decoded_record(rec);
   MCTF_FINISH();
}

MCTF_TEST(test_walbridge_migration_switch_drop)
{
   struct decoded_xlog_record* rec = NULL;

   /* an upstream XLOG_SWITCH marks an *upstream* segment boundary. Forwarding
    * it verbatim would embed a switch in the middle of a downstream segment,
    * which PG downstream interprets as the end of that segment. */
   rec = create_decoded_record(RM_XLOG_ID, XLOG_SWITCH, 0x1000, NULL, 0);
   MCTF_ASSERT_PTR_NONNULL(rec, cleanup, "create record failed");

   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL, false),
                      PGMONETA_MIGRATION_DROP, cleanup, "upstream XLOG_SWITCH must be dropped");

cleanup:
   destroy_decoded_record(rec);
   MCTF_FINISH();
}

MCTF_TEST(test_walbridge_checksum_recompute)
{
   struct lsn_map* map = NULL;
   struct wal_store* store = NULL;
   struct decoded_xlog_record* rec = NULL;
   char base[PATH_MAX];
   char map_path[PATH_MAX];
   char downstream_dir[PATH_MAX];
   uint8_t* page = NULL;
   uint8_t bod[1] = {0};
   uint16_t c1 = 0;
   uint16_t c2 = 0;
   uint16_t garbage = 0;

   pgmoneta_snprintf(base, sizeof(base), "%s", "/tmp/walbridge_test_checksum");
   pgmoneta_mkdir(base);
   pgmoneta_snprintf(map_path, sizeof(map_path), "%s/lsn.map", base);
   pgmoneta_snprintf(downstream_dir, sizeof(downstream_dir), "%s/wal", base);

   unlink(map_path);
   pgmoneta_mkdir(downstream_dir);

   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_create(map_path, &map), 0, cleanup, "pgmoneta_lsn_map_create failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_create(downstream_dir, map, 999, DEFAULT_WAL_SEGZ_BYTES, 8192, 1, &store), 0, cleanup, "pgmoneta_wal_store_create failed");
   pgmoneta_wal_store_set_checksums(store, true);

   page = calloc(1, 8192);
   MCTF_ASSERT_PTR_NONNULL(page, cleanup, "page alloc failed");

   rec = create_decoded_record(RM_HEAP_ID, XLOG_HEAP_INSERT, 0x10000000, bod, sizeof(bod));
   MCTF_ASSERT_PTR_NONNULL(rec, cleanup, "create record failed");

   rec->max_block_id = 0;
   rec->blocks[0].in_use = true;
   rec->blocks[0].apply_image = true;
   rec->blocks[0].has_image = true;
   rec->blocks[0].bimg_info = 0;
   rec->blocks[0].blkno = 42;
   rec->blocks[0].bkp_image = (char*)page;
   rec->blocks[0].bimg_len = 8192;

   garbage = *((uint16_t*)(page + 8));
   MCTF_ASSERT_INT_EQ((int)garbage, 0, cleanup, "page checksum must start zero");

   pgmoneta_wal_store_recompute_page_checksums(store, rec);
   c1 = *((uint16_t*)(page + 8));
   MCTF_ASSERT(c1 != 0, cleanup, "recomputed checksum must be non-zero");

   /* idempotent: recomputing over the stored checksum must not change it */
   pgmoneta_wal_store_recompute_page_checksums(store, rec);
   c2 = *((uint16_t*)(page + 8));
   MCTF_ASSERT_INT_EQ(c2, c1, cleanup, "checksum recompute must be idempotent");

   /* checksum of identical page content at a different block must differ */
   {
      uint8_t* page2 = calloc(1, 8192);
      uint16_t cb = 0;
      struct decoded_xlog_record* rec2 = NULL;
      MCTF_ASSERT_PTR_NONNULL(page2, cleanup2, "page2 alloc failed");
      memcpy(page2, page, 8192);
      (*((uint16_t*)(page2 + 8))) = 0;
      rec2 = create_decoded_record(RM_HEAP_ID, XLOG_HEAP_INSERT, 0x20000000, bod, sizeof(bod));
      MCTF_ASSERT_PTR_NONNULL(rec2, cleanup2, "create rec2 failed");
      rec2->max_block_id = 0;
      rec2->blocks[0].in_use = true;
      rec2->blocks[0].apply_image = true;
      rec2->blocks[0].has_image = true;
      rec2->blocks[0].bimg_info = 0;
      rec2->blocks[0].blkno = 43;
      rec2->blocks[0].bkp_image = (char*)page2;
      rec2->blocks[0].bimg_len = 8192;
      pgmoneta_wal_store_recompute_page_checksums(store, rec2);
      cb = *((uint16_t*)(page2 + 8));
      MCTF_ASSERT(cb != c1, cleanup2, "same bytes at different block must differ");

cleanup2:
      if (rec2)
      {
         rec2->blocks[0].bkp_image = NULL; /* page2 freed separately */
         destroy_decoded_record(rec2);
      }
      free(page2);
   }

cleanup:
   if (rec)
   {
      rec->blocks[0].bkp_image = NULL; /* page freed separately */
      destroy_decoded_record(rec);
   }
   free(page);
   if (store)
   {
      pgmoneta_wal_store_destroy(store);
   }
   pgmoneta_lsn_map_destroy(map);
   unlink(map_path);
   rmdir(downstream_dir);
   rmdir(base);
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

   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_create(map_path, &map), 0, cleanup, "pgmoneta_lsn_map_create failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_create(downstream_dir, map, 123456789, DEFAULT_WAL_SEGZ_BYTES, 8192, 1, &store), 0, cleanup, "pgmoneta_wal_store_create failed");

   /* heap prune record: { reason, flags } -> { flags, 0 } */
   rec1 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x10000000, prune, sizeof(prune));
   MCTF_ASSERT_PTR_NONNULL(rec1, cleanup, "create rec1 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec1, WAL_MAGIC_V18, WAL_MAGIC_V19, map, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate rec1 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(store, rec1), 0, cleanup, "write rec1 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_sync_partial_page(store), 0, cleanup, "sync rec1 failed");

   /* checkpoint redo record: { wal_level } -> { wal_level, checksum_version } */
   rec2 = create_decoded_record(RM_XLOG_ID, XLOG_CHECKPOINT_REDO, 0x20000000, &wal_level, sizeof(wal_level));
   MCTF_ASSERT_PTR_NONNULL(rec2, cleanup, "create rec2 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec2, WAL_MAGIC_V18, WAL_MAGIC_V19, map, true), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate rec2 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(store, rec2), 0, cleanup, "write rec2 failed");

   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_flush(store), 0, cleanup, "flush failed");

   /* first record must start after the 40-byte long page header */
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_downstream(map, 0x10000000, &down1), 0, cleanup, "map lookup 1 failed");
   MCTF_ASSERT_INT_EQ(down1, 40, cleanup, "first record must start at LSN 40");
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_downstream(map, 0x20000000, &down2), 0, cleanup, "map lookup 2 failed");
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
      MCTF_ASSERT_INT_EQ(rec->header.xl_crc, pgmoneta_wal_store_compute_crc(encoded, total_len), cleanup, "stored record CRC mismatch");
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
         {
            uint32_t cksum_version = 0;
            memcpy(&cksum_version, rec->main_data + 4, sizeof(uint32_t));
            MCTF_ASSERT_INT_EQ((int)cksum_version, 1, cleanup, "checkpoint redo checksum version must be on");
         }
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
      pgmoneta_wal_store_destroy(store);
   }
   pgmoneta_lsn_map_destroy(map);
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

   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_create(map_path, &map), 0, cleanup, "pgmoneta_lsn_map_create failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_create(downstream_dir, map, 123456789, DEFAULT_WAL_SEGZ_BYTES, 8192, 1, &store), 0, cleanup, "pgmoneta_wal_store_create failed");

   rec1 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x10000000, big, big_len);
   MCTF_ASSERT_PTR_NONNULL(rec1, cleanup, "create rec1 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec1, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate rec1 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(store, rec1), 0, cleanup, "write rec1 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_sync_partial_page(store), 0, cleanup, "sync rec1 failed");

   /* 26-byte record at 8184: only 8 bytes fit on page 0, header splits */
   rec2 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x20000000, prune, sizeof(prune));
   MCTF_ASSERT_PTR_NONNULL(rec2, cleanup, "create rec2 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec2, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate rec2 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(store, rec2), 0, cleanup, "write rec2 failed");

   /* third record must start right after the continuation page data */
   rec3 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x30000000, prune, sizeof(prune));
   MCTF_ASSERT_PTR_NONNULL(rec3, cleanup, "create rec3 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec3, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate rec3 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(store, rec3), 0, cleanup, "write rec3 failed");

   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_flush(store), 0, cleanup, "flush failed");

   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_downstream(map, 0x10000000, &down1), 0, cleanup, "map lookup 1 failed");
   MCTF_ASSERT_INT_EQ(down1, 40, cleanup, "first record must start at LSN 40");
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_downstream(map, 0x20000000, &down2), 0, cleanup, "map lookup 2 failed");
   MCTF_ASSERT_INT_EQ(down2, 8184, cleanup, "second record must start 8 bytes before page end");
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_downstream(map, 0x30000000, &down3), 0, cleanup, "map lookup 3 failed");
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

      MCTF_ASSERT_INT_EQ(rec->header.xl_crc, pgmoneta_wal_store_compute_crc(encoded, total_len), cleanup, "stored record CRC mismatch");
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
      pgmoneta_wal_store_destroy(store);
   }
   pgmoneta_lsn_map_destroy(map);
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

   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_create(map_path, &map), 0, cleanup, "pgmoneta_lsn_map_create failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_create(downstream_dir, map, 123456789, DEFAULT_WAL_SEGZ_BYTES, 8192, 1, &store), 0, cleanup, "pgmoneta_wal_store_create failed");

   rec1 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x10000000, big, big_len);
   MCTF_ASSERT_PTR_NONNULL(rec1, cleanup, "create rec1 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec1, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate rec1 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(store, rec1), 0, cleanup, "write rec1 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_sync_partial_page(store), 0, cleanup, "sync rec1 failed");

   rec2 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x20000000, prune, sizeof(prune));
   MCTF_ASSERT_PTR_NONNULL(rec2, cleanup, "create rec2 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec2, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate rec2 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(store, rec2), 0, cleanup, "write rec2 failed");

   rec3 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x30000000, prune, sizeof(prune));
   MCTF_ASSERT_PTR_NONNULL(rec3, cleanup, "create rec3 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec3, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate rec3 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(store, rec3), 0, cleanup, "write rec3 failed");

   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_flush(store), 0, cleanup, "flush failed");

   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_downstream(map, 0x10000000, &down1), 0, cleanup, "map lookup 1 failed");
   MCTF_ASSERT_INT_EQ(down1, 40, cleanup, "first record must start at LSN 40");
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_downstream(map, 0x20000000, &down2), 0, cleanup, "map lookup 2 failed");
   MCTF_ASSERT_INT_EQ(down2, 8192 + 24, cleanup, "second record must start after the page header (0x2018)");
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_downstream(map, 0x30000000, &down3), 0, cleanup, "map lookup 3 failed");
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

      MCTF_ASSERT_INT_EQ(rec->header.xl_crc, pgmoneta_wal_store_compute_crc(encoded, total_len), cleanup, "stored record CRC mismatch");
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
      pgmoneta_wal_store_destroy(store);
   }
   pgmoneta_lsn_map_destroy(map);
   unlink(map_path);
   if (seg_path[0])
   {
      unlink(seg_path);
   }
   rmdir(downstream_dir);
   rmdir(base);
   MCTF_FINISH();
}

static void
write_page_header_craft(char* buf, uint32_t offset, uint16_t magic, uint16_t info,
                        xlog_rec_ptr pageaddr, uint32_t rem_len)
{
   struct xlog_page_header_data* ph = (struct xlog_page_header_data*)(buf + offset);
   ph->xlp_magic = magic;
   ph->xlp_info = info;
   ph->xlp_tli = 1;
   ph->xlp_pageaddr = pageaddr;
   ph->xlp_rem_len = rem_len;
}

static void
write_long_header_craft(char* buf, uint16_t info, xlog_rec_ptr pageaddr, uint32_t rem_len,
                        uint64_t sysid, uint32_t seg_size, uint32_t blcksz)
{
   struct xlog_long_page_header_data* lh = (struct xlog_long_page_header_data*)buf;
   lh->std.xlp_magic = WAL_MAGIC_V18;
   lh->std.xlp_info = info;
   lh->std.xlp_tli = 1;
   lh->std.xlp_pageaddr = pageaddr;
   lh->std.xlp_rem_len = rem_len;
   lh->xlp_sysid = sysid;
   lh->xlp_seg_size = seg_size;
   lh->xlp_xlog_blcksz = blcksz;
}

static void
write_record_header_craft(char* buf, uint32_t offset, uint32_t tot_len)
{
   struct xlog_record* r = (struct xlog_record*)(buf + offset);
   memset(r, 0, SIZE_OF_XLOG_RECORD);
   r->xl_tot_len = tot_len;
   r->xl_xid = 42;
   r->xl_prev = 0;
   r->xl_info = XLOG_NOOP;
   r->xl_rmid = RM_XLOG_ID;
}

/* Filler record: 24-byte header + [DataLong][u32 len][payload], the whole
 * record exactly fills data_len bytes of the record body. */
static void
write_filler_craft(char* buf, uint32_t offset, uint32_t data_len, uint8_t fill)
{
   uint32_t payload_len = data_len - 5;
   write_record_header_craft(buf, offset, SIZE_OF_XLOG_RECORD + data_len);
   ((uint8_t*)buf)[offset + SIZE_OF_XLOG_RECORD] = (uint8_t)XLR_BLOCK_ID_DATA_LONG;
   memcpy(buf + offset + SIZE_OF_XLOG_RECORD + 1, &payload_len, sizeof(uint32_t));
   memset(buf + offset + SIZE_OF_XLOG_RECORD + 5, fill, payload_len);
}

/* convenience: parse path, count non-partial decoded records */
static int
parse_craft_segment(const char* path, struct walfile** wf, int* count)
{
   struct deque_iterator* iter = NULL;
   int rc = 0;

   *count = 0;
   *wf = calloc(1, sizeof(**wf));
   if (*wf == NULL)
   {
      return 1;
   }
   if ((rc = pgmoneta_deque_create(false, &(*wf)->records)) != 0)
   {
      return rc;
   }
   if ((rc = pgmoneta_deque_create(false, &(*wf)->page_headers)) != 0)
   {
      return rc;
   }
   if ((rc = pgmoneta_wal_parse_wal_file((char*)path, -1, *wf)) != 0)
   {
      return rc;
   }
   if ((rc = pgmoneta_deque_iterator_create((*wf)->records, &iter)) != 0)
   {
      return rc;
   }
   while (pgmoneta_deque_iterator_next(iter))
   {
      struct decoded_xlog_record* rec = (struct decoded_xlog_record*)iter->value->data;
      if (rec && !rec->partial)
      {
         (*count)++;
      }
   }
   pgmoneta_deque_iterator_destroy(iter);
   return 0;
}

MCTF_TEST(test_walbridge_reader_cross_segment)
{
   char base[PATH_MAX];
   char seg_a[PATH_MAX];
   char seg_b[PATH_MAX];
   char seg_c[PATH_MAX];
   char* buf = NULL;
   FILE* fd = NULL;
   struct walfile* wf = NULL;
   struct deque_iterator* iter = NULL;
   uint64_t sysid = 0x0102030405060708ULL;
   uint32_t seg_size = 32768;
   uint32_t blcksz = 8192;
   uint8_t expect[94];
   int count = 0;
   int found_recon = 0;
   int i = 0;

   pgmoneta_snprintf(base, sizeof(base), "%s", "/tmp/walbridge_test_crossseg");
   pgmoneta_mkdir(base);
   pgmoneta_snprintf(seg_a, sizeof(seg_a), "%s/%s", base, "000000010000000000000001");
   pgmoneta_snprintf(seg_b, sizeof(seg_b), "%s/%s", base, "000000010000000000000002");
   pgmoneta_snprintf(seg_c, sizeof(seg_c), "%s/%s", base, "000000010000000000000005");

   unlink(seg_a);
   unlink(seg_b);
   unlink(seg_c);

   for (i = 0; i < 94; i++)
   {
      expect[i] = (uint8_t)i;
   }

   /* ---------------- craft segment A (logSegNo 1, base LSN 32768) ----- */
   buf = calloc(1, seg_size);
   MCTF_ASSERT_PTR_NONNULL(buf, cleanup, "alloc buf failed");

   write_long_header_craft(buf, XLP_LONG_HEADER, 32768, 0, sysid, seg_size, blcksz);
   write_page_header_craft(buf, 8192, WAL_MAGIC_V18, 0, 40960, 0);
   write_page_header_craft(buf, 16384, WAL_MAGIC_V18, 0, 49152, 0);
   write_page_header_craft(buf, 24576, WAL_MAGIC_V18, 0, 57344, 0);

   /* fillers exactly fill pages 0-2 and land on page 3. NOTE: a record that
    * overflows a page continues AFTER that page's short header, so the next
    * record starts 24 bytes past the overflow point. */
   write_filler_craft(buf, 40, 8128, 0x41);    /* fills page 0, ends at LSN 8192       */
   write_filler_craft(buf, 8216, 8168, 0x42);  /* overflows p1, ends at LSN 16432      */
   write_filler_craft(buf, 16432, 8120, 0x43); /* fills page 2, ends at LSN 24576      */
   write_filler_craft(buf, 24600, 8080, 0x44); /* ends at LSN 32704                    */

   /* boundary record: header at LSN 32704, 96 body bytes that span
    * [32728, 32824): only the first 40 bytes fit in A (up to LSN 32768),
    * the remaining 56 continue in segment B */
   write_record_header_craft(buf, 32704, SIZE_OF_XLOG_RECORD + 96);
   ((uint8_t*)buf)[32704 + SIZE_OF_XLOG_RECORD] = (uint8_t)XLR_BLOCK_ID_DATA_SHORT;
   buf[32704 + SIZE_OF_XLOG_RECORD + 1] = 94;
   for (i = 0; i < 38; i++)
   {
      buf[32704 + SIZE_OF_XLOG_RECORD + 2 + i] = (uint8_t)i;
   }

   fd = fopen(seg_a, "wb");
   MCTF_ASSERT_PTR_NONNULL(fd, cleanup, "open seg_a failed");
   MCTF_ASSERT_INT_EQ(fwrite(buf, 1, seg_size, fd), (int)seg_size, cleanup, "write seg_a failed");
   fclose(fd);
   fd = NULL;
   free(buf);
   buf = NULL;

   /* ---------------- craft segment B (logSegNo 2, base LSN 65536) ----- */
   buf = calloc(1, seg_size);
   MCTF_ASSERT_PTR_NONNULL(buf, cleanup, "alloc buf failed");

   /* first page carries the 56-byte continuation of the record started in A */
   write_long_header_craft(buf, XLP_LONG_HEADER | XLP_FIRST_IS_CONTRECORD, 65536, 56,
                           sysid, seg_size, blcksz);
   write_page_header_craft(buf, 8192, WAL_MAGIC_V18, 0, 73728, 0);
   write_page_header_craft(buf, 16384, WAL_MAGIC_V18, 0, 81920, 0);
   write_page_header_craft(buf, 24576, WAL_MAGIC_V18, 0, 90112, 0);

   /* continuation = payload[38..93] that crossed out of A */
   for (i = 0; i < 56; i++)
   {
      buf[40 + i] = (uint8_t)(38 + i);
   }

   write_filler_craft(buf, 96, 8072, 0x51);    /* fills page 0, ends at LSN 8192       */
   write_filler_craft(buf, 8216, 8168, 0x52);  /* overflows p1, ends at LSN 16432      */
   write_filler_craft(buf, 16432, 8120, 0x53); /* fills page 2, ends at LSN 24576      */
   write_filler_craft(buf, 24600, 8144, 0x54); /* fills page 3, ends at LSN 32768 (EOF) */

   fd = fopen(seg_b, "wb");
   MCTF_ASSERT_PTR_NONNULL(fd, cleanup, "open seg_b failed");
   MCTF_ASSERT_INT_EQ(fwrite(buf, 1, seg_size, fd), (int)seg_size, cleanup, "write seg_b failed");
   fclose(fd);
   fd = NULL;
   free(buf);
   buf = NULL;

   /* ---------------- craft segment C (logSegNo 5, empty) -------------- */
   buf = calloc(1, seg_size);
   MCTF_ASSERT_PTR_NONNULL(buf, cleanup, "alloc buf failed");

   write_long_header_craft(buf, XLP_LONG_HEADER, 163840, 0, sysid, seg_size, blcksz);
   write_page_header_craft(buf, 8192, WAL_MAGIC_V18, 0, 172032, 0);
   write_page_header_craft(buf, 16384, WAL_MAGIC_V18, 0, 180224, 0);
   write_page_header_craft(buf, 24576, WAL_MAGIC_V18, 0, 188416, 0);

   fd = fopen(seg_c, "wb");
   MCTF_ASSERT_PTR_NONNULL(fd, cleanup, "open seg_c failed");
   MCTF_ASSERT_INT_EQ(fwrite(buf, 1, seg_size, fd), (int)seg_size, cleanup, "write seg_c failed");
   fclose(fd);
   fd = NULL;
   free(buf);
   buf = NULL;

   /* ---- parse A: 4 complete fillers + saved cross-EOF tail ----------- */
   MCTF_ASSERT_INT_EQ(parse_craft_segment(seg_a, &wf, &count), 0, cleanup, "could not parse seg_a");
   MCTF_ASSERT_INT_EQ(count, 4, cleanup, "seg_a must yield 4 complete records");
   MCTF_ASSERT_PTR_NONNULL(partial_record, cleanup, "partial_record must exist after a cross-EOF tail");
   MCTF_ASSERT_INT_EQ((int)partial_record->xlog_record_bytes_read, (int)SIZE_OF_XLOG_RECORD,
                      cleanup, "partial must have saved the full header");
   MCTF_ASSERT_INT_EQ((int)partial_record->data_buffer_bytes_read, 40,
                      cleanup, "partial must have saved 40 body bytes from seg_a");
   MCTF_ASSERT_INT_EQ((int)partial_record->from_seg, 1, cleanup, "partial must be tagged with seg 1");
   MCTF_ASSERT_INT_EQ((int)partial_record->lsn, 32704 + 32768, cleanup, "partial lsn must be true record start");
   if (wf)
   {
      pgmoneta_destroy_walfile(wf);
      wf = NULL;
   }

   /* ---- parse B (immediate successor): reassemble the A record -------- */
   MCTF_ASSERT_INT_EQ(parse_craft_segment(seg_b, &wf, &count), 0, cleanup, "could not parse seg_b");
   MCTF_ASSERT_INT_EQ(count, 5, cleanup, "seg_b must yield the reassembled record + 4 fillers");

   MCTF_ASSERT_INT_EQ(pgmoneta_deque_iterator_create(wf->records, &iter), 0, cleanup, "iterator create failed");
   while (pgmoneta_deque_iterator_next(iter))
   {
      struct decoded_xlog_record* rec = (struct decoded_xlog_record*)iter->value->data;
      if (!rec || rec->partial)
      {
         continue;
      }
      if (rec->lsn == 32704 + 32768)
      {
         MCTF_ASSERT_INT_EQ((int)rec->header.xl_tot_len, (int)(SIZE_OF_XLOG_RECORD + 96),
                            cleanup, "reassembled record tot_len mismatch");
         MCTF_ASSERT_INT_EQ((int)rec->header.xl_xid, 42, cleanup, "reassembled record xid mismatch");
         MCTF_ASSERT_INT_EQ((int)rec->header.xl_prev, 0, cleanup, "reassembled record prev mismatch");
         MCTF_ASSERT_INT_EQ((int)rec->main_data_len, 94, cleanup, "reassembled main data len mismatch");
         MCTF_ASSERT_PTR_NONNULL(rec->main_data, cleanup, "reassembled main data must be present");
         MCTF_ASSERT_INT_EQ(memcmp(rec->main_data, expect, 94), 0, cleanup,
                            "reassembled main data must splice A tail + B tail in order");
         found_recon = 1;
      }
   }
   pgmoneta_deque_iterator_destroy(iter);
   iter = NULL;

   MCTF_ASSERT_INT_EQ(found_recon, 1, cleanup, "reassembled cross-segment record was not decoded");
   MCTF_ASSERT_PTR_NULL(partial_record->xlog_record, cleanup, "partial header must be consumed by reassembly");
   MCTF_ASSERT_PTR_NULL(partial_record->data_buffer, cleanup, "partial body must be consumed by reassembly");
   if (wf)
   {
      pgmoneta_destroy_walfile(wf);
      wf = NULL;
   }

   /* ---- re-parse A: re-saves a partial, tags it with seg 1 ------------ */
   MCTF_ASSERT_INT_EQ(parse_craft_segment(seg_a, &wf, &count), 0, cleanup, "could not re-parse seg_a");
   MCTF_ASSERT_INT_EQ(count, 4, cleanup, "seg_a re-parse must yield 4 complete records");
   MCTF_ASSERT_INT_EQ((int)partial_record->from_seg, 1, cleanup, "re-parse must re-tag partial with seg 1");

   /* ---- parse C: unrelated segment must discard the stale partial ---- */
   if (wf)
   {
      pgmoneta_destroy_walfile(wf);
      wf = NULL;
   }
   MCTF_ASSERT_INT_EQ(parse_craft_segment(seg_c, &wf, &count), 0, cleanup, "could not parse seg_c");
   MCTF_ASSERT_INT_EQ(count, 0, cleanup, "empty seg_c must yield 0 records");
   MCTF_ASSERT_PTR_NULL(partial_record->xlog_record, cleanup, "stale partial header must be discarded");
   MCTF_ASSERT_PTR_NULL(partial_record->data_buffer, cleanup, "stale partial data must be discarded");

cleanup:
   if (fd)
   {
      fclose(fd);
   }
   free(buf);
   if (iter)
   {
      pgmoneta_deque_iterator_destroy(iter);
   }
   if (wf)
   {
      pgmoneta_destroy_walfile(wf);
   }
   unlink(seg_a);
   unlink(seg_b);
   unlink(seg_c);
   rmdir(base);
   MCTF_FINISH();
}

MCTF_TEST(test_walbridge_page_flag_remap)
{
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_remap_page_flags(0), 0, cleanup, "empty flags must stay empty");

   /* XLP_FIRST_IS_CONTRECORD | XLP_LONG_HEADER | XLP_FIRST_IS_OVERWRITE_CONTRECORD */
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_remap_page_flags(XLP_FIRST_IS_CONTRECORD), XLP_FIRST_IS_CONTRECORD,
                      cleanup, "first-is-contrecord must be preserved");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_remap_page_flags(XLP_LONG_HEADER), XLP_LONG_HEADER,
                      cleanup, "long-header must be preserved");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_remap_page_flags(
                         XLP_FIRST_IS_CONTRECORD | XLP_LONG_HEADER | XLP_FIRST_IS_OVERWRITE_CONTRECORD),
                      XLP_FIRST_IS_CONTRECORD | XLP_LONG_HEADER | XLP_FIRST_IS_OVERWRITE_CONTRECORD,
                      cleanup, "all 19.x flags must be preserved");

   /* PG18-only XLP_BKP_REMOVABLE (0x0008) must be stripped */
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_remap_page_flags(0x0008), 0, cleanup, "XLP_BKP_REMOVABLE must be dropped");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_remap_page_flags(XLP_LONG_HEADER | 0x0008), XLP_LONG_HEADER,
                      cleanup, "XLP_BKP_REMOVABLE must be dropped while keeping valid flags");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_remap_page_flags(0x000F), XLP_FIRST_IS_CONTRECORD | XLP_LONG_HEADER | XLP_FIRST_IS_OVERWRITE_CONTRECORD,
                      cleanup, "combined 18.x flags must reduce to the 19.x flag set");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_walbridge_wal_level_discovery)
{
   struct decoded_xlog_record* rec = NULL;
   int wl = -1;

   /* XLOG_CHECKPOINT_REDO: main data is just the 4-byte wal_level */
   int replica = WAL_LEVEL_REPLICA;
   rec = create_decoded_record(RM_XLOG_ID, XLOG_CHECKPOINT_REDO, 0x1000, &replica, sizeof(replica));
   MCTF_ASSERT_PTR_NONNULL(rec, cleanup, "create checkpoint-redo record failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_get_wal_level(rec, &wl), 0, cleanup, "could not discover wal_level from checkpoint redo");
   MCTF_ASSERT_INT_EQ(wl, WAL_LEVEL_REPLICA, cleanup, "wal_level discovery wrong");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_wal_level_ok(wl), 1, cleanup, "replica wal_level must be acceptable");
   destroy_decoded_record(rec);
   rec = NULL;

   int logical = WAL_LEVEL_LOGICAL;
   rec = create_decoded_record(RM_XLOG_ID, XLOG_CHECKPOINT_REDO, 0x1000, &logical, sizeof(logical));
   MCTF_ASSERT_PTR_NONNULL(rec, cleanup, "create logical record failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_wal_level_ok(WAL_LEVEL_LOGICAL), 1, cleanup, "logical wal_level must be acceptable");
   destroy_decoded_record(rec);
   rec = NULL;

   int minimal = WAL_LEVEL_MINIMAL;
   rec = create_decoded_record(RM_XLOG_ID, XLOG_CHECKPOINT_REDO, 0x1000, &minimal, sizeof(minimal));
   MCTF_ASSERT_PTR_NONNULL(rec, cleanup, "create minimal record failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_wal_level_ok(WAL_LEVEL_MINIMAL), 1, cleanup, "minimal wal_level must be accepted");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL, true),
                      PGMONETA_MIGRATION_MODIFIED, cleanup, "minimal wal_level must normalize checkpoint-redo translation");
   destroy_decoded_record(rec);
   rec = NULL;

   /* a non-checkpoint record carries no wal_level */
   wl = -1;
   int xid = 1;
   rec = create_decoded_record(RM_XLOG_ID, XLOG_ASSIGN_LSN, 0x2000, &xid, sizeof(xid));
   MCTF_ASSERT_PTR_NONNULL(rec, cleanup, "create assign-lsn record failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_get_wal_level(rec, &wl), -1, cleanup, "non-checkpoint record must not expose wal_level");

cleanup:
   destroy_decoded_record(rec);
   MCTF_FINISH();
}

MCTF_TEST(test_walbridge_lsn_map_reconnect)
{
   struct lsn_map* map = NULL;
   char path[512];
   uint64_t up = 0;

   pgmoneta_snprintf(path, sizeof(path), "%s", "/tmp/walbridge_reconnect.map");
   unlink(path);
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_create(path, &map), 0, cleanup, "create map failed");

   /* downstream LSNs the replica could have flushed */
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_put(map, 0x10000000, 40), 0, cleanup, "put pair 1 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_put(map, 0x100000C8, 80), 0, cleanup, "put pair 2 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_put(map, 0x20000000, 120), 0, cleanup, "put pair 3 failed");

   /* resolve the replica's downstream resume point backwards to upstream */
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_upstream_at_or_before(map, 120, &up), 0, cleanup, "resolve exact downstream failed");
   MCTF_ASSERT_INT_EQ(up, 0x20000000, cleanup, "exact-match upstream wrong");

   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_upstream_at_or_before(map, 121, &up), 0, cleanup, "resolve at-or-before failed");
   MCTF_ASSERT_INT_EQ(up, 0x20000000, cleanup, "at-or-before (mid-record gap) must yield preceding upstream");

   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_upstream_at_or_before(map, 40, &up), 0, cleanup, "resolve earliest failed");
   MCTF_ASSERT_INT_EQ(up, 0x10000000, cleanup, "earliest upstream wrong");

   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_upstream_at_or_before(map, 39, &up), 1, cleanup, "before any record must fail");

cleanup:
   pgmoneta_lsn_map_destroy(map);
   unlink(path);
   MCTF_FINISH();
}

MCTF_TEST(test_walbridge_store_resume)
{
   char base[512];
   char map_path[512];
   char downstream_dir[512];
   char seg_path[512] = {0};
   struct lsn_map* map = NULL;
   struct wal_store* store = NULL;
   struct wal_store* store2 = NULL;
   struct decoded_xlog_record* rec1 = NULL;
   struct decoded_xlog_record* rec2 = NULL;
   struct decoded_xlog_record* rec3 = NULL;
   struct decoded_xlog_record* rec4 = NULL;
   uint64_t xl_prev = 0;
   uint64_t next_lsn = 0;
   uint64_t segno = 0;
   uint64_t down1 = 0;
   uint64_t down2 = 0;
   uint64_t down3 = 0;
   uint64_t down4 = 0;
   uint8_t prune[2] = {0x01, 0x07};
   struct walfile* wf = NULL;
   struct deque_iterator* iter = NULL;
   int count = 0;

   pgmoneta_snprintf(base, sizeof(base), "%s", "/tmp/walbridge_test_resume");
   pgmoneta_mkdir(base);
   pgmoneta_snprintf(map_path, sizeof(map_path), "%s/lsn.map", base);
   pgmoneta_snprintf(downstream_dir, sizeof(downstream_dir), "%s/wal", base);
   unlink(map_path);

   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_create(map_path, &map), 0, cleanup, "pgmoneta_lsn_map_create failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_create(downstream_dir, map, 123456789, DEFAULT_WAL_SEGZ_BYTES, 8192, 1, &store), 0, cleanup, "create store failed");
   pgmoneta_wal_store_set_checksums(store, true);

   rec1 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x10000000, prune, sizeof(prune));
   rec2 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x20000000, prune, sizeof(prune));
   MCTF_ASSERT_PTR_NONNULL(rec1, cleanup, "create rec1 failed");
   MCTF_ASSERT_PTR_NONNULL(rec2, cleanup, "create rec2 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec1, WAL_MAGIC_V18, WAL_MAGIC_V19, map, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate rec1 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec2, WAL_MAGIC_V18, WAL_MAGIC_V19, map, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate rec2 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(store, rec1), 0, cleanup, "write rec1 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_sync_partial_page(store), 0, cleanup, "sync rec1 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(store, rec2), 0, cleanup, "write rec2 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_sync_partial_page(store), 0, cleanup, "sync rec2 failed");

   pgmoneta_wal_store_get_state(store, &segno, &xl_prev, &next_lsn);
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_downstream(map, 0x10000000, &down1), 0, cleanup, "down1 lookup failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_downstream(map, 0x20000000, &down2), 0, cleanup, "down2 lookup failed");
   MCTF_ASSERT_INT_EQ(xl_prev, down2, cleanup, "xl_prev must be the last written record");
   MCTF_ASSERT_INT_EQ(next_lsn, down2 + MAXALIGN(28), cleanup, "next_lsn must follow the last aligned record");

   /* simulate a restart: new store over the same on-disk stream */
   pgmoneta_wal_store_destroy(store);
   store = NULL;
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_create(downstream_dir, map, 123456789, DEFAULT_WAL_SEGZ_BYTES, 8192, 1, &store2), 0, cleanup, "recreate store failed");
   pgmoneta_wal_store_set_checksums(store2, true);
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_resume(store2, xl_prev, next_lsn), 0, cleanup, "resume store failed");

   rec3 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x30000000, prune, sizeof(prune));
   rec4 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x40000000, prune, sizeof(prune));
   MCTF_ASSERT_PTR_NONNULL(rec3, cleanup, "create rec3 failed");
   MCTF_ASSERT_PTR_NONNULL(rec4, cleanup, "create rec4 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec3, WAL_MAGIC_V18, WAL_MAGIC_V19, map, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate rec3 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec4, WAL_MAGIC_V18, WAL_MAGIC_V19, map, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate rec4 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(store2, rec3), 0, cleanup, "write rec3 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_sync_partial_page(store2), 0, cleanup, "sync rec3 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(store2, rec4), 0, cleanup, "write rec4 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_sync_partial_page(store2), 0, cleanup, "sync rec4 failed");

   /* rec3 must resume exactly where rec2 ended; rec4 must chain to rec3 */
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_downstream(map, 0x30000000, &down3), 0, cleanup, "down3 lookup failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_downstream(map, 0x40000000, &down4), 0, cleanup, "down4 lookup failed");
   MCTF_ASSERT_INT_EQ(down3, next_lsn, cleanup, "resumed record must start at the persisted next_lsn");
   MCTF_ASSERT_INT_EQ(down4, down3 + MAXALIGN(28), cleanup, "resumed stream must stay contiguous");

   pgmoneta_wal_store_flush(store2);

   /* parse the segment back and verify LSNs, chaining and CRCs */
   pgmoneta_snprintf(seg_path, sizeof(seg_path), "%s/000000010000000000000000", downstream_dir);
   wf = calloc(1, sizeof(*wf));
   MCTF_ASSERT_PTR_NONNULL(wf, cleanup, "alloc walfile failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_deque_create(false, &wf->records), 0, cleanup, "records deque failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_deque_create(false, &wf->page_headers), 0, cleanup, "page_headers deque failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_parse_wal_file(seg_path, -1, wf), 0, cleanup, "could not parse resumed downstream segment");

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
      MCTF_ASSERT_INT_EQ(rec->header.xl_crc, pgmoneta_wal_store_compute_crc(encoded, total_len), cleanup, "resumed record CRC mismatch");
      free(encoded);

      if (count == 1)
      {
         MCTF_ASSERT_INT_EQ(rec->lsn, down1, cleanup, "parsed rec1 lsn mismatch");
         MCTF_ASSERT_INT_EQ(rec->header.xl_prev, 0, cleanup, "rec1 xl_prev wrong");
      }
      else if (count == 2)
      {
         MCTF_ASSERT_INT_EQ(rec->lsn, down2, cleanup, "parsed rec2 lsn mismatch");
         MCTF_ASSERT_INT_EQ(rec->header.xl_prev, down1, cleanup, "rec2 must chain to rec1");
      }
      else if (count == 3)
      {
         MCTF_ASSERT_INT_EQ(rec->lsn, down3, cleanup, "parsed rec3 lsn mismatch");
         MCTF_ASSERT_INT_EQ(rec->header.xl_prev, down2, cleanup, "resumed rec3 must chain across the restart");
      }
      else if (count == 4)
      {
         MCTF_ASSERT_INT_EQ(rec->lsn, down4, cleanup, "parsed rec4 lsn mismatch");
         MCTF_ASSERT_INT_EQ(rec->header.xl_prev, down3, cleanup, "rec4 must chain to rec3");
      }
   }
   pgmoneta_deque_iterator_destroy(iter);
   iter = NULL;
   MCTF_ASSERT_INT_EQ(count, 4, cleanup, "expected 4 records in resumed downstream segment");

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
   destroy_decoded_record(rec4);
   if (store)
   {
      pgmoneta_wal_store_destroy(store);
   }
   if (store2)
   {
      pgmoneta_wal_store_destroy(store2);
   }
   pgmoneta_lsn_map_destroy(map);
   unlink(map_path);
   if (seg_path[0])
   {
      unlink(seg_path);
   }
   rmdir(downstream_dir);
   rmdir(base);
   MCTF_FINISH();
}

/* Build a full-page-image record large enough to force page crossing and
 * (with many copies) downstream segment rotation. */
static struct decoded_xlog_record*
create_fpi_record(uint64_t lsn, uint32_t image_size)
{
   struct decoded_xlog_record* rec = calloc(1, sizeof(struct decoded_xlog_record));
   struct decoded_bkp_block* blk = NULL;

   if (rec == NULL)
   {
      return NULL;
   }

   rec->header.xl_rmid = RM_HEAP_ID;
   rec->header.xl_info = XLOG_HEAP_INSERT;
   rec->header.xl_xid = 1;
   rec->lsn = lsn;
   rec->max_block_id = 0;

   blk = &rec->blocks[0];
   blk->in_use = true;
   blk->rlocator.spcOid = 1663;
   blk->rlocator.dbOid = 5;
   blk->rlocator.relNumber = 16385;
   blk->forknum = MAIN_FORKNUM;
   blk->blkno = 1;
   blk->flags = BKPBLOCK_HAS_IMAGE | BKPBLOCK_HAS_DATA;
   blk->has_image = true;
   blk->apply_image = true;
   blk->has_data = true;
   blk->bimg_len = (uint16_t)image_size;
   blk->bimg_info = 0;
   blk->bkp_image = malloc(image_size);
   if (blk->bkp_image == NULL)
   {
      free(rec);
      return NULL;
   }
   memset(blk->bkp_image, 0x5A, image_size);
   blk->data = malloc(4);
   if (blk->data == NULL)
   {
      free(blk->bkp_image);
      free(rec);
      return NULL;
   }
   memset(blk->data, 0x11, 4);
   blk->data_len = 4;

   return rec;
}

static int
concat_bytes(char** dst, size_t* dst_len, const char* src, size_t src_len)
{
   char* n = realloc(*dst, *dst_len + src_len);
   if (n == NULL)
   {
      return 1;
   }
   memcpy(n + *dst_len, src, src_len);
   *dst = n;
   *dst_len += src_len;
   return 0;
}

static char*
read_file_bytes(char* path, size_t* len)
{
   FILE* f = NULL;
   char* buf = NULL;
   long sz = 0;

   f = fopen(path, "rb");
   if (!f)
   {
      return NULL;
   }
   if (fseek(f, 0, SEEK_END) != 0)
   {
      fclose(f);
      return NULL;
   }
   sz = ftell(f);
   if (sz < 0)
   {
      fclose(f);
      return NULL;
   }
   if (fseek(f, 0, SEEK_SET) != 0)
   {
      fclose(f);
      return NULL;
   }
   buf = malloc((size_t)sz);
   if (!buf)
   {
      fclose(f);
      return NULL;
   }
   if (fread(buf, 1, (size_t)sz, f) != (size_t)sz)
   {
      free(buf);
      fclose(f);
      return NULL;
   }
   fclose(f);
   *len = (size_t)sz;
   return buf;
}

/* The stream encoder must reproduce the store's downstream bytes exactly:
 * same position, same content, same CRCs, same page/segment layout. */
MCTF_TEST(test_walbridge_encoder_matches_store)
{
   struct lsn_map* map = NULL;
   struct wal_store* store = NULL;
   struct wal_encoder* enc = NULL;
   struct decoded_xlog_record* rec1 = NULL;
   struct decoded_xlog_record* rec2 = NULL;
   struct decoded_xlog_record* rec3 = NULL;
   struct decoded_xlog_record* rec4 = NULL;
   char base[PATH_MAX];
   char map_path[PATH_MAX];
   char downstream_dir[PATH_MAX];
   char seg_path[PATH_MAX] = "";
   char* store_bytes = NULL;
   char* tail = NULL;
   char* chunk = NULL;
   size_t store_len = 0;
   size_t stream_len = 0;
   size_t spare = 0;
   uint8_t prune[2] = {0x01, 0x07};
   int wal_level = 2;

   pgmoneta_snprintf(base, sizeof(base), "%s", "/tmp/walbridge_test_encstore");
   pgmoneta_mkdir(base);
   pgmoneta_snprintf(map_path, sizeof(map_path), "%s/lsn.map", base);
   pgmoneta_snprintf(downstream_dir, sizeof(downstream_dir), "%s/wal", base);
   unlink(map_path);

   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_create(map_path, &map), 0, cleanup, "lsn_map_create failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_create(downstream_dir, map, 123456789, DEFAULT_WAL_SEGZ_BYTES, 8192, 1, &store), 0, cleanup, "store create failed");
   pgmoneta_wal_store_set_checksums(store, true);
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_create(123456789, DEFAULT_WAL_SEGZ_BYTES, 8192, 1, true, &enc), 0, cleanup, "encoder create failed");

   /* ~33KB record: crosses the 8KB page boundary within the same segment */
   rec1 = create_fpi_record(0x10000000, 33000);
   rec2 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x20000000, prune, sizeof(prune));
   rec3 = create_decoded_record(RM_XLOG_ID, XLOG_CHECKPOINT_REDO, 0x30000000, &wal_level, sizeof(wal_level));
   rec4 = create_fpi_record(0x40000000, 33000);
   MCTF_ASSERT_PTR_NONNULL(rec1, cleanup, "create rec1 failed");
   MCTF_ASSERT_PTR_NONNULL(rec2, cleanup, "create rec2 failed");
   MCTF_ASSERT_PTR_NONNULL(rec3, cleanup, "create rec3 failed");
   MCTF_ASSERT_PTR_NONNULL(rec4, cleanup, "create rec4 failed");

   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec2, WAL_MAGIC_V18, WAL_MAGIC_V19, map, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate rec2 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec3, WAL_MAGIC_V18, WAL_MAGIC_V19, map, true), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate rec3 failed");

   /* feed the identical sequence to the store and the encoder */
   {
      struct decoded_xlog_record* recs[] = {rec1, rec2, rec3, rec4};
      for (int i = 0; i < 4; i++)
      {
         MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(store, recs[i]), 0, cleanup, "store write failed");
         MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_sync_partial_page(store), 0, cleanup, "store sync failed");
         MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_write_record(enc, recs[i], NULL), 0, cleanup, "encoder write failed");
         MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_take(enc, &chunk, &spare), 0, cleanup, "encoder take failed");
         if (chunk)
         {
            MCTF_ASSERT_INT_EQ(concat_bytes(&tail, &stream_len, chunk, spare), 0, cleanup, "append stream failed");
            free(chunk);
            chunk = NULL;
         }
      }
   }
   pgmoneta_wal_store_flush(store);

   MCTF_ASSERT(stream_len > 0, cleanup, "stream must not be empty");

   /* encoder stream length must equal the store's byte cursor (next_lsn) */
   MCTF_ASSERT_INT_EQ((int)stream_len, (int)pgmoneta_wal_encoder_next_lsn(enc), cleanup, "stream length must match encoder next_lsn");

   /* compare against the store's materialized segment 0 bytes */
   pgmoneta_snprintf(seg_path, sizeof(seg_path), "%s/000000010000000000000000", downstream_dir);
   store_bytes = read_file_bytes(seg_path, &store_len);
   MCTF_ASSERT_PTR_NONNULL(store_bytes, cleanup, "could not read store segment");
   MCTF_ASSERT(store_len >= stream_len, cleanup, "store segment must cover the stream");
   MCTF_ASSERT_INT_EQ(memcmp(store_bytes, tail, stream_len), 0, cleanup, "encoder bytes must match store bytes");

cleanup:
   if (chunk)
   {
      free(chunk);
   }
   free(tail);
   free(store_bytes);
   destroy_decoded_record(rec1);
   destroy_decoded_record(rec2);
   destroy_decoded_record(rec3);
   destroy_decoded_record(rec4);
   if (enc)
   {
      pgmoneta_wal_encoder_destroy(enc);
   }
   if (store)
   {
      pgmoneta_wal_store_destroy(store);
   }
   pgmoneta_lsn_map_destroy(map);
   unlink(map_path);
   if (seg_path[0])
   {
      unlink(seg_path);
   }
   rmdir(downstream_dir);
   rmdir(base);
   MCTF_FINISH();
}

/* An encoder resumed at a mid-stream downstream LSN must emit exactly the
 * tail of an encoder that streamed the whole prefix: reconnect at D must look
 * identical to a client that never disconnected. */
MCTF_TEST(test_walbridge_encoder_resume_identity)
{
   struct lsn_map* map = NULL;
   struct wal_encoder* enc = NULL;
   struct wal_encoder* resumed = NULL;
   struct decoded_xlog_record* rec1 = NULL;
   struct decoded_xlog_record* rec2 = NULL;
   struct decoded_xlog_record* rec3 = NULL;
   struct decoded_xlog_record* rec4 = NULL;
   char base[PATH_MAX];
   char map_path[PATH_MAX];
   char* full = NULL;
   char* rtail = NULL;
   char* chunk = NULL;
   size_t spare = 0;
   size_t full_len = 0;
   size_t rtail_len = 0;
   uint8_t prune[2] = {0x01, 0x07};
   uint64_t before2 = 0;
   uint64_t down2 = 0;
   uint64_t after2 = 0;

   pgmoneta_snprintf(base, sizeof(base), "%s", "/tmp/walbridge_test_encresume");
   pgmoneta_mkdir(base);
   pgmoneta_snprintf(map_path, sizeof(map_path), "%s/lsn.map", base);
   unlink(map_path);

   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_create(map_path, &map), 0, cleanup, "lsn_map_create failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_create(123456789, DEFAULT_WAL_SEGZ_BYTES, 8192, 1, false, &enc), 0, cleanup, "encoder create failed");

   rec1 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x10000000, prune, sizeof(prune));
   rec2 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x20000000, prune, sizeof(prune));
   rec3 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x30000000, prune, sizeof(prune));
   rec4 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x40000000, prune, sizeof(prune));
   MCTF_ASSERT_PTR_NONNULL(rec1, cleanup, "create rec1 failed");
   MCTF_ASSERT_PTR_NONNULL(rec2, cleanup, "create rec2 failed");
   MCTF_ASSERT_PTR_NONNULL(rec3, cleanup, "create rec3 failed");
   MCTF_ASSERT_PTR_NONNULL(rec4, cleanup, "create rec4 failed");

   for (int i = 0; i < 4; i++)
   {
      struct decoded_xlog_record* r = i == 0 ? rec1 : (i == 1 ? rec2 : (i == 2 ? rec3 : rec4));
      MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(r, WAL_MAGIC_V18, WAL_MAGIC_V19, map, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate failed");
      if (i == 1)
      {
         before2 = pgmoneta_wal_encoder_next_lsn(enc);
      }
      MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_write_record(enc, r, NULL), 0, cleanup, "encoder write failed");
      if (i == 1)
      {
         after2 = pgmoneta_wal_encoder_next_lsn(enc);
         MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_put(map, 0x20000000, before2), 0, cleanup, "put rec2 failed");
      }
      MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_take(enc, &chunk, &spare), 0, cleanup, "encoder take failed");
      if (chunk)
      {
         MCTF_ASSERT_INT_EQ(concat_bytes(&full, &full_len, chunk, spare), 0, cleanup, "append full failed");
         free(chunk);
         chunk = NULL;
      }
   }

   /* the map (as maintained by the receiver) must agree with the encoder's
    * own placement of rec2: both give the downstream LSN where rec2 starts */
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_downstream(map, 0x20000000, &down2), 0, cleanup, "down2 lookup failed");
   MCTF_ASSERT_INT_EQ((int)down2, (int)before2, cleanup, "map down2 must match encoder placement");
   MCTF_ASSERT(after2 > down2, cleanup, "next_lsn must follow rec2");

   /* a fresh client resuming precisely at after2 must see only the new tail */
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_create(123456789, DEFAULT_WAL_SEGZ_BYTES, 8192, 1, false, &resumed), 0, cleanup, "resumed encoder create failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_resume(resumed, down2, after2), 0, cleanup, "encoder resume failed");
   for (int i = 2; i < 4; i++)
   {
      struct decoded_xlog_record* r = i == 2 ? rec3 : rec4;
      MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_write_record(resumed, r, NULL), 0, cleanup, "resumed encoder write failed");
      MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_take(resumed, &chunk, &spare), 0, cleanup, "resumed encoder take failed");
      if (chunk)
      {
         MCTF_ASSERT_INT_EQ(concat_bytes(&rtail, &rtail_len, chunk, spare), 0, cleanup, "append rtail failed");
         free(chunk);
         chunk = NULL;
      }
   }
   MCTF_ASSERT(rtail != NULL, cleanup, "resumed tail must not be empty");
   MCTF_ASSERT_INT_EQ((int)rtail_len, (int)(full_len - after2), cleanup, "resumed tail length must match full-stream tail");
   MCTF_ASSERT_INT_EQ(memcmp(full + after2, rtail, rtail_len), 0, cleanup, "resumed tail bytes must match full-stream tail bytes");

cleanup:
   if (chunk)
   {
      free(chunk);
   }
   free(full);
   free(rtail);
   destroy_decoded_record(rec1);
   destroy_decoded_record(rec2);
   destroy_decoded_record(rec3);
   destroy_decoded_record(rec4);
   if (resumed)
   {
      pgmoneta_wal_encoder_destroy(resumed);
   }
   if (enc)
   {
      pgmoneta_wal_encoder_destroy(enc);
   }
   pgmoneta_lsn_map_destroy(map);
   unlink(map_path);
   rmdir(base);
   MCTF_FINISH();
}

/* Resuming the encoder exactly on a page boundary must still emit the page
 * header: the peer possesses nothing of that page yet, so the header bytes must
 * be produced (previously page_emitted was set to page_fill, dropping them).
 * Both the short header (intra-segment page) and the long header (segment
 * base) must round-trip. */
MCTF_TEST(test_walbridge_encoder_resume_at_boundary)
{
   struct wal_encoder* enc = NULL;
   struct decoded_xlog_record* rec = NULL;
   uint8_t prune[2] = {0x01, 0x07};
   uint32_t seg_size = DEFAULT_WAL_SEGZ_BYTES;
   uint64_t sysid = 123456789ULL;
   char* chunk = NULL;
   size_t n = 0;

   rec = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x10000000, prune, sizeof(prune));
   MCTF_ASSERT_PTR_NONNULL(rec, cleanup, "create rec failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate failed");

   /* short header: resume on an intra-segment page boundary */
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_create(sysid, seg_size, 8192, 1, true, &enc), 0, cleanup, "encoder create failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_resume(enc, 0, 8192), 0, cleanup, "resume at page boundary failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_write_record(enc, rec, NULL), 0, cleanup, "write after boundary resume failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_take(enc, &chunk, &n), 0, cleanup, "take after boundary resume failed");
   MCTF_ASSERT(chunk != NULL && n >= 20, cleanup, "emitted bytes must include the short page header");
   {
      uint16_t magic;
      uint64_t pageaddr = 0;
      memcpy(&magic, chunk, 2);
      memcpy(&pageaddr, chunk + 8, 8);
      MCTF_ASSERT_INT_EQ((int)magic, (int)WAL_MAGIC_V19, cleanup, "short page magic must be V19");
      MCTF_ASSERT_INT_EQ((int)(uint8_t)chunk[2], 0, cleanup, "short page info must be clear");
      MCTF_ASSERT_INT_EQ((int)pageaddr, (int)8192, cleanup, "short page pageaddr must be 8192");
   }
   free(chunk);
   chunk = NULL;
   pgmoneta_wal_encoder_destroy(enc);
   enc = NULL;

   /* long header: resume exactly on a segment base */
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_create(sysid, seg_size, 8192, 1, true, &enc), 0, cleanup, "encoder create failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_resume(enc, 0, seg_size), 0, cleanup, "resume at segment base failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_write_record(enc, rec, NULL), 0, cleanup, "write after segment resume failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_take(enc, &chunk, &n), 0, cleanup, "take after segment resume failed");
   MCTF_ASSERT(chunk != NULL && n >= 40, cleanup, "emitted bytes must include the long page header");
   {
      uint16_t magic;
      uint64_t pageaddr = 0;
      uint64_t sysid_out = 0;
      memcpy(&magic, chunk, 2);
      memcpy(&pageaddr, chunk + 8, 8);
      memcpy(&sysid_out, chunk + 24, 8);
      MCTF_ASSERT_INT_EQ((int)magic, (int)WAL_MAGIC_V19, cleanup, "long page magic must be V19");
      MCTF_ASSERT_INT_EQ((int)(uint8_t)chunk[2], (int)XLP_LONG_HEADER, cleanup, "long page info must mark the long header");
      MCTF_ASSERT_INT_EQ((int)pageaddr, (int)seg_size, cleanup, "long page pageaddr must be the segment base");
      MCTF_ASSERT_INT_EQ((int)sysid_out, (int)sysid, cleanup, "long page sysid must round-trip");
   }

cleanup:
   if (chunk)
   {
      free(chunk);
   }
   if (enc)
   {
      pgmoneta_wal_encoder_destroy(enc);
   }
   destroy_decoded_record(rec);
   MCTF_FINISH();
}

/* XLOG_END_OF_RECOVERY must be handled explicitly (validated, kept verbatim):
 * the 18.x payload layout is identical to 19.x, so no reshaping is allowed. */
MCTF_TEST(test_walbridge_migration_end_of_recovery)
{
   struct decoded_xlog_record* rec = NULL;
   uint8_t payload[20];
   uint64_t* ts = (uint64_t*)payload;
   uint32_t* tli = (uint32_t*)(payload + 8);
   uint32_t* prev = (uint32_t*)(payload + 12);
   int* wl = (int*)(payload + 16);
   int result = -1;

   *ts = 123456789;
   *tli = 2;
   *prev = 1;
   *wl = WAL_LEVEL_REPLICA;

   rec = create_decoded_record(RM_XLOG_ID, XLOG_END_OF_RECOVERY, 0x50000000, payload, sizeof(payload));
   MCTF_ASSERT_PTR_NONNULL(rec, cleanup, "create rec failed");

   result = pgmoneta_migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, NULL, true);
   MCTF_ASSERT_INT_EQ(result, PGMONETA_MIGRATION_KEEP, cleanup, "end of recovery must be kept verbatim");
   MCTF_ASSERT_INT_EQ((int)rec->main_data_len, (int)sizeof(payload), cleanup, "end of recovery payload must stay 20 bytes");
   MCTF_ASSERT_INT_EQ(memcmp(rec->main_data, payload, sizeof(payload)), 0, cleanup, "end of recovery bytes must be preserved");

cleanup:
   destroy_decoded_record(rec);
   MCTF_FINISH();
}

/* The end-of-recovery record must ride the renderer (encoder) untouched:
 * KEEP at the migration layer, then byte-identical through the store and the
 * encoder, with a valid recomputed xl_crc on the wire. */
MCTF_TEST(test_walbridge_encoder_end_of_recovery)
{
   struct lsn_map* map = NULL;
   struct wal_store* store = NULL;
   struct wal_encoder* enc = NULL;
   struct decoded_xlog_record* rec = NULL;
   struct walfile* wf = NULL;
   struct deque_iterator* iter = NULL;
   struct decoded_xlog_record* parsed = NULL;
   uint8_t eor[20];
   uint64_t* ts = (uint64_t*)eor;
   uint32_t* tli = (uint32_t*)(eor + 8);
   uint32_t* prev = (uint32_t*)(eor + 12);
   int* wl = (int*)(eor + 16);
   char base[PATH_MAX];
   char map_path[PATH_MAX];
   char downstream_dir[PATH_MAX];
   char seg_path[PATH_MAX] = "";
   char* store_bytes = NULL;
   char* stream = NULL;
   char* chunk = NULL;
   char* rebuf = NULL;
   size_t store_len = 0;
   size_t stream_len = 0;
   size_t spare = 0;
   uint32_t wal_size = 1024 * 1024;
   uint32_t rcrc = 0;
   int result = -1;

   *ts = 123456789;
   *tli = 2;
   *prev = 1;
   *wl = WAL_LEVEL_REPLICA;

   pgmoneta_snprintf(base, sizeof(base), "%s", "/tmp/walbridge_test_eoenc");
   pgmoneta_mkdir(base);
   pgmoneta_snprintf(map_path, sizeof(map_path), "%s/lsn.map", base);
   pgmoneta_snprintf(downstream_dir, sizeof(downstream_dir), "%s/wal", base);
   unlink(map_path);

   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_create(map_path, &map), 0, cleanup, "lsn_map_create failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_create(downstream_dir, map, 123456789, wal_size, 8192, 1, &store), 0, cleanup, "store create failed");
   pgmoneta_wal_store_set_checksums(store, true);
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_create(123456789, wal_size, 8192, 1, true, &enc), 0, cleanup, "encoder create failed");

   rec = create_decoded_record(RM_XLOG_ID, XLOG_END_OF_RECOVERY, 0x50000000, eor, sizeof(eor));
   MCTF_ASSERT_PTR_NONNULL(rec, cleanup, "create rec failed");

   result = pgmoneta_migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, map, true);
   MCTF_ASSERT_INT_EQ(result, PGMONETA_MIGRATION_KEEP, cleanup, "end of recovery must be kept verbatim");

   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(store, rec), 0, cleanup, "store write failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_sync_partial_page(store), 0, cleanup, "store sync failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_flush(store), 0, cleanup, "store flush failed");

   MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_write_record(enc, rec, NULL), 0, cleanup, "encoder write failed");
   while (true)
   {
      MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_take(enc, &chunk, &spare), 0, cleanup, "encoder take failed");
      if (!chunk)
      {
         break;
      }
      MCTF_ASSERT_INT_EQ(concat_bytes(&stream, &stream_len, chunk, spare), 0, cleanup, "append stream failed");
      free(chunk);
      chunk = NULL;
   }
   MCTF_ASSERT(stream_len > 0, cleanup, "encoder stream must not be empty");

   /* the renderer output must be a byte-copy of what the store materializes */
   pgmoneta_snprintf(seg_path, sizeof(seg_path), "%s/000000010000000000000000", downstream_dir);
   store_bytes = read_file_bytes(seg_path, &store_len);
   MCTF_ASSERT_PTR_NONNULL(store_bytes, cleanup, "could not read store segment");
   MCTF_ASSERT(store_len >= stream_len, cleanup, "store segment must cover the stream");
   MCTF_ASSERT_INT_EQ(memcmp(store_bytes, stream, stream_len), 0, cleanup, "renderer bytes must match store bytes");

   /* parse the store segment as a real V19 segment and verify the record */
   wf = calloc(1, sizeof(*wf));
   MCTF_ASSERT_PTR_NONNULL(wf, cleanup, "could not allocate walfile");
   MCTF_ASSERT_INT_EQ(pgmoneta_deque_create(false, &wf->records), 0, cleanup, "records deque create failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_deque_create(false, &wf->page_headers), 0, cleanup, "page headers deque create failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_parse_wal_file(seg_path, 0, wf), 0, cleanup, "parse of renderer output failed");

   if (pgmoneta_deque_iterator_create(wf->records, &iter) == 0)
   {
      while (pgmoneta_deque_iterator_next(iter))
      {
         struct decoded_xlog_record* p = (struct decoded_xlog_record*)iter->value->data;
         if (p && (p->header.xl_rmid == RM_XLOG_ID) &&
             ((p->header.xl_info & ~XLR_INFO_MASK) == XLOG_END_OF_RECOVERY))
         {
            parsed = p;
            break;
         }
      }
      pgmoneta_deque_iterator_destroy(iter);
      iter = NULL;
   }
   MCTF_ASSERT_PTR_NONNULL(parsed, cleanup, "end of recovery record not found in renderer output");
   MCTF_ASSERT_INT_EQ((int)parsed->main_data_len, (int)sizeof(eor), cleanup, "end of recovery payload must stay 20 bytes");
   MCTF_ASSERT_INT_EQ(memcmp(parsed->main_data, eor, sizeof(eor)), 0, cleanup, "end of recovery bytes must be preserved");

   /* the wire CRC must validate against the recomputed crc32c */
   rebuf = pgmoneta_wal_encode_xlog_record(parsed, WAL_MAGIC_V19, NULL);
   MCTF_ASSERT_PTR_NONNULL(rebuf, cleanup, "re-encode failed");
   rcrc = pgmoneta_wal_store_compute_crc(rebuf, ((struct xlog_record*)rebuf)->xl_tot_len);
   MCTF_ASSERT_INT_EQ((int)rcrc, (int)parsed->header.xl_crc, cleanup, "recomputed CRC must equal the wire CRC");

cleanup:
   free(chunk);
   free(stream);
   free(store_bytes);
   free(rebuf);
   destroy_decoded_record(rec);
   if (wf)
   {
      if (iter)
      {
         pgmoneta_deque_iterator_destroy(iter);
      }
      pgmoneta_destroy_walfile(wf);
   }
   if (enc)
   {
      pgmoneta_wal_encoder_destroy(enc);
   }
   if (store)
   {
      pgmoneta_wal_store_destroy(store);
   }
   pgmoneta_lsn_map_destroy(map);
   unlink(map_path);
   if (seg_path[0])
   {
      unlink(seg_path);
   }
   rmdir(downstream_dir);
   rmdir(base);
   MCTF_FINISH();
}

MCTF_TEST(test_walbridge_encoder_reserves_switch_space)
{
   struct lsn_map* map = NULL;
   struct lsn_map* seed_map = NULL;
   struct wal_store* store = NULL;
   struct wal_store* seed = NULL;
   struct wal_encoder* enc = NULL;
   struct decoded_xlog_record* rec = NULL;
   struct decoded_xlog_record* filler = NULL;
   char base[PATH_MAX];
   char map_path[PATH_MAX];
   char seed_map_path[PATH_MAX];
   char downstream_dir[PATH_MAX];
   bool found = false;
   uint32_t wal_size = 64 * 1024;
   uint64_t resume_lsn = wal_size - 64;
   uint64_t seed_xl_prev = 0;
   uint64_t seed_next_lsn = 0;
   uint64_t store_xl_prev = 0;
   uint64_t store_next_lsn = 0;
   uint64_t enc_xl_prev = 0;
   uint64_t enc_next_lsn = 0;

   pgmoneta_snprintf(base, sizeof(base), "%s", "/tmp/walbridge_test_switch_space");
   pgmoneta_mkdir(base);
   pgmoneta_snprintf(map_path, sizeof(map_path), "%s/lsn.map", base);
   pgmoneta_snprintf(seed_map_path, sizeof(seed_map_path), "%s/seed.map", base);
   pgmoneta_snprintf(downstream_dir, sizeof(downstream_dir), "%s/wal", base);
   unlink(map_path);
   unlink(seed_map_path);

   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_create(map_path, &map), 0, cleanup, "lsn_map_create failed");

   /* resume() reconstructs a stream from bytes already on disk, so the segment
    * must hold a real record that reaches past the resume point; otherwise the
    * tail-page read back fails on a store that was created but never written.
    * Write one large filler through a throwaway store into the same directory
    * and resume the real store+encoder where that filler left the stream. */
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_create(seed_map_path, &seed_map), 0, cleanup, "seed lsn_map_create failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_create(downstream_dir, seed_map, 987654321, wal_size, 8192, 1, &seed), 0, cleanup, "seed store create failed");
   pgmoneta_wal_store_set_checksums(seed, false);

   for (uint32_t image_size = 65350; image_size < 65440; image_size++)
   {
      uint32_t total_len;
      char* encoded = NULL;
      struct decoded_xlog_record* cand = create_fpi_record(0, image_size);
      if (!cand)
      {
         continue;
      }
      encoded = pgmoneta_wal_encode_xlog_record(cand, WAL_MAGIC_V19, NULL);
      destroy_decoded_record(cand);
      if (!encoded)
      {
         continue;
      }
      total_len = ((struct xlog_record*)encoded)->xl_tot_len;
      free(encoded);

      if (SIZE_OF_XLOG_LONG_PHD + MAXALIGN(total_len) > resume_lsn &&
          SIZE_OF_XLOG_LONG_PHD + MAXALIGN(total_len) <= wal_size - SIZE_OF_XLOG_RECORD)
      {
         filler = create_fpi_record(0, image_size);
         MCTF_ASSERT_PTR_NONNULL(filler, cleanup, "create filler record failed");
         break;
      }
   }
   MCTF_ASSERT(filler != NULL, cleanup, "no filler record reached the segment tail");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(seed, filler), 0, cleanup, "seed store write failed");
   pgmoneta_wal_store_get_state(seed, NULL, &seed_xl_prev, &seed_next_lsn);
   MCTF_ASSERT(seed_next_lsn > resume_lsn, cleanup, "filler did not advance the stream past the resume point");
   pgmoneta_wal_store_destroy(seed);
   seed = NULL;
   pgmoneta_lsn_map_destroy(seed_map);
   seed_map = NULL;
   unlink(seed_map_path);

   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_create(downstream_dir, map, 987654321, wal_size, 8192, 1, &store), 0, cleanup, "store create failed");
   pgmoneta_wal_store_set_checksums(store, false);
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_create(987654321, wal_size, 8192, 1, false, &enc), 0, cleanup, "encoder create failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_resume(store, seed_xl_prev, resume_lsn), 0, cleanup, "store resume failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_resume(enc, seed_xl_prev, resume_lsn), 0, cleanup, "encoder resume failed");

   for (uint32_t image_size = 1; image_size < 1024; image_size += 8)
   {
      uint32_t total_len;
      char* encoded = NULL;
      struct decoded_xlog_record* cand = create_fpi_record(0x10000000, image_size);
      if (!cand)
      {
         continue;
      }
      if (pgmoneta_migration_engine_translate(cand, WAL_MAGIC_V18, WAL_MAGIC_V19, map, false) != 0)
      {
         destroy_decoded_record(cand);
         continue;
      }
      encoded = pgmoneta_wal_encode_xlog_record(cand, WAL_MAGIC_V19, NULL);
      destroy_decoded_record(cand);
      if (!encoded)
      {
         continue;
      }
      total_len = ((struct xlog_record*)encoded)->xl_tot_len;
      free(encoded);

      if (resume_lsn + MAXALIGN(total_len) <= wal_size &&
          resume_lsn + MAXALIGN(total_len) > wal_size - SIZE_OF_XLOG_RECORD)
      {
         rec = create_fpi_record(0x10000000, image_size);
         MCTF_ASSERT_PTR_NONNULL(rec, cleanup, "create record failed");
         MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, map, false), 0, cleanup, "translate failed");
         found = true;
         break;
      }
   }
   MCTF_ASSERT(found, cleanup, "no candidate record exercised the segment-tail reservation gap");

   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(store, rec), 0, cleanup, "store write failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_write_record(enc, rec, NULL), 0, cleanup, "encoder write failed");

   pgmoneta_wal_store_get_state(store, NULL, &store_xl_prev, &store_next_lsn);
   enc_xl_prev = pgmoneta_wal_encoder_xl_prev(enc);
   enc_next_lsn = pgmoneta_wal_encoder_next_lsn(enc);
   MCTF_ASSERT(enc_xl_prev == store_xl_prev, cleanup, "encoder xl_prev diverged from store at the switch boundary");
   MCTF_ASSERT(enc_next_lsn == store_next_lsn, cleanup, "encoder next_lsn diverged from store at the switch boundary");
   MCTF_ASSERT(store_next_lsn > resume_lsn, cleanup, "write did not advance the downstream stream");

cleanup:
   destroy_decoded_record(rec);
   destroy_decoded_record(filler);
   if (enc)
   {
      pgmoneta_wal_encoder_destroy(enc);
   }
   if (store)
   {
      pgmoneta_wal_store_destroy(store);
   }
   if (seed)
   {
      pgmoneta_wal_store_destroy(seed);
   }
   pgmoneta_lsn_map_destroy(seed_map);
   pgmoneta_lsn_map_destroy(map);
   unlink(seed_map_path);
   unlink(map_path);
   rmdir(downstream_dir);
   rmdir(base);
   MCTF_FINISH();
}

MCTF_TEST(test_walbridge_encoder_rotation_matches_store)
{
   struct lsn_map* map = NULL;
   struct wal_store* store = NULL;
   struct wal_encoder* enc = NULL;
   struct decoded_xlog_record* recs[24] = {0};
   char base[PATH_MAX];
   char map_path[PATH_MAX];
   char downstream_dir[PATH_MAX];
   char seg0_path[PATH_MAX] = "";
   char seg1_path[PATH_MAX] = "";
   char* seg0 = NULL;
   char* seg1 = NULL;
   char* stream = NULL;
   char* chunk = NULL;
   size_t seg0_len = 0;
   size_t seg1_len = 0;
   size_t spare = 0;
   size_t stream_len = 0;
   uint32_t wal_size = 1 * 1024 * 1024;

   pgmoneta_snprintf(base, sizeof(base), "%s", "/tmp/walbridge_test_encrot");
   pgmoneta_mkdir(base);
   pgmoneta_snprintf(map_path, sizeof(map_path), "%s/lsn.map", base);
   pgmoneta_snprintf(downstream_dir, sizeof(downstream_dir), "%s/wal", base);
   unlink(map_path);

   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_create(map_path, &map), 0, cleanup, "lsn_map_create failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_create(downstream_dir, map, 987654321, wal_size, 8192, 1, &store), 0, cleanup, "store create failed");
   pgmoneta_wal_store_set_checksums(store, false);
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_create(987654321, wal_size, 8192, 1, false, &enc), 0, cleanup, "encoder create failed");

   /* ~65.5KB records: 24 of them are ~1.5MB, forcing the store and the encoder
    * through the same downstream segment rotation point */
   for (int i = 0; i < 24; i++)
   {
      recs[i] = create_fpi_record(0x10000000 + (uint64_t)i * 0x800000, 65500);
      MCTF_ASSERT_PTR_NONNULL(recs[i], cleanup, "create rec failed");
      MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(recs[i], WAL_MAGIC_V18, WAL_MAGIC_V19, map, true), 0, cleanup, "translate failed");
      MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(store, recs[i]), 0, cleanup, "store write failed");
      MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_sync_partial_page(store), 0, cleanup, "store sync failed");
      MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_write_record(enc, recs[i], NULL), 0, cleanup, "encoder write failed");
      MCTF_ASSERT_INT_EQ(pgmoneta_wal_encoder_take(enc, &chunk, &spare), 0, cleanup, "encoder take failed");
      if (chunk)
      {
         MCTF_ASSERT_INT_EQ(concat_bytes(&stream, &stream_len, chunk, spare), 0, cleanup, "append stream failed");
         free(chunk);
         chunk = NULL;
      }
   }

   MCTF_ASSERT(stream_len > wal_size + 8192, cleanup, "stream must span the rotation point");

   /* the stream must equal segment 0 (padded) followed by segment 1 */
   pgmoneta_snprintf(seg0_path, sizeof(seg0_path), "%s/000000010000000000000000", downstream_dir);
   pgmoneta_snprintf(seg1_path, sizeof(seg1_path), "%s/000000010000000000000001", downstream_dir);
   seg0 = read_file_bytes(seg0_path, &seg0_len);
   seg1 = read_file_bytes(seg1_path, &seg1_len);
   MCTF_ASSERT_PTR_NONNULL(seg0, cleanup, "could not read segment 0");
   MCTF_ASSERT_PTR_NONNULL(seg1, cleanup, "could not read segment 1");
   MCTF_ASSERT_INT_EQ((int)seg0_len, (int)wal_size, cleanup, "rotated segment 0 must be fully padded");
   MCTF_ASSERT_INT_EQ((int)(seg0_len + seg1_len), (int)stream_len, cleanup, "segments must sum to the stream length");
   MCTF_ASSERT_INT_EQ(memcmp(seg0, stream, seg0_len), 0, cleanup, "encoder bytes must match segment 0");
   MCTF_ASSERT_INT_EQ(memcmp(seg1, stream + seg0_len, seg1_len), 0, cleanup, "encoder bytes must match segment 1");

cleanup:
   if (chunk)
   {
      free(chunk);
   }
   free(stream);
   free(seg0);
   free(seg1);
   for (int i = 0; i < 24; i++)
   {
      destroy_decoded_record(recs[i]);
   }
   if (enc)
   {
      pgmoneta_wal_encoder_destroy(enc);
   }
   if (store)
   {
      pgmoneta_wal_store_destroy(store);
   }
   pgmoneta_lsn_map_destroy(map);
   unlink(map_path);
   if (seg0_path[0])
   {
      unlink(seg0_path);
   }
   if (seg1_path[0])
   {
      unlink(seg1_path);
   }
   rmdir(downstream_dir);
   rmdir(base);
   MCTF_FINISH();
}

/* ============================================================================
 * Socket-level sender integration tests
 *
 * These drive pgmoneta_walbridge_run_sender() in a forked child over a real
 * TCP loopback socket using a PG-wire-protocol client (startup, SCRAM-SHA-256
 * authentication, simple queries, START_REPLICATION / CopyBoth streaming,
 * StandbyStatusUpdate feedback and CopyDone). They exercise Phase A (verbatim
 * serving of the archived downstream store, with byte-identity against the
 * store segment files, exact-record-boundary reconnects and feedback
 * persistence) and Phase B (live transcode of upstream V18 WAL after the
 * store is exhausted, with byte-identity at the handoff boundary).
 *
 * The harness uses a distinct loopback port and a private base directory so a
 * concurrently running pgmoneta-walbridge (e.g. on :9970) is never touched.
 * ========================================================================== */

/* wire protocol message types (mirrors wal_sender.c) */
#define WB_MSG_AUTH           'R'
#define WB_MSG_QUERY          'Q'
#define WB_MSG_TERMINATE      'X'
#define WB_MSG_PARAM          'S'
#define WB_MSG_ROW_DESC       'T'
#define WB_MSG_DATA_ROW       'D'
#define WB_MSG_COPY_BOTH      'W'
#define WB_MSG_COPY_DATA      'd'
#define WB_MSG_COPY_DONE      'c'
#define WB_MSG_COMMAND        'C'
#define WB_MSG_ERROR          'E'
#define WB_MSG_READY          'Z'

#define WB_XLOG_DATA          'w'
#define WB_PRIMARY_KEEPALIVE  'k'
#define WB_STANDBY_STATUS     'r'

#define WB_PROTOCOL_VERSION_3 (3 << 16)
#define WB_AUTH_SASL          10
#define WB_AUTH_SASL_CONT     11
#define WB_AUTH_SASL_FINAL    12
#define WB_AUTH_OK            0

static uint32_t
wb_be32(uint32_t v)
{
   uint32_t n = v;
   unsigned char* p = (unsigned char*)&n;
   return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static uint16_t
wb_be16(uint16_t v)
{
   uint16_t n = v;
   unsigned char* p = (unsigned char*)&n;
   return (uint16_t)((uint16_t)p[0] << 8 | (uint16_t)p[1]);
}

static uint64_t
wb_be64(uint64_t v)
{
   uint64_t hi = (uint64_t)wb_be32((uint32_t)(v >> 32));
   uint64_t lo = (uint64_t)wb_be32((uint32_t)v);
   return (lo << 32) | hi;
}

static int
wb_write_fully(int fd, const void* buf, size_t n)
{
   const char* p = buf;
   while (n > 0)
   {
      ssize_t w = write(fd, p, n);
      if (w < 0)
      {
         if (errno == EINTR)
         {
            continue;
         }
         return 1;
      }
      p += w;
      n -= (size_t)w;
   }
   return 0;
}

static int
wb_read_fully(int fd, void* buf, size_t n, int timeout_ms)
{
   char* p = buf;
   while (n > 0)
   {
      struct pollfd pfd;
      int pr;

      pfd.fd = fd;
      pfd.events = POLLIN;
      pfd.revents = 0;
      pr = poll(&pfd, 1, timeout_ms);
      if (pr < 0)
      {
         if (errno == EINTR)
         {
            continue;
         }
         return 1;
      }
      if (pr == 0)
      {
         errno = ETIMEDOUT;
         return 1;
      }
      if (!(pfd.revents & (POLLIN | POLLHUP)))
      {
         usleep(1000);
         continue;
      }
      {
         ssize_t r = read(fd, p, n);
         if (r < 0)
         {
            if (errno == EINTR)
            {
               continue;
            }
            return 1;
         }
         if (r == 0)
         {
            errno = ECONNRESET;
            return 1;
         }
         p += r;
         n -= (size_t)r;
      }
   }
   return 0;
}

static int
wb_send_msg(int fd, char type, const void* body, uint32_t body_len)
{
   char hdr[5];
   uint32_t net = wb_be32(body_len + 4);

   hdr[0] = type;
   memcpy(hdr + 1, &net, 4);
   if (wb_write_fully(fd, hdr, 5))
   {
      return 1;
   }
   if (body_len > 0 && wb_write_fully(fd, body, body_len))
   {
      return 1;
   }
   return 0;
}

static int
wb_recv_msg_timeout(int fd, char* type, unsigned char** body, uint32_t* body_len, int timeout_ms)
{
   uint32_t net;

   *body = NULL;
   *body_len = 0;
   if (wb_read_fully(fd, type, 1, timeout_ms))
   {
      return 1;
   }
   if (wb_read_fully(fd, &net, 4, timeout_ms))
   {
      return 1;
   }
   net = wb_be32(net);
   if (net < 4)
   {
      return 1;
   }
   net -= 4;
   if (net > 0)
   {
      *body = malloc(net);
      if (*body == NULL)
      {
         return 1;
      }
      if (wb_read_fully(fd, *body, net, timeout_ms))
      {
         free(*body);
         *body = NULL;
         return 1;
      }
   }
   *body_len = net;
   return 0;
}

static int
wb_recv_msg(int fd, char* type, unsigned char** body, uint32_t* body_len)
{
   return wb_recv_msg_timeout(fd, type, body, body_len, 15000);
}

/* SCRAM-SHA-256 client: returns 0 on success. Validates the server signature. */
static int
wb_scram_client(int fd, const char* username, const char* password)
{
   char cnonce[64];
   char client_first[256];
   char client_first_bare[256];
   char* server_first = NULL;
   char* sattr = NULL;
   char* rattr = NULL;
   char* iattr = NULL;
   char client_final_wo[512];
   char* proof_b64 = NULL;
   size_t proof_b64_len = 0;
   char* client_final = NULL;
   char* vattr = NULL;
   char* sig_b64 = NULL;
   size_t sig_b64_len = 0;
   unsigned char* sig = NULL;
   size_t sig_len = 0;
   unsigned char* salt = NULL;
   size_t salt_len = 0;
   unsigned char* proof = NULL;
   size_t proof_len = 0;
   uint32_t code;
   char type;
   unsigned char* body = NULL;
   uint32_t body_len = 0;
   int salt_iters = 0;
   int rc = 1;

   pgmoneta_snprintf(cnonce, sizeof(cnonce), "wbtest%u", (unsigned)getpid());
   pgmoneta_snprintf(client_first, sizeof(client_first), "n,,n=%s,r=%s", username, cnonce);
   pgmoneta_snprintf(client_first_bare, sizeof(client_first_bare), "n=%s,r=%s", username, cnonce);

   /* initial response payload: mechanism\0 + Int32(len) + client-first */
   {
      size_t mlen = strlen("SCRAM-SHA-256") + 1;
      size_t cflen = strlen(client_first);
      unsigned char* payload = malloc(mlen + 4 + cflen);
      if (payload == NULL)
      {
         goto cleanup;
      }
      memcpy(payload, "SCRAM-SHA-256", mlen);
      {
         uint32_t net = wb_be32((uint32_t)cflen);
         memcpy(payload + mlen, &net, 4);
      }
      memcpy(payload + mlen + 4, client_first, cflen);
      rc = wb_send_msg(fd, 'p', payload, (uint32_t)(mlen + 4 + cflen));
      free(payload);
      if (rc)
      {
         goto cleanup;
      }
   }

   /* AuthenticationSASLContinue */
   if (wb_recv_msg(fd, &type, &body, &body_len) || type != WB_MSG_AUTH || body_len < 8)
   {
      goto cleanup;
   }
   memcpy(&code, body, 4);
   if (wb_be32(code) != WB_AUTH_SASL_CONT)
   {
      goto cleanup;
   }
   server_first = malloc(body_len - 4 + 1);
   if (server_first == NULL)
   {
      goto cleanup;
   }
   memcpy(server_first, body + 4, body_len - 4);
   server_first[body_len - 4] = '\0';
   free(body);
   body = NULL;

   rattr = NULL;
   sattr = NULL;
   iattr = NULL;
   {
      char* p = server_first;
      while (p && *p)
      {
         char* eq = strchr(p, '=');
         char* comma = strchr(p, ',');
         char* end = comma ? comma : p + strlen(p);
         if (eq && eq < end)
         {
            size_t vlen = (size_t)(end - eq - 1);
            if (p[0] == 'r')
            {
               free(rattr);
               rattr = malloc(vlen + 1);
               memcpy(rattr, eq + 1, vlen);
               rattr[vlen] = '\0';
            }
            else if (p[0] == 's')
            {
               free(sattr);
               sattr = malloc(vlen + 1);
               memcpy(sattr, eq + 1, vlen);
               sattr[vlen] = '\0';
            }
            else if (p[0] == 'i')
            {
               free(iattr);
               iattr = malloc(vlen + 1);
               memcpy(iattr, eq + 1, vlen);
               iattr[vlen] = '\0';
            }
         }
         p = comma ? comma + 1 : NULL;
      }
   }
   if (rattr == NULL || sattr == NULL || iattr == NULL || strncmp(rattr, cnonce, strlen(cnonce)) != 0)
   {
      goto cleanup;
   }
   salt_iters = atoi(iattr);
   if (salt_iters <= 0)
   {
      goto cleanup;
   }
   if (pgmoneta_base64_decode(sattr, strlen(sattr), (void**)&salt, &salt_len))
   {
      goto cleanup;
   }

   pgmoneta_snprintf(client_final_wo, sizeof(client_final_wo), "c=biws,r=%s", rattr);
   if (pgmoneta_client_proof((char*)password, (char*)salt, (int)salt_len, salt_iters,
                             client_first_bare, strlen(client_first_bare),
                             server_first, strlen(server_first),
                             client_final_wo, strlen(client_final_wo),
                             &proof, &proof_len))
   {
      goto cleanup;
   }
   if (pgmoneta_base64_encode(proof, proof_len, &proof_b64, &proof_b64_len))
   {
      goto cleanup;
   }
   {
      size_t clen = strlen(client_final_wo) + 3 + proof_b64_len + 1;
      client_final = malloc(clen);
      if (client_final == NULL)
      {
         goto cleanup;
      }
      pgmoneta_snprintf(client_final, clen, "%s,p=%s", client_final_wo, proof_b64);
   }
   rc = wb_send_msg(fd, 'p', client_final, (uint32_t)strlen(client_final));
   if (rc)
   {
      goto cleanup;
   }

   /* AuthenticationSASLFinal: v=<server signature> */
   if (wb_recv_msg(fd, &type, &body, &body_len) || type != WB_MSG_AUTH || body_len < 8)
   {
      goto cleanup;
   }
   memcpy(&code, body, 4);
   if (wb_be32(code) != WB_AUTH_SASL_FINAL)
   {
      goto cleanup;
   }
   vattr = malloc(body_len - 4 + 1);
   if (vattr == NULL)
   {
      goto cleanup;
   }
   memcpy(vattr, body + 4, body_len - 4);
   vattr[body_len - 4] = '\0';
   free(body);
   body = NULL;

   if (pgmoneta_server_signature((char*)password, (char*)salt, (int)salt_len, salt_iters,
                                 NULL, 0,
                                 client_first_bare, strlen(client_first_bare),
                                 server_first, strlen(server_first),
                                 client_final_wo, strlen(client_final_wo),
                                 &sig, &sig_len))
   {
      goto cleanup;
   }
   if (pgmoneta_base64_encode(sig, sig_len, &sig_b64, &sig_b64_len))
   {
      goto cleanup;
   }
   if (strcmp(vattr + 2, sig_b64) != 0)
   {
      goto cleanup;
   }

   /* AuthenticationOk */
   if (wb_recv_msg(fd, &type, &body, &body_len) || type != WB_MSG_AUTH || body_len < 4)
   {
      goto cleanup;
   }
   memcpy(&code, body, 4);
   if (wb_be32(code) != WB_AUTH_OK)
   {
      goto cleanup;
   }
   free(body);
   body = NULL;

   /* ParameterStatus + ReadyForQuery */
   rc = 1;
   while (true)
   {
      if (wb_recv_msg(fd, &type, &body, &body_len))
      {
         goto cleanup;
      }
      if (type == WB_MSG_PARAM)
      {
         free(body);
         body = NULL;
         continue;
      }
      if (type == WB_MSG_READY)
      {
         rc = 0;
         break;
      }
      goto cleanup;
   }

cleanup:
   free(body);
   free(server_first);
   free(rattr);
   free(sattr);
   free(iattr);
   free(vattr);
   free(proof_b64);
   free(client_final);
   free(sig_b64);
   free(sig);
   free(salt);
   free(proof);
   return rc;
}

static int
wb_connect_auth(int port, const char* username, const char* password)
{
   int fd = -1;
   struct sockaddr_in addr;
   int rc;

   fd = socket(AF_INET, SOCK_STREAM, 0);
   if (fd < 0)
   {
      return -1;
   }
   memset(&addr, 0, sizeof(addr));
   addr.sin_family = AF_INET;
   addr.sin_port = htons((uint16_t)port);
   addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
   {
      close(fd);
      return -1;
   }

   /* startup message: Int32 len, Int32 version, key\0value\0...key\0value\0\0 */
   {
      char payload[128];
      char* p = payload + 8;
      uint32_t ver = wb_be32(WB_PROTOCOL_VERSION_3);

      memcpy(p, "user", 4);
      p += 4;
      *p++ = '\0';
      memcpy(p, username, strlen(username));
      p += strlen(username);
      *p++ = '\0';
      memcpy(p, "database", 8);
      p += 8;
      *p++ = '\0';
      memcpy(p, "postgres", 8);
      p += 8;
      *p++ = '\0';
      memcpy(p, "replication", 11);
      p += 11;
      *p++ = '\0';
      memcpy(p, "true", 4);
      p += 4;
      *p++ = '\0';
      *p++ = '\0';
      {
         uint32_t plen = (uint32_t)(p - (payload + 8));
         uint32_t len = wb_be32((uint32_t)(8 + plen));
         memcpy(payload, &len, 4);
         memcpy(payload + 4, &ver, 4);
         if (wb_write_fully(fd, payload, 8 + plen))
         {
            close(fd);
            return -1;
         }
      }
   }

   /* AuthenticationSASL first */
   {
      char type = 0;
      unsigned char* body = NULL;
      uint32_t body_len = 0;
      uint32_t code;

      if (wb_recv_msg(fd, &type, &body, &body_len) || type != WB_MSG_AUTH || body_len < 18)
      {
         if (body)
         {
            free(body);
         }
         close(fd);
         return -1;
      }
      memcpy(&code, body, 4);
      if (wb_be32(code) != WB_AUTH_SASL || memcmp(body + 4, "SCRAM-SHA-256", 13) != 0)
      {
         free(body);
         close(fd);
         return -1;
      }
      free(body);
   }

   rc = wb_scram_client(fd, username, password);
   if (rc)
   {
      close(fd);
      return -1;
   }
   return fd;
}

/* Simple query: send the query and read until ReadyForQuery. Any DataRow
 * payloads are appended to *csv. Returns 0 on query success, 1 on error. */
static int
wb_query(int fd, const char* query, unsigned char** csv, size_t* csv_len)
{
   char type;
   unsigned char* body = NULL;
   uint32_t body_len = 0;
   size_t qlen = strlen(query) + 1;
   int rc = 1;

   if (wb_send_msg(fd, WB_MSG_QUERY, query, (uint32_t)qlen))
   {
      return 1;
   }
   while (true)
   {
      if (wb_recv_msg(fd, &type, &body, &body_len))
      {
         return 1;
      }
      if (type == WB_MSG_DATA_ROW)
      {
         if (csv)
         {
            if (concat_bytes((char**)csv, csv_len, (const char*)body, body_len))
            {
               free(body);
               return 1;
            }
         }
      }
      else if (type == WB_MSG_ERROR || type == WB_MSG_TERMINATE)
      {
         free(body);
         return 1;
      }
      else if (type == WB_MSG_READY)
      {
         rc = 0;
         free(body);
         break;
      }
      free(body);
   }
   return rc;
}

/* Run a simple query that returns a single-row, single-column text result
 * (SHOW wal_level etc.) via the binary DataRow wire format and copy the value
 * into out. Returns 0 on success (including a SQL NULL result). */
static int
wb_query_text(int fd, const char* query, char* out, size_t outsz)
{
   char type;
   unsigned char* body = NULL;
   uint32_t body_len = 0;
   size_t qlen = strlen(query) + 1;
   int rc = 1;

   if (wb_send_msg(fd, WB_MSG_QUERY, query, (uint32_t)qlen))
   {
      return 1;
   }
   while (true)
   {
      if (wb_recv_msg(fd, &type, &body, &body_len))
      {
         return 1;
      }
      if (type == WB_MSG_DATA_ROW)
      {
         uint16_t ncols = 0;
         int32_t vlen = 0;
         if (body_len >= 6)
         {
            uint16_t ncn;
            int32_t vln;
            memcpy(&ncn, body, 2);
            ncols = wb_be16(ncn);
            memcpy(&vln, body + 2, 4);
            vlen = (int32_t)wb_be32((uint32_t)vln);
            if (ncols == 1 && vlen >= 0 && (uint32_t)vlen <= body_len - 6 &&
                (size_t)vlen < outsz)
            {
               memcpy(out, body + 6, (size_t)vlen);
               out[vlen] = '\0';
               rc = 0;
            }
         }
         free(body);
         body = NULL;
         break;
      }
      else if (type == WB_MSG_ERROR || type == WB_MSG_TERMINATE)
      {
         free(body);
         return 1;
      }
      else if (type == WB_MSG_READY)
      {
         free(body);
         break;
      }
      free(body);
      body = NULL;
   }
   return rc;
}

static uint64_t
wb_parse_identify_position(const unsigned char* csv, size_t csv_len)
{
   uint64_t pos = 0;
   int col = 0;
   size_t i = 0;

   while (i < csv_len && col < 3)
   {
      size_t end = i;
      while (end < csv_len && csv[end] != '\0')
      {
         end++;
      }
      if (col == 2)
      {
         unsigned int hi = 0, lo = 0;
         sscanf((const char*)csv + i, "%X/%X", &hi, &lo);
         pos = ((uint64_t)hi << 32) | lo;
         break;
      }
      col++;
      i = end + 1;
   }
   return pos;
}

/* START_REPLICATION: returns 0 once CopyBothResponse has been received. */
static int
wb_start_replication(int fd, uint64_t start_lsn)
{
   char query[64];
   char type;
   unsigned char* body = NULL;
   uint32_t body_len = 0;
   int rc = 1;

   pgmoneta_snprintf(query, sizeof(query), "START_REPLICATION %X/%X",
                     (uint32_t)(start_lsn >> 32), (uint32_t)start_lsn);
   if (wb_send_msg(fd, WB_MSG_QUERY, query, (uint32_t)(strlen(query) + 1)))
   {
      return 1;
   }
   while (true)
   {
      if (wb_recv_msg(fd, &type, &body, &body_len))
      {
         return 1;
      }
      if (type == WB_MSG_COPY_BOTH)
      {
         rc = 0;
         free(body);
         break;
      }
      if (type == WB_MSG_ERROR || type == WB_MSG_TERMINATE)
      {
         free(body);
         return 1;
      }
      free(body);
   }
   return rc;
}

/* Send a StandbyStatusUpdate ('r') feedback message. */
static int
wb_send_feedback(int fd, uint64_t flush_lsn)
{
   unsigned char body[25];

   body[0] = WB_STANDBY_STATUS;
   memcpy(body + 1, &(uint64_t){wb_be64(flush_lsn)}, 8);
   memcpy(body + 9, &(uint64_t){wb_be64(flush_lsn)}, 8);
   memcpy(body + 17, &(uint64_t){wb_be64((uint64_t)time(NULL))}, 8);
   body[25 - 1] = 0;
   return wb_send_msg(fd, WB_MSG_COPY_DATA, body, 25);
}

/* Read the COPY BOTH stream. For every WALData message:
 *  - asserts wire-frame continuity (start == expected frame offset)
 *  - compares the payload against the expected byte buffer
 * Propagates *stop when the peer has delivered byte bytes beyond the frame
 * base set by wb_stream_base_start (defaults to start_lsn).
 * Returns 0 on success, 1 on protocol/compare error. */
struct wb_stream_ctx
{
   uint64_t frame_base;   /* LSN of the first byte of the first WALData chunk */
   uint64_t delivered;    /* bytes delivered so far */
   uint64_t compare_base; /* absolute LSN with which delivered is compared */
};

static int
wb_stream_read(int fd, const char* expected, size_t expected_len, uint64_t base_lsn,
               uint64_t want, struct wb_stream_ctx* st, bool* copydone)
{
   char type;
   unsigned char* body = NULL;
   uint32_t body_len = 0;
   int rc = 0;

   *copydone = false;
   while (st->delivered < want)
   {
      if (wb_recv_msg(fd, &type, &body, &body_len))
      {
         rc = 1;
         goto cleanup;
      }
      if (type == WB_MSG_COPY_DATA)
      {
         if (body_len >= 25 && body[0] == WB_PRIMARY_KEEPALIVE)
         {
            free(body);
            body = NULL;
            continue;
         }
         if (body_len >= 25 && body[0] == WB_XLOG_DATA)
         {
            uint64_t wraw;
            uint64_t wstart;
            uint64_t wend;
            uint32_t dlen = body_len - 25;
            uint64_t frame_lsn = st->compare_base + st->delivered;

            memcpy(&wraw, body + 1, 8);
            wstart = wb_be64(wraw);
            memcpy(&wraw, body + 9, 8);
            wend = wb_be64(wraw);
            if (wstart != frame_lsn)
            {
               MCTF_ASSERT_INT_EQ((int)(wstart >> 32), (int)(frame_lsn >> 32), cleanup, "WALData frame start LSN mismatch");
               MCTF_ASSERT_INT_EQ((int)wstart, (int)frame_lsn, cleanup, "WALData frame start LSN mismatch");
            }
            if (wend != frame_lsn + dlen)
            {
               MCTF_ASSERT_INT_EQ((int)(wend >> 32), (int)((frame_lsn + dlen) >> 32), cleanup, "WALData frame end LSN mismatch");
               MCTF_ASSERT_INT_EQ((int)wend, (int)(frame_lsn + dlen), cleanup, "WALData frame end LSN mismatch");
            }
            if (st->delivered + dlen > expected_len)
            {
               MCTF_ASSERT_INT_EQ((int)dlen, 0, cleanup, "expected byte buffer too short");
            }
            if (memcmp(body + 25, expected + st->delivered, dlen) != 0)
            {
               int first = -1;
               for (uint32_t k = 0; k < dlen; k++)
               {
                  if (body[25 + k] != (unsigned char)expected[st->delivered + k])
                  {
                     first = (int)k;
                     break;
                  }
               }
               MCTF_ASSERT_INT_EQ(first, -1, cleanup, "WALData payload byte mismatch");
            }
            st->delivered += dlen;
            free(body);
            body = NULL;
            continue;
         }
         MCTF_ASSERT_INT_EQ((int)body[0], WB_XLOG_DATA, cleanup, "unexpected CopyData kind");
      }
      else if (type == WB_MSG_COPY_DONE || type == WB_MSG_READY || type == WB_MSG_ERROR)
      {
         if (type == WB_MSG_COPY_DONE)
         {
            *copydone = true;
         }
         rc = 1;
         free(body);
         goto cleanup;
      }
      free(body);
   }

cleanup:
   if (body)
   {
      free(body);
   }
   return rc;
}

/* Prepare shared-memory config for the sender child. */
static void
wb_config_setup(char* base, int port, uint32_t wal_size, bool checksums)
{
   struct main_configuration* config = (struct main_configuration*)shmem;

   memset(config->walbridge_host, 0, sizeof(config->walbridge_host));
   pgmoneta_snprintf(config->walbridge_host, sizeof(config->walbridge_host), "127.0.0.1");
   config->walbridge = port;
   pgmoneta_snprintf(config->base_dir, sizeof(config->base_dir), "%s", base);
   config->common.number_of_servers = 1;
   memset(&config->common.servers[0], 0, sizeof(config->common.servers[0]));
   pgmoneta_snprintf(config->common.servers[0].name, sizeof(config->common.servers[0].name), "primary");
   config->common.servers[0].wal_size = (int)wal_size;
   config->common.servers[0].checksums = checksums;
   config->common.servers[0].valid = true;
   config->common.number_of_users = 1;
   pgmoneta_snprintf(config->common.users[0].username, sizeof(config->common.users[0].username), "repl");
   pgmoneta_snprintf(config->common.users[0].password, sizeof(config->common.users[0].password), "replpass");
}

/* Fork a sender serving downstream_dir/map_path on the configured port.
 * Returns the child pid, or -1. */
static pid_t
wb_start_sender(char* downstream_dir, char* map_path)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   pid_t pid;
   int fd;
   int i;

   config->running = true;

   pid = fork();
   if (pid < 0)
   {
      return -1;
   }
   if (pid == 0)
   {
      int rc = pgmoneta_walbridge_run_sender(0, downstream_dir, map_path);
      _exit(rc);
   }

   /* poll-connect until the listener is up */
   for (i = 0; i < 100; i++)
   {
      fd = socket(AF_INET, SOCK_STREAM, 0);
      if (fd >= 0)
      {
         struct sockaddr_in addr;
         memset(&addr, 0, sizeof(addr));
         addr.sin_family = AF_INET;
         addr.sin_port = htons((uint16_t)config->walbridge);
         addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
         if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0)
         {
            close(fd);
            return pid;
         }
         close(fd);
      }
      usleep(50000);
   }
   kill(pid, SIGKILL);
   waitpid(pid, NULL, 0);
   return -1;
}

static void
wb_stop_sender(pid_t pid)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   config->running = false;
   if (pid > 0)
   {
      int st = 0;
      kill(pid, SIGTERM);
      for (int i = 0; i < 40; i++)
      {
         if (waitpid(pid, &st, WNOHANG) == pid)
         {
            return;
         }
         usleep(50000);
      }
      kill(pid, SIGKILL);
      waitpid(pid, NULL, 0);
   }
}

/* T1: Phase A — verbatim store serving, exact-record-boundary reconnect,
 * feedback persistence, mid-stream disconnect. */
MCTF_TEST(test_walbridge_sender_phase_a)
{
   struct lsn_map* map = NULL;
   struct wal_store* store = NULL;
   struct decoded_xlog_record* P1 = NULL;
   struct decoded_xlog_record* P2 = NULL;
   struct decoded_xlog_record* P3 = NULL;
   struct decoded_xlog_record* P4 = NULL;
   char base[PATH_MAX];
   char map_path[PATH_MAX];
   char downstream_dir[PATH_MAX];
   char seg_path[PATH_MAX] = "";
   char* store_bytes = NULL;
   size_t store_len = 0;
   uint64_t down2 = 0;
   uint64_t down3 = 0;
   uint64_t down4 = 0;
   char* fb = NULL;
   size_t fb_len = 0;
   int fd = -1;
   pid_t spid = -1;
   uint8_t prune[2] = {0x01, 0x07};
   int wal_level = 2;
   struct wb_stream_ctx st;
   bool copydone = false;
   int port = 19551;

   pgmoneta_snprintf(base, sizeof(base), "%s", "/tmp/walbridge_test_socka");
   pgmoneta_mkdir(base);
   pgmoneta_snprintf(map_path, sizeof(map_path), "%s/lsn.map", base);
   pgmoneta_snprintf(downstream_dir, sizeof(downstream_dir), "%s/wal", base);
   pgmoneta_mkdir(downstream_dir);
   pgmoneta_snprintf(seg_path, sizeof(seg_path), "%s/000000010000000000000000", downstream_dir);

   unlink(map_path);

   wb_config_setup(base, port, DEFAULT_WAL_SEGZ_BYTES, false);

   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_create(map_path, &map), 0, cleanup, "lsn map create failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_create(downstream_dir, map, 123456789, DEFAULT_WAL_SEGZ_BYTES, 8192, 1, &store), 0, cleanup, "store create failed");

   P1 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x10000000, prune, sizeof(prune));
   P2 = create_decoded_record(RM_XLOG_ID, XLOG_CHECKPOINT_REDO, 0x20000000, &wal_level, sizeof(wal_level));
   P3 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, 0x30000000, prune, sizeof(prune));
   P4 = create_decoded_record(RM_XLOG_ID, XLOG_CHECKPOINT_REDO, 0x40000000, &wal_level, sizeof(wal_level));
   MCTF_ASSERT_PTR_NONNULL(P1, cleanup, "create P1 failed");
   MCTF_ASSERT_PTR_NONNULL(P2, cleanup, "create P2 failed");
   MCTF_ASSERT_PTR_NONNULL(P3, cleanup, "create P3 failed");
   MCTF_ASSERT_PTR_NONNULL(P4, cleanup, "create P4 failed");

   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(P1, WAL_MAGIC_V18, WAL_MAGIC_V19, map, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate P1 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(store, P1), 0, cleanup, "write P1 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(P2, WAL_MAGIC_V18, WAL_MAGIC_V19, map, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate P2 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(store, P2), 0, cleanup, "write P2 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(P3, WAL_MAGIC_V18, WAL_MAGIC_V19, map, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate P3 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(store, P3), 0, cleanup, "write P3 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_migration_engine_translate(P4, WAL_MAGIC_V18, WAL_MAGIC_V19, map, false), PGMONETA_MIGRATION_MODIFIED, cleanup, "translate P4 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_write_record(store, P4), 0, cleanup, "write P4 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_wal_store_flush(store), 0, cleanup, "store flush failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_downstream(map, 0x20000000, &down2), 0, cleanup, "map down P2 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_downstream(map, 0x30000000, &down3), 0, cleanup, "map down P3 failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_downstream(map, 0x40000000, &down4), 0, cleanup, "map down P4 failed");

   MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_flush(map), 0, cleanup, "map flush failed");

   store_bytes = read_file_bytes(seg_path, &store_len);
   MCTF_ASSERT_PTR_NONNULL(store_bytes, cleanup, "read store segment failed");

   spid = wb_start_sender(downstream_dir, map_path);
   MCTF_ASSERT_INT_EQ(spid > 0, 1, cleanup, "start sender failed");

   /* full phase-A stream from 0/0 */
   fd = wb_connect_auth(port, "repl", "replpass");
   MCTF_ASSERT_INT_EQ(fd >= 0, 1, cleanup, "connect/auth failed");

   {
      unsigned char* csv = NULL;
      size_t csv_len = 0;
      MCTF_ASSERT_INT_EQ(wb_query(fd, "IDENTIFY_SYSTEM", &csv, &csv_len), 0, cleanup, "IDENTIFY_SYSTEM failed");
      {
         uint64_t ip = wb_parse_identify_position(csv, csv_len);
         (void)ip;
      }
      free(csv);
   }
   {
      char wl[64] = "";
      MCTF_ASSERT_INT_EQ(wb_query_text(fd, "SHOW wal_level", wl, sizeof(wl)), 0, cleanup, "SHOW wal_level failed");
      MCTF_ASSERT_INT_EQ(strcmp(wl, "replica") == 0, 1, cleanup, "SHOW wal_level not replica");
   }

   MCTF_ASSERT_INT_EQ(wb_start_replication(fd, 0), 0, cleanup, "START_REPLICATION failed");
   memset(&st, 0, sizeof(st));
   MCTF_ASSERT_INT_EQ(wb_stream_read(fd, store_bytes, store_len, 0, store_len, &st, &copydone), 0, cleanup, "phase-A byte identity failed");
   MCTF_ASSERT_INT_EQ(st.delivered, store_len, cleanup, "phase-A delivered count mismatch");

   /* disconnect mid-stream: CopyDone then Terminate */
   wb_send_msg(fd, WB_MSG_COPY_DONE, NULL, 0);
   close(fd);
   fd = -1;

   /* reconnect at an exact mid-stream record boundary (down3): bytes from
    * that record's start to the end of the store must be byte-identical,
    * with a continuous wire-frame sequence starting at down3. */
   fd = wb_connect_auth(port, "repl", "replpass");
   MCTF_ASSERT_INT_EQ(fd >= 0, 1, cleanup, "re-connect/auth failed");
   MCTF_ASSERT_INT_EQ(wb_start_replication(fd, down3), 0, cleanup, "re-START_REPLICATION failed");
   memset(&st, 0, sizeof(st));
   st.compare_base = down3;
   st.frame_base = down3;
   MCTF_ASSERT_INT_EQ(wb_stream_read(fd, store_bytes + down3, store_len - down3, down3, store_len - down3, &st, &copydone), 0, cleanup, "reconnect byte identity failed");
   MCTF_ASSERT_INT_EQ(st.delivered, store_len - down3, cleanup, "reconnect delivered count mismatch");

   /* feedback while streaming: persist the downstream flushed position */
   {
      uint64_t fb_down = down4 + 1;
      uint64_t expect_up = 0;
      char expect[128];
      char fb_path[PATH_MAX];

      MCTF_ASSERT_INT_EQ(pgmoneta_lsn_map_get_upstream_at_or_before(map, fb_down, &expect_up), 0, cleanup, "map lookup for feedback failed");
      MCTF_ASSERT_INT_EQ(wb_send_feedback(fd, fb_down), 0, cleanup, "feedback send failed");

      pgmoneta_snprintf(fb_path, sizeof(fb_path), "%s.feedback", map_path);
      pgmoneta_snprintf(expect, sizeof(expect), "%lu %lu\n", (unsigned long)expect_up, (unsigned long)fb_down);
      for (int i = 0; i < 50 && !(fb && fb_len > 0); i++)
      {
         free(fb);
         fb = NULL;
         fb_len = 0;
         fb = read_file_bytes(fb_path, &fb_len);
         if (!(fb && fb_len > 0))
         {
            usleep(100000);
         }
      }
      MCTF_ASSERT_PTR_NONNULL(fb, cleanup, "feedback file was not written");
      MCTF_ASSERT_INT_EQ(fb_len == strlen(expect) && memcmp(fb, expect, strlen(expect)) == 0,
                         1, cleanup, "feedback file content mismatch");
      free(fb);
      fb = NULL;
      fb_len = 0;
   }

   /* stop and clean up */
   wb_stop_sender(spid);
   spid = -1;

cleanup:
   if (fd >= 0)
   {
      close(fd);
   }
   wb_stop_sender(spid);
   spid = -1;
   destroy_decoded_record(P1);
   destroy_decoded_record(P2);
   destroy_decoded_record(P3);
   destroy_decoded_record(P4);
   if (store)
   {
      pgmoneta_wal_store_destroy(store);
   }
   pgmoneta_lsn_map_destroy(map);
   free(store_bytes);
   free(fb);
   if (seg_path[0])
   {
      unlink(seg_path);
   }
   unlink("/tmp/walbridge_test_socka/lsn.map");
   unlink("/tmp/walbridge_test_socka/lsn.map.feedback");
   rmdir(downstream_dir);
   rmdir(base);
   MCTF_FINISH();
}

/* Shared topology for the live-tail sender tests: a downstream archive holding
 * a complete 32 KiB segment 0, an upstream PostgreSQL 18 live segment 1 (three
 * records), and the byte-exact expected stream (archived bytes + translated
 * live tail). wb_lt_setup() builds it and wb_lt_teardown() cleans it up. */
struct wb_live_tail;

static void wb_lt_teardown(struct wb_live_tail* t);

struct wb_live_tail
{
   char base[PATH_MAX];
   char map_path[PATH_MAX];
   char downstream_dir[PATH_MAX];
   char upstream_dir[PATH_MAX];
   char seg_path[PATH_MAX];
   char up_seg_path[PATH_MAX];
   char* store_bytes;
   char* live_ref;
   char* expected;
   size_t store_len;
   size_t live_len;
   size_t expected_len;
   uint64_t floor_up;
   uint64_t floor_down;
   struct lsn_map* map;
   struct wal_store* store;
};

static int
wb_lt_setup(struct wb_live_tail* t, const char* base, int port)
{
   struct wal_encoder* upenc = NULL;
   struct wal_encoder* refenc = NULL;
   struct decoded_xlog_record* rec = NULL;
   struct decoded_xlog_record* L1 = NULL;
   struct decoded_xlog_record* L2 = NULL;
   struct decoded_xlog_record* L3 = NULL;
   char* chunk = NULL;
   char* upraw = NULL;
   size_t spare = 0;
   size_t upraw_len = 0;
   uint64_t segno = 0;
   uint64_t xl_prev = 0;
   uint64_t next = 0;
   uint8_t prune[2] = {0x01, 0x07};
   int wal_level = 2;
   uint64_t sysid = 123456789;
   uint32_t seg_size = 32768;
   uint32_t blksz = 8192;
   FILE* uf = NULL;

   memset(t, 0, sizeof(*t));

   pgmoneta_snprintf(t->base, sizeof(t->base), "%s", base);
   pgmoneta_mkdir(t->base);
   pgmoneta_snprintf(t->map_path, sizeof(t->map_path), "%s/lsn.map", t->base);
   pgmoneta_snprintf(t->downstream_dir, sizeof(t->downstream_dir), "%s/wal", t->base);
   pgmoneta_mkdir(t->downstream_dir);
   pgmoneta_snprintf(t->upstream_dir, sizeof(t->upstream_dir), "%s/primary", t->base);
   pgmoneta_mkdir(t->upstream_dir);
   pgmoneta_snprintf(t->upstream_dir, sizeof(t->upstream_dir), "%s/primary/wal", t->base);
   pgmoneta_mkdir(t->upstream_dir);
   pgmoneta_snprintf(t->seg_path, sizeof(t->seg_path), "%s/000000010000000000000000", t->downstream_dir);
   pgmoneta_snprintf(t->up_seg_path, sizeof(t->up_seg_path), "%s/000000010000000000000001", t->upstream_dir);

   unlink(t->map_path);
   unlink(t->seg_path);
   unlink(t->up_seg_path);

   wb_config_setup(t->base, port, seg_size, false);

   if (pgmoneta_lsn_map_create(t->map_path, &t->map) ||
       pgmoneta_wal_store_create(t->downstream_dir, t->map, sysid, seg_size, blksz, 1, &t->store) ||
       pgmoneta_wal_encoder_create(sysid, seg_size, blksz, 1, false, &upenc) ||
       pgmoneta_wal_encoder_create(sysid, seg_size, blksz, 1, false, &refenc))
   {
      goto fail;
   }

   /* Fill the downstream archive with a complete first segment. The consumer
    * must exhaust it byte-for-byte before the live tail begins, so the store
    * holds exactly one full segment (the flushed tail page pads up to 32768). */
   for (int i = 0; i < 800; i++)
   {
      pgmoneta_wal_store_get_state(t->store, &segno, &xl_prev, &next);
      if (next >= (uint64_t)seg_size)
      {
         break;
      }
      rec = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, next, prune, sizeof(prune));
      if (!rec)
      {
         goto fail;
      }
      if (pgmoneta_wal_store_write_record(t->store, rec))
      {
         destroy_decoded_record(rec);
         goto fail;
      }
      destroy_decoded_record(rec);
      rec = NULL;
   }
   if (next < (uint64_t)seg_size - blksz || next >= (uint64_t)seg_size)
   {
      goto fail;
   }
   if (pgmoneta_wal_store_flush(t->store) || pgmoneta_lsn_map_flush(t->map))
   {
      goto fail;
   }

   t->store_bytes = read_file_bytes(t->seg_path, &t->store_len);
   if (!t->store_bytes || t->store_len != seg_size)
   {
      goto fail;
   }

   /* Upstream V18 segment 1 (LSNs 32768..65535): three records, written raw
    * (pre-translation) exactly as the archive took them in, then padded to a
    * full segment with the V18 page magic. */
   L1 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, seg_size, prune, sizeof(prune));
   L2 = create_decoded_record(RM_XLOG_ID, XLOG_CHECKPOINT_REDO, seg_size, &wal_level, sizeof(wal_level));
   L3 = create_decoded_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_VACUUM_CLEANUP, seg_size, prune, sizeof(prune));
   if (!L1 || !L2 || !L3 ||
       pgmoneta_wal_encoder_write_record(upenc, L1, NULL) ||
       pgmoneta_wal_encoder_write_record(upenc, L2, NULL) ||
       pgmoneta_wal_encoder_write_record(upenc, L3, NULL))
   {
      goto fail;
   }

   while (true)
   {
      if (pgmoneta_wal_encoder_take(upenc, &chunk, &spare))
      {
         goto fail;
      }
      if (!chunk)
      {
         break;
      }
      if (concat_bytes(&upraw, &upraw_len, chunk, spare))
      {
         free(chunk);
         goto fail;
      }
      free(chunk);
      chunk = NULL;
   }
   if (upraw_len == 0)
   {
      goto fail;
   }
   upraw = realloc(upraw, seg_size);
   if (!upraw)
   {
      goto fail;
   }
   memset(upraw + upraw_len, 0, seg_size - upraw_len);
   upraw[0] = (char)0x18; /* WAL_MAGIC_V18 little-endian */
   upraw[1] = (char)0xD1;

   uf = fopen(t->up_seg_path, "wb");
   if (!uf || fwrite(upraw, 1, seg_size, uf) != seg_size)
   {
      if (uf)
      {
         fclose(uf);
      }
      free(upraw);
      upraw = NULL;
      goto fail;
   }
   fclose(uf);
   uf = NULL;
   free(upraw);
   upraw = NULL;

   /* The sender resolves the floor exactly like this: the greatest map entry
    * at or below the store size, then back to its downstream start. */
   if (pgmoneta_lsn_map_get_upstream_at_or_before(t->map, (uint64_t)seg_size, &t->floor_up) ||
       pgmoneta_lsn_map_get_downstream(t->map, t->floor_up, &t->floor_down) ||
       t->floor_up >= (uint64_t)seg_size)
   {
      goto fail;
   }

   /* Reference live bytes: resume at (floor_down, 32768) and feed the same
    * translated records the sender will decode from the upstream file. */
   if (pgmoneta_wal_encoder_resume(refenc, t->floor_down, (uint64_t)seg_size) ||
       pgmoneta_migration_engine_translate(L1, WAL_MAGIC_V18, WAL_MAGIC_V19, t->map, false) != PGMONETA_MIGRATION_MODIFIED ||
       pgmoneta_wal_encoder_write_record(refenc, L1, NULL) ||
       pgmoneta_migration_engine_translate(L2, WAL_MAGIC_V18, WAL_MAGIC_V19, t->map, false) != PGMONETA_MIGRATION_MODIFIED ||
       pgmoneta_wal_encoder_write_record(refenc, L2, NULL) ||
       pgmoneta_migration_engine_translate(L3, WAL_MAGIC_V18, WAL_MAGIC_V19, t->map, false) != PGMONETA_MIGRATION_MODIFIED ||
       pgmoneta_wal_encoder_write_record(refenc, L3, NULL))
   {
      goto fail;
   }

   while (true)
   {
      if (pgmoneta_wal_encoder_take(refenc, &chunk, &spare))
      {
         goto fail;
      }
      if (!chunk)
      {
         break;
      }
      if (concat_bytes(&t->live_ref, &t->live_len, chunk, spare))
      {
         free(chunk);
         goto fail;
      }
      free(chunk);
      chunk = NULL;
   }
   if (t->live_len == 0)
   {
      goto fail;
   }

   t->expected_len = t->store_len + t->live_len;
   t->expected = malloc(t->expected_len);
   if (!t->expected)
   {
      goto fail;
   }
   memcpy(t->expected, t->store_bytes, t->store_len);
   memcpy(t->expected + t->store_len, t->live_ref, t->live_len);

   pgmoneta_wal_encoder_destroy(upenc);
   pgmoneta_wal_encoder_destroy(refenc);
   destroy_decoded_record(L1);
   destroy_decoded_record(L2);
   destroy_decoded_record(L3);
   return 0;

fail:
   pgmoneta_log_error("wb_lt_setup: live-tail topology build failed");
   pgmoneta_wal_encoder_destroy(upenc);
   pgmoneta_wal_encoder_destroy(refenc);
   destroy_decoded_record(rec);
   destroy_decoded_record(L1);
   destroy_decoded_record(L2);
   destroy_decoded_record(L3);
   free(chunk);
   free(upraw);
   wb_lt_teardown(t);
   return 1;
}

static void
wb_lt_teardown(struct wb_live_tail* t)
{
   if (!t)
   {
      return;
   }
   if (t->store)
   {
      pgmoneta_wal_store_destroy(t->store);
      t->store = NULL;
   }
   if (t->map)
   {
      pgmoneta_lsn_map_destroy(t->map);
      t->map = NULL;
   }
   free(t->store_bytes);
   free(t->live_ref);
   free(t->expected);
   t->store_bytes = NULL;
   t->live_ref = NULL;
   t->expected = NULL;
   if (t->seg_path[0])
   {
      unlink(t->seg_path);
   }
   if (t->up_seg_path[0])
   {
      unlink(t->up_seg_path);
   }
   if (t->map_path[0])
   {
      unlink(t->map_path);
      char p[PATH_MAX];
      pgmoneta_snprintf(p, sizeof(p), "%s.feedback", t->map_path);
      unlink(p);
   }
   if (t->upstream_dir[0])
   {
      rmdir(t->upstream_dir);
      char p[PATH_MAX];
      pgmoneta_snprintf(p, sizeof(p), "%s/primary", t->base);
      rmdir(p);
      rmdir(t->downstream_dir);
      rmdir(t->base);
   }
}

/* Phase A -> Phase B handoff: a peer draining the whole downstream stream must
 * read the archived store segment verbatim and then continue at the exact next
 * byte with the translated upstream live tail. The first live record is chained
 * to the last archived record (xl_prev == floor_down) and the wire frame
 * sequence stays continuous across the boundary. */
MCTF_TEST(test_walbridge_sender_live_tail)
{
   struct wb_live_tail lt;
   int fd = -1;
   pid_t spid = -1;
   struct wb_stream_ctx st;
   bool copydone = false;

   MCTF_ASSERT_INT_EQ(wb_lt_setup(&lt, "/tmp/walbridge_test_live", 19552), 0, cleanup, "live-tail topology setup failed");

   spid = wb_start_sender(lt.downstream_dir, lt.map_path);
   MCTF_ASSERT_INT_EQ(spid > 0, 1, cleanup, "start sender failed");

   fd = wb_connect_auth(19552, "repl", "replpass");
   MCTF_ASSERT_INT_EQ(fd >= 0, 1, cleanup, "connect/auth failed");

   {
      unsigned char* csv = NULL;
      size_t csv_len = 0;
      MCTF_ASSERT_INT_EQ(wb_query(fd, "IDENTIFY_SYSTEM", &csv, &csv_len), 0, cleanup, "IDENTIFY_SYSTEM failed");
      {
         uint64_t ip = wb_parse_identify_position(csv, csv_len);
         (void)ip;
      }
      free(csv);
   }
   {
      char wl[64] = "";
      MCTF_ASSERT_INT_EQ(wb_query_text(fd, "SHOW wal_level", wl, sizeof(wl)), 0, cleanup, "SHOW wal_level failed");
      MCTF_ASSERT_INT_EQ(strcmp(wl, "replica") == 0, 1, cleanup, "SHOW wal_level not replica");
   }

   /* full stream: archived segment 0 then the live tail, as one continuous
    * frame sequence with byte identity through the handoff */
   MCTF_ASSERT_INT_EQ(wb_start_replication(fd, 0), 0, cleanup, "START_REPLICATION failed");
   memset(&st, 0, sizeof(st));
   MCTF_ASSERT_INT_EQ(wb_stream_read(fd, lt.expected, lt.expected_len, 0, lt.expected_len, &st, &copydone), 0, cleanup, "live-tail byte identity failed");
   MCTF_ASSERT_INT_EQ((int)st.delivered, (int)lt.expected_len, cleanup, "delivered count mismatch");
   MCTF_ASSERT_INT_EQ((int)(st.delivered - lt.store_len), (int)lt.live_len, cleanup, "live portion length mismatch");

   /* CopyDone and clean up */
   wb_send_msg(fd, WB_MSG_COPY_DONE, NULL, 0);
   close(fd);
   fd = -1;

   wb_stop_sender(spid);
   spid = -1;

cleanup:
   if (fd >= 0)
   {
      close(fd);
   }
   wb_stop_sender(spid);
   spid = -1;
   wb_lt_teardown(&lt);
   MCTF_FINISH();
}

/* Phase A -> Phase B cold resume: START_REPLICATION at the exact seam between
 * the archived store (downstream 0..32768) and the live upstream tail must
 * short-circuit Phase A (the downstream segment at 32768 does not exist) and
 * resume straight into the translated live stream. Byte identity is against
 * the live reference and frame LSNs continue from 32768, the position a
 * draining first connection finishes on. */
MCTF_TEST(test_walbridge_sender_live_tail_resume)
{
   struct wb_live_tail lt;
   int fd = -1;
   pid_t spid = -1;
   struct wb_stream_ctx st;
   bool copydone = false;

   MCTF_ASSERT_INT_EQ(wb_lt_setup(&lt, "/tmp/walbridge_test_resume", 19554), 0, cleanup, "live-tail topology setup failed");

   spid = wb_start_sender(lt.downstream_dir, lt.map_path);
   MCTF_ASSERT_INT_EQ(spid > 0, 1, cleanup, "start sender failed");

   fd = wb_connect_auth(19554, "repl", "replpass");
   MCTF_ASSERT_INT_EQ(fd >= 0, 1, cleanup, "connect/auth failed");

   MCTF_ASSERT_INT_EQ(wb_start_replication(fd, lt.store_len), 0, cleanup, "START_REPLICATION at seam failed");

   memset(&st, 0, sizeof(st));
   st.compare_base = lt.store_len;
   st.frame_base = lt.store_len;
   MCTF_ASSERT_INT_EQ(wb_stream_read(fd, lt.live_ref, lt.live_len, lt.store_len, lt.live_len, &st, &copydone), 0, cleanup, "seam resume byte identity failed");
   MCTF_ASSERT_INT_EQ((int)st.delivered, (int)lt.live_len, cleanup, "resume delivered count mismatch");

   wb_send_msg(fd, WB_MSG_TERMINATE, NULL, 0);
   close(fd);
   fd = -1;

   wb_stop_sender(spid);
   spid = -1;

cleanup:
   if (fd >= 0)
   {
      close(fd);
   }
   wb_stop_sender(spid);
   spid = -1;
   wb_lt_teardown(&lt);
   MCTF_FINISH();
}
/* Phase B keepalive and clean disconnect: after the live tail has been drained
 * the sender must keep the peer alive with PRIMARY_KEEPALIVE messages (18-byte
 * CopyData whose first byte is 'k', walEnd == last byte flushed). A CopyDone
 * issued while the sender is still inside the Phase B loop must be answered by
 * a COPY CommandComplete + ReadyForQuery, a Terminate must tear the session
 * down, and the sender must keep accepting the next client. */
MCTF_TEST(test_walbridge_sender_live_tail_keepalive)
{
   struct wb_live_tail lt;
   int fd = -1;
   pid_t spid = -1;
   struct wb_stream_ctx st;
   bool copydone = false;

   MCTF_ASSERT_INT_EQ(wb_lt_setup(&lt, "/tmp/walbridge_test_keepalive", 19553), 0, cleanup, "live-tail topology setup failed");

   spid = wb_start_sender(lt.downstream_dir, lt.map_path);
   MCTF_ASSERT_INT_EQ(spid > 0, 1, cleanup, "start sender failed");

   fd = wb_connect_auth(19553, "repl", "replpass");
   MCTF_ASSERT_INT_EQ(fd >= 0, 1, cleanup, "connect/auth failed");

   MCTF_ASSERT_INT_EQ(wb_start_replication(fd, 0), 0, cleanup, "START_REPLICATION failed");

   /* drain the full stream (archived segment 0 + live tail) */
   memset(&st, 0, sizeof(st));
   MCTF_ASSERT_INT_EQ(wb_stream_read(fd, lt.expected, lt.expected_len, 0, lt.expected_len, &st, &copydone), 0, cleanup, "stream identity failed");
   MCTF_ASSERT_INT_EQ((int)st.delivered, (int)lt.expected_len, cleanup, "delivered count mismatch");

   /* while parked on the live tail the sender must emit PRIMARY_KEEPALIVE;
    * its walEnd points at the last flushed byte of the downstream stream */
   {
      char type;
      unsigned char* body = NULL;
      uint32_t body_len = 0;
      uint64_t walend = 0;
      bool ka_ok = false;

      for (int i = 0; i < 8; i++)
      {
         if (wb_recv_msg(fd, &type, &body, &body_len))
         {
            break;
         }
         if (type == WB_MSG_COPY_DATA && body_len == 18 && body[0] == WB_PRIMARY_KEEPALIVE)
         {
            memcpy(&walend, body + 1, 8);
            walend = wb_be64(walend);
            MCTF_ASSERT_INT_EQ((int)(walend == (uint64_t)lt.expected_len - 1), 1, cleanup, "keepalive walEnd mismatch");
            MCTF_ASSERT_INT_EQ((int)body[17], 0, cleanup, "keepalive reply flag must be clear");
            ka_ok = true;
            free(body);
            body = NULL;
            break;
         }
         free(body);
         body = NULL;
      }
      MCTF_ASSERT_INT_EQ(ka_ok, 1, cleanup, "no PRIMARY_KEEPALIVE received on the live tail");
   }

   /* CopyDone mid-live-tail: the sender must stop the copy with CommandComplete
    * and ReadyForQuery, then accept a clean Terminate */
   wb_send_msg(fd, WB_MSG_COPY_DONE, NULL, 0);
   {
      char type;
      unsigned char* body = NULL;
      uint32_t body_len = 0;
      bool seen_cc = false;
      bool seen_ready = false;

      for (int i = 0; i < 8 && !(seen_cc && seen_ready); i++)
      {
         if (wb_recv_msg(fd, &type, &body, &body_len))
         {
            break;
         }
         if (type == WB_MSG_COMMAND && body_len == 7 && memcmp(body, "COPY 0", 6) == 0)
         {
            seen_cc = true;
         }
         else if (type == WB_MSG_READY && body_len == 1 && body[0] == 'I')
         {
            seen_ready = true;
         }
         free(body);
         body = NULL;
      }
      MCTF_ASSERT_INT_EQ(seen_cc, 1, cleanup, "CommandComplete after CopyDone not received");
      MCTF_ASSERT_INT_EQ(seen_ready, 1, cleanup, "ReadyForQuery after CopyDone not received");
   }

   wb_send_msg(fd, WB_MSG_TERMINATE, NULL, 0);
   close(fd);
   fd = -1;

   /* the accept loop must survive the first client: a fresh connection still
    * authenticates and answers IDENTIFY_SYSTEM */
   fd = wb_connect_auth(19553, "repl", "replpass");
   MCTF_ASSERT_INT_EQ(fd >= 0, 1, cleanup, "reconnect/auth failed");
   {
      unsigned char* csv = NULL;
      size_t csv_len = 0;
      MCTF_ASSERT_INT_EQ(wb_query(fd, "IDENTIFY_SYSTEM", &csv, &csv_len), 0, cleanup, "IDENTIFY_SYSTEM on reconnect failed");
      free(csv);
   }
   wb_send_msg(fd, WB_MSG_TERMINATE, NULL, 0);
   close(fd);
   fd = -1;

   wb_stop_sender(spid);
   spid = -1;

cleanup:
   if (fd >= 0)
   {
      close(fd);
   }
   wb_stop_sender(spid);
   spid = -1;
   wb_lt_teardown(&lt);
   MCTF_FINISH();
}
