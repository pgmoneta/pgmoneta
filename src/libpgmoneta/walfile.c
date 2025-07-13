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

#include <aes.h>
#include <compression.h>
#include <logging.h>
#include <utils.h>
#include <walfile.h>

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
   struct xlog_page_header_data* page_header = NULL;
   struct deque_iterator* page_header_iterator = NULL;

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

   if (pgmoneta_deque_iterator_create(wf->page_headers, &page_header_iterator) || pgmoneta_deque_iterator_create(wf->records, &record_iterator))
   {
      pgmoneta_log_error("Failed to create WAL deque iterators for writing");
      error_code = PGMONETA_WAL_ERR_MEMORY;
      goto error;
   }

   if (fwrite(wf->long_phd, SIZE_OF_XLOG_LONG_PHD, 1, file) != 1)
   {
      pgmoneta_log_error("Failed to write WAL header to file: %s", path);
      pgmoneta_deque_iterator_destroy(page_header_iterator);
      pgmoneta_deque_iterator_destroy(record_iterator);
      error_code = PGMONETA_WAL_ERR_IO;
      goto error;
   }

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
   if (num_zeros > 0)
   {
      char* zeros = (char*)calloc(num_zeros, sizeof(char));
      if (zeros)
      {
         fwrite(zeros, sizeof(char), num_zeros, file);
         free(zeros);
      }
   }

   fclose(file);
   return PGMONETA_WAL_SUCCESS;

error:
   if (file)
   {
      fclose(file);
   }
   pgmoneta_deque_iterator_destroy(page_header_iterator);
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
pgmoneta_describe_walfile(char* path, enum value_type type, FILE* out, bool quiet, bool color,
                          struct deque* rms, uint64_t start_lsn, uint64_t end_lsn, struct deque* xids,
                          uint32_t limit, bool summary, char** included_objects)
{
   char* tmp_wal = NULL;
   struct walfile* wf = NULL;
   struct deque_iterator* record_iterator = NULL;
   struct decoded_xlog_record* record = NULL;
   char* decompressed_file_name = NULL;
   char* decrypted_file_name = NULL;
   char* wal_path = NULL;
   bool copy = true;

   if (!pgmoneta_is_file(path))
   {
      pgmoneta_log_fatal("WAL file at %s does not exist", path);
      goto error;
   }

   wal_path = pgmoneta_append(wal_path, path);

   // Based on the file extension, if it's an encrypted file, decrypt it in /tmp
   if (pgmoneta_is_encrypted(wal_path))
   {
      tmp_wal = pgmoneta_format_and_append(tmp_wal, "/tmp/%s", basename(wal_path));

      // Temporarily copying the encrypted WAL file, because the decrypt
      // functions delete the source file
      pgmoneta_copy_file(wal_path, tmp_wal, NULL);
      copy = false;

      pgmoneta_strip_extension(basename(wal_path), &decrypted_file_name);

      free(wal_path);
      wal_path = NULL;

      wal_path = pgmoneta_format_and_append(wal_path, "/tmp/%s", decrypted_file_name);
      free(decrypted_file_name);

      if (pgmoneta_decrypt_file(tmp_wal, wal_path))
      {
         pgmoneta_log_fatal("Failed to decrypt WAL file at %s", path);
         goto error;
      }
   }

   // Based on the file extension, if it's a compressed file, decompress it
   // in /tmp
   if (pgmoneta_is_compressed(wal_path))
   {
      free(tmp_wal);
      tmp_wal = NULL;

      tmp_wal = pgmoneta_format_and_append(tmp_wal, "/tmp/%s", basename(wal_path));

      if (copy)
      {
         // Temporarily copying the compressed WAL file, because the decompress
         // functions delete the source file
         pgmoneta_copy_file(wal_path, tmp_wal, NULL);
      }

      pgmoneta_strip_extension(basename(wal_path), &decompressed_file_name);

      free(wal_path);
      wal_path = NULL;

      wal_path = pgmoneta_format_and_append(wal_path, "/tmp/%s", decompressed_file_name);
      free(decompressed_file_name);

      if (pgmoneta_decompress(tmp_wal, wal_path))
      {
         pgmoneta_log_fatal("Failed to decompress WAL file at %s", path);
         goto error;
      }
   }

   if (pgmoneta_read_walfile(-1, wal_path, &wf))
   {
      pgmoneta_log_fatal("Failed to read WAL file at %s", path);
      goto error;
   }

   if (pgmoneta_deque_iterator_create(wf->records, &record_iterator))
   {
      pgmoneta_log_fatal("Failed to create deque iterator");
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
         record = (struct decoded_xlog_record*) record_iterator->value->data;
         if (summary)
         {
            pgmoneta_wal_record_modify_rmgr_occurance(record, start_lsn, end_lsn);
         }
         else
         {
            pgmoneta_wal_record_display(record, wf->long_phd->std.xlp_magic, type, out, quiet, color,
                                        rms, start_lsn, end_lsn, xids, limit, included_objects);
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
         record = (struct decoded_xlog_record*) record_iterator->value->data;
         if (summary)
         {
            pgmoneta_wal_record_modify_rmgr_occurance(record, start_lsn, end_lsn);
         }
         else
         {
            pgmoneta_wal_record_display(record, wf->long_phd->std.xlp_magic, type, out, quiet, color,
                                        rms, start_lsn, end_lsn, xids, limit, included_objects);
         }
      }
   }

   free(tmp_wal);
   free(wal_path);
   pgmoneta_deque_iterator_destroy(record_iterator);
   pgmoneta_destroy_walfile(wf);
   return 0;

error:

   free(tmp_wal);
   free(wal_path);
   pgmoneta_destroy_walfile(wf);
   pgmoneta_deque_iterator_destroy(record_iterator);
   return 1;
}

int
pgmoneta_describe_walfiles_in_directory(char* dir_path, enum value_type type, FILE* output, bool quiet, bool color,
                                        struct deque* rms, uint64_t start_lsn, uint64_t end_lsn, struct deque* xids,
                                        uint32_t limit, bool summary, char** included_objects)
{
   int file_count = 0;
   int free_counter = 0;
   char** files = NULL;
   char* file_path = malloc(MAX_PATH);

   if (pgmoneta_get_wal_files(dir_path, &file_count, &files))
   {
      free(file_path);
      return 1;
   }

   for (int i = 0; i < file_count; i++)
   {
      snprintf(file_path, MAX_PATH, "%s/%s", dir_path, files[i]);
      if (pgmoneta_describe_walfile(file_path, type, output, quiet, color,
                                    rms, start_lsn, end_lsn, xids, limit, summary, included_objects))
      {
         free_counter = i;
         goto error;
      }
      free(files[i]);
   }

   free(file_path);
   free(files);
   return 0;

error:
   for (int i = free_counter; i < file_count; i++)
   {
      free(files[i]);
   }
   free(file_path);
   free(files);
   return 1;
}
