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

#include <brt.h>
#include <logging.h>
#include <utils.h>
#include <walfile.h>
#include <walfile/wal_reader.h>

#include <dirent.h>
#include <libgen.h>

struct partial_xlog_record* partial_record = NULL;

/**
 * Validate if a WAL file exists and is accessible before processing.
 * Returns PGMONETA_WAL_SUCCESS if valid, otherwise an error code.
 */
static int
validate_wal_file(char* path)
{
   int error_code = PGMONETA_WAL_SUCCESS;
   FILE* file = NULL;

   if (!pgmoneta_is_file(path))
   {
      pgmoneta_log_error("WAL file does not exist: %s", path);
      error_code = PGMONETA_WAL_ERR_IO;
      goto error;
   }

   file = fopen(path, "rb");
   if (!file)
   {
      pgmoneta_log_error("Failed to open WAL file: %s", path);
      error_code = PGMONETA_WAL_ERR_IO;
      goto error;
   }

   fflush(file);
   fclose(file);
   return PGMONETA_WAL_SUCCESS;

error:
   if (file)
   {
      fflush(file);
      fclose(file);
   }
   return error_code;
}

int
pgmoneta_read_walfile(int server, char* path, struct walfile** wf)
{
   int error_code = PGMONETA_WAL_SUCCESS;
   struct walfile* new_wf = NULL;
   int validation_status = validate_wal_file(path);
   if (validation_status != PGMONETA_WAL_SUCCESS)
   {
      return validation_status;
   }

   if (!pgmoneta_is_file(path))
   {
      pgmoneta_log_error("WAL file does not exist: %s", path);
      error_code = PGMONETA_WAL_ERR_IO;
      goto error;
   }

   new_wf = malloc(sizeof(struct walfile));
   if (!new_wf)
   {
      pgmoneta_log_error("Memory allocation failed for WAL file structure");
      error_code = PGMONETA_WAL_ERR_MEMORY;
      goto error;
   }

   if (pgmoneta_deque_create(false, &new_wf->records) || pgmoneta_deque_create(false, &new_wf->page_headers))
   {
      pgmoneta_log_error("Failed to initialize WAL deque structures");
      error_code = PGMONETA_WAL_ERR_MEMORY;
      goto error;
   }

   if (pgmoneta_wal_parse_wal_file(path, server, new_wf))
   {
      pgmoneta_log_error("Failed to parse WAL file: %s", path);
      error_code = PGMONETA_WAL_ERR_FORMAT;
      goto error;
   }

   *wf = new_wf;

   return PGMONETA_WAL_SUCCESS;

error:
   if (new_wf)
   {
      pgmoneta_destroy_walfile(new_wf);
   }
   return error_code;
}

int
pgmoneta_write_walfile(struct walfile* wf, int server __attribute__((unused)), char* path)
{
   int error_code = PGMONETA_WAL_SUCCESS;
   FILE* file = NULL;
   struct deque_iterator* record_iterator = NULL;
   uint32_t block_size = wf->long_phd->xlp_xlog_blcksz;
   uint64_t seg_size = wf->long_phd->xlp_seg_size;
   int current_page = 0;
   size_t current_pos = SIZE_OF_XLOG_LONG_PHD; /* Position in current page */
   size_t file_pos = current_pos;              /* Absolute file position */
   char* encoded_record = NULL;
   struct decoded_xlog_record* record = NULL;
   uint32_t written = 0;
   uint32_t total_length = 0;
   size_t space_left = 0;
   size_t to_write = 0;
   size_t current_size = 0;

   if (!wf || !path)
   {
      pgmoneta_log_error("Invalid parameters provided to pgmoneta_write_walfile");
      return PGMONETA_WAL_ERR_PARAM;
   }

   file = fopen(path, "wb");
   if (!file)
   {
      pgmoneta_log_error("Unable to open WAL file for writing: %s", path);
      error_code = PGMONETA_WAL_ERR_IO;
      goto error;
   }

   if (pgmoneta_deque_iterator_create(wf->records, &record_iterator))
   {
      pgmoneta_log_error("Failed to create WAL record iterator");
      error_code = PGMONETA_WAL_ERR_MEMORY;
      goto error;
   }

   if (fwrite(wf->long_phd, SIZE_OF_XLOG_LONG_PHD, 1, file) != 1)
   {
      pgmoneta_log_error("Failed to write WAL header to file: %s", path);
      error_code = PGMONETA_WAL_ERR_IO;
      goto error;
   }

   /* Iterate through all records */
   while (pgmoneta_deque_iterator_next(record_iterator))
   {
      record = (struct decoded_xlog_record*)record_iterator->value->data;

      if (encoded_record)
      {
         free(encoded_record);
         encoded_record = NULL;
      }

      /* Encode the record */
      encoded_record = pgmoneta_wal_encode_xlog_record(record, wf->long_phd->std.xlp_magic, encoded_record);
      if (!encoded_record)
      {
         pgmoneta_log_error("Failed to encode WAL record");
         error_code = PGMONETA_WAL_ERR_FORMAT;
         goto error;
      }

      total_length = record->header.xl_tot_len;
      written = 0;

      while (written < total_length)
      {
         /* Check if we need to start a new page */
         if (current_pos >= block_size)
         {
            current_page++;
            current_pos = 0;
            file_pos = current_page * block_size;
            fseek(file, file_pos, SEEK_SET);
         }

         /* Write short header if we're at the start of a new page (not page 0) */
         if (current_page > 0 && current_pos == 0)
         {
            struct xlog_page_header_data short_header;
            short_header.xlp_magic = wf->long_phd->std.xlp_magic;
            short_header.xlp_info = (written == 0) ? 0 : XLP_FIRST_IS_CONTRECORD;
            short_header.xlp_tli = wf->long_phd->std.xlp_tli;
            short_header.xlp_pageaddr = wf->long_phd->std.xlp_pageaddr + (current_page * block_size);
            short_header.xlp_rem_len = total_length - written;

            if (fwrite(&short_header, SIZE_OF_XLOG_SHORT_PHD, 1, file) != 1)
            {
               pgmoneta_log_error("Failed to write page header");
               error_code = PGMONETA_WAL_ERR_IO;
               goto error;
            }

            current_pos = SIZE_OF_XLOG_SHORT_PHD;
            file_pos += SIZE_OF_XLOG_SHORT_PHD;
         }

         /* Calculate space left in current page */
         space_left = block_size - current_pos;
         if (space_left == 0)
         {
            continue; /* Page is full, go to next */
         }

         /* Calculate how much to write in this chunk */
         to_write = (total_length - written) < space_left ? (total_length - written) : space_left;

         /* Write record data */
         if (fwrite(encoded_record + written, 1, to_write, file) != to_write)
         {
            pgmoneta_log_error("Failed to write WAL record data");
            error_code = PGMONETA_WAL_ERR_IO;
            goto error;
         }

         written += to_write;
         current_pos += to_write;
         file_pos += to_write;
      }

      /* Add padding for alignment after record */
      if (current_pos % MAXIMUM_ALIGNOF != 0)
      {
         size_t padding = MAXIMUM_ALIGNOF - (current_pos % MAXIMUM_ALIGNOF);
         if (padding > 0)
         {
            char zero_padding[8] = {0};
            if (fwrite(zero_padding, 1, padding, file) != padding)
            {
               pgmoneta_log_error("Failed to write padding after WAL record (page %d, position %zu, padding %zu bytes) to file: %s",
                                  current_page, current_pos, padding, path);
               error_code = PGMONETA_WAL_ERR_IO;
               goto error;
            }
            current_pos += padding;
            file_pos += padding;
         }
      }
   }

   /* Fill remaining segment with zeros */
   current_size = file_pos;
   if (current_size < seg_size)
   {
      size_t zero_bytes = seg_size - current_size;
      char* zeros = calloc(1, zero_bytes);
      if (!zeros)
      {
         pgmoneta_log_error("Failed to allocate zero buffer");
         error_code = PGMONETA_WAL_ERR_MEMORY;
         goto error;
      }

      if (fwrite(zeros, 1, zero_bytes, file) != zero_bytes)
      {
         free(zeros);
         pgmoneta_log_error("Failed to write zero padding");
         error_code = PGMONETA_WAL_ERR_IO;
         goto error;
      }
      free(zeros);
   }

   pgmoneta_deque_iterator_destroy(record_iterator);
   fclose(file);
   free(encoded_record);
   return PGMONETA_WAL_SUCCESS;

error:
   if (file)
   {
      fclose(file);
   }
   free(encoded_record);
   pgmoneta_deque_iterator_destroy(record_iterator);
   return error_code;
}

void
pgmoneta_destroy_walfile(struct walfile* wf)
{
   struct deque_iterator* record_iterator = NULL;
   struct deque_iterator* page_header_iterator = NULL;

   if (wf == NULL)
   {
      return;
   }

   if (pgmoneta_deque_iterator_create(wf->records, &record_iterator) || pgmoneta_deque_iterator_create(wf->page_headers, &page_header_iterator))
   {
      return;
   }

   while (pgmoneta_deque_iterator_next(record_iterator))
   {
      struct decoded_xlog_record* record = (struct decoded_xlog_record*)record_iterator->value->data;
      if (record->partial)
      {
         free(record);
         continue;
      }
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
      struct xlog_page_header_data* page_header = (struct xlog_page_header_data*)page_header_iterator->value->data;
      free(page_header);
   }
   pgmoneta_deque_iterator_destroy(page_header_iterator);
   pgmoneta_deque_destroy(wf->page_headers);

   free(wf->long_phd);
   free(wf);
}
