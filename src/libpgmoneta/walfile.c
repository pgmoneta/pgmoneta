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

#include <compression.h>
#include <deque.h>
#include <json.h>
#include <logging.h>
#include <utils.h>
#include <walfile.h>
#include <walfile/wal_reader.h>

#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>

int
pgmoneta_read_walfile(int server, char* path, struct walfile** wf)
{
   struct walfile* new_wf = NULL;

   new_wf = malloc(sizeof(struct walfile));
   if (new_wf == NULL)
   {
      goto error;
   }

   if (pgmoneta_deque_create(false, &new_wf->records) || pgmoneta_deque_create(false, &new_wf->page_headers))
   {
      goto error;
   }

   if (pgmoneta_wal_parse_wal_file(path, server, new_wf))
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
      encoded_record = pgmoneta_wal_encode_xlog_record(record, wf->long_phd->std.xlp_magic, encoded_record);

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
      struct decoded_xlog_record* record = (struct decoded_xlog_record*) record_iterator->value->data;
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
      struct xlog_page_header_data* page_header = (struct xlog_page_header_data*) page_header_iterator->value->data;
      free(page_header);
   }
   pgmoneta_deque_iterator_destroy(page_header_iterator);
   pgmoneta_deque_destroy(wf->page_headers);

   free(wf->long_phd);
   free(wf);
}

int
pgmoneta_describe_walfile(char* path, enum value_type type, char* output, bool quiet, bool color)
{
   FILE* out = NULL;
   char* decompressed_wal_path = NULL;
   char* tmp_compressed_wal = NULL;
   struct walfile* wf = NULL;
   struct deque_iterator* record_iterator = NULL;
   struct decoded_xlog_record* record = NULL;

   if (!pgmoneta_is_file(path))
   {
      pgmoneta_log_fatal("WAL file at %s does not exist", path);
      goto error;
   }

   // Based on the file extension, if it's a compressed file, decompress it in /tmp
   if (pgmoneta_is_file_archive(path))
   {
      tmp_compressed_wal = pgmoneta_format_and_append(tmp_compressed_wal, "/tmp/%s", basename(path));
      // Temporarily copying the compressed WAL file, because the decompress functions delete the source file
      pgmoneta_copy_file(path, tmp_compressed_wal, NULL);

      decompressed_wal_path = "/tmp/decompressed_wal";
      if (pgmoneta_decompress(tmp_compressed_wal, decompressed_wal_path))
      {
         pgmoneta_log_fatal("Failed to decompress WAL file at %s", path);
         goto error;
      }
   }
   else
   {
      decompressed_wal_path = path;
   }

   if (pgmoneta_read_walfile(-1, decompressed_wal_path, &wf))
   {
      pgmoneta_log_fatal("Failed to read WAL file at %s", path);
      goto error;
   }
   if (pgmoneta_deque_iterator_create(wf->records, &record_iterator))
   {
      pgmoneta_log_fatal("Failed to create deque iterator");
      goto error;
   }

   if (output == NULL)
   {
      out = stdout;
   }
   else
   {
      out = fopen(output, "w");
      color = false;
   }

   if (type == ValueJSON)
   {
      int count = 0;

      if (!quiet)
      {
         fprintf(out, "{ \"WAL\": [\n");
      }

      while (pgmoneta_deque_iterator_next(record_iterator))
      {
         if (!quiet)
         {
            fprintf(out, "{\"Record\": ");
         }
         record = (struct decoded_xlog_record*) record_iterator->value->data;
         pgmoneta_wal_record_display(record, wf->long_phd->std.xlp_magic, type, out, quiet, color);

         if (!quiet)
         {
            fprintf(out, "}");
            if (++count < pgmoneta_deque_size(wf->records))
            {
               fprintf(out, ",\n");
            }
         }
      }

      if (!quiet)
      {
         fprintf(out, "]\n}");
      }
   }
   else
   {
      while (pgmoneta_deque_iterator_next(record_iterator))
      {
         record = (struct decoded_xlog_record*) record_iterator->value->data;
         pgmoneta_wal_record_display(record, wf->long_phd->std.xlp_magic, type, out, quiet, color);
      }
   }

   if (output != NULL)
   {
      fflush(out);
      fclose(out);
   }

   free(tmp_compressed_wal);
   pgmoneta_deque_iterator_destroy(record_iterator);
   pgmoneta_destroy_walfile(wf);
   return 0;

error:

   if (output != NULL)
   {
      fflush(out);
      fclose(out);
   }

   free(tmp_compressed_wal);
   pgmoneta_destroy_walfile(wf);
   pgmoneta_deque_iterator_destroy(record_iterator);
   return 1;
}
