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

/* pgmoneta */
#include <pgmoneta.h>
#include <configuration.h>
#include <deque.h>
#include <walfile/pg_control.h>
#include <walfile/rm_btree.h>
#include <walfile/rm_heap.h>
#include <walfile/rmgr.h>
#include <utils.h>
#include <value.h>
#include <tsclient.h>
#include <walfile.h>
#include <tswalutils.h>

/* system */
#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>

static struct decoded_xlog_record*
create_record(uint8_t rmid, uint8_t info, transaction_id xid,
              void* main_data_src, uint32_t main_data_len)
{
   struct decoded_xlog_record* rec = NULL;
   uint32_t total_length = 0;

   rec = (struct decoded_xlog_record*)malloc(sizeof(struct decoded_xlog_record));
   if (rec == NULL)
   {
      goto error;
   }

   memset(rec, 0, sizeof(struct decoded_xlog_record));

   rec->main_data_len = main_data_len;
   rec->max_block_id = RANDOM_MAX_BLOCK_ID;
   rec->oversized = RANDOM_OVERSIZED;
   rec->record_origin = RANDOM_RECORD_ORIGIN;
   rec->toplevel_xid = RANDOM_TOPLEVEL_XID;
   rec->partial = RANDOM_PARTIAL;

   total_length = sizeof(struct xlog_record);

   if (rec->record_origin != INVALID_REP_ORIGIN_ID)
   {
      total_length += sizeof(uint8_t);
      total_length += sizeof(rep_origin_id);
   }

   if (rec->toplevel_xid != INVALID_TRANSACTION_ID)
   {
      total_length += sizeof(uint8_t);
      total_length += sizeof(transaction_id);
   }

   if (rec->main_data_len > 0)
   {
      total_length += sizeof(uint8_t);
      if (rec->main_data_len <= UINT8_MAX)
      {
         total_length += sizeof(uint8_t);
      }
      else
      {
         total_length += sizeof(uint32_t);
      }
      total_length += rec->main_data_len;
   }

   rec->header.xl_tot_len = total_length;
   rec->header.xl_xid = xid;
   rec->header.xl_prev = 0;
   rec->header.xl_info = info;
   rec->header.xl_rmid = rmid;
   rec->header.xl_crc = 0;
   rec->size = rec->header.xl_tot_len;

   for (int i = 0; i < XLR_MAX_BLOCK_ID + 1; i++)
   {
      rec->blocks[i].in_use = false;
      rec->blocks[i].bimg_len = 0;
      rec->blocks[i].data_len = 0;
      rec->blocks[i].bkp_image = NULL;
      rec->blocks[i].data = NULL;
   }

   if (main_data_len > 0 && main_data_src != NULL)
   {
      rec->main_data = (char*)malloc(main_data_len);
      if (rec->main_data == NULL)
      {
         goto error;
      }
      memcpy(rec->main_data, main_data_src, main_data_len);
   }

   return rec;

error:
   if (rec != NULL)
   {
      free(rec->main_data);
      free(rec);
   }

   return NULL;
}

static struct walfile*
create_walfile_shell(void)
{
   struct walfile* wf = NULL;

   wf = (struct walfile*)malloc(sizeof(struct walfile));
   if (wf == NULL)
   {
      goto error;
   }

   memset(wf, 0, sizeof(struct walfile));

   wf->long_phd = (struct xlog_long_page_header_data*)malloc(sizeof(struct xlog_long_page_header_data));
   if (wf->long_phd == NULL)
   {
      goto error;
   }

   memset(wf->long_phd, 0, sizeof(struct xlog_long_page_header_data));
   wf->long_phd->std.xlp_pageaddr = RANDOM_PAGEADDR;
   wf->long_phd->std.xlp_magic = RANDOM_MAGIC;
   wf->long_phd->std.xlp_info = RANDOM_INFO;
   wf->long_phd->std.xlp_tli = RANDOM_TLI;
   wf->long_phd->xlp_seg_size = RANDOM_SEG_SIZE;
   wf->long_phd->xlp_xlog_blcksz = RANDOM_XLOG_BLCKSZ;
   wf->long_phd->std.xlp_rem_len = RANDOM_REMLEN;

   if (pgmoneta_deque_create(false, &wf->page_headers))
   {
      goto error;
   }

   if (pgmoneta_deque_create(false, &wf->records))
   {
      goto error;
   }

   return wf;

error:
   if (wf != NULL)
   {
      pgmoneta_deque_destroy(wf->records);
      pgmoneta_deque_destroy(wf->page_headers);
      free(wf->long_phd);
      free(wf);
   }

   return NULL;
}

struct walfile*
pgmoneta_test_generate_check_point_shutdown_v17(void)
{
   struct walfile* wf = NULL;
   struct decoded_xlog_record* rec = NULL;

   struct check_point_v17 cp;
   memset(&cp, 0, sizeof(cp));

   cp.redo = RANDOM_REDO;
   cp.this_timeline_id = RANDOM_THIS_TLI;
   cp.prev_timeline_id = RANDOM_PREV_TLI;
   cp.full_page_writes = RANDOM_FULL_PAGE_WRITES;
   cp.wal_level = RANDOM_WAL_LEVEL;
   cp.next_xid.value = RANDOM_NEXT_XID;
   cp.next_oid = RANDOM_NEXT_OID;
   cp.next_multi = RANDOM_NEXT_MULTI;
   cp.next_multi_offset = RANDOM_NEXT_MULTI_OFFSET;
   cp.oldest_xid = RANDOM_OLDEST_XID;
   cp.oldest_xid_db = RANDOM_OLDEST_XID_DB;
   cp.oldest_multi = RANDOM_OLDEST_MULTI;
   cp.oldest_multi_db = RANDOM_OLDEST_MULTI_DB;
   cp.time = RANDOM_TIME;
   cp.oldest_commit_ts_xid = RANDOM_OLDEST_COMMIT_TS_XID;
   cp.newest_commit_ts_xid = RANDOM_NEWEST_COMMIT_TS_XID;
   cp.oldest_active_xid = RANDOM_OLDEST_ACTIVE_XID;

   wf = create_walfile_shell();
   if (wf == NULL)
   {
      goto error;
   }

   rec = create_record(RM_XLOG_ID, XLOG_CHECKPOINT_SHUTDOWN, 0,
                       &cp, sizeof(cp));
   if (rec == NULL)
   {
      goto error;
   }

   if (pgmoneta_deque_add(wf->records, NULL, (uintptr_t)rec, ValueRef))
   {
      goto error;
   }

   return wf;

error:
   if (rec != NULL)
   {
      free(rec->main_data);
      free(rec);
   }

   pgmoneta_destroy_walfile(wf);

   return NULL;
}

struct walfile*
pgmoneta_test_generate_mixed_heap_wal_v17(void)
{
   struct walfile* wf = NULL;
   struct decoded_xlog_record* rec_cp = NULL;
   struct decoded_xlog_record* rec_ins = NULL;
   struct decoded_xlog_record* rec_del = NULL;

   struct check_point_v17 cp;
   memset(&cp, 0, sizeof(cp));
   cp.redo = RANDOM_REDO;
   cp.this_timeline_id = RANDOM_THIS_TLI;
   cp.prev_timeline_id = RANDOM_PREV_TLI;
   cp.full_page_writes = RANDOM_FULL_PAGE_WRITES;
   cp.wal_level = RANDOM_WAL_LEVEL;
   cp.next_xid.value = RANDOM_NEXT_XID;
   cp.next_oid = RANDOM_NEXT_OID;
   cp.next_multi = RANDOM_NEXT_MULTI;
   cp.next_multi_offset = RANDOM_NEXT_MULTI_OFFSET;
   cp.oldest_xid = RANDOM_OLDEST_XID;
   cp.oldest_xid_db = RANDOM_OLDEST_XID_DB;
   cp.oldest_multi = RANDOM_OLDEST_MULTI;
   cp.oldest_multi_db = RANDOM_OLDEST_MULTI_DB;
   cp.time = RANDOM_TIME;
   cp.oldest_commit_ts_xid = RANDOM_OLDEST_COMMIT_TS_XID;
   cp.newest_commit_ts_xid = RANDOM_NEWEST_COMMIT_TS_XID;
   cp.oldest_active_xid = RANDOM_OLDEST_ACTIVE_XID;

   struct xl_heap_insert ins;
   memset(&ins, 0, sizeof(ins));
   ins.offnum = 1;
   ins.flags = 0;

   struct xl_heap_delete del;
   memset(&del, 0, sizeof(del));
   del.xmax = 200;
   del.offnum = 2;
   del.infobits_set = 0;
   del.flags = 0;

   wf = create_walfile_shell();
   if (wf == NULL)
   {
      return NULL;
   }

   rec_cp = create_record(RM_XLOG_ID, XLOG_CHECKPOINT_SHUTDOWN, 0,
                          &cp, sizeof(cp));
   if (rec_cp == NULL)
   {
      goto error;
   }

   if (pgmoneta_deque_add(wf->records, NULL, (uintptr_t)rec_cp, ValueRef))
   {
      goto error;
   }
   rec_cp = NULL;

   rec_ins = create_record(RM_HEAP_ID, XLOG_HEAP_INSERT, 100,
                           &ins, sizeof(ins));
   if (rec_ins == NULL)
   {
      goto error;
   }

   if (pgmoneta_deque_add(wf->records, NULL, (uintptr_t)rec_ins, ValueRef))
   {
      goto error;
   }
   rec_ins = NULL;

   rec_del = create_record(RM_HEAP_ID, XLOG_HEAP_DELETE, 200,
                           &del, sizeof(del));
   if (rec_del == NULL)
   {
      goto error;
   }

   if (pgmoneta_deque_add(wf->records, NULL, (uintptr_t)rec_del, ValueRef))
   {
      goto error;
   }
   rec_del = NULL;

   return wf;

error:
   if (rec_del != NULL)
   {
      free(rec_del->main_data);
      free(rec_del);
   }
   if (rec_ins != NULL)
   {
      free(rec_ins->main_data);
      free(rec_ins);
   }
   if (rec_cp != NULL)
   {
      free(rec_cp->main_data);
      free(rec_cp);
   }
   pgmoneta_destroy_walfile(wf);
   return NULL;
}

static int
add_block(struct decoded_xlog_record* rec, int block_id, rel_file_number rel, block_number blkno,
          void* data, uint16_t data_len, bool image)
{
   struct decoded_bkp_block* blk = &rec->blocks[block_id];

   blk->in_use = true;
   blk->rlocator.spcOid = 1663;
   blk->rlocator.dbOid = 5;
   blk->rlocator.relNumber = rel;
   blk->forknum = MAIN_FORKNUM;
   blk->blkno = blkno;
   blk->flags = MAIN_FORKNUM;

   if (data_len > 0)
   {
      blk->data = malloc(data_len);
      if (blk->data == NULL)
      {
         return 1;
      }
      memcpy(blk->data, data, data_len);
      blk->data_len = data_len;
      blk->has_data = true;
      blk->flags |= BKPBLOCK_HAS_DATA;
   }

   if (image)
   {
      blk->bkp_image = calloc(1, RANDOM_XLOG_BLCKSZ);
      if (blk->bkp_image == NULL)
      {
         return 1;
      }
      blk->bimg_len = RANDOM_XLOG_BLCKSZ;
      blk->hole_offset = 0;
      blk->bimg_info = 0x02; /* BKPIMAGE_APPLY from PostgreSQL 15 on */
      blk->has_image = true;
      blk->apply_image = true;
      blk->flags |= BKPBLOCK_HAS_IMAGE;
   }

   if (block_id > rec->max_block_id)
   {
      rec->max_block_id = block_id;
   }

   return 0;
}

/* create_record() sizes a record without block references; take the real size from the encoder */
static int
fix_total_length(struct decoded_xlog_record* rec)
{
   struct xlog_record header;
   char* encoded = pgmoneta_wal_encode_xlog_record(rec, RANDOM_MAGIC, NULL);

   if (encoded == NULL)
   {
      return 1;
   }

   memcpy(&header, encoded, sizeof(header));
   rec->header.xl_tot_len = header.xl_tot_len;
   rec->size = header.xl_tot_len;
   free(encoded);

   return 0;
}

static void
free_record(struct decoded_xlog_record* rec)
{
   if (rec == NULL)
   {
      return;
   }

   for (int i = 0; i <= rec->max_block_id; i++)
   {
      free(rec->blocks[i].data);
      free(rec->blocks[i].bkp_image);
   }
   free(rec->main_data);
   free(rec);
}

struct walfile*
pgmoneta_test_generate_walfilter_conflict_wal_v17(enum walfilter_later_record later)
{
   struct walfile* wf = NULL;
   struct decoded_xlog_record* rec = NULL;
   struct check_point_v17 cp;
   struct xl_heap_delete del;
   struct xl_heap_insert ins;
   struct xl_heap_prune_v17 prune;
   struct xl_btree_delete_v16 bdel;
   char prune_data[sizeof(uint16_t) + sizeof(offset_number)];
   uint16_t ntargets = 1;
   offset_number pruned = later == WALFILTER_LATER_PRUNE_SAME ? 3 : 4;

   memset(&cp, 0, sizeof(cp));
   cp.redo = RANDOM_REDO;
   cp.this_timeline_id = RANDOM_THIS_TLI;
   cp.prev_timeline_id = RANDOM_PREV_TLI;
   cp.full_page_writes = RANDOM_FULL_PAGE_WRITES;
   cp.wal_level = RANDOM_WAL_LEVEL;
   cp.next_xid.value = RANDOM_NEXT_XID;
   cp.next_oid = RANDOM_NEXT_OID;
   cp.time = RANDOM_TIME;

   wf = create_walfile_shell();
   if (wf == NULL)
   {
      return NULL;
   }

   rec = create_record(RM_XLOG_ID, XLOG_CHECKPOINT_SHUTDOWN, 0, &cp, sizeof(cp));
   if (rec == NULL || pgmoneta_deque_add(wf->records, NULL, (uintptr_t)rec, ValueRef))
   {
      goto error;
   }

   /* The DELETE that a DELETE or XID 200 rule filters out */
   memset(&del, 0, sizeof(del));
   del.xmax = 200;
   del.offnum = 3;
   rec = create_record(RM_HEAP_ID, XLOG_HEAP_DELETE, 200, &del, sizeof(del));
   if (rec == NULL || add_block(rec, 0, 16384, 7, NULL, 0, false) || fix_total_length(rec) ||
       pgmoneta_deque_add(wf->records, NULL, (uintptr_t)rec, ValueRef))
   {
      goto error;
   }

   memset(&ins, 0, sizeof(ins));
   ins.offnum = 1;

   switch (later)
   {
      case WALFILTER_LATER_NONE:
         rec = create_record(RM_HEAP_ID, XLOG_HEAP_INSERT, 300, &ins, sizeof(ins));
         if (rec == NULL || add_block(rec, 0, 16384, 8, NULL, 0, false))
         {
            goto error;
         }
         break;
      case WALFILTER_LATER_IMAGE:
         rec = create_record(RM_HEAP_ID, XLOG_HEAP_INSERT, 300, &ins, sizeof(ins));
         if (rec == NULL || add_block(rec, 0, 16384, 7, NULL, 0, true))
         {
            goto error;
         }
         break;
      case WALFILTER_LATER_PRUNE_SAME:
      case WALFILTER_LATER_PRUNE_OTHER:
         memset(&prune, 0, sizeof(prune));
         prune.flags = XLHP_HAS_DEAD_ITEMS;
         memcpy(prune_data, &ntargets, sizeof(ntargets));
         memcpy(prune_data + sizeof(ntargets), &pruned, sizeof(pruned));
         rec = create_record(RM_HEAP2_ID, XLOG_HEAP2_PRUNE_ON_ACCESS, 0, &prune, sizeof(prune));
         if (rec == NULL || add_block(rec, 0, 16384, 7, prune_data, sizeof(prune_data), false))
         {
            goto error;
         }
         break;
      case WALFILTER_LATER_BTREE_NEWER:
      case WALFILTER_LATER_BTREE_OLDER:
         memset(&bdel, 0, sizeof(bdel));
         bdel.snapshot_conflict_horizon = later == WALFILTER_LATER_BTREE_NEWER ? 200 : 150;
         rec = create_record(RM_BTREE_ID, XLOG_BTREE_DELETE, 300, &bdel, sizeof(bdel));
         if (rec == NULL || add_block(rec, 0, 16390, 1, NULL, 0, false))
         {
            goto error;
         }
         break;
   }

   if (fix_total_length(rec) || pgmoneta_deque_add(wf->records, NULL, (uintptr_t)rec, ValueRef))
   {
      goto error;
   }

   return wf;

error:
   free_record(rec);
   pgmoneta_destroy_walfile(wf);
   return NULL;
}
