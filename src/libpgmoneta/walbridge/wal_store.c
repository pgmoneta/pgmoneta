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
#include <walfile/pg_control.h>
#include <walfile/rmgr.h>
#include <walbridge/lsn_map.h>
#include <walbridge/wal_store.h>

/* system */
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define WAL_STORE_PAGE_SIZE 8192

struct wal_store
{
   char downstream_dir[MAX_PATH];
   struct lsn_map* map;
   uint64_t sysid;
   uint32_t wal_seg_size;
   uint32_t xlog_blksz;
   uint32_t tli;
   uint64_t segno;
   uint64_t xl_prev;   /* downstream LSN of the last record written */
   uint64_t next_lsn;  /* downstream LSN where the next record will start */
   int fd;             /* current segment file descriptor */
   char* page;         /* current page buffer */
   uint32_t page_fill; /* bytes used in the current page */
   uint64_t page_lsn;  /* LSN of the start of the current page */
};

static uint32_t crc32c_table[256];
static bool crc32c_initialized = false;

static void
crc32c_init_table(void)
{
   for (uint32_t i = 0; i < 256; i++)
   {
      uint32_t crc = i;
      for (int k = 0; k < 8; k++)
      {
         crc = crc & 1 ? (crc >> 1) ^ 0x82F63B78 : crc >> 1;
      }
      crc32c_table[i] = crc;
   }
}

static uint32_t
crc32c_update(uint32_t crc, const char* data, size_t len)
{
   if (!crc32c_initialized)
   {
      crc32c_init_table();
      crc32c_initialized = true;
   }

   for (size_t i = 0; i < len; i++)
   {
      crc = crc32c_table[(crc ^ (unsigned char)data[i]) & 0xFF] ^ (crc >> 8);
   }

   return crc;
}

uint32_t
wal_store_compute_crc(const char* buffer, uint32_t total_len)
{
   uint32_t crc;

   if (total_len < SIZE_OF_XLOG_RECORD)
   {
      return 0;
   }

   crc = crc32c_update(0xFFFFFFFF, buffer + SIZE_OF_XLOG_RECORD, total_len - SIZE_OF_XLOG_RECORD);
   crc = crc32c_update(crc, buffer, offsetof(struct xlog_record, xl_crc));

   return crc ^ 0xFFFFFFFF;
}

static void
wal_store_segment_filename(struct wal_store* store, char* buf, size_t bufsz)
{
   pgmoneta_snprintf(buf, bufsz, "%08X%08X%08X",
            store->tli, (uint32_t)(store->segno >> 32), (uint32_t)(store->segno & 0xFFFFFFFF));
}

static int
wal_store_open_segment(struct wal_store* store)
{
   char fname[33];
   char path[MAX_PATH];
   int n;

   wal_store_segment_filename(store, fname, sizeof(fname));
   n = pgmoneta_snprintf(path, sizeof(path), "%s/%s", store->downstream_dir, fname);
   if (n < 0 || n >= (int)sizeof(path))
   {
      pgmoneta_log_error("wal_store: downstream segment path too long");
      return 1;
   }

   store->fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
   if (store->fd == -1)
   {
      pgmoneta_log_error("wal_store: could not open downstream segment %s: %m", path);
      return 1;
   }

   store->page_lsn = store->segno * (uint64_t)store->wal_seg_size;
   store->next_lsn = store->page_lsn;

   return 0;
}

static void
wal_store_init_page(struct wal_store* store, bool contrecord, uint32_t rem_len)
{
   bool long_header = (store->page_lsn % (uint64_t)store->wal_seg_size) == 0;
   struct xlog_page_header_data* phd;
   struct xlog_long_page_header_data* long_phd;

   memset(store->page, 0, WAL_STORE_PAGE_SIZE);

   if (long_header)
   {
      long_phd = (struct xlog_long_page_header_data*)store->page;
      long_phd->std.xlp_magic = WAL_MAGIC_V19;
      long_phd->std.xlp_info = XLP_LONG_HEADER;
      if (contrecord)
      {
         long_phd->std.xlp_info |= XLP_FIRST_IS_CONTRECORD;
      }
      long_phd->std.xlp_tli = store->tli;
      long_phd->std.xlp_pageaddr = store->page_lsn;
      long_phd->std.xlp_rem_len = rem_len;
      long_phd->xlp_sysid = store->sysid;
      long_phd->xlp_seg_size = store->wal_seg_size;
      long_phd->xlp_xlog_blcksz = store->xlog_blksz;
      store->page_fill = SIZE_OF_XLOG_LONG_PHD;
   }
   else
   {
      phd = (struct xlog_page_header_data*)store->page;
      phd->xlp_magic = WAL_MAGIC_V19;
      phd->xlp_info = 0;
      if (contrecord)
      {
         phd->xlp_info |= XLP_FIRST_IS_CONTRECORD;
      }
      phd->xlp_tli = store->tli;
      phd->xlp_pageaddr = store->page_lsn;
      phd->xlp_rem_len = rem_len;
      store->page_fill = SIZE_OF_XLOG_SHORT_PHD;
   }
}

static int
wal_store_flush_page(struct wal_store* store)
{
   off_t offset;

   if (store->fd == -1)
   {
      return 1;
   }

   offset = (off_t)(store->page_lsn % (uint64_t)store->wal_seg_size);

   if (pwrite(store->fd, store->page, WAL_STORE_PAGE_SIZE, offset) != WAL_STORE_PAGE_SIZE)
   {
      pgmoneta_log_error("wal_store: could not write page at %llu: %m",
                         (unsigned long long)offset);
      return 1;
   }

   store->page_lsn += WAL_STORE_PAGE_SIZE;

   return 0;
}

int
wal_store_sync_partial_page(struct wal_store* store)
{
   off_t offset;

   if (!store || store->fd == -1 || store->page_fill == 0)
   {
      return 0;
   }

   offset = (off_t)(store->page_lsn % (uint64_t)store->wal_seg_size);

   if (pwrite(store->fd, store->page, store->page_fill, offset) != (ssize_t)store->page_fill)
   {
      pgmoneta_log_error("wal_store: could not sync partial page at %llu: %m",
                         (unsigned long long)offset);
      return 1;
   }

   return 0;
}

static int
wal_store_pad_and_flush_page(struct wal_store* store)
{
   if (store->page_fill < WAL_STORE_PAGE_SIZE)
   {
      memset(store->page + store->page_fill, 0, WAL_STORE_PAGE_SIZE - store->page_fill);
   }
   return wal_store_flush_page(store);
}

static int
wal_store_pad_segment_tail(struct wal_store* store)
{
   uint64_t seg_off = store->page_lsn % (uint64_t)store->wal_seg_size;
   char zeros[WAL_STORE_PAGE_SIZE];

   memset(zeros, 0, sizeof(zeros));

   while (seg_off < (uint64_t)store->wal_seg_size)
   {
      if (pwrite(store->fd, zeros, WAL_STORE_PAGE_SIZE, (off_t)seg_off) != WAL_STORE_PAGE_SIZE)
      {
         pgmoneta_log_error("wal_store: could not pad segment: %m");
         return 1;
      }
      seg_off += WAL_STORE_PAGE_SIZE;
   }

   store->page_lsn = (store->page_lsn / (uint64_t)store->wal_seg_size + 1) * (uint64_t)store->wal_seg_size;

   return 0;
}

static int
wal_store_close_segment(struct wal_store* store)
{
   if (store->fd == -1)
   {
      return 0;
   }

   if (fsync(store->fd) != 0)
   {
      pgmoneta_log_error("wal_store: could not fsync segment: %m");
   }
   if (close(store->fd) != 0)
   {
      pgmoneta_log_error("wal_store: could not close segment: %m");
   }
   store->fd = -1;

   return 0;
}

static int
wal_store_place(struct wal_store* store, const char* buf, uint32_t len)
{
   uint32_t off = 0;
   uint32_t pad;

   while (off < len)
   {
      uint32_t remaining = WAL_STORE_PAGE_SIZE - store->page_fill;
      uint32_t copy;

      if (remaining == 0)
      {
         if (wal_store_flush_page(store))
         {
            return 1;
         }
         wal_store_init_page(store, off > 0, off > 0 ? len - off : 0);
         continue;
      }

      copy = MIN(remaining, len - off);
      memcpy(store->page + store->page_fill, buf + off, copy);
      store->page_fill += copy;
      off += copy;
   }

   pad = (8 - (len & 7)) & 7;
   if (pad)
   {
      memset(store->page + store->page_fill, 0, pad);
      store->page_fill += pad;
   }

   return 0;
}

static int
wal_store_end_segment(struct wal_store* store)
{
   struct decoded_xlog_record sw;
   char* buf = NULL;
   uint64_t sw_lsn;
   uint32_t crc;

   memset(&sw, 0, sizeof(sw));
   sw.header.xl_tot_len = SIZE_OF_XLOG_RECORD;
   sw.header.xl_xid = INVALID_TRANSACTION_ID;
   sw.header.xl_prev = store->xl_prev;
   sw.header.xl_info = XLOG_SWITCH;
   sw.header.xl_rmid = RM_XLOG_ID;
   sw.max_block_id = -1;
   sw.main_data_len = 0;

   sw_lsn = store->page_lsn + store->page_fill;

   if (sw_lsn + SIZE_OF_XLOG_RECORD > (store->segno + 1) * (uint64_t)store->wal_seg_size)
   {
      if (wal_store_pad_and_flush_page(store))
      {
         return 1;
      }
      if (wal_store_pad_segment_tail(store))
      {
         return 1;
      }
      if (wal_store_close_segment(store))
      {
         return 1;
      }
      store->segno++;
      return wal_store_open_segment(store);
   }

   if (store->page_fill == WAL_STORE_PAGE_SIZE)
   {
      if (wal_store_flush_page(store))
      {
         return 1;
      }
      wal_store_init_page(store, false, 0);
      sw_lsn = store->page_lsn + store->page_fill;
   }

   buf = pgmoneta_wal_encode_xlog_record(&sw, WAL_MAGIC_V19, NULL);
   if (!buf)
   {
      pgmoneta_log_error("wal_store: could not encode XLOG_SWITCH record");
      return 1;
   }

   crc = wal_store_compute_crc(buf, sw.header.xl_tot_len);
   ((struct xlog_record*)buf)->xl_crc = crc;

   if (wal_store_place(store, buf, sw.header.xl_tot_len))
   {
      free(buf);
      return 1;
   }

   free(buf);

   store->xl_prev = sw_lsn;
   store->next_lsn = store->page_lsn + store->page_fill;

   if (wal_store_pad_and_flush_page(store))
   {
      return 1;
   }
   if (wal_store_pad_segment_tail(store))
   {
      return 1;
   }
   if (wal_store_close_segment(store))
   {
      return 1;
   }

   store->segno++;

   return wal_store_open_segment(store);
}

int
wal_store_create(const char* downstream_dir, struct lsn_map* map, uint64_t sysid, uint32_t wal_seg_size, uint32_t xlog_blksz, uint32_t tli, struct wal_store** store)
{
   struct wal_store* s = NULL;

   if (store == NULL || downstream_dir == NULL || map == NULL)
   {
      return 1;
   }

   s = calloc(1, sizeof(struct wal_store));
   if (!s)
   {
      pgmoneta_log_error("wal_store: out of memory");
      return 1;
   }

   pgmoneta_snprintf(s->downstream_dir, sizeof(s->downstream_dir), "%s", downstream_dir);
   s->map = map;
   s->sysid = sysid;
   s->wal_seg_size = wal_seg_size;
   s->xlog_blksz = xlog_blksz;
   s->tli = tli;
   s->segno = 0;
   s->xl_prev = 0;
   s->next_lsn = 0;
   s->fd = -1;

   s->page = malloc(WAL_STORE_PAGE_SIZE);
   if (!s->page)
   {
      free(s);
      pgmoneta_log_error("wal_store: out of memory");
      return 1;
   }

   pgmoneta_mkdir((char*)downstream_dir);

   if (wal_store_open_segment(s))
   {
      free(s->page);
      free(s);
      return 1;
   }

   wal_store_init_page(s, false, 0);

   *store = s;
   return 0;
}

int
wal_store_write_record(struct wal_store* store, struct decoded_xlog_record* record)
{
   char* buf = NULL;
   uint32_t total_len;
   uint32_t crc;
   uint64_t record_lsn;

   if (!store || !record)
   {
      return 1;
   }

   record->header.xl_prev = store->xl_prev;

   buf = pgmoneta_wal_encode_xlog_record(record, WAL_MAGIC_V19, NULL);
   if (!buf)
   {
      pgmoneta_log_error("wal_store: could not encode record at %X/%X",
                         LSN_FORMAT_ARGS(record->lsn));
      return 1;
   }

   total_len = ((struct xlog_record*)buf)->xl_tot_len;

   record_lsn = store->page_lsn + store->page_fill;

   if (store->page_fill == WAL_STORE_PAGE_SIZE)
   {
      if (wal_store_flush_page(store))
      {
         free(buf);
         return 1;
      }
      wal_store_init_page(store, false, 0);
      record_lsn = store->page_lsn + store->page_fill;
   }

   if (record_lsn + MAXALIGN(total_len) > (store->segno + 1) * (uint64_t)store->wal_seg_size)
   {
      pgmoneta_log_info("wal_store: rotating downstream segment at %X/%X",
                        LSN_FORMAT_ARGS(record_lsn));
      if (wal_store_end_segment(store))
      {
         free(buf);
         return 1;
      }
      record_lsn = store->page_lsn + store->page_fill;
      if (record_lsn + MAXALIGN(total_len) > (store->segno + 1) * (uint64_t)store->wal_seg_size)
      {
         pgmoneta_log_error("wal_store: record at %X/%X (%u bytes) is larger than a downstream segment",
                            LSN_FORMAT_ARGS(record->lsn), total_len);
         free(buf);
         return 1;
      }
   }

   ((struct xlog_record*)buf)->xl_prev = store->xl_prev;

   crc = wal_store_compute_crc(buf, total_len);
   ((struct xlog_record*)buf)->xl_crc = crc;

   if (wal_store_place(store, buf, total_len))
   {
      free(buf);
      return 1;
   }

   free(buf);

   store->next_lsn = store->page_lsn + store->page_fill;
   store->xl_prev = record_lsn;

   if (lsn_map_put(store->map, (uint64_t)record->lsn, record_lsn))
   {
      pgmoneta_log_error("wal_store: could not record LSN mapping %X/%X -> %X/%X",
                         LSN_FORMAT_ARGS(record->lsn), LSN_FORMAT_ARGS(record_lsn));
      return 1;
   }

   return 0;
}

int
wal_store_flush(struct wal_store* store)
{
   if (!store)
   {
      return 1;
   }

   if (store->fd == -1)
   {
      return 0;
   }

   if (store->page_fill > 0)
   {
      if (wal_store_flush_page(store))
      {
         return 1;
      }
      wal_store_init_page(store, false, 0);
   }

   if (fsync(store->fd) != 0)
   {
      pgmoneta_log_error("wal_store: could not fsync file: %m");
   }

   return 0;
}

void
wal_store_destroy(struct wal_store* store)
{
   if (!store)
   {
      return;
   }

   wal_store_flush(store);
   wal_store_close_segment(store);

   if (store->page)
   {
      free(store->page);
   }
   free(store);
}
