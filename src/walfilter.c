/*
 * Copyright (C) 2025 The pgexporter community
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
#include <aes.h>
#include <cmd.h>
#include <compression.h>
#include <configuration.h>
#include <info.h>
#include <logging.h>
#include <pgmoneta.h>
#include <shmem.h>
#include <utils.h>
#include <walfile.h>
#include <walfile/pg_control.h>
#include <walfile/rm_heap.h>
#include <walfile/rmgr.h>
#include <walfile/wal_reader.h>
#include <yaml_utils.h>

/* system */
#include <dirent.h>
#include <unistd.h>
#include <err.h>
#include <libgen.h>
#include <stdio.h>

#define OPERATION_DELETE "DELETE"

int pgmoneta_init_crc32c(uint32_t* crc);
int pgmoneta_create_crc32c_buffer(void* buffer, size_t size, uint32_t* crc);
int pgmoneta_finalize_crc32c(uint32_t* crc);
static int pgmoneta_recalculate_record_crc(struct decoded_xlog_record* record, uint16_t magic);

static void
usage(void)
{
   printf("pgmoneta-walfilter %s\n", VERSION);
   printf("  Command line utility to filter PostgreSQL Write-Ahead Log (WAL) files based on user-defined rules\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgmoneta-walfilter <yaml_config_file> [OPTIONS]\n");
   printf("\n");
   printf("Options:\n");
   printf("  -c, --config CONFIG_PATH  Override configuration file path from YAML\n");
   printf("\n");
   printf("pgmoneta: %s\n", PGMONETA_HOMEPAGE);
   printf("Report bugs: %s\n", PGMONETA_ISSUES);
}

/**
 * Process and maintain WAL files integrity after filtering
 *
 * @param walfile_count Number of walfiles
 * @param walfiles Array of walfile pointers
 */
void
pgmoneta_process_walfiles(int walfile_count, struct walfile** walfiles)
{
   if (walfiles == NULL || walfile_count <= 0)
   {
      pgmoneta_log_error("No WAL files to process for CRC recalculation\n");
      return;
   }

   pgmoneta_log_debug("Processing %d WAL files for CRC recalculation", walfile_count);

   for (int i = 0; i < walfile_count; i++)
   {
      struct walfile* wf = walfiles[i];

      if (wf == NULL)
      {
         pgmoneta_log_error("WAL file #%d is NULL, skipping", i);
         continue;
      }

      if (wf->records == NULL)
      {
         pgmoneta_log_error("WAL file #%d has no records, skipping", i);
         continue;
      }

      pgmoneta_log_debug("Processing WAL file #%d...", i);

      struct deque_iterator* iter = NULL;
      if (pgmoneta_deque_iterator_create(wf->records, &iter))
      {
         pgmoneta_log_error("Failed to create iterator for WAL file records");
         return;
      }

      while (pgmoneta_deque_iterator_next(iter))
      {
         struct decoded_xlog_record* record = (struct decoded_xlog_record*)iter->value->data;

         if (record == NULL)
         {
            continue;
         }

         // Check if this record was modified (converted to NOOP)
         if (record->header.xl_rmid == RM_XLOG_ID &&
             (record->header.xl_info & ~XLR_INFO_MASK) == XLOG_NOOP)
         {
            if (!pgmoneta_recalculate_record_crc(record, wf->long_phd->std.xlp_magic))
            {
               xlog_rec_ptr prev_lsn = record->lsn;
               if (pgmoneta_deque_iterator_next(iter))
               {
                  record = (struct decoded_xlog_record*)iter->value->data;
                  record->header.xl_prev = prev_lsn;

                  if (pgmoneta_recalculate_record_crc(record, wf->long_phd->std.xlp_magic))
                  {
                     pgmoneta_log_error("Failed to recalculate CRC for record (with updated xl_prev) at LSN %X/%X", LSN_FORMAT_ARGS(record->lsn));
                  }
               }
            }
            else
            {
               pgmoneta_log_error("Failed to recalculate CRC for NOOP record at LSN %X/%X", LSN_FORMAT_ARGS(record->lsn));
            }
         }
      }

      pgmoneta_deque_iterator_destroy(iter);
   }
   pgmoneta_log_debug("WAL files processing completed");
}

/*
 * Recalculate record CRC using the encoded bytes from pgmoneta_wal_encode_xlog_record.
 * Returns 0 on success, 1 on error.
 */
static int
pgmoneta_recalculate_record_crc(struct decoded_xlog_record* record, uint16_t magic)
{
   char* encoded_record = NULL;

   if (record == NULL)
   {
      goto error;
   }

   uint32_t crc = 0;

   if (pgmoneta_init_crc32c(&crc))
   {
      pgmoneta_log_error("Failed to initialize CRC32C");
      goto error;
   }

   /* Ensure header.xl_crc is zero before encoding & CRCing */
   record->header.xl_crc = 0;

   encoded_record = pgmoneta_wal_encode_xlog_record(record, magic, NULL);

   if (encoded_record == NULL)
   {
      pgmoneta_log_error("Failed to encode WAL record for CRC calculation");
      goto error;
   }

   /* CRC over payload (everything after header) */
   if (pgmoneta_create_crc32c_buffer(encoded_record + SIZE_OF_XLOG_RECORD,
                                     record->header.xl_tot_len - SIZE_OF_XLOG_RECORD,
                                     &crc))
   {
      pgmoneta_log_error("Failed to calculate CRC for record data");
      goto error;
   }

   /* CRC over header bytes up to xl_crc (exclude xl_crc) */
   if (pgmoneta_create_crc32c_buffer(encoded_record,
                                     offsetof(struct xlog_record, xl_crc),
                                     &crc))
   {
      pgmoneta_log_error("Failed to calculate CRC for record header");
      goto error;
   }

   /* Finalize */
   if (pgmoneta_finalize_crc32c(&crc))
   {
      pgmoneta_log_error("Failed to finalize CRC32C calculation");
      goto error;
   }

   /* Save computed CRC into record header */
   record->header.xl_crc = crc;

   free(encoded_record);
   encoded_record = NULL;
   return 0;

error:
   if (encoded_record)
   {
      free(encoded_record);
      encoded_record = NULL;
   }
   return 1;
}

/**
 * Filter out DELETE operations from WAL files
 *
 * @param file_count Pointer to the number of WAL files
 * @param walfiles Pointer to the array of WAL file pointers
 * @return 0 on success, non-zero on failure
 */
int
pgmoneta_filter_operation_delete(int* file_count, struct walfile*** walfiles)
{
#define XID_IS_DELETED(xid) \
        ({ int found = 0; \
           for (int k = 0; k < delete_xid_count; k++) { \
              if (delete_xids[k] == (xid)) { found = 1; break; } \
           } found; })

   transaction_id* delete_xids = NULL;
   int delete_xid_count = 0;
   int delete_xid_capacity = 16;
   int records_marked = 0;

   delete_xids = malloc(delete_xid_capacity * sizeof(transaction_id));
   if (!delete_xids)
   {
      return 1;
   }

   /* Pass 1: collect XIDs from DELETE records */
   for (int i = 0; i < *file_count; i++)
   {
      struct walfile* wf = (*walfiles)[i];
      if (wf == NULL || wf->records == NULL)
      {
         continue;
      }

      struct deque_iterator* iter = NULL;
      pgmoneta_deque_iterator_create(wf->records, &iter);

      while (pgmoneta_deque_iterator_next(iter))
      {
         struct decoded_xlog_record* rec = (struct decoded_xlog_record*)iter->value->data;
         if (rec->header.xl_rmid == RM_HEAP_ID)
         {
            uint8_t info = rec->header.xl_info & ~XLR_INFO_MASK;
            info &= XLOG_HEAP_OPMASK;
            if (info == XLOG_HEAP_DELETE)
            {
               struct xl_heap_delete* del = (struct xl_heap_delete*)rec->main_data;

               if (!del)
               {
                  continue;
               }

               int already_exists = 0;
               for (int j = 0; j < delete_xid_count; j++)
               {
                  if (delete_xids[j] == del->xmax)
                  {
                     already_exists = 1;
                     break;
                  }
               }

               if (already_exists)
               {
                  continue;
               }

               if (delete_xid_count == delete_xid_capacity)
               {
                  delete_xid_capacity *= 2;
                  transaction_id* new_delete_xids = realloc(delete_xids, delete_xid_capacity * sizeof(transaction_id));
                  if (!new_delete_xids)
                  {
                     pgmoneta_deque_iterator_destroy(iter);
                     free(delete_xids);
                     return 1;
                  }
                  delete_xids = new_delete_xids;
               }

               delete_xids[delete_xid_count++] = del->xmax;
            }
         }
      }
      pgmoneta_deque_iterator_destroy(iter);
   }

   /* Pass 2: mark matching records as NOOP */
   for (int i = 0; i < *file_count; i++)
   {
      struct walfile* wf = (*walfiles)[i];
      if (wf == NULL || wf->records == NULL)
      {
         continue;
      }

      struct deque_iterator* rec_iter = NULL;
      pgmoneta_deque_iterator_create(wf->records, &rec_iter);

      while (pgmoneta_deque_iterator_next(rec_iter))
      {
         struct decoded_xlog_record* rec = (struct decoded_xlog_record*)rec_iter->value->data;
         int match = 0;

         if (rec->header.xl_rmid == RM_HEAP_ID)
         {
            uint8_t info = rec->header.xl_info & ~XLR_INFO_MASK;
            info &= XLOG_HEAP_OPMASK;
            if (info == XLOG_HEAP_DELETE)
            {
               match = 1;
            }
         }

         if (!match)
         {
            if (XID_IS_DELETED(rec->header.xl_xid) || XID_IS_DELETED(rec->toplevel_xid))
            {
               match = 1;
            }
         }

         if (match)
         {
            /* Change to NOOP (RM_XLOG, XLOG_NOOP) */
            rec->header.xl_info = XLOG_NOOP;
            rec->header.xl_rmid = RM_XLOG_ID;

            records_marked++;
         }
      }
      pgmoneta_deque_iterator_destroy(rec_iter);
   }

   pgmoneta_log_debug("Total records marked as NOOP: %d", records_marked);
   pgmoneta_log_debug("Total XIDs collected from DELETE: %d", delete_xid_count);
   if (delete_xid_count > 0)
   {
      char* delete_xids_str = NULL;
      for (int i = 0; i < delete_xid_count; i++)
      {
         delete_xids_str = pgmoneta_format_and_append(delete_xids_str, "%u%s", delete_xids[i], (i < delete_xid_count - 1) ? ", " : "");
      }
      pgmoneta_log_debug("Collected XIDs: %s", delete_xids_str ? delete_xids_str : "");
      free(delete_xids_str);
   }

   free(delete_xids);

   return 0;
}

/**
 * Filter out records with specific XIDs from WAL files
 *
 * @param file_count Pointer to the number of WAL files
 * @param walfiles Pointer to the array of WAL file pointers
 * @param xids Array of XIDs to filter out
 * @param xid_count Number of XIDs in the array
 * @return 0 on success, non-zero on failure
 */
int
pgmoneta_filter_xids(int* file_count, struct walfile*** walfiles, int* xids, int xid_count)
{
#define XID_IS_FILTERED(xid) \
        ({ int found = 0; \
           for (int k = 0; k < xid_count; k++) { \
              if ((transaction_id) xids[k] == (xid)) { found = 1; break; } \
           } found; })

   int match = 0;
   struct deque_iterator* rec_iter = NULL;
   struct decoded_xlog_record* rec = NULL;

   if (xids == NULL || xid_count <= 0)
   {
      goto done;
   }

   /* Mark matching records as NOOP */
   for (int i = 0; i < *file_count; i++)
   {
      struct walfile* wf = (*walfiles)[i];
      if (wf == NULL || wf->records == NULL)
      {
         continue;
      }

      rec_iter = NULL;
      if (pgmoneta_deque_iterator_create(wf->records, &rec_iter))
      {
         pgmoneta_log_error("Failed to create iterator for WAL file records");
         goto error;
      }

      while (pgmoneta_deque_iterator_next(rec_iter))
      {
         rec = (struct decoded_xlog_record*)rec_iter->value->data;
         match = 0;

         /* Check if the record's XID or toplevel_xid matches any of the filtered XIDs */
         if (XID_IS_FILTERED(rec->header.xl_xid) || XID_IS_FILTERED(rec->toplevel_xid))
         {
            match = 1;
         }

         if (match)
         {
            /* Change to NOOP (RM_XLOG, XLOG_NOOP) */
            rec->header.xl_info = XLOG_NOOP;
            rec->header.xl_rmid = RM_XLOG_ID;
         }
      }
      pgmoneta_deque_iterator_destroy(rec_iter);
   }

   if (xid_count > 0)
   {
      char* filter_xids_str = NULL;
      for (int i = 0; i < xid_count; i++)
      {
         filter_xids_str = pgmoneta_format_and_append(filter_xids_str, "%u%s", xids[i], (i < xid_count - 1) ? ", " : "");
      }
      pgmoneta_log_debug("Filtered XIDs: %s", filter_xids_str ? filter_xids_str : "");
      free(filter_xids_str);
   }

done:
   return 0;

error:
   if (rec_iter)
   {
      pgmoneta_deque_iterator_destroy(rec_iter);
      rec_iter = NULL;
   }

   return 1;
}

int
main(int argc, char* argv[])
{
   config_t yaml_config;
   struct walfilter_configuration* config = NULL;
   char* logfile = NULL;
   int file_count = 0;
   char** files = NULL;
   char* file_path = NULL;
   char* wal_files_path = NULL;
   int loaded = 1;
   char* configuration_path = NULL;
   size_t size;
   char* tmp_wal = NULL;
   struct walfile* wf = NULL;
   char* decompressed_file_name = NULL;
   char* decrypted_file_name = NULL;
   char* wal_path = NULL;
   bool copy = true;
   struct walfile** walfiles = NULL;
   int walfile_count = 0;
   char* target_pg_wal_dir = NULL;
   int optind = 0;
   char* yaml_file = NULL;
   int num_results = 0;
   int num_options = 0;

   cli_option options[] = {
      {"c", "config", true},
      {"q", "quiet", false},
   };

   num_options = sizeof(options) / sizeof(options[0]);
   cli_result results[num_options];

   if (argc < 2)
   {
      usage();
      return 0;
   }

   num_results = cmd_parse(argc, argv, options, num_options, results, num_options, true, &yaml_file, &optind);

   if (num_results < 0)
   {
      pgmoneta_log_error("Error parsing command line\n");
      return 1;
   }

   for (int i = 0; i < num_results; i++)
   {
      char* optname = results[i].option_name;
      char* optarg = results[i].argument;

      if (optname == NULL)
      {
         break;
      }
      else if (!strcmp(optname, "c") || !strcmp(optname, "config"))
      {
         configuration_path = optarg;
      }
   }

   if (yaml_file == NULL)
   {
      warnx("Missing <yaml_config_file> argument");
      usage();
      return 1;
   }

   size = sizeof(struct walfilter_configuration);
   if (pgmoneta_create_shared_memory(size, HUGEPAGE_OFF, &shmem))
   {
      pgmoneta_log_fatal("Error creating shared memory");
      goto error;
   }

   if (pgmoneta_parse_yaml_config(yaml_file, &yaml_config))
   {
      pgmoneta_log_fatal("Failed to parse configuration\n");
      goto error;
   }

   if (yaml_config.source_dir == NULL || yaml_config.target_dir == NULL)
   {
      pgmoneta_log_error("Source and target directories must be specified in the configuration");
      goto error;
   }

   pgmoneta_init_walfilter_configuration(shmem);
   config = (struct walfilter_configuration*)shmem;

   /* Override configuration_path if provided via CLI, otherwise use from YAML */
   if (configuration_path == NULL)
   {
      configuration_path = yaml_config.configuration_file;
   }

   if (configuration_path != NULL)
   {
      if (pgmoneta_exists(configuration_path))
      {
         loaded = pgmoneta_read_walfilter_configuration(shmem, configuration_path);
      }

      if (loaded)
      {
         pgmoneta_log_debug("Configuration not found: %s", configuration_path);
      }
   }
   else
   {
      config->common.log_level = PGMONETA_LOGGING_LEVEL_WARN;
   }

   if (loaded && pgmoneta_exists(PGMONETA_WALFILTER_DEFAULT_CONFIG_FILE_PATH))
   {
      loaded = pgmoneta_read_walfilter_configuration(shmem, PGMONETA_WALFILTER_DEFAULT_CONFIG_FILE_PATH);
   }

   if (loaded)
   {
      config->common.log_type = PGMONETA_LOGGING_TYPE_CONSOLE;
   }
   else
   {
      if (logfile)
      {
         config->common.log_type = PGMONETA_LOGGING_TYPE_FILE;
         memset(&config->common.log_path[0], 0, MISC_LENGTH);
         memcpy(&config->common.log_path[0], logfile, MIN((size_t)(MISC_LENGTH - 1), strlen(logfile)));
      }
   }

   if (pgmoneta_validate_walfilter_configuration())
   {
      goto error;
   }

   if (pgmoneta_start_logging())
   {
      goto error;
   }

   /* Use source_dir directly as the WAL files directory */
   wal_files_path = pgmoneta_append(wal_files_path, yaml_config.source_dir);

   pgmoneta_log_debug("WAL files path: %s", wal_files_path);

   if (pgmoneta_get_wal_files(wal_files_path, &file_count, &files))
   {
      pgmoneta_log_error("Failed to get WAL files from %s\n", wal_files_path);
      goto error;
   }

   partial_record = malloc(sizeof(struct partial_xlog_record));
   if (partial_record == NULL)
   {
      pgmoneta_log_error("Failed to allocate memory for partial_record");
      goto error;
   }

   partial_record->data_buffer_bytes_read = 0;
   partial_record->xlog_record_bytes_read = 0;
   partial_record->xlog_record = NULL;
   partial_record->data_buffer = NULL;

   walfiles = malloc(file_count * sizeof(struct walfile*));
   if (walfiles == NULL)
   {
      pgmoneta_log_error("Failed to allocate memory for walfiles array");
      goto error;
   }

   for (int i = 0; i < file_count; i++)
   {
      walfiles[i] = NULL;
   }

   for (int i = 0; i < file_count; i++)
   {
      file_path = malloc(MAX_PATH);
      snprintf(file_path, MAX_PATH, "%s/%s", wal_files_path, files[i]);

      if (!pgmoneta_is_file(file_path))
      {
         pgmoneta_log_fatal("WAL file at %s does not exist", file_path);
         goto error;
      }

      free(wal_path);
      wal_path = NULL;
      wal_path = pgmoneta_append(wal_path, file_path);

      if (pgmoneta_is_encrypted(wal_path))
      {
         free(tmp_wal);
         tmp_wal = NULL;
         tmp_wal = pgmoneta_format_and_append(tmp_wal, "/tmp/%s", basename(wal_path));

         pgmoneta_copy_file(wal_path, tmp_wal, NULL);
         copy = false;

         pgmoneta_strip_extension(basename(wal_path), &decrypted_file_name);

         free(wal_path);
         wal_path = NULL;

         wal_path = pgmoneta_format_and_append(wal_path, "/tmp/%s", decrypted_file_name);
         free(decrypted_file_name);
         decrypted_file_name = NULL;

         if (pgmoneta_decrypt_file(tmp_wal, wal_path))
         {
            pgmoneta_log_fatal("Failed to decrypt WAL file at %s", file_path);
            goto error;
         }
      }

      if (pgmoneta_is_compressed(wal_path))
      {
         free(tmp_wal);
         tmp_wal = NULL;

         tmp_wal = pgmoneta_format_and_append(tmp_wal, "/tmp/%s", basename(wal_path));

         if (copy)
         {
            pgmoneta_copy_file(wal_path, tmp_wal, NULL);
         }

         pgmoneta_strip_extension(basename(wal_path), &decompressed_file_name);

         free(wal_path);
         wal_path = NULL;

         wal_path = pgmoneta_format_and_append(wal_path, "/tmp/%s", decompressed_file_name);
         free(decompressed_file_name);
         decompressed_file_name = NULL;

         if (pgmoneta_decompress(tmp_wal, wal_path))
         {
            pgmoneta_log_fatal("Failed to decompress WAL file at %s", file_path);
            goto error;
         }
      }

      if (pgmoneta_read_walfile(-1, wal_path, &wf))
      {
         pgmoneta_log_fatal("Failed to read WAL file at %s", file_path);
         goto error;
      }

      walfiles[walfile_count] = wf;
      walfile_count++;
      wf = NULL;

      free(file_path);
      file_path = NULL;
   }

   if (yaml_config.operation_count > 0)
   {
      for (int i = 0; i < yaml_config.operation_count; i++)
      {
         if (!strcmp(yaml_config.operations[i], OPERATION_DELETE))
         {
            if (pgmoneta_filter_operation_delete(&walfile_count, &walfiles))
            {
               pgmoneta_log_error("Failed to apply filter on operation %s", yaml_config.operations[i]);
               goto error;
            }
         }
      }
   }

   if (yaml_config.xid_count > 0)
   {
      if (pgmoneta_filter_xids(&walfile_count, &walfiles, yaml_config.xids, yaml_config.xid_count))
      {
         pgmoneta_log_error("Failed to apply filter on XIDs");
         goto error;
      }
   }

   pgmoneta_process_walfiles(walfile_count, walfiles);

   if (pgmoneta_exists(yaml_config.target_dir))
   {
      if (pgmoneta_delete_directory(yaml_config.target_dir))
      {
         pgmoneta_log_error("Failed to clear target data directory: %s", yaml_config.target_dir);
         goto error;
      }
   }

   if (pgmoneta_mkdir(yaml_config.target_dir))
   {
      pgmoneta_log_error("Failed to create target data directory: %s", yaml_config.target_dir);
      goto error;
   }

   target_pg_wal_dir = pgmoneta_append(target_pg_wal_dir, yaml_config.target_dir);

   if (chdir(target_pg_wal_dir))
   {
      pgmoneta_log_error("Failed to change directory to %s", target_pg_wal_dir);
      goto error;
   }

   for (int i = 0; i < walfile_count; i++)
   {
      if (pgmoneta_is_compressed(files[i]) || pgmoneta_is_encrypted(files[i]))
      {
         // Remove extension for compressed or encrypted files
         char* dot = strrchr(files[i], '.');
         if (dot != NULL)
         {
            *dot = '\0';
         }
      }

      if (pgmoneta_write_walfile(walfiles[i], -1, files[i]))
      {
         pgmoneta_log_error("Failed to write WAL file %d", i);
         goto error;
      }
      else
      {
         pgmoneta_log_debug("WAL file %d written successfully: %s", i, files[i]);
      }
   }

   pgmoneta_log_info("Filtered WAL files written successfully to %s", target_pg_wal_dir);

   if (target_pg_wal_dir != NULL)
   {
      free(target_pg_wal_dir);
      target_pg_wal_dir = NULL;
   }

   if (walfiles != NULL)
   {
      for (int i = 0; i < walfile_count; i++)
      {
         if (walfiles[i] != NULL)
         {
            pgmoneta_destroy_walfile(walfiles[i]);
            walfiles[i] = NULL;
         }
      }
      free(walfiles);
      walfiles = NULL;
   }

   free(tmp_wal);
   tmp_wal = NULL;
   free(wal_path);
   wal_path = NULL;
   if (partial_record != NULL)
   {
      if (partial_record->xlog_record != NULL)
      {
         free(partial_record->xlog_record);
         partial_record->xlog_record = NULL;
      }
      if (partial_record->data_buffer != NULL)
      {
         free(partial_record->data_buffer);
         partial_record->data_buffer = NULL;
      }
      free(partial_record);
      partial_record = NULL;
   }
   if (file_path != NULL)
   {
      free(file_path);
      file_path = NULL;
   }
   if (wal_files_path != NULL)
   {
      free(wal_files_path);
      wal_files_path = NULL;
   }
   if (files)
   {
      for (int i = 0; i < file_count; i++)
      {
         if (files[i] != NULL)
         {
            free(files[i]);
         }
      }
      free(files);
      files = NULL;
   }

   cleanup_config(&yaml_config);

   if (shmem != NULL)
   {
      pgmoneta_destroy_shared_memory(shmem, size);
   }

   return 0;

error:
   if (target_pg_wal_dir != NULL)
   {
      free(target_pg_wal_dir);
      target_pg_wal_dir = NULL;
   }

   if (walfiles != NULL)
   {
      for (int i = 0; i < walfile_count; i++)
      {
         if (walfiles[i] != NULL)
         {
            pgmoneta_destroy_walfile(walfiles[i]);
            walfiles[i] = NULL;
         }
      }
      free(walfiles);
      walfiles = NULL;
   }

   free(tmp_wal);
   tmp_wal = NULL;
   free(wal_path);
   wal_path = NULL;
   if (partial_record != NULL)
   {
      if (partial_record->xlog_record != NULL)
      {
         free(partial_record->xlog_record);
         partial_record->xlog_record = NULL;
      }
      if (partial_record->data_buffer != NULL)
      {
         free(partial_record->data_buffer);
         partial_record->data_buffer = NULL;
      }
      free(partial_record);
      partial_record = NULL;
   }
   if (file_path != NULL)
   {
      free(file_path);
      file_path = NULL;
   }
   if (wal_files_path != NULL)
   {
      free(wal_files_path);
      wal_files_path = NULL;
   }
   if (files)
   {
      for (int i = 0; i < file_count; i++)
      {
         if (files[i] != NULL)
         {
            free(files[i]);
         }
      }
      free(files);
      files = NULL;
   }

   cleanup_config(&yaml_config);
   pgmoneta_log_error("An error occurred while processing WAL files. Please check the logs for details.");

   if (shmem != NULL)
   {
      pgmoneta_destroy_shared_memory(shmem, size);
   }

   return 1;
}
