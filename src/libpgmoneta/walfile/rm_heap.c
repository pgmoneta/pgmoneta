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
#include <utils.h>
#include <walfile/rm_heap.h>
#include <walfile/wal_reader.h>
#include <wal.h>

/* system */
#include <assert.h>

struct xl_heap_freeze_page*
pgmoneta_wal_create_xl_heap_freeze_page(void)
{
   struct xl_heap_freeze_page* wrapper = malloc(sizeof(struct xl_heap_freeze_page));

   if (server_config->version >= 16)
   {
      wrapper->parse = pgmoneta_wal_parse_xl_heap_freeze_page_v16;
      wrapper->format = pgmoneta_wal_format_xl_heap_freeze_page_v16;
   }
   else
   {
      wrapper->parse = pgmoneta_wal_parse_xl_heap_freeze_page_v15;
      wrapper->format = pgmoneta_wal_format_xl_heap_freeze_page_v15;
   }

   return wrapper;
}

/**
 * Parses a version 15 xl_heap_freeze_page structure.
 */
void
pgmoneta_wal_parse_xl_heap_freeze_page_v15(struct xl_heap_freeze_page* wrapper, void* rec)
{
   char* ptr = (char*)rec;
   memcpy(&wrapper->data.v15.cutoff_xid, ptr, sizeof(transaction_id));
   ptr += sizeof(transaction_id);
   memcpy(&wrapper->data.v15.ntuples, ptr, sizeof(uint16_t));
}

/**
 * Parses a version 16 xl_heap_freeze_page structure.
 */
void
pgmoneta_wal_parse_xl_heap_freeze_page_v16(struct xl_heap_freeze_page* wrapper, void* rec)
{
   char* ptr = (char*)rec;
   memcpy(&wrapper->data.v16.snapshot_conflict_horizon, ptr, sizeof(transaction_id));
   ptr += sizeof(transaction_id);
   memcpy(&wrapper->data.v16.nplans, ptr, sizeof(uint16_t));
   ptr += sizeof(uint16_t);
   memcpy(&wrapper->data.v16.is_catalog_rel, ptr, sizeof(bool));
}

/**
 * Formats a version 15 xl_heap_freeze_page structure.
 */
char*
pgmoneta_wal_format_xl_heap_freeze_page_v15(struct xl_heap_freeze_page* wrapper, char* buf)
{
   struct xl_heap_freeze_page_v15* xlrec = &wrapper->data.v15;
   buf = pgmoneta_format_and_append(buf, "cutoff xid %u ntuples %u",
                                    xlrec->cutoff_xid, xlrec->ntuples);
   return buf;
}

/**
 * Formats a version 16 xl_heap_freeze_page structure.
 */
char*
pgmoneta_wal_format_xl_heap_freeze_page_v16(struct xl_heap_freeze_page* wrapper, char* buf)
{
   struct xl_heap_freeze_page_v16* xlrec = &wrapper->data.v16;
   buf = pgmoneta_format_and_append(buf, "snapshot_conflict_horizon_id %u nplans %u",
                                    xlrec->snapshot_conflict_horizon, xlrec->nplans);
   return buf;
}

static char*
out_infobits(char* buf, uint8_t infobits)
{
   if (infobits & XLHL_XMAX_IS_MULTI)
   {
      buf = pgmoneta_format_and_append(buf, "IS_MULTI ");
   }
   if (infobits & XLHL_XMAX_LOCK_ONLY)
   {
      buf = pgmoneta_format_and_append(buf, "LOCK_ONLY ");
   }
   if (infobits & XLHL_XMAX_EXCL_LOCK)
   {
      buf = pgmoneta_format_and_append(buf, "EXCL_LOCK ");
   }
   if (infobits & XLHL_XMAX_KEYSHR_LOCK)
   {
      buf = pgmoneta_format_and_append(buf, "KEYSHR_LOCK ");
   }
   if (infobits & XLHL_KEYS_UPDATED)
   {
      buf = pgmoneta_format_and_append(buf, "KEYS_UPDATED ");
   }
   return buf;
}

char*
pgmoneta_wal_heap_desc(char* buf, struct decoded_xlog_record* record)
{
   char* rec = record->main_data;
   uint8_t info = record->header.xl_info & ~XLR_INFO_MASK;

   info &= XLOG_HEAP_OPMASK;
   if (info == XLOG_HEAP_INSERT)
   {
      struct xl_heap_insert* xlrec = (struct xl_heap_insert*)rec;

      buf = pgmoneta_format_and_append(buf, "off %u flags 0x%02X", xlrec->offnum, xlrec->flags);
   }
   else if (info == XLOG_HEAP_DELETE)
   {
      struct xl_heap_delete* xlrec = (struct xl_heap_delete*)rec;

      buf = pgmoneta_format_and_append(buf, "off %u flags 0x%02X ",
                                       xlrec->offnum,
                                       xlrec->flags);
      buf = out_infobits(buf, xlrec->infobits_set);
   }
   else if (info == XLOG_HEAP_UPDATE)
   {
      struct xl_heap_update* xlrec = (struct xl_heap_update*)rec;

      buf = pgmoneta_format_and_append(buf, "off %u xmax %u flags 0x%02X ",
                                       xlrec->old_offnum,
                                       xlrec->old_xmax,
                                       xlrec->flags);
      buf = out_infobits(buf, xlrec->old_infobits_set);
      buf = pgmoneta_format_and_append(buf, "; new off %u xmax %u",
                                       xlrec->new_offnum,
                                       xlrec->new_xmax);
   }
   else if (info == XLOG_HEAP_HOT_UPDATE)
   {
      struct xl_heap_update* xlrec = (struct xl_heap_update*)rec;

      buf = pgmoneta_format_and_append(buf, "off %u xmax %u flags 0x%02X ",
                                       xlrec->old_offnum,
                                       xlrec->old_xmax,
                                       xlrec->flags);
      buf = out_infobits(buf, xlrec->old_infobits_set);
      buf = pgmoneta_format_and_append(buf, "; new off %u xmax %u",
                                       xlrec->new_offnum,
                                       xlrec->new_xmax);
   }
   else if (info == XLOG_HEAP_TRUNCATE)
   {
      struct xl_heap_truncate* xlrec = (struct xl_heap_truncate*)rec;
      size_t i;

      if (xlrec->flags & XLH_TRUNCATE_CASCADE)
      {
         buf = pgmoneta_format_and_append(buf, "cascade ");
      }
      if (xlrec->flags & XLH_TRUNCATE_RESTART_SEQS)
      {
         buf = pgmoneta_format_and_append(buf, "restart_seqs ");
      }
      buf = pgmoneta_format_and_append(buf, "nrelids %u relids", xlrec->nrelids);
      for (i = 0; i < xlrec->nrelids; i++)
      {
         buf = pgmoneta_format_and_append(buf, " %u", xlrec->relids[i]);
      }
   }
   else if (info == XLOG_HEAP_CONFIRM)
   {
      struct xl_heap_confirm* xlrec = (struct xl_heap_confirm*)rec;

      buf = pgmoneta_format_and_append(buf, "off %u", xlrec->offnum);
   }
   else if (info == XLOG_HEAP_LOCK)
   {
      struct xl_heap_lock* xlrec = (struct xl_heap_lock*)rec;

      buf = pgmoneta_format_and_append(buf, "off %u: xid %u: flags 0x%02X ",
                                       xlrec->offnum, xlrec->locking_xid, xlrec->flags);
      buf = out_infobits(buf, xlrec->infobits_set);
   }
   else if (info == XLOG_HEAP_INPLACE)
   {
      struct xl_heap_inplace* xlrec = (struct xl_heap_inplace*)rec;

      buf = pgmoneta_format_and_append(buf, "off %u", xlrec->offnum);
   }
   return buf;
}

char*
pgmoneta_wal_heap2_desc(char* buf, struct decoded_xlog_record* record)
{
   char* rec = record->main_data;
   uint8_t info = record->header.xl_info & ~XLR_INFO_MASK;
   char* dbname = NULL;
   char* relname = NULL;
   char* spcname = NULL;

   info &= XLOG_HEAP_OPMASK;

   if ((server_config->version >= 17) && (info == XLOG_HEAP2_PRUNE_ON_ACCESS || info == XLOG_HEAP2_PRUNE_VACUUM_SCAN || info == XLOG_HEAP2_PRUNE_VACUUM_CLEANUP))
   {
      struct xl_heap_prune_v17* xlrec = (struct xl_heap_prune_v17*)rec;

      if (xlrec->flags & XLHP_HAS_CONFLICT_HORIZON)
      {
         transaction_id conflict_xid;

         memcpy(&conflict_xid, rec + SizeOfHeapPruneV17, sizeof(transaction_id));

         buf = pgmoneta_format_and_append(buf, "snapshot_conflict_horizon_id: %u", conflict_xid);
      }

      buf = pgmoneta_format_and_append(buf, ", is_catalog_rel: %c", xlrec->flags & XLHP_IS_CATALOG_REL ? 'T' : 'F');

      if (XLogRecHasBlockData(record, 0))
      {
         offset_number* redirected;
         offset_number* nowdead;
         offset_number* nowunused;
         int nredirected;
         int nunused;
         int ndead;
         int nplans;
         struct xlhp_freeze_plan* plans;
         offset_number* frz_offsets;

         struct decoded_bkp_block bkpb = record->blocks[0];
         char* cursor = bkpb.data;

         heap_xlog_deserialize_prune_and_freeze(cursor, xlrec->flags,
                                                &nplans, &plans, &frz_offsets,
                                                &nredirected, &redirected,
                                                &ndead, &nowdead,
                                                &nunused, &nowunused);

         buf = pgmoneta_format_and_append(buf, ", nplans: %u, nredirected: %u, ndead: %u, nunused: %u",
                                          nplans, nredirected, ndead, nunused);

         if (nplans > 0)
         {
            buf = pgmoneta_format_and_append(buf, ", plans:");
            buf = pgmoneta_wal_array_desc(buf, plans, sizeof(struct xlhp_freeze_plan), nplans);
         }

         if (nredirected > 0)
         {
            buf = pgmoneta_format_and_append(buf, ", redirected:");
            buf = pgmoneta_wal_array_desc(buf, redirected, sizeof(offset_number) * 2, nredirected);
         }

         if (ndead > 0)
         {
            buf = pgmoneta_format_and_append(buf, ", dead:");
            buf = pgmoneta_wal_array_desc(buf, nowdead, sizeof(offset_number), ndead);
         }

         if (nunused > 0)
         {
            buf = pgmoneta_format_and_append(buf, ", unused:");
            buf = pgmoneta_wal_array_desc(buf, nowunused, sizeof(offset_number), nunused);
         }
      }
   }
   else if (server_config->version < 17 && info == XLOG_HEAP2_PRUNE)
   {
      struct xl_heap_prune_v16* xlrec = (struct xl_heap_prune_v16*)rec;

      buf = pgmoneta_format_and_append(buf, "snapshot_conflict_horizon_id %u nredirected %u ndead %u",
                                       xlrec->snapshotConflictHorizon,
                                       xlrec->nredirected,
                                       xlrec->ndead);
   }
   else if (info == XLOG_HEAP2_VACUUM)
   {
      struct xl_heap_vacuum* xlrec = (struct xl_heap_vacuum*)rec;

      buf = pgmoneta_format_and_append(buf, "nunused %u", xlrec->nunused);
   }
   else if (info == XLOG_HEAP2_FREEZE_PAGE)
   {
      struct xl_heap_freeze_page* xlrec = pgmoneta_wal_create_xl_heap_freeze_page();
      xlrec->parse(xlrec, rec);
      buf = xlrec->format(xlrec, buf);
      free(xlrec);
   }
   else if (info == XLOG_HEAP2_VISIBLE)
   {
      struct xl_heap_visible* xlrec = (struct xl_heap_visible*)rec;

      buf = pgmoneta_format_and_append(buf, "cutoff xid %u flags 0x%02X",
                                       xlrec->cutoff_xid, xlrec->flags);
   }
   else if (info == XLOG_HEAP2_MULTI_INSERT)
   {
      struct xl_heap_multi_insert* xlrec = (struct xl_heap_multi_insert*)rec;

      buf = pgmoneta_format_and_append(buf, "%d tuples flags 0x%02X", xlrec->ntuples,
                                       xlrec->flags);
   }
   else if (info == XLOG_HEAP2_LOCK_UPDATED)
   {
      struct xl_heap_lock_updated* xlrec = (struct xl_heap_lock_updated*)rec;

      buf = pgmoneta_format_and_append(buf, "off %u: xmax %u: flags 0x%02X ",
                                       xlrec->offnum, xlrec->xmax, xlrec->flags);
      buf = out_infobits(buf, xlrec->infobits_set);
   }
   else if (info == XLOG_HEAP2_NEW_CID)
   {
      struct xl_heap_new_cid* xlrec = (struct xl_heap_new_cid*)rec;

      if (pgmoneta_get_database_name(xlrec->target_node.dbNode, &dbname))
      {
         goto error;
      }

      if (pgmoneta_get_relation_name(xlrec->target_node.relNode, &relname))
      {
         goto error;
      }

      if (pgmoneta_get_tablespace_name(xlrec->target_node.spcNode, &spcname))
      {
         goto error;
      }

      buf = pgmoneta_format_and_append(buf, "rel %s/%s/%s; tid %u/%u",
                                       spcname,
                                       dbname,
                                       relname,
                                       ITEM_POINTER_GET_BLOCK_NUMBER(&(xlrec->target_tid)),
                                       ITEM_POINTER_GET_OFFSET_NUMBER(&(xlrec->target_tid)));
      buf = pgmoneta_format_and_append(buf, "; cmin: %u, cmax: %u, combo: %u",
                                       xlrec->cmin, xlrec->cmax, xlrec->combocid);
   }

   free(dbname);
   free(spcname);
   free(relname);
   return buf;

error:
   free(dbname);
   free(spcname);
   free(relname);
   return NULL;
}
void
heap_xlog_deserialize_prune_and_freeze(char* cursor, uint8_t flags,
                                       int* nplans, struct xlhp_freeze_plan** plans,
                                       offset_number** frz_offsets,
                                       int* nredirected, offset_number** redirected,
                                       int* ndead, offset_number** nowdead,
                                       int* nunused, offset_number** nowunused)
{
   if (flags & XLHP_HAS_FREEZE_PLANS)
   {
      struct xlhp_freeze_plans* freeze_plans = (struct xlhp_freeze_plans*)cursor;

      *nplans = freeze_plans->nplans;
      assert(*nplans > 0);
      *plans = freeze_plans->plans;

      cursor += offsetof(struct xlhp_freeze_plans, plans);
      cursor += sizeof(struct xlhp_freeze_plan) * *nplans;
   }
   else
   {
      *nplans = 0;
      *plans = NULL;
   }

   if (flags & XLHP_HAS_REDIRECTIONS)
   {
      struct xlhp_prune_items* subrecord = (struct xlhp_prune_items*)cursor;

      *nredirected = subrecord->ntargets;
      assert(*nredirected > 0);
      *redirected = &subrecord->data[0];

      cursor += offsetof(struct xlhp_prune_items, data);
      cursor += sizeof(offset_number[2]) * *nredirected;
   }
   else
   {
      *nredirected = 0;
      *redirected = NULL;
   }

   if (flags & XLHP_HAS_DEAD_ITEMS)
   {
      struct xlhp_prune_items* subrecord = (struct xlhp_prune_items*)cursor;

      *ndead = subrecord->ntargets;
      assert(*ndead > 0);
      *nowdead = subrecord->data;

      cursor += offsetof(struct xlhp_prune_items, data);
      cursor += sizeof(offset_number) * *ndead;
   }
   else
   {
      *ndead = 0;
      *nowdead = NULL;
   }

   if (flags & XLHP_HAS_NOW_UNUSED_ITEMS)
   {
      struct xlhp_prune_items* subrecord = (struct xlhp_prune_items*)cursor;

      *nunused = subrecord->ntargets;
      assert(*nunused > 0);
      *nowunused = subrecord->data;

      cursor += offsetof(struct xlhp_prune_items, data);
      cursor += sizeof(offset_number) * *nunused;
   }
   else
   {
      *nunused = 0;
      *nowunused = NULL;
   }

   *frz_offsets = (offset_number*)cursor;
}

char*
offset_elem_desc(char* buf, void* offset, void* data __attribute__((unused)))
{
   buf = pgmoneta_format_and_append(buf, "%u", *(offset_number*)offset);
   return buf;
}

char*
plan_elem_desc(char* buf, void* plan, void* data)
{
   struct xlhp_freeze_plan* new_plan = (struct xlhp_freeze_plan*)plan;
   offset_number** offsets = data;

   buf = pgmoneta_format_and_append(buf, "{ xmax: %u, infomask: %u, infomask2: %u, ntuples: %u",
                                    new_plan->xmax,
                                    new_plan->t_infomask, new_plan->t_infomask2,
                                    new_plan->ntuples);

   buf = pgmoneta_format_and_append(buf, ", offsets:");
   buf = pgmoneta_wal_array_desc(buf, *offsets, sizeof(offset_number), new_plan->ntuples);

   *offsets += new_plan->ntuples;

   buf = pgmoneta_format_and_append(buf, " }");
   return buf;
}

char*
redirect_elem_desc(char* buf, void* offset, void* data __attribute__((unused)))
{
   offset_number* new_offset = (offset_number*)offset;

   buf = pgmoneta_format_and_append(buf, "%u->%u", new_offset[0], new_offset[1]);
   return buf;
}

char*
oid_elem_desc(char* buf, void* relid, void* data __attribute__((unused)))
{
   char* relname = NULL;

   if (pgmoneta_get_relation_name(*(oid*)relid, &relname))
   {
      free(relname);
      return NULL;
   }

   buf = pgmoneta_format_and_append(buf, "rel %s", relname);
   free(relname);
   return buf;
}
