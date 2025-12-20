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
 */

#include <walfile/rm.h>
#include <walfile/rm_gist.h>
#include <walfile/transaction.h>
#include <utils.h>
#include <wal.h>

struct gist_xlog_delete*
create_gist_xlog_delete(void)
{
   struct gist_xlog_delete* wrapper = malloc(sizeof(struct gist_xlog_delete));

   if (server_config->version >= 16)
   {
      wrapper->parse = pgmoneta_wal_parse_gist_xlog_delete_v16;
      wrapper->format = pgmoneta_wal_format_gist_xlog_delete_v16;
   }
   else
   {
      wrapper->parse = pgmoneta_wal_parse_gist_xlog_delete_v15;
      wrapper->format = pgmoneta_wal_format_gist_xlog_delete_v15;
   }

   return wrapper;
}

// Parsing function for version 15
void
pgmoneta_wal_parse_gist_xlog_delete_v15(struct gist_xlog_delete* wrapper, void* rec)
{
   char* ptr = (char*)rec;
   memcpy(&wrapper->data.v15.latestRemovedXid, ptr, sizeof(transaction_id));
   ptr += sizeof(transaction_id);
   memcpy(&wrapper->data.v15.ntodelete, ptr, sizeof(uint32_t));
}

// Parsing function for version 16
void
pgmoneta_wal_parse_gist_xlog_delete_v16(struct gist_xlog_delete* wrapper, void* rec)
{
   char* ptr = (char*)rec;
   memcpy(&wrapper->data.v16.snapshotConflictHorizon, ptr, sizeof(transaction_id));
   ptr += sizeof(transaction_id);
   memcpy(&wrapper->data.v16.ntodelete, ptr, sizeof(uint32_t));
   ptr += sizeof(uint8_t);
   if (wrapper->data.v16.ntodelete > 0)
   {
      memcpy(wrapper->data.v16.offsets, ptr,
             wrapper->data.v16.ntodelete * sizeof(offset_number));
   }
}

// Formatting function for version 15
char*
pgmoneta_wal_format_gist_xlog_delete_v15(struct gist_xlog_delete* wrapper, char* buf)
{
   struct gist_xlog_delete_v15* xlrec = &wrapper->data.v15;
   buf = pgmoneta_format_and_append(buf, "latestRemovedXid: %u; ntodelete: %u", xlrec->latestRemovedXid, xlrec->ntodelete);
   return buf;
}

// Formatting function for version 16
char*
pgmoneta_wal_format_gist_xlog_delete_v16(struct gist_xlog_delete* wrapper, char* buf)
{
   struct gist_xlog_delete_v16* xlrec = &wrapper->data.v16;
   buf = pgmoneta_format_and_append(buf, "delete: snapshot_conflict_horizon_id %u, nitems: %u",
                                    xlrec->snapshotConflictHorizon, xlrec->ntodelete);
   return buf;
}

struct gist_xlog_page_reuse*
create_gist_xlog_page_reuse(void)
{
   struct gist_xlog_page_reuse* wrapper = malloc(sizeof(struct gist_xlog_page_reuse));

   if (server_config->version >= 16)
   {
      wrapper->parse = pgmoneta_wal_parse_gist_xlog_page_reuse_v16;
      wrapper->format = pgmoneta_wal_format_gist_xlog_page_reuse_v16;
   }
   else
   {
      wrapper->parse = pgmoneta_wal_parse_gist_xlog_page_reuse_v15;
      wrapper->format = pgmoneta_wal_format_gist_xlog_page_reuse_v15;
   }

   return wrapper;
}

void
pgmoneta_wal_parse_gist_xlog_page_reuse_v15(struct gist_xlog_page_reuse* wrapper, void* rec)
{
   char* ptr = (char*)rec;
   memcpy(&wrapper->data.v15.node, ptr, sizeof(struct rel_file_node));
   ptr += sizeof(struct rel_file_node);
   memcpy(&wrapper->data.v15.block, ptr, sizeof(block_id));
   ptr += sizeof(block_id);
   memcpy(&wrapper->data.v15.latestRemovedFullXid, ptr, sizeof(struct full_transaction_id));
}

void
pgmoneta_wal_parse_gist_xlog_page_reuse_v16(struct gist_xlog_page_reuse* wrapper, void* rec)
{
   char* ptr = (char*)rec;
   memcpy(&wrapper->data.v16.locator, ptr, sizeof(struct rel_file_locator));
   ptr += sizeof(struct rel_file_locator);
   memcpy(&wrapper->data.v16.block, ptr, sizeof(block_id));
   ptr += sizeof(block_id);
   memcpy(&wrapper->data.v16.snapshot_conflict_horizon, ptr, sizeof(struct full_transaction_id));
}

char*
pgmoneta_wal_format_gist_xlog_page_reuse_v15(struct gist_xlog_page_reuse* wrapper, char* buf)
{
   struct gist_xlog_page_reuse_v15* xlrec = &wrapper->data.v15;
   char* dbname = NULL;
   char* relname = NULL;
   char* spcname = NULL;

   if (pgmoneta_get_database_name(xlrec->node.dbNode, &dbname))
   {
      goto error;
   }

   if (pgmoneta_get_relation_name(xlrec->node.relNode, &relname))
   {
      goto error;
   }

   if (pgmoneta_get_tablespace_name(xlrec->node.spcNode, &spcname))
   {
      goto error;
   }

   buf = pgmoneta_format_and_append(buf, "rel %s/%s/%s; blk %u; latestRemovedXid %u:%u",
                                    spcname, dbname,
                                    relname, xlrec->block,
                                    EPOCH_FROM_FULL_TRANSACTION_ID(xlrec->latestRemovedFullXid),
                                    XID_FROM_FULL_TRANSACTION_ID(xlrec->latestRemovedFullXid));

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

char*
pgmoneta_wal_format_gist_xlog_page_reuse_v16(struct gist_xlog_page_reuse* wrapper, char* buf)
{
   struct gist_xlog_page_reuse_v16* xlrec = &wrapper->data.v16;

   char* dbname = NULL;
   char* relname = NULL;
   char* spcname = NULL;

   if (pgmoneta_get_database_name(xlrec->locator.dbOid, &dbname))
   {
      goto error;
   }

   if (pgmoneta_get_relation_name(xlrec->locator.relNumber, &relname))
   {
      goto error;
   }

   if (pgmoneta_get_tablespace_name(xlrec->locator.spcOid, &spcname))
   {
      goto error;
   }

   buf = pgmoneta_format_and_append(buf, "rel %s/%s/%s; blk %u; snapshot_conflict_horizon_id %u:%u",
                                    spcname, dbname,
                                    relname, xlrec->block,
                                    EPOCH_FROM_FULL_TRANSACTION_ID(xlrec->snapshot_conflict_horizon),
                                    XID_FROM_FULL_TRANSACTION_ID(xlrec->snapshot_conflict_horizon));

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

static char*
out_gistxlogPageUpdate(char* buf, struct gist_xlog_page_update* xlrec __attribute__((unused)))
{
   return buf;
}

static char*
out_gistxlogPageSplit(char* buf, struct gist_xlog_page_split* xlrec)
{
   buf = pgmoneta_format_and_append(buf, "page_split: splits to %d pages",
                                    xlrec->npage);
   return buf;
}

static char*
out_gistxlogPageDelete(char* buf, struct gist_xlog_page_delete* xlrec)
{
   buf = pgmoneta_format_and_append(buf, "deleteXid %u:%u; downlink %u",
                                    EPOCH_FROM_FULL_TRANSACTION_ID(xlrec->deleteXid),
                                    XID_FROM_FULL_TRANSACTION_ID(xlrec->deleteXid),
                                    xlrec->downlinkOffset);
   return buf;
}

char*
pgmoneta_wal_gist_desc(char* buf, struct decoded_xlog_record* record)
{
   char* rec = XLOG_REC_GET_DATA(record);
   uint8_t info = XLOG_REC_GET_INFO(record) & ~XLR_INFO_MASK;
   struct gist_xlog_page_reuse* xlrec_reuse;
   struct gist_xlog_delete* xlrec_delete;

   switch (info)
   {
      case XLOG_GIST_PAGE_UPDATE:
         buf = out_gistxlogPageUpdate(buf, (struct gist_xlog_page_update*)rec);
         break;
      case XLOG_GIST_PAGE_REUSE:
         xlrec_reuse = create_gist_xlog_page_reuse();
         xlrec_reuse->parse(xlrec_reuse, rec);
         buf = xlrec_reuse->format(xlrec_reuse, buf);
         free(xlrec_reuse);
         break;
      case XLOG_GIST_DELETE:
         xlrec_delete = create_gist_xlog_delete();
         xlrec_delete->parse(xlrec_delete, rec);
         buf = xlrec_delete->format(xlrec_delete, buf);
         free(xlrec_delete);
         break;
      case XLOG_GIST_PAGE_SPLIT:
         buf = out_gistxlogPageSplit(buf, (struct gist_xlog_page_split*)rec);
         break;
      case XLOG_GIST_PAGE_DELETE:
         buf = out_gistxlogPageDelete(buf, (struct gist_xlog_page_delete*)rec);
         break;
      case XLOG_GIST_ASSIGN_LSN:
         /* No details to write out */
         break;
   }
   return buf;
}
