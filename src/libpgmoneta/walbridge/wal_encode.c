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
#include <walfile/wal_reader.h>
#include <walfile/rmgr.h>
#include <walfile/pg_control.h>

#include <walbridge/wal_encode.h>
#include <walbridge/wal_store.h>

/* system */
#include <stdlib.h>
#include <string.h>

#define WAL_ENCODER_PAGE_SIZE 8192

struct wal_encoder
{
   uint64_t sysid;
   uint32_t wal_seg_size;
   uint32_t xlog_blksz;
   uint32_t tli;
   uint64_t segno;
   uint64_t xl_prev;      /* downstream LSN of the last record written */
   uint64_t next_lsn;     /* downstream LSN where the next record will start */
   char* page;            /* current page buffer */
   uint32_t page_fill;    /* bytes used in the current page */
   uint64_t page_lsn;     /* LSN of the start of the current page */
   uint32_t page_emitted; /* bytes of the current page already handed out */
   bool checksums;        /* recompute page checksums on rewritten full-page images */

   /* emit buffer: downstream bytes produced so far, consumed by take() */
   char* emit;
   size_t emit_len;
   size_t emit_cap;
};

static int wal_encoder_init_page(struct wal_encoder* encoder, bool contrecord, uint32_t rem_len);
static int wal_encoder_flush_page(struct wal_encoder* encoder);
static int wal_encoder_pad_and_flush_page(struct wal_encoder* encoder);
static int wal_encoder_pad_segment_tail(struct wal_encoder* encoder);
static int wal_encoder_end_segment(struct wal_encoder* encoder);
static int wal_encoder_place(struct wal_encoder* encoder, const char* buf, uint32_t len);

static int
wal_encoder_emit_append(struct wal_encoder* encoder, const void* data, size_t len)
{
   if (encoder->emit_len + len > encoder->emit_cap)
   {
      size_t nc = encoder->emit_cap == 0 ? 4096 : encoder->emit_cap;
      while (nc < encoder->emit_len + len)
      {
         nc *= 2;
      }
      char* np = realloc(encoder->emit, nc);
      if (!np)
      {
         pgmoneta_log_error("wal_encode: out of memory growing emit buffer");
         return 1;
      }
      encoder->emit = np;
      encoder->emit_cap = nc;
   }
   if (len > 0)
   {
      memcpy(encoder->emit + encoder->emit_len, data, len);
      encoder->emit_len += len;
   }
   return 0;
}

int
pgmoneta_wal_encoder_create(uint64_t sysid, uint32_t wal_seg_size, uint32_t xlog_blksz,
                            uint32_t tli, bool checksums, struct wal_encoder** encoder)
{
   struct wal_encoder* e = NULL;

   if (encoder == NULL)
   {
      return 1;
   }

   e = calloc(1, sizeof(struct wal_encoder));
   if (!e)
   {
      pgmoneta_log_error("wal_encode: out of memory");
      return 1;
   }

   e->sysid = sysid;
   e->wal_seg_size = wal_seg_size;
   e->xlog_blksz = xlog_blksz;
   e->tli = tli;
   e->segno = 0;
   e->xl_prev = 0;
   e->next_lsn = 0;
   e->checksums = checksums;

   e->page = malloc(WAL_ENCODER_PAGE_SIZE);
   if (!e->page)
   {
      free(e);
      pgmoneta_log_error("wal_encode: out of memory");
      return 1;
   }

   e->page_lsn = 0;
   wal_encoder_init_page(e, false, 0);

   *encoder = e;
   return 0;
}

int
pgmoneta_wal_encoder_resume(struct wal_encoder* encoder, uint64_t xl_prev, uint64_t next_lsn)
{
   uint64_t segno;
   uint64_t page_lsn;
   uint32_t page_fill;
   uint32_t page_emitted;

   if (!encoder)
   {
      return 1;
   }

   if (next_lsn == 0)
   {
      /* genuine fresh start: segment 0, long header page at LSN 0 (the
       * create() already initialised the page and the page content will be
       * emitted once the first records are placed) */
      encoder->xl_prev = 0;
      encoder->next_lsn = 0;
      encoder->segno = 0;
      encoder->page_lsn = 0;
      wal_encoder_init_page(encoder, false, 0);
      return 0;
   }

   /* next_lsn is the byte offset of the next record, so it belongs to the
    * segment given by next_lsn / wal_seg_size (a segment base like 16MB is the
    * first byte of segment 1, not the continuation of segment 0) */
   segno = next_lsn / (uint64_t)encoder->wal_seg_size;
   encoder->segno = segno;
   encoder->xl_prev = xl_prev;
   encoder->next_lsn = next_lsn;

   /* reconstruct the page state so the next record is placed exactly at
    * next_lsn. The prefix bytes [page_lsn,next_lsn) are already in the peer's
    * possession, so byte emission starts at page_fill (page_emitted). */
   if (next_lsn % WAL_ENCODER_PAGE_SIZE == 0)
   {
      page_lsn = next_lsn;
      page_fill = 0;
   }
   else
   {
      page_lsn = next_lsn & ~(uint64_t)(WAL_ENCODER_PAGE_SIZE - 1);
      page_fill = (uint32_t)(next_lsn - page_lsn);
   }
   encoder->page_lsn = page_lsn;

   memset(encoder->page, 0, WAL_ENCODER_PAGE_SIZE);
   if (page_fill == 0)
   {
      /* resume landed exactly on a page boundary: nothing of the page is in
       * the peer's hands yet, so this is a fresh page (long header on a
       * segment base, short header elsewhere) */
      wal_encoder_init_page(encoder, false, 0);
      page_fill = encoder->page_fill;
      page_emitted = 0;
   }
   else
   {
      page_emitted = page_fill;
   }
   encoder->page_fill = page_fill;

   /* Bytes below next_lsn are already in the peer's possession, so the emit
    * watermark starts at the page offset of next_lsn. When the resume lands
    * exactly on a page boundary there are no possessed bytes inside this page,
    * so the fresh page header itself must still be emitted. */
   encoder->page_emitted = page_emitted;

   return 0;
}

static int
wal_encoder_init_page(struct wal_encoder* encoder, bool contrecord, uint32_t rem_len)
{
   bool long_header = (encoder->page_lsn % (uint64_t)encoder->wal_seg_size) == 0;
   struct xlog_page_header_data* phd;
   struct xlog_long_page_header_data* long_phd;

   memset(encoder->page, 0, WAL_ENCODER_PAGE_SIZE);

   if (long_header)
   {
      long_phd = (struct xlog_long_page_header_data*)encoder->page;
      long_phd->std.xlp_magic = WAL_MAGIC_V19;
      long_phd->std.xlp_info = XLP_LONG_HEADER;
      if (contrecord)
      {
         long_phd->std.xlp_info |= XLP_FIRST_IS_CONTRECORD;
      }
      long_phd->std.xlp_tli = encoder->tli;
      long_phd->std.xlp_pageaddr = encoder->page_lsn;
      long_phd->std.xlp_rem_len = rem_len;
      long_phd->xlp_sysid = encoder->sysid;
      long_phd->xlp_seg_size = encoder->wal_seg_size;
      long_phd->xlp_xlog_blcksz = encoder->xlog_blksz;
      encoder->page_fill = SIZE_OF_XLOG_LONG_PHD;
   }
   else
   {
      phd = (struct xlog_page_header_data*)encoder->page;
      phd->xlp_magic = WAL_MAGIC_V19;
      phd->xlp_info = 0;
      if (contrecord)
      {
         phd->xlp_info |= XLP_FIRST_IS_CONTRECORD;
      }
      phd->xlp_tli = encoder->tli;
      phd->xlp_pageaddr = encoder->page_lsn;
      phd->xlp_rem_len = rem_len;
      encoder->page_fill = SIZE_OF_XLOG_SHORT_PHD;
   }

   encoder->page_emitted = 0;

   return 0;
}

static int
wal_encoder_flush_page(struct wal_encoder* encoder)
{
   if (encoder->page_lsn >= (encoder->segno + 1) * (uint64_t)encoder->wal_seg_size)
   {
      pgmoneta_log_error("wal_encode: flush past end of downstream segment %lu at %llu",
                         (unsigned long)encoder->segno,
                         (unsigned long long)encoder->page_lsn);
      return 1;
   }

   if (encoder->page_emitted < WAL_ENCODER_PAGE_SIZE)
   {
      if (wal_encoder_emit_append(encoder,
                                  encoder->page + encoder->page_emitted,
                                  WAL_ENCODER_PAGE_SIZE - encoder->page_emitted))
      {
         return 1;
      }
   }

   encoder->page_lsn += WAL_ENCODER_PAGE_SIZE;
   encoder->page_emitted = 0;

   return 0;
}

static int
wal_encoder_pad_and_flush_page(struct wal_encoder* encoder)
{
   if (encoder->page_fill < WAL_ENCODER_PAGE_SIZE)
   {
      memset(encoder->page + encoder->page_fill, 0, WAL_ENCODER_PAGE_SIZE - encoder->page_fill);
   }
   return wal_encoder_flush_page(encoder);
}

static int
wal_encoder_pad_segment_tail(struct wal_encoder* encoder)
{
   uint64_t seg_off;
   char zeros[WAL_ENCODER_PAGE_SIZE];

   if (encoder->page_lsn >= (encoder->segno + 1) * (uint64_t)encoder->wal_seg_size)
   {
      encoder->page_lsn = (encoder->segno + 1) * (uint64_t)encoder->wal_seg_size;
      return 0;
   }

   seg_off = encoder->page_lsn % (uint64_t)encoder->wal_seg_size;
   memset(zeros, 0, sizeof(zeros));

   while (seg_off < (uint64_t)encoder->wal_seg_size)
   {
      if (wal_encoder_emit_append(encoder, zeros, WAL_ENCODER_PAGE_SIZE))
      {
         return 1;
      }
      seg_off += WAL_ENCODER_PAGE_SIZE;
   }

   encoder->page_lsn = (uint64_t)encoder->segno * (uint64_t)encoder->wal_seg_size + encoder->wal_seg_size;
   encoder->page_emitted = 0;

   return 0;
}

static int
wal_encoder_place(struct wal_encoder* encoder, const char* buf, uint32_t len)
{
   uint32_t off = 0;
   uint32_t pad;

   while (off < len)
   {
      uint32_t remaining = WAL_ENCODER_PAGE_SIZE - encoder->page_fill;
      uint32_t copy;

      if (remaining == 0)
      {
         if (wal_encoder_flush_page(encoder))
         {
            return 1;
         }
         wal_encoder_init_page(encoder, off > 0, off > 0 ? len - off : 0);
         continue;
      }

      copy = MIN(remaining, len - off);
      memcpy(encoder->page + encoder->page_fill, buf + off, copy);
      encoder->page_fill += copy;
      off += copy;
   }

   pad = (8 - (len & 7)) & 7;
   if (pad)
   {
      memset(encoder->page + encoder->page_fill, 0, pad);
      encoder->page_fill += pad;
   }

   return 0;
}

static int
wal_encoder_end_segment(struct wal_encoder* encoder)
{
   struct decoded_xlog_record sw;
   char* buf = NULL;
   uint64_t sw_lsn;
   uint32_t crc;

   memset(&sw, 0, sizeof(sw));
   sw.header.xl_tot_len = SIZE_OF_XLOG_RECORD;
   sw.header.xl_xid = INVALID_TRANSACTION_ID;
   sw.header.xl_prev = encoder->xl_prev;
   sw.header.xl_info = XLOG_SWITCH;
   sw.header.xl_rmid = RM_XLOG_ID;
   sw.max_block_id = -1;
   sw.main_data_len = 0;

   sw_lsn = encoder->page_lsn + encoder->page_fill;

   if (sw_lsn + SIZE_OF_XLOG_RECORD > (encoder->segno + 1) * (uint64_t)encoder->wal_seg_size)
   {
      if (wal_encoder_pad_and_flush_page(encoder))
      {
         return 1;
      }
      if (wal_encoder_pad_segment_tail(encoder))
      {
         return 1;
      }

      encoder->segno++;
      encoder->page_lsn = encoder->segno * (uint64_t)encoder->wal_seg_size;
      wal_encoder_init_page(encoder, false, 0);

      return 0;
   }

   if (encoder->page_fill == WAL_ENCODER_PAGE_SIZE)
   {
      if (wal_encoder_flush_page(encoder))
      {
         return 1;
      }
      wal_encoder_init_page(encoder, false, 0);
      sw_lsn = encoder->page_lsn + encoder->page_fill;
   }

   buf = pgmoneta_wal_encode_xlog_record(&sw, WAL_MAGIC_V19, NULL);
   if (!buf)
   {
      pgmoneta_log_error("wal_encode: could not encode XLOG_SWITCH record");
      return 1;
   }

   crc = pgmoneta_wal_store_compute_crc(buf, sw.header.xl_tot_len);
   ((struct xlog_record*)buf)->xl_crc = crc;

   if (wal_encoder_place(encoder, buf, sw.header.xl_tot_len))
   {
      free(buf);
      return 1;
   }

   free(buf);

   encoder->xl_prev = sw_lsn;
   encoder->next_lsn = encoder->page_lsn + encoder->page_fill;

   if (wal_encoder_pad_and_flush_page(encoder))
   {
      return 1;
   }
   if (wal_encoder_pad_segment_tail(encoder))
   {
      return 1;
   }

   encoder->segno++;
   encoder->page_lsn = encoder->segno * (uint64_t)encoder->wal_seg_size;
   wal_encoder_init_page(encoder, false, 0);

   return 0;
}

int
pgmoneta_wal_encoder_write_record(struct wal_encoder* encoder, struct decoded_xlog_record* record, uint64_t* record_lsn)
{
   char* buf = NULL;
   uint32_t total_len;
   uint32_t crc;
   uint64_t rlsn;

   if (!encoder || !record)
   {
      return 1;
   }

   if (encoder->checksums)
   {
      pgmoneta_wal_store_recompute_record_page_checksums(record);
   }

   record->header.xl_prev = encoder->xl_prev;

   buf = pgmoneta_wal_encode_xlog_record(record, WAL_MAGIC_V19, NULL);
   if (!buf)
   {
      pgmoneta_log_error("wal_encode: could not encode record at %X/%X",
                         LSN_FORMAT_ARGS(record->lsn));
      goto error;
   }

   total_len = ((struct xlog_record*)buf)->xl_tot_len;
   rlsn = encoder->page_lsn + encoder->page_fill;

   if (encoder->page_fill == WAL_ENCODER_PAGE_SIZE)
   {
      if (wal_encoder_flush_page(encoder))
      {
         goto error;
      }
      wal_encoder_init_page(encoder, false, 0);
      rlsn = encoder->page_lsn + encoder->page_fill;
   }

   if (rlsn + MAXALIGN(total_len) > (encoder->segno + 1) * (uint64_t)encoder->wal_seg_size - SIZE_OF_XLOG_RECORD)
   {
      /* a record may never consume the tail of a segment: PostgreSQL always
       * ends a completed segment with an XLOG_SWITCH record, so reserve its
       * space before the record so the switch always fits on rotation */
      if (wal_encoder_end_segment(encoder))
      {
         goto error;
      }
      rlsn = encoder->page_lsn + encoder->page_fill;
      if (rlsn + MAXALIGN(total_len) > (encoder->segno + 1) * (uint64_t)encoder->wal_seg_size - SIZE_OF_XLOG_RECORD)
      {
         pgmoneta_log_error("wal_encode: record at %X/%X (%u bytes) is larger than a downstream segment",
                            LSN_FORMAT_ARGS(record->lsn), total_len);
         goto error;
      }
   }

   ((struct xlog_record*)buf)->xl_prev = encoder->xl_prev;

   crc = pgmoneta_wal_store_compute_crc(buf, total_len);
   ((struct xlog_record*)buf)->xl_crc = crc;

   if (wal_encoder_place(encoder, buf, total_len))
   {
      goto error;
   }

   encoder->next_lsn = encoder->page_lsn + encoder->page_fill;
   encoder->xl_prev = rlsn;

#if PGMONETA_DEBUG
   if (encoder->page_emitted > encoder->page_fill)
   {
      pgmoneta_log_debug("wal_encode: emit watermark %u exceeds page fill %u",
                         encoder->page_emitted, encoder->page_fill);
   }
#endif

   /* make the newly placed bytes available to the caller immediately */
   if (encoder->page_emitted < encoder->page_fill)
   {
      if (wal_encoder_emit_append(encoder,
                                  encoder->page + encoder->page_emitted,
                                  encoder->page_fill - encoder->page_emitted))
      {
         goto error;
      }
      encoder->page_emitted = encoder->page_fill;
   }

   if (record_lsn)
   {
      *record_lsn = rlsn;
   }

   free(buf);
   return 0;

error:
   free(buf);
   return 1;
}

int
pgmoneta_wal_encoder_take(struct wal_encoder* encoder, char** out, size_t* len)
{
   if (!encoder)
   {
      return 1;
   }

   if (encoder->emit_len == 0)
   {
      *out = NULL;
      *len = 0;
      return 0;
   }

   *out = encoder->emit;
   *len = encoder->emit_len;
   encoder->emit = NULL;
   encoder->emit_len = 0;
   encoder->emit_cap = 0;

   return 0;
}

uint64_t
pgmoneta_wal_encoder_next_lsn(struct wal_encoder* encoder)
{
   return encoder ? encoder->next_lsn : 0;
}

uint64_t
pgmoneta_wal_encoder_xl_prev(struct wal_encoder* encoder)
{
   return encoder ? encoder->xl_prev : 0;
}

uint64_t
pgmoneta_wal_encoder_segno(struct wal_encoder* encoder)
{
   return encoder ? encoder->segno : 0;
}

void
pgmoneta_wal_encoder_destroy(struct wal_encoder* encoder)
{
   if (!encoder)
   {
      return;
   }
   free(encoder->page);
   free(encoder->emit);
   free(encoder);
}