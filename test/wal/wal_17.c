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

/* pgmoneta */
#include <pgmoneta.h>
#include <configuration.h>
#include <deque.h>
#include <json.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <walfile/pg_control.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>
#include <value.h>
#include <tsclient.h>
#include <walfile.h>
#include <wal_utils.h>

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
generate_check_point_shutdown_v17(void)
{
   struct walfile* wf = NULL;
   struct xlog_long_page_header_data* long_phd = NULL;
   struct deque* page_headers = NULL;
   struct deque* records = NULL;
   struct xlog_page_header_data* ph = NULL;
   struct decoded_xlog_record* rec = NULL;

   wf = (struct walfile*)malloc(sizeof(struct walfile));
   if (wf == NULL)
   {
      goto error;
   }

   wf->magic_number = 0xD116;

   long_phd = (struct xlog_long_page_header_data*)malloc(sizeof(struct xlog_long_page_header_data));
   if (long_phd == NULL)
   {
      goto error;
   }
   long_phd->std.xlp_magic = 0xD116;
   long_phd->std.xlp_info = 0;
   long_phd->std.xlp_tli = 1;
   long_phd->std.xlp_pageaddr = 0;
   long_phd->xlp_seg_size = 16777216;
   long_phd->xlp_xlog_blcksz = 50;
   long_phd->std.xlp_rem_len = 0;
   wf->long_phd = long_phd;

   if (pgmoneta_deque_create(false, &page_headers))
   {
      goto error;
   }
   wf->page_headers = page_headers;

   ph = (struct xlog_page_header_data*)malloc(sizeof(struct xlog_page_header_data));
   if (ph == NULL)
   {
      goto error;
   }
   ph->xlp_magic = 0xD116;
   ph->xlp_info = 0;
   ph->xlp_tli = 1;
   ph->xlp_pageaddr = 0;
   if (pgmoneta_deque_add(page_headers, NULL, (uintptr_t)ph, 0))
   {
      goto error;
   }

   if (pgmoneta_deque_create(false, &records))
   {
      goto error;
   }
   wf->records = records;

   rec = (struct decoded_xlog_record*)malloc(sizeof(struct decoded_xlog_record));
   if (rec == NULL)
   {
      goto error;
   }
   rec->header.xl_tot_len = sizeof(struct xlog_record) + sizeof(struct check_point_v17);
   rec->header.xl_xid = 0;
   rec->header.xl_prev = 0;
   rec->header.xl_info = XLOG_CHECKPOINT_SHUTDOWN;
   rec->header.xl_rmid = 0;
   rec->header.xl_crc = 0;
   rec->main_data_len = sizeof(struct check_point_v17);
   rec->main_data = (char*)malloc(rec->main_data_len);
   if (rec->main_data == NULL)
   {
      goto error;
   }

   struct check_point_v17 cp;
   memset(&cp, 0, sizeof(cp));
   cp.redo = 0x123456789ABCDEF0ULL;
   cp.this_timeline_id = 1;
   cp.prev_timeline_id = 1;
   cp.full_page_writes = true;
   cp.wal_level = 2;
   cp.next_xid.value = 42;
   cp.next_oid = 100;
   cp.next_multi = 200;
   cp.next_multi_offset = 300;
   cp.oldest_xid = 400;
   cp.oldest_xid_db = 500;
   cp.oldest_multi = 600;
   cp.oldest_multi_db = 700;
   cp.time = 800;
   cp.oldest_commit_ts_xid = 900;
   cp.newest_commit_ts_xid = 1000;
   cp.oldest_active_xid = 1100;
   memcpy(rec->main_data, &cp, sizeof(cp));
   if (pgmoneta_deque_add(records, NULL, (uintptr_t)rec, 0))
   {
      goto error;
   }

   memset(rec->blocks, 0, sizeof(rec->blocks));
   rec->max_block_id = -1; // No block references

   return wf;

error:
   if (wf != NULL)
   {
      if (long_phd != NULL)
      {
         free(long_phd);
      }
      if (page_headers != NULL)
      {
         struct deque_iterator* iter = NULL;
         if (pgmoneta_deque_iterator_create(page_headers, &iter) == 0)
         {
            while (pgmoneta_deque_iterator_next(iter))
            {
               free((void*)iter->value->data);
            }
            pgmoneta_deque_iterator_destroy(iter);
         }
         pgmoneta_deque_destroy(page_headers);
      }
      if (records != NULL)
      {
         struct deque_iterator* iter = NULL;
         if (pgmoneta_deque_iterator_create(records, &iter) == 0)
         {
            while (pgmoneta_deque_iterator_next(iter))
            {
               struct decoded_xlog_record* r = (struct decoded_xlog_record*)iter->value->data;
               free(r->main_data);
               free(r);
            }
            pgmoneta_deque_iterator_destroy(iter);
         }
         pgmoneta_deque_destroy(records);
      }
      free(wf);
   }
   return NULL;
}

struct walfile*
generate_check_point_online_v17()
{
   return NULL;  // Placeholder for actual implementation
}

struct walfile*
generate_commit_ts_truncate_v17()
{
   return NULL;  // Placeholder for actual implementation
}

struct walfile*
generate_heap_prune_v17()
{
   return NULL;  // Placeholder for actual implementation
}

struct walfile*
generate_end_of_recovery_v17()
{
   return NULL;  // Placeholder for actual implementation
}