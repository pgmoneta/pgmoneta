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
   bool checksums;     /* recompute page checksums on rewritten full-page images */
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
pgmoneta_wal_store_compute_crc(const char* buffer, uint32_t total_len)
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

/* PostgreSQL 18 accepted XLP_BKP_REMOVABLE (0x0008) as a valid page-header
 * flag; PostgreSQL 19 removed it, so 19's XLP_ALL_FLAGS is 0x0007 (see
 * walfile/wal_reader.h). The remaining XLP_* page-header flags did not change
 * between the two versions. The explicit remapping table below keeps every
 * defined 19.x flag and drops the legacy 18-only bit. */
uint16_t
pgmoneta_wal_store_remap_page_flags(uint16_t flags)
{
   uint16_t out = 0;

   if (flags & XLP_FIRST_IS_CONTRECORD)
   {
      out |= XLP_FIRST_IS_CONTRECORD;
   }
   if (flags & XLP_LONG_HEADER)
   {
      out |= XLP_LONG_HEADER;
   }
   if (flags & XLP_FIRST_IS_OVERWRITE_CONTRECORD)
   {
      out |= XLP_FIRST_IS_OVERWRITE_CONTRECORD;
   }
   /* XLP_BKP_REMOVABLE (0x0008 in PG18) has no PG19 equivalent; it is dropped. */

   return out;
}

/* PostgreSQL data-page checksum (storage/checksum_impl.h, 32-way FNV-1a).
 * The checksum covers the whole block with pd_checksum zeroed, mixed with the
 * block number, reduced to a non-zero uint16.
 */
#define PAGE_CHECKSUM_SUMS  32
#define PAGE_CHECKSUM_PRIME 16777619

static const uint32_t page_checksum_base_offsets[PAGE_CHECKSUM_SUMS] = {
   0x5B1F36E9, 0xB8525960, 0x02AB50AA, 0x1DE66D2A,
   0x79FF467A, 0x9BB9F8A3, 0x217E7CD2, 0x83E13D2C,
   0xF8D4474F, 0xE39EB970, 0x42C6AE16, 0x993216FA,
   0x7B093B5D, 0x98DAFF3C, 0xF718902A, 0x0B1C9CDB,
   0xE58F764B, 0x187636BC, 0x5D7B3BB1, 0xE73DE7DE,
   0x92BEC979, 0xCCA6C0B2, 0x304A0979, 0x85AA43D4,
   0x783125BB, 0x6CA8EAA2, 0xE407EAC6, 0x4B5CFC3E,
   0x9FBF8C76, 0x15CA20BE, 0xF2CA9FD3, 0x959BD756};

static uint32_t
page_checksum_comp(uint32_t checksum, uint32_t value)
{
   uint32_t tmp = checksum ^ value;
   return tmp * PAGE_CHECKSUM_PRIME ^ (tmp >> 17);
}

static uint32_t
page_checksum_block(const char* page)
{
   uint32_t sums[PAGE_CHECKSUM_SUMS];
   uint32_t result = 0;
   uint32_t i, j;

   memcpy(sums, page_checksum_base_offsets, sizeof(page_checksum_base_offsets));

   for (i = 0; i < (uint32_t)(WAL_STORE_PAGE_SIZE / (sizeof(uint32_t) * PAGE_CHECKSUM_SUMS)); i++)
      for (j = 0; j < PAGE_CHECKSUM_SUMS; j++)
      {
         uint32_t word;
         memcpy(&word, page + (i * PAGE_CHECKSUM_SUMS + j) * sizeof(uint32_t), sizeof(uint32_t));
         sums[j] = page_checksum_comp(sums[j], word);
      }

   for (i = 0; i < 2; i++)
      for (j = 0; j < PAGE_CHECKSUM_SUMS; j++)
         sums[j] = page_checksum_comp(sums[j], 0);

   for (i = 0; i < PAGE_CHECKSUM_SUMS; i++)
      result ^= sums[i];

   return result;
}

static uint16_t
wal_store_page_checksum(char* page, uint32_t blkno)
{
   uint16_t save;
   uint32_t checksum;

   save = *((uint16_t*)(page + 8));
   *((uint16_t*)(page + 8)) = 0;

   checksum = page_checksum_block(page);
   checksum ^= blkno;

   *((uint16_t*)(page + 8)) = save;

   return (uint16_t)((checksum % 65535) + 1);
}

static void
wal_store_recompute_page_checksum(struct decoded_bkp_block* block)
{
   uint16_t checksum;

   if (!block || !block->in_use || !block->has_image || !block->apply_image)
   {
      return;
   }

   /* only full-page images can be checksummed in place */
   if ((block->bimg_info & BKPIMAGE_HAS_HOLE) || (block->bimg_info & BKPIMAGE_IS_COMPRESSED))
   {
      return;
   }

   checksum = wal_store_page_checksum(block->bkp_image, (uint32_t)block->blkno);
   *((uint16_t*)(block->bkp_image + 8)) = checksum;
}

void
pgmoneta_wal_store_recompute_page_checksums(struct wal_store* store, struct decoded_xlog_record* record)
{
   if (!store || !store->checksums || !record)
   {
      return;
   }

   pgmoneta_wal_store_recompute_record_page_checksums(record);
}

void
pgmoneta_wal_store_recompute_record_page_checksums(struct decoded_xlog_record* record)
{
   int i;

   if (!record)
   {
      return;
   }

   for (i = 0; i <= record->max_block_id; i++)
   {
      wal_store_recompute_page_checksum(&record->blocks[i]);
   }
}

static void
wal_store_segment_filename(struct wal_store* store, char* buf, size_t bufsz)
{
   pgmoneta_snprintf(buf, bufsz, "%08X%08X%08X",
                     store->tli, (uint32_t)(store->segno >> 32), (uint32_t)(store->segno & 0xFFFFFFFF));
}

static int
wal_store_open_segment_ex(struct wal_store* store, bool truncate)
{
   char fname[33];
   char path[MAX_PATH];
   int n;
   int flags = O_RDWR | O_CREAT;

   wal_store_segment_filename(store, fname, sizeof(fname));
   n = pgmoneta_snprintf(path, sizeof(path), "%s/%s", store->downstream_dir, fname);
   if (n < 0 || n >= (int)sizeof(path))
   {
      pgmoneta_log_error("wal_store: downstream segment path too long");
      return 1;
   }

   if (truncate)
   {
      flags |= O_TRUNC;
   }

   store->fd = open(path, flags, 0644);
   if (store->fd == -1)
   {
      pgmoneta_log_error("wal_store: could not open downstream segment %s: %m", path);
      return 1;
   }

   store->page_lsn = store->segno * (uint64_t)store->wal_seg_size;
   store->next_lsn = store->page_lsn;

   return 0;
}

static int
wal_store_open_segment(struct wal_store* store)
{
   return wal_store_open_segment_ex(store, true);
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

   if (store->page_lsn >= (store->segno + 1) * (uint64_t)store->wal_seg_size)
   {
      pgmoneta_log_error("wal_store: flush past end of downstream segment %lu at %llu",
                         (unsigned long)store->segno,
                         (unsigned long long)store->page_lsn);
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
pgmoneta_wal_store_sync_partial_page(struct wal_store* store)
{
   off_t offset;

   if (!store || store->fd == -1 || store->page_fill == 0)
   {
      return 0;
   }

   if (store->page_lsn >= (store->segno + 1) * (uint64_t)store->wal_seg_size)
   {
      pgmoneta_log_error("wal_store: sync past end of downstream segment %lu at %llu",
                         (unsigned long)store->segno,
                         (unsigned long long)store->page_lsn);
      return 1;
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
   uint64_t seg_off;
   char zeros[WAL_STORE_PAGE_SIZE];

   /* if the write cursor is already at (or past) the end of the current
    * segment there is nothing left to pad; flushing any further would wrap
    * around to offset 0 and corrupt the segment header */
   if (store->page_lsn >= (store->segno + 1) * (uint64_t)store->wal_seg_size)
   {
      store->page_lsn = (store->segno + 1) * (uint64_t)store->wal_seg_size;
      return 0;
   }

   seg_off = store->page_lsn % (uint64_t)store->wal_seg_size;
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
   int failed = 0;

   if (store->fd == -1)
   {
      return 0;
   }

   if (fsync(store->fd) != 0)
   {
      pgmoneta_log_error("wal_store: could not fsync segment: %m");
      failed = 1;
   }
   if (close(store->fd) != 0)
   {
      pgmoneta_log_error("wal_store: could not close segment: %m");
      failed = 1;
   }
   store->fd = -1;

   return failed;
}

static int wal_store_end_segment(struct wal_store* store);

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
         uint64_t seg_end = (store->segno + 1) * (uint64_t)store->wal_seg_size;
         if (store->page_lsn >= seg_end)
         {
            /* the page buffer holds a full page whose start sits at (or past)
             * the current downstream segment end; rotate before flushing so the
             * page header is written into the correct segment */
            if (wal_store_end_segment(store))
            {
               return 1;
            }
         }
         else if (wal_store_flush_page(store))
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
      if (store->page_lsn < (store->segno + 1) * (uint64_t)store->wal_seg_size)
      {
         /* there is a prefix of the current segment that still needs to be
          * flushed; otherwise the in-memory page belongs to the next segment */
         if (wal_store_pad_and_flush_page(store))
         {
            return 1;
         }
         if (wal_store_pad_segment_tail(store))
         {
            return 1;
         }
      }
      if (wal_store_close_segment(store))
      {
         return 1;
      }

      store->segno++;

      if (wal_store_open_segment(store))
      {
         return 1;
      }

      /* the open dropped us on a fresh segment whose in-memory page buffer still
       * holds the previous segment's stale content; start a proper long-header page
       * at the new segment base before any record is placed */
      wal_store_init_page(store, false, 0);

      return 0;
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

   crc = pgmoneta_wal_store_compute_crc(buf, sw.header.xl_tot_len);
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

   if (wal_store_open_segment(store))
   {
      return 1;
   }

   /* the open dropped us on a fresh segment whose in-memory page buffer still
    * holds the previous segment's stale content; start a proper long-header page
    * at the new segment base before any record is placed */
   wal_store_init_page(store, false, 0);

   return 0;
}

int
pgmoneta_wal_store_create(const char* downstream_dir, struct lsn_map* map, uint64_t sysid, uint32_t wal_seg_size, uint32_t xlog_blksz, uint32_t tli, struct wal_store** store)
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

   /* open segment 0 without truncation: on a restart the segment file may
    * already contain data that pgmoneta_wal_store_resume() must read back. */
   if (wal_store_open_segment_ex(s, false))
   {
      free(s->page);
      free(s);
      return 1;
   }

   wal_store_init_page(s, false, 0);

   *store = s;
   return 0;
}

/* Resume an existing downstream stream. The store keeps the last written
 * segment open in append mode and reconstructs the in-memory page from the
 * bytes that are already on disk, so the very next record is appended exactly
 * at next_lsn. xl_prev is the downstream LSN of the last record written; a
 * next_lsn of 0 means nothing was written yet and the store stays fresh. */
int
pgmoneta_wal_store_resume(struct wal_store* store, uint64_t xl_prev, uint64_t next_lsn)
{
   uint64_t segno;
   uint64_t page_lsn;
   uint32_t page_fill;

   if (!store || store->fd == -1)
   {
      return 1;
   }

   if (next_lsn == 0)
   {
      /* genuine fresh start: wipe any stale tail left in segment 0 */
      if (ftruncate(store->fd, 0) != 0)
      {
         pgmoneta_log_error("wal_store: could not truncate segment 0 on fresh start: %m");
         return 1;
      }
      return 0;
   }

   /* close the fresh (truncated) segment that create() opened; its content
    * was only the empty long-header page, which the resume below replaces */
   if (close(store->fd) != 0)
   {
      pgmoneta_log_error("wal_store: could not close fresh segment at resume: %m");
      store->fd = -1;
      return 1;
   }
   store->fd = -1;

   segno = (next_lsn - 1) / (uint64_t)store->wal_seg_size;
   store->segno = segno;

   if (wal_store_open_segment_ex(store, false))
   {
      return 1;
   }

   store->xl_prev = xl_prev;
   store->next_lsn = next_lsn;

   /* reconstruct the in-memory page. When the stream ends exactly on a page
    * boundary the previous page was already flushed in full, so start a fresh
    * empty page at next_lsn (keeping a full in-memory page from the previous
    * page would make its address fall before the current segment). */
   if (next_lsn % WAL_STORE_PAGE_SIZE == 0)
   {
      page_lsn = next_lsn;
      page_fill = 0;
   }
   else
   {
      page_lsn = next_lsn & ~(uint64_t)(WAL_STORE_PAGE_SIZE - 1);
      page_fill = (uint32_t)(next_lsn - page_lsn);
   }
   store->page_lsn = page_lsn;

   memset(store->page, 0, WAL_STORE_PAGE_SIZE);
   if (page_fill == 0)
   {
      /* resume landed exactly on a page boundary: there is no partial page to
       * read back, so start a proper fresh page at next_lsn; leaving the buffer
       * headerless would let the first record clobber the page header slot */
      wal_store_init_page(store, false, 0);
      page_fill = store->page_fill;
   }
   else
   {
      off_t off = (off_t)(store->page_lsn % (uint64_t)store->wal_seg_size);
      if (pread(store->fd, store->page, page_fill, off) != (ssize_t)page_fill)
      {
         pgmoneta_log_error("wal_store: could not read back partial page at resume: %m");
         return 1;
      }
   }
   store->page_fill = page_fill;

   /* trim any stale data beyond the last record so the sender never parses it.
    * The segment file holds content at segment-local offsets (page_lsn % seg_size),
    * so the size must be trimmed to the segment-local tail offset, not the
    * absolute stream position. */
   if (ftruncate(store->fd, (off_t)(next_lsn % (uint64_t)store->wal_seg_size)) != 0)
   {
      pgmoneta_log_error("wal_store: could not trim stale tail at resume: %m");
      return 1;
   }

   pgmoneta_log_info("wal_store: resumed downstream stream at %X/%X (segment %lu next_lsn %X/%X)",
                     LSN_FORMAT_ARGS(xl_prev),
                     (unsigned long)segno,
                     LSN_FORMAT_ARGS(next_lsn));

   return 0;
}

void
pgmoneta_wal_store_get_state(struct wal_store* store, uint64_t* segno, uint64_t* xl_prev, uint64_t* next_lsn)
{
   if (!store)
   {
      return;
   }
   if (segno)
   {
      *segno = store->segno;
   }
   if (xl_prev)
   {
      *xl_prev = store->xl_prev;
   }
   if (next_lsn)
   {
      *next_lsn = store->next_lsn;
   }
}

void
pgmoneta_wal_store_set_checksums(struct wal_store* store, bool checksums)
{
   if (store)
   {
      store->checksums = checksums;
   }
}

int
pgmoneta_wal_store_write_record(struct wal_store* store, struct decoded_xlog_record* record)
{
   char* buf = NULL;
   uint32_t total_len;
   uint32_t crc;
   uint64_t record_lsn;

   if (!store || !record)
   {
      return 1;
   }

   pgmoneta_wal_store_recompute_page_checksums(store, record);

   record->header.xl_prev = store->xl_prev;

   buf = pgmoneta_wal_encode_xlog_record(record, WAL_MAGIC_V19, NULL);
   if (!buf)
   {
      pgmoneta_log_error("wal_store: could not encode record at %X/%X",
                         LSN_FORMAT_ARGS(record->lsn));
      goto error;
   }

   total_len = ((struct xlog_record*)buf)->xl_tot_len;

   record_lsn = store->page_lsn + store->page_fill;

   if (store->page_fill == WAL_STORE_PAGE_SIZE)
   {
      if (store->page_lsn >= (store->segno + 1) * (uint64_t)store->wal_seg_size)
      {
         if (wal_store_end_segment(store))
         {
            goto error;
         }
         record_lsn = store->page_lsn + store->page_fill;
      }
      else if (wal_store_flush_page(store))
      {
         goto error;
      }
      wal_store_init_page(store, false, 0);
      record_lsn = store->page_lsn + store->page_fill;
   }

   if (record_lsn + MAXALIGN(total_len) > (store->segno + 1) * (uint64_t)store->wal_seg_size - SIZE_OF_XLOG_RECORD)
   {
      /* a record may never consume the tail of a segment: PostgreSQL always
       * ends a completed segment with an XLOG_SWITCH record, so reserve its
       * space before the record so the switch always fits on rotation */
      pgmoneta_log_info("wal_store: rotating downstream segment at %X/%X",
                        LSN_FORMAT_ARGS(record_lsn));
      if (wal_store_end_segment(store))
      {
         goto error;
      }
      record_lsn = store->page_lsn + store->page_fill;
      if (record_lsn + MAXALIGN(total_len) > (store->segno + 1) * (uint64_t)store->wal_seg_size - SIZE_OF_XLOG_RECORD)
      {
         pgmoneta_log_error("wal_store: record at %X/%X (%u bytes) is larger than a downstream segment",
                            LSN_FORMAT_ARGS(record->lsn), total_len);
         goto error;
      }
   }

   ((struct xlog_record*)buf)->xl_prev = store->xl_prev;

   crc = pgmoneta_wal_store_compute_crc(buf, total_len);
   ((struct xlog_record*)buf)->xl_crc = crc;

   if (wal_store_place(store, buf, total_len))
   {
      goto error;
   }

   store->next_lsn = store->page_lsn + store->page_fill;
   store->xl_prev = record_lsn;

   if (pgmoneta_lsn_map_put(store->map, (uint64_t)record->lsn, record_lsn))
   {
      pgmoneta_log_error("wal_store: could not record LSN mapping %X/%X -> %X/%X",
                         LSN_FORMAT_ARGS(record->lsn), LSN_FORMAT_ARGS(record_lsn));
      goto error;
   }

   free(buf);
   return 0;

error:
   free(buf);
   return 1;
}

int
pgmoneta_wal_store_flush(struct wal_store* store)
{
   if (!store)
   {
      return 1;
   }

   if (store->fd == -1)
   {
      return 0;
   }

   /* If the in-memory page starts at (or past) the end of the current
    * segment, the page buffer only holds the header of the next segment
    * (no data of its own yet): there is nothing to write here, and writing
    * it would corrupt the next segment's page header. The previous segment
    * was fully padded and fsynced when it was closed. */
   if (store->page_fill > 0 && store->page_lsn < (store->segno + 1) * (uint64_t)store->wal_seg_size)
   {
      if (store->page_fill == WAL_STORE_PAGE_SIZE)
      {
         /* a complete page: write it out and move to a fresh page */
         if (wal_store_flush_page(store))
         {
            return 1;
         }
         wal_store_init_page(store, false, 0);
      }
      else
      {
         /* a partially-filled page: PG WAL files always have full pages on
          * disk, so pad the rest of the page and write it out in full, but
          * keep the in-memory page (and its LSN) in place so the next record
          * continues on the same page instead of skipping to the next one. */
         off_t offset = (off_t)(store->page_lsn % (uint64_t)store->wal_seg_size);
         memset(store->page + store->page_fill, 0, WAL_STORE_PAGE_SIZE - store->page_fill);
         if (pwrite(store->fd, store->page, WAL_STORE_PAGE_SIZE, offset) != WAL_STORE_PAGE_SIZE)
         {
            pgmoneta_log_error("wal_store: could not write padded page at %llu: %m",
                               (unsigned long long)offset);
            return 1;
         }
      }
   }

   if (fsync(store->fd) != 0)
   {
      pgmoneta_log_error("wal_store: could not fsync file: %m");
      return 1;
   }

   return 0;
}

struct lsn_map*
pgmoneta_wal_store_get_map(struct wal_store* store)
{
   if (!store)
   {
      return NULL;
   }
   return store->map;
}

void
pgmoneta_wal_store_destroy(struct wal_store* store)
{
   int failed = 0;

   if (!store)
   {
      return;
   }

   /* make the last written page durable and close cleanly; report a failure
    * so the caller (receiver shutdown) can flag a partially persisted stream */
   if (pgmoneta_wal_store_flush(store))
   {
      failed = 1;
   }
   if (wal_store_close_segment(store))
   {
      failed = 1;
   }

   if (failed)
   {
      pgmoneta_log_error("wal_store: downstream stream not fully persisted at shutdown");
   }

   if (store->page)
   {
      free(store->page);
   }
   free(store);
}
