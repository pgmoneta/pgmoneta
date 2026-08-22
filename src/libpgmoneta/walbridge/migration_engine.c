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
#include <logging.h>
#include <utils.h>
#include <walfile.h>
#include <walfile/rmgr.h>
#include <walfile/rm_heap.h>
#include <walfile/rm_gist.h>
#include <walfile/rm_mxact.h>
#include <walfile/pg_control.h>
#include <walbridge/migration_engine.h>
#include <walbridge/lsn_map.h>

/* system */
#include <stdlib.h>
#include <string.h>

/* PG19 xl_heap_prune flags (RM_HEAP2, see heapam_xlog.h) */
#ifndef XLHP_VM_ALL_VISIBLE
#define XLHP_VM_ALL_VISIBLE (1 << 8)
#endif
#ifndef XLHP_VM_ALL_FROZEN
#define XLHP_VM_ALL_FROZEN (1 << 9)
#endif

/* PG18 xl_heap_visible flags (visibilitymapdefs.h) */
#define VM_ALL_VISIBLE      0x01
#define VM_ALL_FROZEN       0x02
#define VM_XLOG_CATALOG_REL 0x04

/* PG18/PG19 checkpoint constants */
#define CHECKPOINT_V18_SIZE 80
#define CHECKPOINT_V19_SIZE 96

/* PG18 multixact record sizes */
#define MULTIXACT_CREATE_HDR_V18    12
#define MULTIXACT_TRUNCATE_SIZE_V18 20
#define MULTIXACT_TRUNCATE_SIZE_V19 16

#define INFO(x)                     ((x)->header.xl_info & ~XLR_INFO_MASK)

struct check_point_v19
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

static int translate_heap2_prune(struct decoded_xlog_record* record);
static int translate_heap2_visible(struct decoded_xlog_record* record);
static int translate_checkpoint(struct decoded_xlog_record* record);
static int translate_checkpoint_redo(struct decoded_xlog_record* record);
static int translate_multixact_create(struct decoded_xlog_record* record);
static int translate_multixact_truncate(struct decoded_xlog_record* record);
static int translate_gist_assign_lsn(struct decoded_xlog_record* record);

int
migration_engine_translate(struct decoded_xlog_record* record, uint16_t src_magic, uint16_t tgt_magic, struct lsn_map* map)
{
   uint8_t info;

   if (!record)
   {
      return 1;
   }

   (void)src_magic;
   (void)tgt_magic;
   (void)map;

   info = INFO(record);

   switch (record->header.xl_rmid)
   {
      case RM_HEAP2_ID:
         if (info == XLOG_HEAP2_PRUNE_ON_ACCESS ||
             info == XLOG_HEAP2_PRUNE_VACUUM_SCAN ||
             info == XLOG_HEAP2_PRUNE_VACUUM_CLEANUP)
         {
            return translate_heap2_prune(record);
         }
         else if (info == XLOG_HEAP2_VISIBLE)
         {
            return translate_heap2_visible(record);
         }
         break;
      case RM_XLOG_ID:
         if (info == XLOG_CHECKPOINT_SHUTDOWN || info == XLOG_CHECKPOINT_ONLINE)
         {
            return translate_checkpoint(record);
         }
         else if (info == XLOG_CHECKPOINT_REDO)
         {
            return translate_checkpoint_redo(record);
         }
         break;
      case RM_MULTIXACT_ID:
         if (info == XLOG_MULTIXACT_CREATE_ID)
         {
            return translate_multixact_create(record);
         }
         else if (info == XLOG_MULTIXACT_TRUNCATE_ID)
         {
            return translate_multixact_truncate(record);
         }
         break;
      case RM_GIST_ID:
         if (info == XLOG_GIST_ASSIGN_LSN)
         {
            return translate_gist_assign_lsn(record);
         }
         break;
      default:
         break;
   }

   /* passthrough */
   return 0;
}

static int
translate_heap2_prune(struct decoded_xlog_record* record)
{
   uint8_t flags;

   if (record->main_data_len < SizeOfHeapPruneV17)
   {
      pgmoneta_log_error("migration_engine: short heap prune record (%u bytes) at %X/%X",
                         record->main_data_len, LSN_FORMAT_ARGS(record->lsn));
      return 1;
   }

   flags = ((uint8_t*)record->main_data)[1];

   ((uint8_t*)record->main_data)[0] = flags;
   ((uint8_t*)record->main_data)[1] = 0;

   return 0;
}

static int
translate_heap2_visible(struct decoded_xlog_record* record)
{
   struct decoded_bkp_block heap;
   struct decoded_bkp_block vm;
   struct decoded_bkp_block* blk0 = &record->blocks[0];
   struct decoded_bkp_block* blk1 = &record->blocks[1];
   uint8_t vflags;
   transaction_id conflict;
   uint16_t flags = 0;
   size_t new_len;
   char* new_data = NULL;
   char* p;

   if (record->main_data_len < 5)
   {
      pgmoneta_log_error("migration_engine: short heap visible record (%u bytes) at %X/%X",
                         record->main_data_len, LSN_FORMAT_ARGS(record->lsn));
      return 1;
   }

   if (!blk0->in_use || !blk1->in_use)
   {
      pgmoneta_log_error("migration_engine: heap visible record at %X/%X lacks expected block references (vm=%d heap=%d)",
                         LSN_FORMAT_ARGS(record->lsn), blk0->in_use, blk1->in_use);
      return 1;
   }

   memcpy(&conflict, record->main_data, sizeof(transaction_id));
   vflags = ((uint8_t*)record->main_data)[4];

   flags |= XLHP_VM_ALL_VISIBLE;
   if (vflags & VM_ALL_FROZEN)
   {
      flags |= XLHP_VM_ALL_FROZEN;
   }
   if (vflags & VM_XLOG_CATALOG_REL)
   {
      flags |= XLHP_IS_CATALOG_REL;
   }
   if (conflict != INVALID_TRANSACTION_ID)
   {
      flags |= XLHP_HAS_CONFLICT_HORIZON;
   }

   new_len = sizeof(uint16_t) + ((flags & XLHP_HAS_CONFLICT_HORIZON) ? sizeof(transaction_id) : 0);

   new_data = malloc(new_len);
   if (!new_data)
   {
      pgmoneta_log_error("migration_engine: out of memory synthesizing prune record");
      return 1;
   }

   p = new_data;
   memcpy(p, &flags, sizeof(uint16_t));
   p += sizeof(uint16_t);
   if (flags & XLHP_HAS_CONFLICT_HORIZON)
   {
      memcpy(p, &conflict, sizeof(transaction_id));
   }

   heap = *blk1;
   vm = *blk0;

   /* block 0 can never use SAME_REL (no previous rel to inherit from) */
   heap.flags &= ~BKPBLOCK_SAME_REL;
   heap.forknum = MAIN_FORKNUM;
   heap.flags = (heap.flags & BKPBLOCK_FLAG_MASK) | MAIN_FORKNUM;

   vm.forknum = VISIBILITYMAP_FORKNUM;
   vm.flags = (vm.flags & BKPBLOCK_FLAG_MASK) | VISIBILITYMAP_FORKNUM;

   *blk0 = heap;
   *blk1 = vm;

   /* the record becomes a vacuum cleanup prune record */
   record->header.xl_info = (record->header.xl_info & XLR_INFO_MASK) | XLOG_HEAP2_PRUNE_VACUUM_CLEANUP;

   free(record->main_data);
   record->main_data = new_data;
   record->main_data_len = new_len;

   return 0;
}

static int
translate_checkpoint(struct decoded_xlog_record* record)
{
   struct check_point_v17 v17;
   struct check_point_v19 v19;
   char* new_data = NULL;

   if (record->main_data_len < CHECKPOINT_V18_SIZE)
   {
      pgmoneta_log_error("migration_engine: short checkpoint record (%u bytes) at %X/%X",
                         record->main_data_len, LSN_FORMAT_ARGS(record->lsn));
      return 1;
   }

   memset(&v17, 0, sizeof(v17));
   memcpy(&v17, record->main_data, CHECKPOINT_V18_SIZE);

   memset(&v19, 0, sizeof(v19));
   v19.redo = v17.redo;
   v19.this_timeline_id = v17.this_timeline_id;
   v19.prev_timeline_id = v17.prev_timeline_id;
   v19.full_page_writes = v17.full_page_writes;
   v19.wal_level = v17.wal_level;
   v19.logical_decoding_enabled = (v17.wal_level == WAL_LEVEL_LOGICAL);
   v19.next_xid = v17.next_xid;
   v19.next_oid = v17.next_oid;
   v19.next_multi = v17.next_multi;
   v19.next_multi_offset = (uint64_t)v17.next_multi_offset;
   v19.oldest_xid = v17.oldest_xid;
   v19.oldest_xid_db = v17.oldest_xid_db;
   v19.oldest_multi = v17.oldest_multi;
   v19.oldest_multi_db = v17.oldest_multi_db;
   v19.time = v17.time;
   v19.oldest_commit_ts_xid = v17.oldest_commit_ts_xid;
   v19.newest_commit_ts_xid = v17.newest_commit_ts_xid;
   v19.oldest_active_xid = v17.oldest_active_xid;
   v19.data_checksum_state = 0; /* PG_DATA_CHECKSUM_OFF: source runs without checksums */

   new_data = malloc(CHECKPOINT_V19_SIZE);
   if (!new_data)
   {
      pgmoneta_log_error("migration_engine: out of memory translating checkpoint record");
      return 1;
   }

   memcpy(new_data, &v19, CHECKPOINT_V19_SIZE);

   free(record->main_data);
   record->main_data = new_data;
   record->main_data_len = CHECKPOINT_V19_SIZE;

   return 0;
}

static int
translate_checkpoint_redo(struct decoded_xlog_record* record)
{
   int wal_level;
   uint32_t data_checksum_version = 0;
   char* new_data = NULL;

   if (record->main_data_len < sizeof(int))
   {
      pgmoneta_log_error("migration_engine: short checkpoint redo record (%u bytes) at %X/%X",
                         record->main_data_len, LSN_FORMAT_ARGS(record->lsn));
      return 1;
   }

   memcpy(&wal_level, record->main_data, sizeof(int));

   new_data = malloc(2 * sizeof(int));
   if (!new_data)
   {
      pgmoneta_log_error("migration_engine: out of memory translating checkpoint redo record");
      return 1;
   }

   memcpy(new_data, &wal_level, sizeof(int));
   memcpy(new_data + sizeof(int), &data_checksum_version, sizeof(uint32_t));

   free(record->main_data);
   record->main_data = new_data;
   record->main_data_len = 2 * sizeof(int);

   return 0;
}

static int
translate_multixact_create(struct decoded_xlog_record* record)
{
   uint32_t mid;
   uint64_t moff;
   int32_t nmembers;
   size_t members_len;
   char* new_data = NULL;
   char* p;

   if (record->main_data_len < MULTIXACT_CREATE_HDR_V18)
   {
      pgmoneta_log_error("migration_engine: short multixact create record (%u bytes) at %X/%X",
                         record->main_data_len, LSN_FORMAT_ARGS(record->lsn));
      return 1;
   }

   memcpy(&mid, record->main_data, sizeof(uint32_t));
   {
      uint32_t moff32;
      memcpy(&moff32, record->main_data + 4, sizeof(uint32_t));
      moff = (uint64_t)moff32;
   }
   memcpy(&nmembers, record->main_data + 8, sizeof(int32_t));
   members_len = record->main_data_len - MULTIXACT_CREATE_HDR_V18;

   new_data = malloc(members_len + MULTIXACT_CREATE_HDR_V18 + 8);
   if (!new_data)
   {
      pgmoneta_log_error("migration_engine: out of memory translating multixact create record");
      return 1;
   }

   p = new_data;
   memcpy(p, &mid, sizeof(uint32_t));
   p += sizeof(uint32_t);
   memset(p, 0, 4); /* alignment padding before the 64-bit offset */
   p += 4;
   memcpy(p, &moff, sizeof(uint64_t));
   p += sizeof(uint64_t);
   memcpy(p, &nmembers, sizeof(int32_t));
   p += sizeof(int32_t);
   memcpy(p, record->main_data + MULTIXACT_CREATE_HDR_V18, members_len);

   free(record->main_data);
   record->main_data = new_data;
   record->main_data_len = members_len + MULTIXACT_CREATE_HDR_V18 + 8;

   return 0;
}

static int
translate_multixact_truncate(struct decoded_xlog_record* record)
{
   uint32_t oldest_multi_db;
   uint32_t oldest_multi;
   uint64_t oldest_offset;
   char* new_data = NULL;
   char* p;

   if (record->main_data_len < MULTIXACT_TRUNCATE_SIZE_V18)
   {
      pgmoneta_log_error("migration_engine: short multixact truncate record (%u bytes) at %X/%X",
                         record->main_data_len, LSN_FORMAT_ARGS(record->lsn));
      return 1;
   }

   memcpy(&oldest_multi_db, record->main_data, sizeof(uint32_t));
   memcpy(&oldest_multi, record->main_data + 8, sizeof(uint32_t)); /* endTruncOff */
   {
      uint32_t end_memb;
      memcpy(&end_memb, record->main_data + 16, sizeof(uint32_t)); /* endTruncMemb */
      oldest_offset = (uint64_t)end_memb;
   }

   new_data = malloc(MULTIXACT_TRUNCATE_SIZE_V19);
   if (!new_data)
   {
      pgmoneta_log_error("migration_engine: out of memory translating multixact truncate record");
      return 1;
   }

   p = new_data;
   memcpy(p, &oldest_multi_db, sizeof(uint32_t));
   p += sizeof(uint32_t);
   memcpy(p, &oldest_multi, sizeof(uint32_t));
   p += sizeof(uint32_t);
   memcpy(p, &oldest_offset, sizeof(uint64_t));

   free(record->main_data);
   record->main_data = new_data;
   record->main_data_len = MULTIXACT_TRUNCATE_SIZE_V19;

   return 0;
}

static int
translate_gist_assign_lsn(struct decoded_xlog_record* record)
{
   if (record->main_data_len < sizeof(int))
   {
      pgmoneta_log_error("migration_engine: short gist assign lsn record (%u bytes) at %X/%X",
                         record->main_data_len, LSN_FORMAT_ARGS(record->lsn));
      return 1;
   }

   record->header.xl_rmid = RM_XLOG_ID;
   record->header.xl_info = (record->header.xl_info & XLR_INFO_MASK) | XLOG_ASSIGN_LSN;

   return 0;
}
