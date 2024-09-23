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

#include <deque.h>
#include <logging.h>
#include <walfile.h>

#include <assert.h>
#include <stdlib.h>

static char*
pgmoneta_wal_encode_xlog_record(struct decoded_xlog_record* decoded, struct server* server_info, char* buffer);

int
pgmoneta_read_walfile(int server, char* path, struct walfile** wf)
{
   struct walfile* new_wf = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   new_wf = malloc(sizeof(struct walfile));
   if (new_wf == NULL)
   {
      goto error;
   }

   if (pgmoneta_deque_create(false, &new_wf->records) || pgmoneta_deque_create(false, &new_wf->page_headers))
   {
      goto error;
   }

   if (pgmoneta_wal_parse_wal_file(path, &config->servers[server], new_wf))
   {
      goto error;
   }

   *wf = new_wf;

   return 0;

error:
   return 1;
}

int
pgmoneta_write_walfile(struct walfile* wf, int server, char* path)
{
   struct deque_iterator* record_iterator = NULL;
   struct xlog_page_header_data* page_header = NULL;
   struct deque_iterator* page_header_iterator = NULL;
   FILE* file = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   file = fopen(path, "wb");
   if (file == NULL)
   {
      goto error;
   }

   if (pgmoneta_deque_iterator_create(wf->page_headers, &page_header_iterator) || pgmoneta_deque_iterator_create(wf->records, &record_iterator))
   {
      goto error;
   }

   fwrite(wf->long_phd, SIZE_OF_XLOG_LONG_PHD, 1, file);

   int page_number = 1;
   while (pgmoneta_deque_iterator_next(page_header_iterator))
   {
      page_header = (struct xlog_page_header_data*) page_header_iterator->value->data;
      fseek(file, page_number * wf->long_phd->xlp_xlog_blcksz, SEEK_SET);
      fwrite(page_header, sizeof(struct xlog_page_header_data), 1, file);
      page_number++;
   }

   pgmoneta_deque_iterator_destroy(page_header_iterator);

   page_number = 0;
   fseek(file, SIZE_OF_XLOG_LONG_PHD, SEEK_SET);

   while (pgmoneta_deque_iterator_next(record_iterator))
   {
      char* encoded_record = NULL;
      struct decoded_xlog_record* record = (struct decoded_xlog_record*) record_iterator->value->data;
      encoded_record = pgmoneta_wal_encode_xlog_record(record, &config->servers[server], encoded_record);

      uint32_t total_length = record->header.xl_tot_len;
      uint32_t written = 0;
      do
      {
         uint32_t remaining_space_in_current_page = (page_number + 1) * wf->long_phd->xlp_xlog_blcksz - ftell(file);

         if (remaining_space_in_current_page > 0)
         {
            written += fwrite(encoded_record + written, 1, MIN(remaining_space_in_current_page, total_length - written), file);
         }
         if (written != total_length)
         {
            page_number++;
            fseek(file, page_number * wf->long_phd->xlp_xlog_blcksz + SIZE_OF_XLOG_SHORT_PHD, SEEK_SET);
         }
         else
         {
            /* Add padding until MAXALIGN */
            int padding = MAXALIGN(ftell(file)) - ftell(file);
            if (padding > 0)
            {
               char zero_padding[padding];
               memset(zero_padding, 0, padding);
               fwrite(zero_padding, 1, padding, file);
            }
         }
      }
      while (written != total_length);
      free(encoded_record);
   }

   pgmoneta_deque_iterator_destroy(record_iterator);

   pgmoneta_log_trace("Done writing\n");
   pgmoneta_log_trace("total pages: %d\n", page_number);
   pgmoneta_log_trace("total records: %d\n", pgmoneta_deque_size(wf->records));
   pgmoneta_log_trace("total page headers: %d\n", pgmoneta_deque_size(wf->page_headers) + 1);
   pgmoneta_log_trace("xlp seg size: %d\n", wf->long_phd->xlp_seg_size);
   pgmoneta_log_trace("xlp xlog blcksz: %d\n", wf->long_phd->xlp_xlog_blcksz);

   /* Fill the rest of the file until xlp_seg_size with zeros */
   long num_zeros = wf->long_phd->xlp_seg_size - ftell(file);
   char* zeros = (char*)calloc(num_zeros, sizeof(char));
   fwrite(zeros, sizeof(char), num_zeros, file);
   free(zeros);

   fclose(file);
   return 0;

error:
   return 1;
}

static char*
pgmoneta_wal_encode_xlog_record(struct decoded_xlog_record* decoded, struct server* server_info, char* buffer)
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

         if (pgmoneta_wal_is_bkp_image_compressed(server_info, blk->bimg_info))
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

void
pgmoneta_destroy_walfile(struct walfile* wf)
{
   struct deque_iterator* record_iterator = NULL;
   struct deque_iterator* page_header_iterator = NULL;

   if (pgmoneta_deque_iterator_create(wf->records, &record_iterator) || pgmoneta_deque_iterator_create(wf->page_headers, &page_header_iterator))
   {
      return;
   }

   while (pgmoneta_deque_iterator_next(record_iterator))
   {
      struct decoded_xlog_record* record = (struct decoded_xlog_record*) record_iterator->value->data;
      if (record->main_data != NULL)
      {
         free(record->main_data);
      }
      for (int i = 0; i <= record->max_block_id; i++)
      {
         if (record->blocks[i].has_data)
         {
            free(record->blocks[i].data);
         }
         if (record->blocks[i].has_image)
         {
            free(record->blocks[i].bkp_image);
         }
      }
      free(record);
   }
   pgmoneta_deque_iterator_destroy(record_iterator);
   pgmoneta_deque_destroy(wf->records);

   while (pgmoneta_deque_iterator_next(page_header_iterator))
   {
      struct xlog_page_header_data* page_header = (struct xlog_page_header_data*) page_header_iterator->value->data;
      free(page_header);
   }
   pgmoneta_deque_iterator_destroy(page_header_iterator);
   pgmoneta_deque_destroy(wf->page_headers);

   free(wf->long_phd);
   free(wf);
}