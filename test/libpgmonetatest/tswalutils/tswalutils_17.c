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

struct walfile*
pgmoneta_test_generate_check_point_shutdown_v17(void)
{
   struct walfile* wf = NULL;
   struct xlog_page_header_data* ph = NULL;
   struct decoded_xlog_record* rec = NULL;

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

   rec = (struct decoded_xlog_record*)malloc(sizeof(struct decoded_xlog_record));
   if (rec == NULL)
   {
      goto error;
   }

   memset(rec, 0, sizeof(struct decoded_xlog_record));

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

   rec->main_data_len = RANDOM_MAIN_DATA_LEN;
   rec->max_block_id = RANDOM_MAX_BLOCK_ID;
   rec->oversized = RANDOM_OVERSIZED;
   rec->record_origin = RANDOM_RECORD_ORIGIN;
   rec->toplevel_xid = RANDOM_TOPLEVEL_XID;
   rec->partial = RANDOM_PARTIAL;

   uint32_t total_length = sizeof(struct xlog_record);

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
   rec->header.xl_xid = 0;
   rec->header.xl_prev = 0;
   rec->header.xl_info = XLOG_CHECKPOINT_SHUTDOWN;
   rec->header.xl_rmid = 0;
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

   rec->main_data = (char*)malloc(rec->main_data_len);
   if (rec->main_data == NULL)
   {
      goto error;
   }

   memcpy(rec->main_data, &cp, sizeof(cp));

   if (pgmoneta_deque_add(wf->records, NULL, (uintptr_t)rec, ValueRef))
   {
      goto error;
   }

   return wf;

error:
   if (ph != NULL)
   {
      free(ph);
   }

   if (rec != NULL)
   {
      if (rec->main_data != NULL)
      {
         free(rec->main_data);
      }
      free(rec);
   }

   if (wf != NULL)
   {
      if (wf->long_phd != NULL)
      {
         free(wf->long_phd);
      }

      if (wf->page_headers != NULL)
      {
         pgmoneta_deque_destroy(wf->page_headers);
      }

      if (wf->records != NULL)
      {
         pgmoneta_deque_destroy(wf->records);
      }

      free(wf);
   }

   return NULL;
}
