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

static int describe_walfile_internal(char* path, enum value_type type, FILE* out, bool quiet, bool color,
                                     struct deque* rms, uint64_t start_lsn, uint64_t end_lsn, struct deque* xids,
                                     uint32_t limit, bool summary, char** included_objects,
                                     struct column_widths* provided_widths);

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

   fclose(file);
   return PGMONETA_WAL_SUCCESS;

error:
   if (file)
   {
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

int
pgmoneta_describe_walfile(char* path, enum value_type type, FILE* out, bool quiet, bool color,
                          struct deque* rms, uint64_t start_lsn, uint64_t end_lsn, struct deque* xids,
                          uint32_t limit, bool summary, char** included_objects)
{
   return describe_walfile_internal(path, type, out, quiet, color, rms, start_lsn, end_lsn,
                                    xids, limit, summary, included_objects, NULL);
}

/**
 * Internal helper function that describes a WAL file with optional pre-calculated column widths.
 * If provided_widths is non-NULL, it will be used instead of calculating new widths.
 */
static int
describe_walfile_internal(char* path, enum value_type type, FILE* out, bool quiet, bool color,
                          struct deque* rms, uint64_t start_lsn, uint64_t end_lsn, struct deque* xids,
                          uint32_t limit, bool summary, char** included_objects,
                          struct column_widths* provided_widths)
{
   struct walfile* wf = NULL;
   struct deque_iterator* record_iterator = NULL;
   struct decoded_xlog_record* record = NULL;
   char* from = NULL;
   char* to = NULL;
   struct column_widths local_widths = {0};
   struct column_widths* widths = provided_widths ? provided_widths : &local_widths;

   if (!pgmoneta_is_file(path))
   {
      pgmoneta_log_error("WAL file at %s does not exist", path);
      goto error;
   }

   from = pgmoneta_append(from, path);
   to = pgmoneta_append(to, "/tmp/");
   to = pgmoneta_append(to, basename(path));

   if (pgmoneta_copy_and_extract_file(from, &to))
   {
      pgmoneta_log_error("Failed to extract WAL file from %s to %s", from, to);
      goto error;
   }

   if (pgmoneta_read_walfile(-1, to, &wf))
   {
      pgmoneta_log_error("Failed to read WAL file at %s", path);
      goto error;
   }

   if (type == ValueString && !summary && !provided_widths)
   {
      pgmoneta_calculate_column_widths(wf, start_lsn, end_lsn, rms, xids, included_objects, widths);
   }

   if (pgmoneta_deque_iterator_create(wf->records, &record_iterator))
   {
      pgmoneta_log_error("Failed to create deque iterator");
      goto error;
   }

   if (type == ValueJSON)
   {
      if (!quiet && !summary)
      {
         fprintf(out, "{ \"WAL\": [\n");
      }

      while (pgmoneta_deque_iterator_next(record_iterator))
      {
         record = (struct decoded_xlog_record*)record_iterator->value->data;
         if (summary)
         {
            pgmoneta_wal_record_collect_stats(record, start_lsn, end_lsn);
         }
         else
         {
            pgmoneta_wal_record_display(record, wf->long_phd->std.xlp_magic, type, out, quiet, color,
                                        rms, start_lsn, end_lsn, xids, limit, included_objects, widths);
         }
      }

      if (!quiet && !summary)
      {
         fprintf(out, "\n]}");
      }
   }
   else
   {
      while (pgmoneta_deque_iterator_next(record_iterator))
      {
         record = (struct decoded_xlog_record*)record_iterator->value->data;
         if (summary)
         {
            pgmoneta_wal_record_collect_stats(record, start_lsn, end_lsn);
         }
         else
         {
            pgmoneta_wal_record_display(record, wf->long_phd->std.xlp_magic, type, out, quiet, color,
                                        rms, start_lsn, end_lsn, xids, limit, included_objects, widths);
         }
      }
   }

   free(from);
   pgmoneta_deque_iterator_destroy(record_iterator);
   pgmoneta_destroy_walfile(wf);

   if (to != NULL)
   {
      pgmoneta_delete_file(to, NULL);
      free(to);
   }

   return 0;

error:
   free(from);
   pgmoneta_destroy_walfile(wf);
   pgmoneta_deque_iterator_destroy(record_iterator);

   if (to != NULL)
   {
      pgmoneta_delete_file(to, NULL);
      free(to);
   }

   return 1;
}

int
pgmoneta_describe_walfiles_in_directory(char* dir_path, enum value_type type, FILE* output, bool quiet, bool color,
                                        struct deque* rms, uint64_t start_lsn, uint64_t end_lsn, struct deque* xids,
                                        uint32_t limit, bool summary, char** included_objects)
{
   struct deque* files = NULL;
   struct deque_iterator* file_iterator = NULL;
   char* file_path = malloc(MAX_PATH);
   struct column_widths widths = {0};
   struct walfile* wf = NULL;
   char* from = NULL;
   char* to = NULL;

   if (pgmoneta_get_wal_files(dir_path, &files))
   {
      free(file_path);
      return 1;
   }

   if (type == ValueString && !summary)
   {
      pgmoneta_deque_iterator_create(files, &file_iterator);
      while (pgmoneta_deque_iterator_next(file_iterator))
      {
         snprintf(file_path, MAX_PATH, "%s/%s", dir_path, (char*)file_iterator->value->data);

         if (!pgmoneta_is_file(file_path))
         {
            continue;
         }

         from = pgmoneta_append(from, file_path);
         to = pgmoneta_append(to, "/tmp/");
         to = pgmoneta_append(to, basename(file_path));

         if (pgmoneta_copy_and_extract_file(from, &to))
         {
            free(from);
            free(to);
            from = NULL;
            to = NULL;
            continue;
         }

         if (pgmoneta_read_walfile(-1, to, &wf) == 0)
         {
            pgmoneta_calculate_column_widths(wf, start_lsn, end_lsn, rms, xids, included_objects, &widths);
            pgmoneta_destroy_walfile(wf);
            wf = NULL;
         }

         if (to != NULL)
         {
            pgmoneta_delete_file(to, NULL);
            free(to);
            to = NULL;
         }
         free(from);
         from = NULL;
      }
      pgmoneta_deque_iterator_destroy(file_iterator);
      file_iterator = NULL;
   }

   pgmoneta_deque_iterator_create(files, &file_iterator);
   while (pgmoneta_deque_iterator_next(file_iterator))
   {
      snprintf(file_path, MAX_PATH, "%s/%s", dir_path, (char*)file_iterator->value->data);

      struct column_widths* widths_to_use = (type == ValueString && !summary) ? &widths : NULL;
      if (describe_walfile_internal(file_path, type, output, quiet, color,
                                    rms, start_lsn, end_lsn, xids, limit, summary, included_objects, widths_to_use))
      {
         goto error;
      }
   }
   pgmoneta_deque_iterator_destroy(file_iterator);
   file_iterator = NULL;

   pgmoneta_deque_destroy(files);
   free(file_path);
   return 0;

error:
   pgmoneta_deque_destroy(files);
   pgmoneta_deque_iterator_destroy(file_iterator);

   free(file_path);
   free(from);
   if (to != NULL)
   {
      pgmoneta_delete_file(to, NULL);
      free(to);
   }
   pgmoneta_destroy_walfile(wf);
   return 1;
}

int
pgmoneta_summarize_walfile(char* path, uint64_t start_lsn, uint64_t end_lsn, block_ref_table* brt)
{
   struct walfile* wf = NULL;
   struct deque_iterator* record_iterator = NULL;
   struct decoded_xlog_record* record = NULL;
   char* from = NULL;
   char* to = NULL;

   from = pgmoneta_append(from, path);
   /* Extract the wal file in /tmp/ */
   to = pgmoneta_append(to, "/tmp/");
   to = pgmoneta_append(to, basename(path));

   if (pgmoneta_copy_and_extract_file(from, &to))
   {
      pgmoneta_log_error("Failed to extract WAL file from %s to %s", from, to);
      goto error;
   }

   /* Read and Parse the WAL records of this WAL file */
   if (pgmoneta_read_walfile(-1, to, &wf))
   {
      pgmoneta_log_error("Failed to read WAL file at %s", path);
      goto error;
   }

   if (pgmoneta_deque_iterator_create(wf->records, &record_iterator))
   {
      pgmoneta_log_error("Failed to create deque iterator");
      goto error;
   }

   /* Iterate each record */
   while (pgmoneta_deque_iterator_next(record_iterator))
   {
      record = (struct decoded_xlog_record*)record_iterator->value->data;
      if (pgmoneta_wal_record_summary(record, start_lsn, end_lsn, brt))
      {
         pgmoneta_log_error("Failed to summarize the WAL record at %s", pgmoneta_lsn_to_string(record->lsn));
         goto error;
      }
   }

   free(from);
   pgmoneta_deque_iterator_destroy(record_iterator);
   pgmoneta_destroy_walfile(wf);

   if (to != NULL)
   {
      pgmoneta_delete_file(to, NULL);
      free(to);
   }

   return 0;

error:
   free(from);
   pgmoneta_deque_iterator_destroy(record_iterator);
   pgmoneta_destroy_walfile(wf);

   if (to != NULL)
   {
      pgmoneta_delete_file(to, NULL);
      free(to);
   }

   return 1;
}

int
pgmoneta_summarize_walfiles(char* dir_path, uint64_t start_lsn, uint64_t end_lsn, block_ref_table* brt)
{
   struct deque* files = NULL;
   struct deque_iterator* file_iterator = NULL;
   char* file_path = malloc(MAX_PATH);

   if (pgmoneta_get_wal_files(dir_path, &files))
   {
      goto error;
   }

   pgmoneta_deque_iterator_create(files, &file_iterator);
   while (pgmoneta_deque_iterator_next(file_iterator))
   {
      char* file = (char*)file_iterator->value->data;

      if (!pgmoneta_ends_with(dir_path, "/"))
      {
         snprintf(file_path, MAX_PATH, "%s/%s", dir_path, file);
      }
      else
      {
         snprintf(file_path, MAX_PATH, "%s%s", dir_path, file);
      }

      if (!pgmoneta_is_file(file_path))
      {
         pgmoneta_log_error("WAL file at %s does not exist", file_path);
         goto error;
      }

      if (pgmoneta_summarize_walfile(file_path, start_lsn, end_lsn, brt))
      {
         goto error;
      }
   }
   pgmoneta_deque_iterator_destroy(file_iterator);
   file_iterator = NULL;

   free(file_path);
   pgmoneta_deque_destroy(files);
   return 0;

error:
   free(file_path);
   pgmoneta_deque_destroy(files);
   pgmoneta_deque_iterator_destroy(file_iterator);
   return 1;
}
