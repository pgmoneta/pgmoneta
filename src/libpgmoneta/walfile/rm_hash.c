/*
 * Copyright (C) 2024 The pgmoneta community
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

#include <walfile/rm.h>
#include <walfile/rm_hash.h>
#include <utils.h>

struct xl_hash_vacuum_one_page*
pgmoneta_wal_create_xl_hash_vacuum_one_page(void)
{
   struct xl_hash_vacuum_one_page* wrapper = malloc(sizeof(struct xl_hash_vacuum_one_page));

   if (server_config->version >= 16)
   {
      wrapper->parse = pgmoneta_wal_parse_xl_hash_vacuum_one_page_v16;
      wrapper->format = pgmoneta_wal_format_xl_hash_vacuum_one_page_v16;
   }
   else
   {
      wrapper->parse = pgmoneta_wal_parse_xl_hash_vacuum_one_page_v15;
      wrapper->format = pgmoneta_wal_format_xl_hash_vacuum_one_page_v15;
   }

   return wrapper;
}

void
pgmoneta_wal_parse_xl_hash_vacuum_one_page_v15(struct xl_hash_vacuum_one_page* wrapper, const void* rec)
{
   memcpy(&wrapper->data.v15, rec, sizeof(struct xl_hash_vacuum_one_page_v15));
}

void
pgmoneta_wal_parse_xl_hash_vacuum_one_page_v16(struct xl_hash_vacuum_one_page* wrapper, const void* rec)
{
   memcpy(&wrapper->data.v16, rec, sizeof(struct xl_hash_vacuum_one_page_v16));
}

char*
pgmoneta_wal_format_xl_hash_vacuum_one_page_v15(struct xl_hash_vacuum_one_page* wrapper, char* buf)
{
   struct xl_hash_vacuum_one_page_v15* xlrec = &wrapper->data.v15;
   buf = pgmoneta_format_and_append(buf, "ntuples %d, latestRemovedXid %u",
                                    xlrec->ntuples,
                                    xlrec->latestRemovedXid);
   return buf;
}

char*
pgmoneta_wal_format_xl_hash_vacuum_one_page_v16(struct xl_hash_vacuum_one_page* wrapper, char* buf)
{
   struct xl_hash_vacuum_one_page_v16* xlrec = &wrapper->data.v16;
   buf = pgmoneta_format_and_append(buf, "ntuples %d, snapshot_conflict_horizon_id %u",
                                    xlrec->ntuples,
                                    xlrec->snaphost_conflict_horizon);
   return buf;
}

char*
pgmoneta_wal_hash_desc(char* buf, struct decoded_xlog_record* record)
{
   char* rec = XLOG_REC_GET_DATA(record);
   uint8_t info = XLOG_REC_GET_INFO(record) & ~XLR_INFO_MASK;

   switch (info)
   {
      case XLOG_HASH_INIT_META_PAGE:
      {
         struct xl_hash_init_meta_page* xlrec = (struct xl_hash_init_meta_page*) rec;

         buf = pgmoneta_format_and_append(buf, "num_tuples %g, fillfactor %d",
                                          xlrec->num_tuples, xlrec->ffactor);
         break;
      }
      case XLOG_HASH_INIT_BITMAP_PAGE:
      {
         struct xl_hash_init_bitmap_page* xlrec = (struct xl_hash_init_bitmap_page*) rec;

         buf = pgmoneta_format_and_append(buf, "bmsize %d", xlrec->bmsize);
         break;
      }
      case XLOG_HASH_INSERT:
      {
         struct xl_hash_insert* xlrec = (struct xl_hash_insert*) rec;

         buf = pgmoneta_format_and_append(buf, "off %u", xlrec->offnum);
         break;
      }
      case XLOG_HASH_ADD_OVFL_PAGE:
      {
         struct xl_hash_add_ovfl_page* xlrec = (struct xl_hash_add_ovfl_page*) rec;

         buf = pgmoneta_format_and_append(buf, "bmsize %d, bmpage_found %c",
                                          xlrec->bmsize, (xlrec->bmpage_found) ? 'T' : 'F');
         break;
      }
      case XLOG_HASH_SPLIT_ALLOCATE_PAGE:
      {
         struct xl_hash_split_allocate_page* xlrec = (struct xl_hash_split_allocate_page*) rec;

         buf = pgmoneta_format_and_append(buf, "new_bucket %u, meta_page_masks_updated %c, issplitpoint_changed %c",
                                          xlrec->new_bucket,
                                          (xlrec->flags & XLH_SPLIT_META_UPDATE_MASKS) ? 'T' : 'F',
                                          (xlrec->flags & XLH_SPLIT_META_UPDATE_SPLITPOINT) ? 'T' : 'F');
         break;
      }
      case XLOG_HASH_SPLIT_COMPLETE:
      {
         struct xl_hash_split_complete* xlrec = (struct xl_hash_split_complete*) rec;

         buf = pgmoneta_format_and_append(buf, "old_bucket_flag %u, new_bucket_flag %u",
                                          xlrec->old_bucket_flag, xlrec->new_bucket_flag);
         break;
      }
      case XLOG_HASH_MOVE_PAGE_CONTENTS:
      {
         struct xl_hash_move_page_contents* xlrec = (struct xl_hash_move_page_contents*) rec;

         buf = pgmoneta_format_and_append(buf, "ntups %d, is_primary %c",
                                          xlrec->ntups,
                                          xlrec->is_prim_bucket_same_wrt ? 'T' : 'F');
         break;
      }
      case XLOG_HASH_SQUEEZE_PAGE:
      {
         struct xl_hash_squeeze_page* xlrec = (struct xl_hash_squeeze_page*) rec;

         buf = pgmoneta_format_and_append(buf, "prevblkno %u, nextblkno %u, ntups %d, is_primary %c",
                                          xlrec->prevblkno,
                                          xlrec->nextblkno,
                                          xlrec->ntups,
                                          xlrec->is_prim_bucket_same_wrt ? 'T' : 'F');
         break;
      }
      case XLOG_HASH_DELETE:
      {
         struct xl_hash_delete* xlrec = (struct xl_hash_delete*) rec;

         buf = pgmoneta_format_and_append(buf, "clear_dead_marking %c, is_primary %c",
                                          xlrec->clear_dead_marking ? 'T' : 'F',
                                          xlrec->is_primary_bucket_page ? 'T' : 'F');
         break;
      }
      case XLOG_HASH_UPDATE_META_PAGE:
      {
         struct xl_hash_update_meta_page* xlrec = (struct xl_hash_update_meta_page*) rec;

         buf = pgmoneta_format_and_append(buf, "ntuples %g",
                                          xlrec->ntuples);
         break;
      }
      case XLOG_HASH_VACUUM_ONE_PAGE:
      {
         struct xl_hash_vacuum_one_page* xlrec = pgmoneta_wal_create_xl_hash_vacuum_one_page();
         xlrec->parse(xlrec, rec);
         buf = xlrec->format(xlrec, buf);
         free(xlrec);
         break;
      }
   }
   return buf;
}
