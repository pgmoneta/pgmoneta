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

/* pgmoneta */
#include <pgmoneta.h>
#include <json.h>
#include <logging.h>
#include <utils.h>
#include <wal.h>
#include <walfile.h>
#include <walfile/rmgr.h>
#include <walfile/wal_reader.h>

/* system */
#include <assert.h>
#include <libgen.h>
#include <stdint.h>
#include <string.h>

struct server* server_config;

static int decode_xlog_record(char* buffer, struct decoded_xlog_record* decoded, struct xlog_record* record, uint32_t block_size, uint16_t magic_value, xlog_rec_ptr lsn);
static void record_json(struct decoded_xlog_record* record, uint8_t magic_value, struct value** value);
static bool get_record_block_tag_extended(struct decoded_xlog_record* pRecord, int id, struct rel_file_locator* pLocator, enum fork_number* pNumber, block_number* pInt, buffer* pVoid);
static char* get_record_block_ref_info(char* buf, struct decoded_xlog_record* record, bool pretty, bool detailed_format, uint32_t* fpi_len, uint8_t magic_value);
static int magic_value_to_postgres_version(uint16_t magic_value);

static bool is_included(char* rm, struct deque* rms,
                        uint64_t s_lsn, uint64_t start_lsn,
                        uint64_t e_lsn, uint64_t end_lsn,
                        uint32_t xid, struct deque* xids, char** included_objects, char* rm_desc);

bool
is_bimg_apply(uint8_t bimg_info)
{
   int BKPIMAGE_APPLY = 0x04;
   if (server_config->version >= 15)
   {
      BKPIMAGE_APPLY = 0x02;
   }
   return (bimg_info & BKPIMAGE_APPLY) != 0;
}

static int
magic_value_to_postgres_version(uint16_t magic_value)
{
   switch (magic_value)
   {
      case 0xD106:
         return 13;
      case 0xD10D:
         return 14;
      case 0xD110:
         return 15;
      case 0xD113:
         return 16;
      case 0xD116:
         return 17;
      case 0xD118:
         return 18;
      default:
         return -1;
   }
}

bool
pgmoneta_wal_is_bkp_image_compressed(uint16_t magic_value, uint8_t bimg_info)
{
   if (magic_value_to_postgres_version(magic_value) >= 15)
   {
      return (bimg_info & (BKPIMAGE_COMPRESS_PGLZ | BKPIMAGE_COMPRESS_LZ4 | BKPIMAGE_COMPRESS_ZSTD)) != 0;
   }
   else
   {
      return ((bimg_info & BKPIMAGE_IS_COMPRESSED) != 0);
   }
}

static inline int
xlog_from_file_name(char* fname, timeline_id* tli, xlog_seg_no* logSegNo, int wal_segsz_bytes)
{
#define XLogSegmentsPerXLogId(wal_segsz_bytes) \
        (0x100000000UL / (wal_segsz_bytes))
   uint32_t log;
   uint32_t seg;
   int items_scanned;

   items_scanned = sscanf(fname, "%08X%08X%08X", tli, &log, &seg);
   if (items_scanned != 3)
   {
      return 1;
   }

   *logSegNo = (uint64_t) log * XLogSegmentsPerXLogId(wal_segsz_bytes) + seg;
   return 0;
}

char*
pgmoneta_wal_array_desc(char* buf, void* array, size_t elem_size, int count)
{
   if (count == 0)
   {
      buf = pgmoneta_format_and_append(buf, " []");
      return buf;
   }
   buf = pgmoneta_format_and_append(buf, " [");
   for (int i = 0; i < count; i++)
   {
      buf = pgmoneta_format_and_append(buf, "%u", *(offset_number*) ((char*) array + elem_size * i));
      if (i < count - 1)
      {
         buf = pgmoneta_format_and_append(buf, ", ");
      }
   }
   buf = pgmoneta_format_and_append(buf, "]");
   return buf;
}

static void
read_all_page_headers(FILE* file, struct xlog_long_page_header_data* long_header, struct walfile* wal_file)
{
   int page_number = 1;
   while (true)
   {
      fseek(file, page_number * long_header->xlp_xlog_blcksz, SEEK_SET);
      struct xlog_page_header_data* page_header = NULL;
      page_header = malloc(SIZE_OF_XLOG_SHORT_PHD);
      size_t bytes_read = fread(page_header, SIZE_OF_XLOG_SHORT_PHD, 1, file);
      if (feof(file))
      {
         free(page_header);
         goto finish;
      }
      if (bytes_read != 1)
      {
         pgmoneta_log_error("Error: Failed to read the complete data");
         goto error;
      }
      pgmoneta_deque_add(wal_file->page_headers, NULL, (uintptr_t) page_header, ValueRef);
      page_number++;
   }
finish:
   return;
error:
   pgmoneta_log_fatal("Error: Could not read all page headers");
   return;
}

int
pgmoneta_wal_parse_wal_file(char* path, int server, struct walfile* wal_file)
{
#define MALLOC(pointer, size) \
        pointer = malloc(size); \
        if (pointer == NULL) \
        { \
           pgmoneta_log_fatal("Error: Could not allocate memory for %s", #pointer); \
           goto error; \
        }

   struct xlog_record* record = NULL;
   struct xlog_long_page_header_data* long_header = NULL;
   char* buffer = NULL;
   struct decoded_xlog_record* decoded = NULL;
   struct xlog_page_header_data* page_header = NULL;
   struct walinfo_configuration* config = NULL;
   timeline_id tli = 0;
   xlog_seg_no logSegNo = 0;
   xlog_rec_ptr base;
   int wal_segz_bytes = DEFAULT_WAL_SEGZ_BYTES;
   char* temp_buffer = NULL;
   u_int32_t temp_next_record = 0;
   bool initialized = false;

   config = (struct walinfo_configuration*) shmem;

   FILE* file = fopen(path, "rb");
   if (file == NULL)
   {
      pgmoneta_log_fatal("Error: Could not open file %s", path);
      goto error;
   }

   // calculate the size of the file
   fseek(file, 0, SEEK_END);
   wal_segz_bytes = ftell(file);
   fseek(file, 0, SEEK_SET);

   MALLOC(long_header, SIZE_OF_XLOG_LONG_PHD);
   size_t bytes_read = fread(long_header, SIZE_OF_XLOG_LONG_PHD, 1, file);

   if (bytes_read < 1)
   {
      pgmoneta_log_error("Error: Failed to read the complete data");
      goto error;
   }

   assert(magic_value_to_postgres_version(long_header->std.xlp_magic) != -1);

   if (server == -1)
   {
      config->common.servers[0].version = magic_value_to_postgres_version(long_header->std.xlp_magic);
      server_config = &config->common.servers[0];
   }
   else
   {
      assert(config->common.servers[server].version == magic_value_to_postgres_version(long_header->std.xlp_magic));
      server_config = &config->common.servers[server];
   }

   uint32_t next_record = MAXALIGN(
      ftell(file) +
      ((long_header->std.xlp_rem_len / long_header->xlp_xlog_blcksz) * SIZE_OF_XLOG_SHORT_PHD) +
      long_header->std.xlp_rem_len % long_header->xlp_xlog_blcksz
      );
   int page_number = 0;
   wal_file->long_phd = long_header;
   read_all_page_headers(file, wal_file->long_phd, wal_file);

   if (xlog_from_file_name(basename(path), &tli, &logSegNo, wal_segz_bytes))
   {
      pgmoneta_log_fatal("Failed to extract LSN from the filename");
      goto error;
   }
   XLOG_SEG_NO_OFFEST_TO_REC_PTR(logSegNo, 0, wal_segz_bytes, base);

   if (long_header->std.xlp_rem_len > 0)
   {
      if (partial_record->xlog_record_bytes_read == 0)
      {
         decoded = malloc(sizeof(struct decoded_xlog_record));
         if (decoded == NULL)
         {
            pgmoneta_log_fatal("Error: Could not allocate memory for decoded");
            goto error;
         }
         decoded->partial = true;
         if (pgmoneta_deque_add(wal_file->records, NULL, (uintptr_t) decoded, ValueRef))
         {
            free(decoded);
            decoded = NULL;
            goto error;
         }
         decoded = NULL;
      }
      else
      {
         temp_next_record = next_record;
         next_record = SIZE_OF_XLOG_LONG_PHD;
      }
   }
   long_header = NULL;
   fseek(file, next_record, SEEK_SET);
   while (true)
   {
      // Check if next record is beyond the current page
      if (next_record >= (wal_file->long_phd->xlp_xlog_blcksz * (page_number + 1)))
      {
         page_number++;
         fseek(file, page_number * wal_file->long_phd->xlp_xlog_blcksz, SEEK_SET);
         MALLOC(page_header, SIZE_OF_XLOG_SHORT_PHD);
         bytes_read = fread(page_header, SIZE_OF_XLOG_SHORT_PHD, 1, file);
         if (feof(file))
         {
            decoded = calloc(1, sizeof(struct decoded_xlog_record));
            if (decoded == NULL)
            {
               pgmoneta_log_fatal("Error: Could not allocate memory for decoded");
               free(page_header);
               page_header = NULL;
               goto error;
            }
            decoded->partial = true;
            if (pgmoneta_deque_add(wal_file->records, NULL, (uintptr_t) decoded, ValueRef))
            {
               free(decoded);
               decoded = NULL;
               free(page_header);
               page_header = NULL;
               goto error;
            }
            decoded = NULL;
            free(page_header);
            page_header = NULL;
            goto finish;
         }
         if (bytes_read < 1)
         {
            pgmoneta_log_error("Error: Failed to read the complete data");
            free(page_header);
            page_header = NULL;
            goto error;
         }
         next_record = MAXALIGN(ftell(file) + page_header->xlp_rem_len);
         free(page_header);
         page_header = NULL;
         continue;
      }
      fseek(file, next_record, SEEK_SET);

      // Check if record crosses the page boundary
      if (ftell(file) + SIZE_OF_XLOG_RECORD > wal_file->long_phd->xlp_xlog_blcksz * (page_number + 1))
      {
         MALLOC(temp_buffer, SIZE_OF_XLOG_RECORD);
         uint32_t end_of_page = (page_number + 1) * wal_file->long_phd->xlp_xlog_blcksz;
         bytes_read = fread(temp_buffer, 1, end_of_page - ftell(file), file);

         fseek(file, SIZE_OF_XLOG_SHORT_PHD, SEEK_CUR);
         bytes_read += fread(temp_buffer + bytes_read, 1, SIZE_OF_XLOG_RECORD - bytes_read, file);

         if (feof(file) && bytes_read != SIZE_OF_XLOG_RECORD)
         {
            /* Save split header */
            MALLOC(partial_record->xlog_record, SIZE_OF_XLOG_RECORD);
            memcpy(partial_record->xlog_record, temp_buffer, bytes_read);
            partial_record->xlog_record_bytes_read = bytes_read;
            free(temp_buffer);
            temp_buffer = NULL;
            decoded = NULL;
            goto finish;
         }

         assert(bytes_read == SIZE_OF_XLOG_RECORD);
         record = (struct xlog_record*) temp_buffer;
         temp_buffer = NULL;
         page_number++;
      }
      else
      {
         if (partial_record->xlog_record != NULL)
         {
            MALLOC(record, SIZE_OF_XLOG_RECORD);
            if (partial_record->xlog_record_bytes_read != 0)
            {
               bytes_read = fread(partial_record->xlog_record + partial_record->xlog_record_bytes_read, 1, SIZE_OF_XLOG_RECORD - partial_record->xlog_record_bytes_read, file);
            }
            memcpy(record, partial_record->xlog_record, SIZE_OF_XLOG_RECORD);
            free(partial_record->xlog_record);
            partial_record->xlog_record = NULL;
            partial_record->xlog_record_bytes_read = 0;
            initialized = true;
         }
         else
         {
            MALLOC(record, SIZE_OF_XLOG_RECORD);
            bytes_read = fread(record, SIZE_OF_XLOG_RECORD, 1, file);
            if (bytes_read < 1)
            {
               pgmoneta_log_error("Error: Failed to read the complete data");
               goto error;
            }
         }
      }

      if (record->xl_tot_len == 0)
      {
         free(record);
         record = NULL;
         break;
      }
      uint32_t data_length = record->xl_tot_len - SIZE_OF_XLOG_RECORD;
      xlog_rec_ptr lsn = ftell(file) + base - SIZE_OF_XLOG_RECORD;
      next_record = ftell(file) + MAXALIGN(record->xl_tot_len - SIZE_OF_XLOG_RECORD);
      uint32_t end_of_page = (page_number + 1) * wal_file->long_phd->xlp_xlog_blcksz;

      MALLOC(buffer, data_length);

      // Read record data, possibly across page boundaries
      if (data_length + ftell(file) >= end_of_page)
      {
         size_t bytes_read = 0;
         size_t total_bytes_read = 0;
         uint32_t remaining_data_length = data_length;
         bytes_read = fread(buffer, 1, end_of_page - ftell(file), file);
         total_bytes_read += bytes_read;
         remaining_data_length -= bytes_read;
         while (remaining_data_length != 0)
         {
            if (feof(file))
            {
               /* Save xlog_record and any remaining split data */
               MALLOC(partial_record->xlog_record, SIZE_OF_XLOG_RECORD);
               memcpy(partial_record->xlog_record, record, SIZE_OF_XLOG_RECORD);
               partial_record->xlog_record_bytes_read = SIZE_OF_XLOG_RECORD;
               if (total_bytes_read != 0)
               {
                  MALLOC(partial_record->data_buffer, total_bytes_read);
                  memcpy(partial_record->data_buffer, buffer, total_bytes_read);
                  partial_record->data_buffer_bytes_read = total_bytes_read;
               }
               free(record);
               record = NULL;
               decoded = NULL;
               free(buffer);
               buffer = NULL;
               goto finish;
            }
            fseek(file, SIZE_OF_XLOG_SHORT_PHD, SEEK_CUR);
            bytes_read = fread(buffer + total_bytes_read, 1,
                               MIN(remaining_data_length, wal_file->long_phd->xlp_xlog_blcksz - SIZE_OF_XLOG_SHORT_PHD), file);
            remaining_data_length -= bytes_read;
            total_bytes_read += bytes_read;
         }
         assert(total_bytes_read == data_length);
      }
      else
      {
         if (partial_record->data_buffer_bytes_read != 0)
         {
            /* Copy partial data from previous file */
            memcpy(buffer, partial_record->data_buffer, partial_record->data_buffer_bytes_read);
            free(partial_record->data_buffer);
            partial_record->data_buffer = NULL;

            uint32_t bytes_needed = data_length - partial_record->data_buffer_bytes_read;
            bytes_read = fread(buffer + partial_record->data_buffer_bytes_read, 1, bytes_needed, file);

            partial_record->data_buffer_bytes_read = 0;
         }
         else
         {
            bytes_read = fread(buffer, 1, data_length, file);
            if (bytes_read != data_length)
            {
               pgmoneta_log_error("Error: Actual bytes read do not match the expected length");
               goto error;
            }
         }
         if (initialized)
         {
            next_record = temp_next_record; initialized = false;
         }
      }

      decoded = calloc(1, sizeof(struct decoded_xlog_record));
      if (decoded == NULL)
      {
         pgmoneta_log_fatal("Error: Could not allocate memory for decoded");
         goto error;
      }

      if (decode_xlog_record(buffer, decoded, record, wal_file->long_phd->xlp_xlog_blcksz, wal_file->long_phd->std.xlp_magic, lsn))
      {
         free(decoded);
         decoded = NULL;
         goto error;
      }
      else
      {
         if (pgmoneta_deque_add(wal_file->records, NULL, (uintptr_t) decoded, ValueRef))
         {
            free(decoded);
            decoded = NULL;
            goto error;
         }
         decoded = NULL;
      }
      free(buffer);
      buffer = NULL;
      free(record);
      record = NULL;
   }
finish:
   fclose(file);
   return 0;

error:
   // Cleanup all allocated resources
   free(long_header);
   free(page_header);
   free(temp_buffer);
   free(buffer);
   free(record);
   free(decoded);
   if (file != NULL)
   {
      fclose(file);
   }
   pgmoneta_log_fatal("Error: Could not parse WAL file");
   return 1;
}

static int
decode_xlog_record(char* buffer, struct decoded_xlog_record* decoded, struct xlog_record* record, uint32_t block_size, uint16_t magic_value, xlog_rec_ptr lsn)
{
#define COPY_HEADER_FIELD(_dst, _size)          \
        do {                                        \
           if (remaining < _size)                  \
           goto shortdata_err;                 \
           memcpy(_dst, ptr, _size);               \
           ptr += _size;                           \
           remaining -= _size;                     \
        } while (0)

   decoded->header = *record;
   decoded->lsn = lsn;
   decoded->next = NULL;
   decoded->record_origin = INVALID_REP_ORIGIN_ID;
   decoded->toplevel_xid = INVALID_TRANSACTION_ID;
   decoded->main_data = NULL;
   decoded->main_data_len = 0;
   decoded->max_block_id = -1;

   //read id
   int remaining = 0;
   uint32_t datatotal = 0;
   char* ptr = NULL;
   struct rel_file_locator* rlocator = NULL;
   uint8_t block_id;

   remaining = record->xl_tot_len - SIZE_OF_XLOG_RECORD;
   ptr = buffer;

   while (remaining > datatotal)
   {
      COPY_HEADER_FIELD(&block_id, sizeof(uint8_t));

      if (block_id == XLR_BLOCK_ID_DATA_SHORT)
      {
         /* xlog_record_data_header_short */
         uint8_t main_data_len;
         COPY_HEADER_FIELD(&main_data_len, sizeof(uint8_t));
         decoded->main_data_len = main_data_len;
         datatotal += main_data_len;
      }
      else if (block_id == XLR_BLOCK_ID_DATA_LONG)
      {
         /* xlog_record_data_header_long */
         uint32_t main_data_len;
         COPY_HEADER_FIELD(&main_data_len, sizeof(uint32_t));
         decoded->main_data_len = main_data_len;
         datatotal += main_data_len;
      }
      else if (block_id == XLR_BLOCK_ID_ORIGIN)
      {
         COPY_HEADER_FIELD(&decoded->record_origin, sizeof(rep_origin_id));
      }
      else if (block_id == XLR_BLOCK_ID_TOPLEVEL_XID)
      {
         COPY_HEADER_FIELD(&decoded->toplevel_xid, sizeof(transaction_id));
      }
      else if (block_id <= XLR_MAX_BLOCK_ID)
      {
         /* xlog_record_bloch_header */
         struct decoded_bkp_block* blk;
         uint8_t fork_flags;

         /* mark any intervening block IDs as not in use */
         for (int i = decoded->max_block_id + 1; i < block_id; ++i)
         {
            decoded->blocks[i].in_use = false;
         }

         if (block_id <= decoded->max_block_id)
         {
            goto err;
         }
         decoded->max_block_id = block_id;

         blk = &decoded->blocks[block_id];
         blk->in_use = true;
         blk->apply_image = false;

         COPY_HEADER_FIELD(&fork_flags, sizeof(uint8_t));
         blk->forknum = fork_flags & BKPBLOCK_FORK_MASK;
         blk->flags = fork_flags;
         blk->has_image = ((fork_flags & BKPBLOCK_HAS_IMAGE) != 0);
         blk->has_data = ((fork_flags & BKPBLOCK_HAS_DATA) != 0);

         blk->prefetch_buffer = InvalidBuffer;
         COPY_HEADER_FIELD(&blk->data_len, sizeof(uint16_t));
         /* cross-check that the HAS_DATA flag is set iff data_length > 0 */
         if (blk->has_data && blk->data_len == 0)
         {
            pgmoneta_log_fatal("BKPBLOCK_HAS_DATA set, but no data included");
            goto err;
         }
         if (!blk->has_data && blk->data_len != 0)
         {
            pgmoneta_log_fatal("BKPBLOCK_HAS_DATA not set, but data length is not zero");
            goto err;
         }
         datatotal += blk->data_len;

         if (blk->has_image)
         {
            COPY_HEADER_FIELD(&blk->bimg_len, sizeof(uint16_t));
            COPY_HEADER_FIELD(&blk->hole_offset, sizeof(uint16_t));
            COPY_HEADER_FIELD(&blk->bimg_info, sizeof(uint8_t));

            blk->apply_image = is_bimg_apply(blk->bimg_info);

            if (pgmoneta_wal_is_bkp_image_compressed(magic_value, blk->bimg_info))
            {
               if (blk->bimg_info & BKPIMAGE_HAS_HOLE)
               {
                  COPY_HEADER_FIELD(&blk->hole_length, sizeof(uint16_t));
               }
               else
               {
                  blk->hole_length = 0;
               }
            }
            else
            {
               blk->hole_length = block_size - blk->bimg_len;
            }
            datatotal += blk->bimg_len;

            /*
             * cross-check that hole_offset > 0, hole_length > 0 and
             * bimg_len < BLCKSZ if the HAS_HOLE flag is set.
             */
            if ((blk->bimg_info & BKPIMAGE_HAS_HOLE) &&
                (blk->hole_offset == 0 ||
                 blk->hole_length == 0 ||
                 blk->bimg_len == block_size))
            {
               pgmoneta_log_fatal(
                  "BKPIMAGE_HAS_HOLE set, but hole offset %u length %u block image length %u at %X/%X");
               goto err;
            }

            /*
             * cross-check that hole_offset == 0 and hole_length == 0 if
             * the HAS_HOLE flag is not set.
             */
            if (!(blk->bimg_info & BKPIMAGE_HAS_HOLE) &&
                (blk->hole_offset != 0 || blk->hole_length != 0))
            {
               pgmoneta_log_fatal("BKPIMAGE_HAS_HOLE not set, but hole offset %u length %u at %X/%X");
               goto err;
            }

            /*
             * Cross-check that bimg_len < BLCKSZ if it is compressed.
             */
            if (pgmoneta_wal_is_bkp_image_compressed(magic_value, blk->bimg_info) &&
                blk->bimg_len == block_size)
            {
               pgmoneta_log_fatal("BKPIMAGE_COMPRESSED set, but block image length %u at %X/%X");
               goto err;
            }

            /*
             * cross-check that bimg_len = BLCKSZ if neither HAS_HOLE is
             * set nor COMPRESSED().
             */
            if (!(blk->bimg_info & BKPIMAGE_HAS_HOLE) &&
                !pgmoneta_wal_is_bkp_image_compressed(magic_value, blk->bimg_info) &&
                blk->bimg_len != block_size)
            {
               pgmoneta_log_fatal(
                  "neither BKPIMAGE_HAS_HOLE nor BKPIMAGE_COMPRESSED set, but block image length is %u at %X/%X");
               goto err;
            }
         }
         if (!(fork_flags & BKPBLOCK_SAME_REL))
         {
            COPY_HEADER_FIELD(&blk->rlocator, sizeof(struct rel_file_locator));
            rlocator = &blk->rlocator;
         }
         else
         {
            if (rlocator == NULL)
            {
               pgmoneta_log_fatal("BKPBLOCK_SAME_REL set but no previous rel at %X/%X");
               goto err;
            }

            blk->rlocator = *rlocator;
         }
         COPY_HEADER_FIELD(&blk->blkno, sizeof(block_number));
      }
      else
      {
         pgmoneta_log_fatal("Invalid block_id %u at %X/%X");
         goto err;
      }
   }
   assert(remaining == datatotal);

   for (block_id = 0; block_id <= decoded->max_block_id; block_id++)
   {
      struct decoded_bkp_block* blk = &decoded->blocks[block_id];

      if (!blk->in_use)
      {
         continue;
      }

      assert(blk->has_image || !blk->apply_image);

      if (blk->has_image)
      {
         /* no need to align image */
         blk->bkp_image = malloc(blk->bimg_len);
         memcpy(blk->bkp_image, ptr, blk->bimg_len);
         ptr += blk->bimg_len;
      }
      if (blk->has_data)
      {
         blk->data = malloc(blk->data_len);
         memcpy(blk->data, ptr, blk->data_len);
         ptr += blk->data_len;
      }
   }

   if (decoded->main_data_len > 0)
   {
      decoded->main_data = malloc(decoded->main_data_len);
      if (decoded->main_data == NULL)
      {
         goto
         shortdata_err;
      }
      memcpy(decoded->main_data, ptr, decoded->main_data_len);
      ptr += decoded->main_data_len;
   }
   decoded->partial = false;

   return 0;

shortdata_err:
   pgmoneta_log_fatal("shortdata error");
   return 1;

err:
   return 1;
}

char*
pgmoneta_wal_get_record_block_data(struct decoded_xlog_record* record, uint8_t block_id, size_t* len)
{
   struct decoded_bkp_block* bkpb;

   if (block_id > record->max_block_id ||
       !record->blocks[block_id].in_use)
   {
      return NULL;
   }

   bkpb = &record->blocks[block_id];

   if (!bkpb->has_data)
   {
      if (len)
      {
         *len = 0;
      }
      return NULL;
   }
   else
   {
      if (len)
      {
         *len = bkpb->data_len;
      }
      return bkpb->data;
   }
}

static void
get_record_length(struct decoded_xlog_record* record, uint32_t* rec_len, uint32_t* fpi_len)
{
   int block_id;

   *fpi_len = 0;
   for (block_id = 0; block_id <= record->max_block_id; block_id++)
   {
      if (!XLogRecHasBlockRef(record, block_id))
      {
         continue;
      }

      if (XLogRecHasBlockImage(record, block_id))
      {
         *fpi_len += record->blocks[block_id].bimg_len;
      }
   }

/*
 * Calculate the length of the record as the total length - the length of
 * all the block images.
 */
   *rec_len = record->header.xl_tot_len - *fpi_len;
}

static char*
get_record_block_ref_info(char* buf, struct decoded_xlog_record* record, bool pretty, bool detailed_format, uint32_t* fpi_len, uint8_t magic_value)
{
   int block_id;

   assert(record != NULL);
   buf = pgmoneta_append(buf, "");

   if (detailed_format && pretty)
   {
      buf = pgmoneta_format_and_append(buf, "\n");
   }

   char* dbname = NULL;
   char* relname = NULL;
   char* spcname = NULL;

   for (block_id = 0; block_id <= record->max_block_id; block_id++)
   {
      struct rel_file_locator rlocator;
      enum fork_number forknum;
      block_number blk;

      if (!get_record_block_tag_extended(record, block_id, &rlocator, &forknum, &blk, NULL))
      {
         continue;
      }

      if (detailed_format)
      {
         /* Get block references in detailed format. */

         if (pretty)
         {
            buf = pgmoneta_format_and_append(buf, "\t");
         }
         else if (block_id > 0)
         {
            buf = pgmoneta_format_and_append(buf, " ");
         }

         if (pgmoneta_get_database_name(rlocator.dbOid, &dbname))
         {
            goto error;
         }

         if (pgmoneta_get_relation_name(rlocator.relNumber, &relname))
         {
            goto error;
         }

         if (pgmoneta_get_tablespace_name(rlocator.spcOid, &spcname))
         {
            goto error;
         }

         buf = pgmoneta_format_and_append(buf, "blkref #%d: rel %s/%s/%s forknum %d blk %u",
                                          block_id,
                                          spcname, dbname, relname,
                                          forknum,
                                          blk);

         if (XLogRecHasBlockImage(record, block_id))
         {
            uint8_t bimg_info = record->blocks[block_id].bimg_info;

            /* Calculate the amount of FPI data in the record. */
            if (fpi_len)
            {
               *fpi_len += record->blocks[block_id].bimg_len;
            }

            if (pgmoneta_wal_is_bkp_image_compressed(magic_value, bimg_info))
            {
               char* method;

               if ((bimg_info & BKPIMAGE_COMPRESS_PGLZ) != 0)
               {
                  method = "pglz";
               }
               else if ((bimg_info & BKPIMAGE_COMPRESS_LZ4) != 0)
               {
                  method = "lz4";
               }
               else if ((bimg_info & BKPIMAGE_COMPRESS_ZSTD) != 0)
               {
                  method = "zstd";
               }
               else
               {
                  method = "unknown";
               }

               buf = pgmoneta_format_and_append(buf,
                                                " (FPW%s); hole: offset: %u, length: %u, "
                                                "compression saved: %u, method: %s",
                                                record->blocks[block_id].apply_image ?
                                                "" : " for WAL verification",
                                                record->blocks[block_id].hole_offset,
                                                record->blocks[block_id].hole_length,
                                                8192 -
                                                record->blocks[block_id].hole_length -
                                                record->blocks[block_id].bimg_len,
                                                method);
            }
            else
            {
               buf = pgmoneta_format_and_append(buf,
                                                " (FPW%s); hole: offset: %u, length: %u",
                                                XLOG_REC_BLOCK_IMAGE_APPLY(record, block_id) ?
                                                "" : " for WAL verification",
                                                XLOG_REC_GET_BLOCK(record, block_id)->hole_offset,
                                                XLOG_REC_GET_BLOCK(record, block_id)->hole_length);
            }
         }

         if (pretty)
         {
            buf = pgmoneta_format_and_append(buf, "\n");
         }
      }
      else
      {
         /* Get block references in short format. */

         if (pgmoneta_get_database_name(rlocator.dbOid, &dbname))
         {
            goto error;
         }

         if (pgmoneta_get_relation_name(rlocator.relNumber, &relname))
         {
            goto error;
         }

         if (pgmoneta_get_tablespace_name(rlocator.spcOid, &spcname))
         {
            goto error;
         }

         if (forknum != MAIN_FORKNUM)
         {
            buf = pgmoneta_format_and_append(buf,
                                             ", blkref #%d: rel %u/%u/%u fork %d blk %u",
                                             block_id,
                                             spcname, dbname, relname,
                                             forknum,
                                             blk);
         }
         else
         {
            buf = pgmoneta_format_and_append(buf,
                                             ", blkref #%d: rel %u/%u/%u blk %u",
                                             block_id,
                                             spcname, dbname, relname,
                                             blk);
         }

         if (XLogRecHasBlockImage(record, block_id))
         {
            /* Calculate the amount of FPI data in the record. */
            if (fpi_len)
            {
               *fpi_len += XLOG_REC_GET_BLOCK(record, block_id)->bimg_len;
            }

            if (XLOG_REC_BLOCK_IMAGE_APPLY(record, block_id))
            {
               buf = pgmoneta_format_and_append(buf, " FPW");
            }
            else
            {
               buf = pgmoneta_format_and_append(buf, " FPW for WAL verification");
            }
         }
      }

      if (!detailed_format && pretty)
      {
         buf = pgmoneta_format_and_append(buf, "\n");
      }

      free(dbname);
      free(spcname);
      free(relname);
   }

   return buf;

error:
   free(dbname);
   free(spcname);
   free(relname);
   free(buf);
   return NULL;
}

static bool
get_record_block_tag_extended(struct decoded_xlog_record* pRecord, int id, struct rel_file_locator* pLocator, enum fork_number* pNumber,
                              block_number* pInt, buffer* pVoid)
{
   struct decoded_bkp_block* bkpb;

   if (!XLogRecHasBlockRef(pRecord, id))
   {
      return false;
   }

   bkpb = XLOG_REC_GET_BLOCK(pRecord, id);
   if (pLocator)
   {
      *pLocator = bkpb->rlocator;
   }
   if (pNumber)
   {
      *pNumber = bkpb->forknum;
   }
   if (pInt)
   {
      *pInt = bkpb->blkno;
   }
   if (pVoid)
   {
      *pVoid = bkpb->prefetch_buffer;
   }
   return true;
}

void
pgmoneta_wal_record_display(struct decoded_xlog_record* record, uint16_t magic_value, enum value_type type, FILE* out, bool quiet, bool color,
                            struct deque* rms, uint64_t start_lsn, uint64_t end_lsn, struct deque* xids, uint32_t limit, char** included_objects)
{
   static uint32_t current_limit = 0;
   char* header_str = NULL;
   char* rm_desc = NULL;
   char* backup_str = NULL;
   char* start_lsn_string = NULL;
   char* end_lsn_string = NULL;
   struct value* record_serialized = NULL;
   char* value_str = NULL;
   uint32_t rec_len = 0;
   uint32_t fpi_len = 0;

   if (!record->partial)
   {
      rm_desc = rmgr_table[record->header.xl_rmid].rm_desc(rm_desc, record);
      backup_str = get_record_block_ref_info(backup_str, record, false, true, &fpi_len, magic_value);

      char* record_desc = pgmoneta_format_and_append(NULL, "%s %s", rm_desc, backup_str);

      if (!is_included(rmgr_table[record->header.xl_rmid].name, rms,
                       record->header.xl_prev, start_lsn,
                       record->lsn, end_lsn,
                       record->header.xl_xid, xids, included_objects, record_desc))
      {
         free(record_desc);
         return;
      }
      free(record_desc);
   }

   current_limit++;
   if (limit > 0 && current_limit > limit)
   {
      return;
   }

   if (type == ValueJSON)
   {
      if (!quiet)
      {
         if (current_limit == 1)
         {
            fprintf(out, "{\"Record\": ");
         }
         else
         {
            fprintf(out, ",\n{\"Record\": ");
         }
         record_json(record, magic_value, &record_serialized);
         value_str = pgmoneta_value_to_string(record_serialized, FORMAT_JSON_COMPACT, NULL, 0);
         fprintf(out, "%s}", value_str);
         pgmoneta_value_destroy(record_serialized);
         free(value_str);
      }
   }
   else if (type == ValueString)
   {
      if (!quiet)
      {
         if (record->partial)
         {
            if (color)
            {
               fprintf(out, "%sIncomplete%s | | | | | %sSkipped%s\n",
                       COLOR_RED, COLOR_WHITE, COLOR_GREEN, COLOR_RESET);
            }
            else
            {
               fprintf(out, "Incomplete | | | | | Skipped\n");
            }
            return;
         }
         get_record_length(record, &rec_len, &fpi_len);
         start_lsn_string = pgmoneta_lsn_to_string(record->header.xl_prev);
         end_lsn_string = pgmoneta_lsn_to_string(record->lsn);

         if (color)
         {
            header_str = pgmoneta_format_and_append(header_str, "%s%s%s | %s%s%s | %s%s%s | %s%d%s | %s%d%s | %s%d%s",
                                                    COLOR_RED, rmgr_table[record->header.xl_rmid].name, COLOR_RESET,
                                                    COLOR_MAGENTA, start_lsn_string, COLOR_RESET,
                                                    COLOR_MAGENTA, end_lsn_string, COLOR_RESET,
                                                    COLOR_BLUE, rec_len, COLOR_RESET,
                                                    COLOR_YELLOW, record->header.xl_tot_len, COLOR_RESET,
                                                    COLOR_CYAN, record->header.xl_xid, COLOR_RESET);
         }
         else
         {
            header_str = pgmoneta_format_and_append(header_str, "%s | %s | %s | %d | %d | %d",
                                                    rmgr_table[record->header.xl_rmid].name,
                                                    start_lsn_string,
                                                    end_lsn_string,
                                                    rec_len,
                                                    record->header.xl_tot_len,
                                                    record->header.xl_xid);
         }

         if (color)
         {
            fprintf(out, "%s%s%s | %s%s %s%s\n",
                    COLOR_RED, header_str, COLOR_WHITE,
                    COLOR_GREEN, rm_desc,
                    backup_str, COLOR_RESET);
         }
         else
         {
            fprintf(out, "%s | %s %s\n", header_str, rm_desc, backup_str);
         }

         free(header_str);
         free(rm_desc);
         free(backup_str);
         free(start_lsn_string);
         free(end_lsn_string);
      }
   }
}

void
pgmoneta_wal_record_modify_rmgr_occurance(struct decoded_xlog_record* record, uint64_t start_lsn, uint64_t end_lsn)
{
   if (record->partial || !is_included(rmgr_table[record->header.xl_rmid].name, NULL,
                                       record->header.xl_prev, start_lsn,
                                       record->lsn, end_lsn,
                                       record->header.xl_xid, NULL, NULL, NULL))
   {
      return;
   }
   /* Increment the resource manager count */
   rmgr_summary_table[record->header.xl_rmid].number_of_records++;
}

static bool
is_included(char* rm, struct deque* rms,
            uint64_t s_lsn, uint64_t start_lsn,
            uint64_t e_lsn, uint64_t end_lsn,
            uint32_t xid, struct deque* xids, char** included_objects, char* rm_desc)
{
   struct deque_iterator* rms_iter = NULL;
   struct deque_iterator* xids_iter = NULL;

   if (start_lsn > 0 && s_lsn < start_lsn)
   {
      goto no;
   }

   if (end_lsn > 0 && e_lsn > end_lsn)
   {
      goto no;
   }

   if (rms != NULL && rm != NULL)
   {
      bool found = false;

      if (pgmoneta_deque_iterator_create(rms, &rms_iter))
      {
         pgmoneta_log_error("Failed to create RMS deque iterator");
         goto no;
      }

      while (!found && pgmoneta_deque_iterator_next(rms_iter))
      {
         char* v = NULL;

         v = (char*)pgmoneta_value_data(rms_iter->value);

         if (!strcmp(rm, v))
         {
            found = true;
         }
      }

      if (!found)
      {
         goto no;
      }
   }

   if (xids != NULL)
   {
      bool found = false;

      if (pgmoneta_deque_iterator_create(xids, &xids_iter))
      {
         pgmoneta_log_error("Failed to create XID deque iterator");
         goto no;
      }

      while (!found && pgmoneta_deque_iterator_next(xids_iter))
      {
         uint32_t v;

         v = (uint32_t)pgmoneta_value_data(xids_iter->value);

         if (xid == v)
         {
            found = true;
         }
      }

      if (!found)
      {
         goto no;
      }
   }

   if (included_objects != NULL && rm_desc != NULL)
   {
      char* oid = NULL;
      bool included = false;

      for (int i = 0; included_objects[i] != NULL; i++)
      {
         // If the object name is already there
         if (pgmoneta_is_substring(included_objects[i], rm_desc))
         {
            included = true;
            free(oid);
            break;
         }

         // If the object name is not in rm_desc, then we need to get its oid
         // and try to find it in the rm_desc again

         // 1. The object maybe a relation
         if (!pgmoneta_get_relation_oid(included_objects[i], &oid))
         {
            // If oid == included_objects[i], then the object was not found
            // and we don't need to check for substring
            if (oid != included_objects[i] && pgmoneta_is_substring(rm_desc, oid))
            {
               included = true;
               free(oid);
               break;
            }
         }

         // 2. The object maybe a database
         if (!pgmoneta_get_database_oid(included_objects[i], &oid))
         {
            // If oid == included_objects[i], then the object was not found
            // and we don't need to check for substring
            if (oid != included_objects[i] && pgmoneta_is_substring(rm_desc, oid))
            {
               included = true;
               free(oid);
               break;
            }
         }

         // 3. The object maybe a tablespace
         if (!pgmoneta_get_tablespace_oid(included_objects[i], &oid))
         {
            // If oid == included_objects[i], then the object was not found
            // and we don't need to check for substring
            if (oid != included_objects[i] && pgmoneta_is_substring(rm_desc, oid))
            {
               included = true;
               free(oid);
               break;
            }
         }

         if (!included)
         {
            free(oid);
            goto no;
         }
      }
   }

   pgmoneta_deque_iterator_destroy(rms_iter);
   pgmoneta_deque_iterator_destroy(xids_iter);

   return true;

no:
   pgmoneta_deque_iterator_destroy(rms_iter);
   pgmoneta_deque_iterator_destroy(xids_iter);

   return false;
}

static void
record_json(struct decoded_xlog_record* record, uint8_t magic_value, struct value** value)
{
   char* rm_desc = NULL;
   char* backup_str = NULL;
   struct json* record_json = NULL;
   uint32_t rec_len;
   uint32_t fpi_len;

   pgmoneta_json_create(&record_json);

   if (record->partial)
   {
      pgmoneta_json_put(record_json, "Partial", true, ValueBool);
      goto finish;
   }

   get_record_length(record, &rec_len, &fpi_len);

   rm_desc = rmgr_table[record->header.xl_rmid].rm_desc(rm_desc, record);
   backup_str = get_record_block_ref_info(backup_str, record, false, true, &fpi_len, magic_value);

   // Header serialization
   pgmoneta_json_put(record_json, "ResourceManager", (uintptr_t) rmgr_table[record->header.xl_rmid].name, ValueString);
   pgmoneta_json_put(record_json, "RecordLength", rec_len, ValueUInt32);
   pgmoneta_json_put(record_json, "TotalLength", record->header.xl_tot_len, ValueUInt32);
   pgmoneta_json_put(record_json, "Xid", record->header.xl_xid, ValueUInt32);
   pgmoneta_json_put(record_json, "Info", record->header.xl_info, ValueUInt8);
   pgmoneta_json_put(record_json, "StartLSN", record->header.xl_prev, ValueUInt64);
   pgmoneta_json_put(record_json, "EndLSN", record->lsn, ValueUInt64);
   pgmoneta_json_put(record_json, "ResourceManagerId", record->header.xl_rmid, ValueUInt8);
   pgmoneta_json_put(record_json, "Crc", record->header.xl_crc, ValueUInt32);

   // Data serialization
   pgmoneta_json_put(record_json, "Data", (uintptr_t) rm_desc, ValueString);

   // Backup serialization
   get_record_length(record, &rec_len, &fpi_len);
   pgmoneta_json_put(record_json, "Description", (uintptr_t) backup_str, ValueString);

finish:
   pgmoneta_value_create(ValueJSON, (uintptr_t) record_json, value);
   free(rm_desc);
   free(backup_str);
}

char*
pgmoneta_wal_encode_xlog_record(struct decoded_xlog_record* decoded, uint16_t magic_value, char* buffer)
{
   uint32_t total_length = 0;
   uint8_t block_id;
   char* ptr = NULL;
   struct xlog_record record = decoded->header;

   /* Compute total length required for the buffer */
   total_length = record.xl_tot_len;

   /* Allocate buffer */
   buffer = malloc(total_length);
   if (!buffer)
   {
      /* Handle allocation failure */
      return NULL;
   }
   ptr = buffer;

   /* Write header */
   memcpy(ptr, &record, SIZE_OF_XLOG_RECORD);
   ptr += SIZE_OF_XLOG_RECORD;

   assert(ptr - buffer == SIZE_OF_XLOG_RECORD);

   /* Write record_origin */
   if (decoded->record_origin != INVALID_REP_ORIGIN_ID)
   {
      /* Write block_id */
      *ptr = (uint8_t)XLR_BLOCK_ID_ORIGIN;
      ptr += sizeof(uint8_t);

      /* Write record_origin */
      memcpy(ptr, &decoded->record_origin, sizeof(rep_origin_id));
      ptr += sizeof(rep_origin_id);
   }

   /* Write toplevel_xid */
   if (decoded->toplevel_xid != INVALID_TRANSACTION_ID)
   {
      /* Write block_id */
      *ptr = (uint8_t)XLR_BLOCK_ID_TOPLEVEL_XID;
      ptr += sizeof(uint8_t);

      /* Write toplevel_xid */
      memcpy(ptr, &decoded->toplevel_xid, sizeof(transaction_id));
      ptr += sizeof(transaction_id);
   }

   /* Write blocks */
   for (block_id = 0; block_id <= decoded->max_block_id; block_id++)
   {
      struct decoded_bkp_block* blk = &decoded->blocks[block_id];

      if (!blk->in_use)
      {
         continue;
      }

      /* Write block_id */
      memcpy(ptr, &block_id, sizeof(uint8_t));
      ptr += sizeof(uint8_t);

      /* Write fork_flags */
      memcpy(ptr, &blk->flags, sizeof(uint8_t));
      ptr += sizeof(uint8_t);

      /* Write data_len */
      uint16_t data_len = blk->data_len;
      memcpy(ptr, &data_len, sizeof(uint16_t));
      ptr += sizeof(uint16_t);

      /* Write image data if present */
      if (blk->has_image)
      {
         /* Write bimg_len */
         uint16_t bimg_len = blk->bimg_len;
         memcpy(ptr, &bimg_len, sizeof(uint16_t));
         ptr += sizeof(uint16_t);

         /* Write hole_offset */
         uint16_t hole_offset = blk->hole_offset;
         memcpy(ptr, &hole_offset, sizeof(uint16_t));
         ptr += sizeof(uint16_t);

         /* Write bimg_info */
         uint8_t bimg_info = blk->bimg_info;
         memcpy(ptr, &bimg_info, sizeof(uint8_t));
         ptr += sizeof(uint8_t);

         if (pgmoneta_wal_is_bkp_image_compressed(magic_value, blk->bimg_info))
         {
            if (blk->bimg_info & BKPIMAGE_HAS_HOLE)
            {
               uint16_t hole_length = blk->hole_length;
               memcpy(ptr, &hole_length, sizeof(uint16_t));
               ptr += sizeof(uint16_t);
            }
         }
      }

      /* Write rlocator if not SAME_REL */
      if (!(blk->flags & BKPBLOCK_SAME_REL))
      {
         memcpy(ptr, &blk->rlocator, sizeof(struct rel_file_locator));
         ptr += sizeof(struct rel_file_locator);

      }

      /* Write blkno */
      memcpy(ptr, &blk->blkno, sizeof(block_number));
      ptr += sizeof(block_number);
   }

   if (decoded->main_data_len > 0)
   {
      if (decoded->main_data_len <= UINT8_MAX)
      {
         /* Write block_id */
         int block_data_short = XLR_BLOCK_ID_DATA_SHORT;
         memcpy(ptr, &block_data_short, sizeof(uint8_t));
         ptr += sizeof(uint8_t);

         /* Write main_data_len (uint8_t) */

         uint8_t main_data_len = decoded->main_data_len;
         memcpy(ptr, &main_data_len, sizeof(uint8_t));
         ptr += sizeof(uint8_t);
      }
      else
      {
         /* Write block_id */
         int block_data_long = XLR_BLOCK_ID_DATA_LONG;
         memcpy(ptr, &block_data_long, sizeof(uint8_t));
         ptr += sizeof(uint8_t);

         /* Write main_data_len (uint32_t) */
         uint32_t main_data_len = decoded->main_data_len;
         memcpy(ptr, &main_data_len, sizeof(uint32_t));
         ptr += sizeof(uint32_t);
      }
   }

   for (block_id = 0; block_id <= decoded->max_block_id; block_id++)
   {
      struct decoded_bkp_block* blk = &decoded->blocks[block_id];
      if (blk->has_data)
      {
         memcpy(ptr, blk->data, blk->data_len);
         ptr += blk->data_len;
      }
      /* Write backup image if present */
      if (blk->has_image)
      {
         memcpy(ptr, blk->bkp_image, blk->bimg_len);
         ptr += blk->bimg_len;
      }
   }

   if (decoded->main_data_len > 0)
   {
      memcpy(ptr, decoded->main_data, decoded->main_data_len);
      ptr += decoded->main_data_len;
   }

   /* Ensure we've written the correct amount of data */
   assert(ptr - buffer == total_length);

   return buffer;
}
