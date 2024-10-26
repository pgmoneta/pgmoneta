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

#include <walfile/rm_btree.h>
#include <walfile/wal_reader.h>
#include <utils.h>

#include <assert.h>
#include <stdbool.h>

struct xl_btree_reuse_page*
pgmoneta_wal_create_xl_btree_reuse_page(void)
{
   struct xl_btree_reuse_page* wrapper = malloc(sizeof(struct xl_btree_reuse_page));

   if (server_config->version >= 16)
   {
      wrapper->parse = pgmoneta_wal_parse_xl_btree_reuse_page_v16;
      wrapper->format = pgmoneta_wal_format_xl_btree_reuse_page_v16;
   }
   else if (server_config->version >= 14)
   {
      wrapper->parse = pgmoneta_wal_parse_xl_btree_reuse_page_v15;
      wrapper->format = pgmoneta_wal_format_xl_btree_reuse_page_v15;
   }
   else
   {
      wrapper->parse = pgmoneta_wal_parse_xl_btree_reuse_page_v13;
      wrapper->format = pgmoneta_wal_format_xl_btree_reuse_page_v13;
   }

   return wrapper;
}

void
pgmoneta_wal_parse_xl_btree_reuse_page_v13(struct xl_btree_reuse_page* wrapper, const void* rec)
{
   memcpy(&wrapper->data.v13, rec, sizeof(struct xl_btree_reuse_page_v15));
}

void
pgmoneta_wal_parse_xl_btree_reuse_page_v15(struct xl_btree_reuse_page* wrapper, const void* rec)
{
   memcpy(&wrapper->data.v15, rec, sizeof(struct xl_btree_reuse_page_v15));
}

void
pgmoneta_wal_parse_xl_btree_reuse_page_v16(struct xl_btree_reuse_page* wrapper, const void* rec)
{
   memcpy(&wrapper->data.v16, rec, sizeof(struct xl_btree_reuse_page_v16));
}

char*
pgmoneta_wal_format_xl_btree_reuse_page_v13(struct xl_btree_reuse_page* wrapper, char* buf)
{
   struct xl_btree_reuse_page_v13* xlrec = &wrapper->data.v13;
   buf = pgmoneta_format_and_append(buf, "rel %u/%u/%u; latestRemovedXid %u",
                                    xlrec->node.spcNode, xlrec->node.dbNode,
                                    xlrec->node.relNode, xlrec->latest_removed_xid);
   return buf;
}

char*
pgmoneta_wal_format_xl_btree_reuse_page_v15(struct xl_btree_reuse_page* wrapper, char* buf)
{
   struct xl_btree_reuse_page_v15* xlrec = &wrapper->data.v15;
   buf = pgmoneta_format_and_append(buf, "rel %u/%u/%u; latestRemovedXid %u:%u",
                                    xlrec->node.spcNode, xlrec->node.dbNode,
                                    xlrec->node.relNode,
                                    EPOCH_FROM_FULL_TRANSACTION_ID(xlrec->latest_removed_full_xid),
                                    XID_FROM_FULL_TRANSACTION_ID(xlrec->latest_removed_full_xid));
   return buf;
}

char*
pgmoneta_wal_format_xl_btree_reuse_page_v16(struct xl_btree_reuse_page* wrapper, char* buf)
{
   struct xl_btree_reuse_page_v16* xlrec = &wrapper->data.v16;
   buf = pgmoneta_format_and_append(buf, "rel %u/%u/%u; snapshot_conflict_horizon_id %u:%u",
                                    xlrec->locator.spcOid, xlrec->locator.dbOid,
                                    xlrec->locator.relNumber,
                                    EPOCH_FROM_FULL_TRANSACTION_ID(xlrec->snapshot_conflict_horizon_id),
                                    XID_FROM_FULL_TRANSACTION_ID(xlrec->snapshot_conflict_horizon_id));
   return buf;
}

struct xl_btree_delete*
pgmoneta_wal_create_xl_btree_delete(void)
{
   struct xl_btree_delete* wrapper = malloc(sizeof(struct xl_btree_delete));

   if (server_config->version >= 16)
   {
      wrapper->parse = pgmoneta_wal_parse_xl_btree_delete_v16;
      wrapper->format = pgmoneta_wal_format_xl_btree_delete_v16;
   }
   else if (server_config->version >= 14)
   {
      wrapper->parse = pgmoneta_wal_parse_xl_btree_delete_v15;
      wrapper->format = pgmoneta_wal_format_xl_btree_delete_v15;
   }
   else
   {
      wrapper->parse = pgmoneta_wal_parse_xl_btree_delete_v13;
      wrapper->format = pgmoneta_wal_format_xl_btree_delete_v13;
   }

   return wrapper;
}

void
pgmoneta_wal_parse_xl_btree_delete_v13(struct xl_btree_delete* wrapper, const void* rec)
{
   wrapper->data.v13 = *(struct xl_btree_delete_v13*) rec;
}

void
pgmoneta_wal_parse_xl_btree_delete_v15(struct xl_btree_delete* wrapper, const void* rec)
{
   wrapper->data.v15 = *(struct xl_btree_delete_v15*) rec;
}

void
pgmoneta_wal_parse_xl_btree_delete_v16(struct xl_btree_delete* wrapper, const void* rec)
{
   wrapper->data.v16 = *(struct xl_btree_delete_v16*) rec;
}

char*
pgmoneta_wal_format_xl_btree_delete_v13(struct xl_btree_delete* wrapper, char* buf)
{
   struct xl_btree_delete_v13* xlrec = &wrapper->data.v13;
   buf = pgmoneta_format_and_append(buf, "latestRemovedXid %u; ndeleted %u",
                                    xlrec->latest_removed_xid, xlrec->ndeleted);
   return buf;
}

char*
pgmoneta_wal_format_xl_btree_delete_v15(struct xl_btree_delete* wrapper, char* buf)
{
   struct xl_btree_delete_v15* xlrec = &wrapper->data.v15;
   buf = pgmoneta_format_and_append(buf, "latestRemovedXid %u; ndeleted %u; nupdated %u",
                                    xlrec->latestRemovedXid, xlrec->ndeleted, xlrec->nupdated);
   return buf;
}

char*
pgmoneta_wal_format_xl_btree_delete_v16(struct xl_btree_delete* wrapper, char* buf)
{
   struct xl_btree_delete_v16* xlrec = &wrapper->data.v16;
   buf = pgmoneta_format_and_append(buf, "snapshot_conflict_horizon_id %u; ndeleted %u; nupdated %u",
                                    xlrec->snapshot_conflict_horizon,
                                    xlrec->ndeleted, xlrec->nupdated);
   return buf;
}

struct xl_btree_metadata*
pgmoneta_wal_create_xl_btree_metadata(void)
{
   struct xl_btree_metadata* wrapper = malloc(sizeof(struct xl_btree_metadata));

   if (server_config->version >= 14)
   {
      wrapper->parse = pgmoneta_wal_parse_xl_btree_metadata_v14;
      wrapper->format = pgmoneta_wal_format_xl_btree_metadata_v14;
   }
   else
   {
      wrapper->parse = pgmoneta_wal_parse_xl_btree_metadata_v13;
      wrapper->format = pgmoneta_wal_format_xl_btree_metadata_v13;
   }

   return wrapper;
}

void
pgmoneta_wal_parse_xl_btree_metadata_v13(struct xl_btree_metadata* wrapper, const char* rec)
{
   memcpy(&wrapper->data.v13, rec, sizeof(struct xl_btree_metadata_v13));
}

void
pgmoneta_wal_parse_xl_btree_metadata_v14(struct xl_btree_metadata* wrapper, const char* rec)
{
   memcpy(&wrapper->data.v14, rec, sizeof(struct xl_btree_metadata_v14));
}

char*
pgmoneta_wal_format_xl_btree_metadata_v13(struct xl_btree_metadata* wrapper, char* buf)
{
   struct xl_btree_metadata_v13* xlrec = &wrapper->data.v13;
   buf = pgmoneta_format_and_append(buf, "oldest_btpo_xact %u; last_cleanup_num_heap_tuples: %f",
                                    xlrec->oldest_btpo_xact,
                                    xlrec->last_cleanup_num_heap_tuples);
   return buf;
}

char*
pgmoneta_wal_format_xl_btree_metadata_v14(struct xl_btree_metadata* wrapper, char* buf)
{
   struct xl_btree_metadata_v14* xlrec = &wrapper->data.v14;
   buf = pgmoneta_format_and_append(buf, "last_cleanup_num_delpages: %u",
                                    xlrec->last_cleanup_num_delpages);
   return buf;
}

struct xl_btree_unlink_page*
pgmoneta_wal_create_xl_btree_unlink_page(void)
{
   struct xl_btree_unlink_page* wrapper = malloc(sizeof(struct xl_btree_unlink_page));

   if (server_config->version >= 14)
   {
      wrapper->parse = pgmoneta_wal_parse_xl_btree_unlink_page_v14;
      wrapper->format = pgmoneta_wal_format_xl_btree_unlink_page_v14;
   }
   else
   {
      wrapper->parse = pgmoneta_wal_parse_xl_btree_unlink_page_v13;
      wrapper->format = pgmoneta_wal_format_xl_btree_unlink_page_v13;
   }

   return wrapper;
}

/**
 * Parses a version 13 xl_btree_unlink_page record.
 *
 * @param wrapper The wrapper struct.
 * @param rec The record to parse.
 */
void
pgmoneta_wal_parse_xl_btree_unlink_page_v13(struct xl_btree_unlink_page* wrapper, const void* rec)
{
   memcpy(&wrapper->data.v13, rec, sizeof(struct xl_btree_unlink_page_v13));
}

/**
 * Parses a version 14 xl_btree_unlink_page record.
 *
 * @param wrapper The wrapper struct.
 * @param rec The record to parse.
 */
void
pgmoneta_wal_parse_xl_btree_unlink_page_v14(struct xl_btree_unlink_page* wrapper, const void* rec)
{
   memcpy(&wrapper->data.v14, rec, sizeof(struct xl_btree_unlink_page_v14));
}

/**
 * Formats a version 13 xl_btree_unlink_page record into a string.
 *
 * @param wrapper The wrapper struct.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char*
pgmoneta_wal_format_xl_btree_unlink_page_v13(struct xl_btree_unlink_page* wrapper, char* buf)
{
   struct xl_btree_unlink_page_v13* xlrec = &wrapper->data.v13;
   buf = pgmoneta_format_and_append(buf, "left %u; right %u; btpo_xact %u; ",
                                    xlrec->leftsib, xlrec->rightsib,
                                    xlrec->btpo_xact);
   buf = pgmoneta_format_and_append(buf, "leafleft %u; leafright %u; topparent %u",
                                    xlrec->leafleftsib, xlrec->leafrightsib,
                                    xlrec->topparent);
   return buf;
}

/**
 * Formats a version 14 xl_btree_unlink_page record into a string.
 *
 * @param wrapper The wrapper struct.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char*
pgmoneta_wal_format_xl_btree_unlink_page_v14(struct xl_btree_unlink_page* wrapper, char* buf)
{
   struct xl_btree_unlink_page_v14* xlrec = &wrapper->data.v14;
   buf = pgmoneta_format_and_append(buf, "left %u; right %u; level %u; safexid %u:%u; ",
                                    xlrec->leftsib, xlrec->rightsib, xlrec->level,
                                    EPOCH_FROM_FULL_TRANSACTION_ID(xlrec->safexid),
                                    XID_FROM_FULL_TRANSACTION_ID(xlrec->safexid));
   buf = pgmoneta_format_and_append(buf, "leafleft %u; leafright %u; leaftopparent %u",
                                    xlrec->leafleftsib, xlrec->leafrightsib,
                                    xlrec->leaftopparent);
   return buf;
}

static char*
pgmoneta_wal_delvacuum_desc(char* buf, char* block_data, uint16_t ndeleted, uint16_t nupdated)
{
   offset_number* deletedoffsets;
   offset_number* updatedoffsets;
   struct xl_btree_update* updates;

   /* Output deleted page offset number array */
   buf = pgmoneta_format_and_append(buf, ", deleted:");
   deletedoffsets = (offset_number*) block_data;
   buf = pgmoneta_wal_array_desc(buf, deletedoffsets, sizeof(offset_number), ndeleted);

   /*
    * Output updates as an array of "update objects", where each element
    * contains a page offset number from updated array.  (This is not the
    * most literal representation of the underlying physical data structure
    * that we could use.  Readability seems more important here.)
    */
   buf = pgmoneta_format_and_append(buf, ", updated: [");
   updatedoffsets = (offset_number*) (block_data + ndeleted *
                                      sizeof(offset_number));
   updates = (struct xl_btree_update*) ((char*) updatedoffsets +
                                        nupdated *
                                        sizeof(offset_number));
   for (int i = 0; i < nupdated; i++)
   {
      offset_number off = updatedoffsets[i];

      assert(OFFSET_NUMBER_IS_VALID(off));
      assert(updates->ndeletedtids > 0);

      /*
       * "ptid" is the symbol name used when building each xl_btree_update's
       * array of offsets into a posting list tuple's item_pointer_data array.
       * xl_btree_update describes a subset of the existing TIDs to delete.
       */
      buf = pgmoneta_format_and_append(buf, "{ off: %u, nptids: %u, ptids: [",
                                       off, updates->ndeletedtids);
      for (int p = 0; p < updates->ndeletedtids; p++)
      {
         uint16_t* ptid;

         ptid = (uint16_t*) ((char*) updates + SIZE_OF_BTREE_UPDATE) + p;
         buf = pgmoneta_format_and_append(buf, "%u", *ptid);

         if (p < updates->ndeletedtids - 1)
         {
            buf = pgmoneta_format_and_append(buf, ", ");
         }
      }
      buf = pgmoneta_format_and_append(buf, "] }");
      if (i < nupdated - 1)
      {
         buf = pgmoneta_format_and_append(buf, ", ");
      }

      updates = (struct xl_btree_update*)
                ((char*) updates + SIZE_OF_BTREE_UPDATE +
                 updates->ndeletedtids * sizeof(uint16_t));
   }
   buf = pgmoneta_format_and_append(buf, "]");

   return buf;
}

char*
pgmoneta_wal_btree_desc(char* buf, struct decoded_xlog_record* record)
{
   char* rec = record->main_data;
   uint8_t info = record->header.xl_info & ~XLR_INFO_MASK;

   switch (info)
   {
      case XLOG_BTREE_INSERT_LEAF:
      case XLOG_BTREE_INSERT_UPPER:
      case XLOG_BTREE_INSERT_META:
      case XLOG_BTREE_INSERT_POST: {
         struct xl_btree_insert* xlrec = (struct xl_btree_insert*) rec;
         buf = pgmoneta_format_and_append(buf, " off: %u", xlrec->offnum);
         break;
      }
      case XLOG_BTREE_SPLIT_L:
      case XLOG_BTREE_SPLIT_R: {
         struct xl_btree_split* xlrec = (struct xl_btree_split*) rec;

         buf = pgmoneta_format_and_append(buf, "level: %u, firstrightoff: %d, newitemoff: %d, postingoff: %d",
                                          xlrec->level, xlrec->firstrightoff,
                                          xlrec->newitemoff, xlrec->postingoff);
         break;
      }
      case XLOG_BTREE_DEDUP: {
         struct xl_btree_dedup* xlrec = (struct xl_btree_dedup*) rec;

         buf = pgmoneta_format_and_append(buf, "nintervals: %u", xlrec->nintervals);
         break;
      }
      case XLOG_BTREE_VACUUM: {
         struct xl_btree_vacuum* xlrec = (struct xl_btree_vacuum*) rec;

         buf = pgmoneta_format_and_append(buf, "ndeleted: %u, nupdated: %u",
                                          xlrec->ndeleted, xlrec->nupdated);

         if (XLogRecHasBlockData(record, 0))
         {
            buf = pgmoneta_wal_delvacuum_desc(buf, pgmoneta_wal_get_record_block_data(record, 0, NULL),
                                              xlrec->ndeleted, xlrec->nupdated);
         }
         break;
      }
      case XLOG_BTREE_DELETE: {
         struct xl_btree_delete* xlrec = pgmoneta_wal_create_xl_btree_delete();
         xlrec->parse(xlrec, rec);
         buf = xlrec->format(xlrec, buf);
         free(xlrec);
         break;
      }
      case XLOG_BTREE_MARK_PAGE_HALFDEAD: {
         struct xl_btree_mark_page_halfdead* xlrec = (struct xl_btree_mark_page_halfdead*) rec;

         buf = pgmoneta_format_and_append(buf, "topparent: %u, leaf: %u, left: %u, right: %u",
                                          xlrec->topparent, xlrec->leafblk, xlrec->leftblk, xlrec->rightblk);
         break;
      }
      case XLOG_BTREE_UNLINK_PAGE_META:
      case XLOG_BTREE_UNLINK_PAGE: {
         struct xl_btree_unlink_page* xlrec = pgmoneta_wal_create_xl_btree_unlink_page();
         xlrec->parse(xlrec, rec);
         buf = xlrec->format(xlrec, buf);
         free(xlrec);

         break;
      }
      case XLOG_BTREE_NEWROOT: {
         struct xl_btree_newroot* xlrec = (struct xl_btree_newroot*) rec;

         buf = pgmoneta_format_and_append(buf, "level: %u", xlrec->level);
         break;
      }
      case XLOG_BTREE_REUSE_PAGE: {
         struct xl_btree_reuse_page* xlrec = pgmoneta_wal_create_xl_btree_reuse_page();
         xlrec->parse(xlrec, rec);
         buf = xlrec->format(xlrec, buf);
         free(xlrec);
         break;
      }
      case XLOG_BTREE_META_CLEANUP: {
         struct xl_btree_metadata* xlrec = pgmoneta_wal_create_xl_btree_metadata();

         xlrec->parse(xlrec, pgmoneta_wal_get_record_block_data(record, 0, NULL));
         buf = xlrec->format(xlrec, buf);
         free(xlrec);
         break;
      }
   }
   return buf;
}

char*
pgmoneta_wal_btree_identify (uint8_t info)
{
   {
      char* id = NULL;

      switch (info & ~XLR_INFO_MASK)
      {
         case XLOG_BTREE_INSERT_LEAF:
            id = "INSERT_LEAF";
            break;
         case XLOG_BTREE_INSERT_UPPER:
            id = "INSERT_UPPER";
            break;
         case XLOG_BTREE_INSERT_META:
            id = "INSERT_META";
            break;
         case XLOG_BTREE_SPLIT_L:
            id = "SPLIT_L";
            break;
         case XLOG_BTREE_SPLIT_R:
            id = "SPLIT_R";
            break;
         case XLOG_BTREE_INSERT_POST:
            id = "INSERT_POST";
            break;
         case XLOG_BTREE_DEDUP:
            id = "DEDUP";
            break;
         case XLOG_BTREE_VACUUM:
            id = "VACUUM";
            break;
         case XLOG_BTREE_DELETE:
            id = "DELETE";
            break;
         case XLOG_BTREE_MARK_PAGE_HALFDEAD:
            id = "MARK_PAGE_HALFDEAD";
            break;
         case XLOG_BTREE_UNLINK_PAGE:
            id = "UNLINK_PAGE";
            break;
         case XLOG_BTREE_UNLINK_PAGE_META:
            id = "UNLINK_PAGE_META";
            break;
         case XLOG_BTREE_NEWROOT:
            id = "NEWROOT";
            break;
         case XLOG_BTREE_REUSE_PAGE:
            id = "REUSE_PAGE";
            break;
         case XLOG_BTREE_META_CLEANUP:
            id = "META_CLEANUP";
            break;
      }

      return id;
   }
}
