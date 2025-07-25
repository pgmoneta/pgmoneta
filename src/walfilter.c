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
#include <compression.h>
#include <configuration.h>
#include <info.h>
#include <logging.h>
#include <pgmoneta.h>
#include <shmem.h>
#include <utils.h>
#include <walfile.h>
#include <yaml_utils.h>
#include <walfile/rm_heap.h>

/* system */
#include <err.h>
#include <stdio.h>
#include <dirent.h>
#include <libgen.h>

#define OPERATION_DELETE "DELETE"

#ifndef RM_HEAP_ID
#define RM_HEAP_ID 10
#endif

static void
usage(void)
{
   printf("pgmoneta-walfilter %s\n", VERSION);
   printf("  Command line utility to read and filter Write-Ahead Log (WAL) files based on user-defined criteria\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgmoneta-walfilter <yaml_config_file>\n");
   printf("\n");
   printf("pgmoneta: %s\n", PGMONETA_HOMEPAGE);
   printf("Report bugs: %s\n", PGMONETA_ISSUES);
}

void
pgmoneta_display_walfiles(struct walfile** walfiles, int walfile_count)
{
   if (walfiles == NULL || walfile_count <= 0)
   {
      printf("No WAL files to display.\n");
      return;
   }

   printf("\n=== Displaying WAL Files Records ===\n");
   printf("Total WAL files: %d\n\n", walfile_count);

   for (int i = 0; i < walfile_count; i++)
   {
      struct walfile* wf = walfiles[i];

      if (wf == NULL)
      {
         printf("WAL file #%d: [NULL]\n\n", i);
         continue;
      }

      printf("WAL file #%d:\n", i);

      if (wf->records == NULL)
      {
         printf("No records in this WAL file.\n\n");
         continue;
      }

      uint32_t record_count = pgmoneta_deque_size(wf->records);
      printf("Total records: %u\n", record_count);
      printf("Records:\n");

      if (record_count == 0)
      {
         printf("  [No records to display]\n\n");
         continue;
      }

      struct deque_iterator* iter = NULL;
      pgmoneta_deque_iterator_create(wf->records, &iter);

      while (pgmoneta_deque_iterator_next(iter))
      {
         struct decoded_xlog_record* record = (struct decoded_xlog_record*)iter->value->data;

         if (record != NULL)
         {
            // pgmoneta_wal_record_display(
            //    record,           // The decoded WAL record
            //    wf->magic_number, // Magic value from the walfile
            //    ValueString,      // Display as string
            //    stdout,           // Output to stdout
            //    false,            // Not quiet (show details)
            //    true,             // Use colors
            //    NULL,             // No resource manager filter
            //    0,                // No start LSN filter
            //    UINT64_MAX,       // No end LSN filter
            //    NULL,             // No XID filter
            //    0,                // No limit
            //    NULL              // No included objects filter
            // );
         }
      }

      pgmoneta_deque_iterator_destroy(iter);
   }

   printf("=== End of WAL Files Display ===\n\n");
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
      return;
   }

   for (int file_idx = 0; file_idx < walfile_count; file_idx++)
   {
      struct walfile* wf = walfiles[file_idx];
      if (wf == NULL || wf->records == NULL)
      {
         continue;
      }

      struct deque_iterator* iter = NULL;
      pgmoneta_deque_iterator_create(wf->records, &iter);

      struct decoded_xlog_record* prev_record = NULL;
      struct decoded_xlog_record* first_record = NULL;
      struct decoded_xlog_record* last_record = NULL;
      int record_count = 0;

      while (pgmoneta_deque_iterator_next(iter))
      {
         struct decoded_xlog_record* rec = (struct decoded_xlog_record*)iter->value->data;
         record_count++;

         if (first_record == NULL)
         {
            first_record = rec;
         }
         last_record = rec;

         if (prev_record != NULL)
         {
            prev_record->next = rec;
            prev_record->next_lsn = rec->lsn;
            rec->header.xl_prev = prev_record->lsn;
         }
         else
         {
            rec->header.xl_prev = 0;
         }

         prev_record = rec;
      }
      pgmoneta_deque_iterator_destroy(iter);

      if (last_record != NULL)
      {
         last_record->next = NULL;
         last_record->next_lsn = 0;
      }
   }

   for (int file_idx = 0; file_idx < walfile_count; file_idx++)
   {
      struct walfile* wf = walfiles[file_idx];
      if (wf == NULL || wf->records == NULL)
      {
         continue;
      }

      struct deque_iterator* iter = NULL;
      pgmoneta_deque_iterator_create(wf->records, &iter);

      struct decoded_xlog_record* first_record = NULL;
      struct decoded_xlog_record* last_record = NULL;

      while (pgmoneta_deque_iterator_next(iter))
      {
         struct decoded_xlog_record* rec = (struct decoded_xlog_record*)iter->value->data;

         if (first_record == NULL)
         {
            first_record = rec;
         }
         last_record = rec;
      }
      pgmoneta_deque_iterator_destroy(iter);

      if (first_record == NULL || last_record == NULL)
      {
         continue;
      }

      if (file_idx < walfile_count - 1)
      {
         struct walfile* next_wf = walfiles[file_idx + 1];
         if (next_wf != NULL && next_wf->records != NULL)
         {
            struct deque_iterator* next_iter = NULL;
            pgmoneta_deque_iterator_create(next_wf->records, &next_iter);

            struct decoded_xlog_record* next_first_record = NULL;
            while (pgmoneta_deque_iterator_next(next_iter))
            {
               struct decoded_xlog_record* rec = (struct decoded_xlog_record*)next_iter->value->data;
               if (next_first_record == NULL)
               {
                  next_first_record = rec;
                  break;
               }
            }
            pgmoneta_deque_iterator_destroy(next_iter);

            if (next_first_record != NULL)
            {
               last_record->next = next_first_record;
               last_record->next_lsn = next_first_record->lsn;

               next_first_record->header.xl_prev = last_record->lsn;
            }
         }
      }
      else
      {
         last_record->next = NULL;
         last_record->next_lsn = 0;
      }
   }
}

/**
 * Filter out DELETE operations from WAL files
 *
 * @param file_count Pointer to the number of WAL files
 * @param walfiles Pointer to the array of WAL file pointers
 * @param backup Pointer to the backup information
 * @return 0 on success, non-zero on failure
 */
int
pgmoneta_filter_operation_delete(int* file_count, struct walfile*** walfiles, struct backup* backup)
{
#define XID_IS_DELETED(xid) \
        ({ int found = 0; \
           for (int k = 0; k < delete_xid_count; k++) { \
              if (delete_xids[k] == (xid)) { found = 1; break; } \
           } found; })

   xlog_rec_ptr end_walpos = 0;
   transaction_id* delete_xids = NULL;
   int delete_xid_count = 0;
   int delete_xid_capacity = 16;
   int records_filtered = 0;
   int nfiles = 0;

   if (backup != NULL)
   {
      end_walpos = ((uint64_t)backup->end_lsn_hi32 << 32) | backup->end_lsn_lo32;
   }

   delete_xids = malloc(delete_xid_capacity * sizeof(transaction_id));
   if (!delete_xids)
   {
      return 1;
   }

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
               if (rec->lsn > end_walpos)
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
      }
      pgmoneta_deque_iterator_destroy(iter);
   }

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
         int skip = 0;

         if (rec->header.xl_rmid == RM_HEAP_ID)
         {
            uint8_t info = rec->header.xl_info & ~XLR_INFO_MASK;
            info &= XLOG_HEAP_OPMASK;
            if (info == XLOG_HEAP_DELETE)
            {
               skip = 1;
               records_filtered++;
            }
         }

         if (!skip)
         {
            if (XID_IS_DELETED(rec->header.xl_xid) || XID_IS_DELETED(rec->toplevel_xid))
            {
               skip = 1;
               records_filtered++;
            }
         }

         if (skip)
         {
            if (rec->main_data)
            {
               free(rec->main_data);
               rec->main_data = NULL;
            }

            for (int block_id = 0; block_id <= XLR_MAX_BLOCK_ID; block_id++)
            {
               if (rec->blocks[block_id].data)
               {
                  free(rec->blocks[block_id].data);
                  rec->blocks[block_id].data = NULL;
               }
               if (rec->blocks[block_id].bkp_image)
               {
                  free(rec->blocks[block_id].bkp_image);
                  rec->blocks[block_id].bkp_image = NULL;
               }
            }

            free(rec);
            pgmoneta_deque_iterator_remove(rec_iter);
         }
      }
      pgmoneta_deque_iterator_destroy(rec_iter);

      (*walfiles)[nfiles++] = wf;
   }

   for (int i = nfiles; i < *file_count; i++)
   {
      (*walfiles)[i] = NULL;
   }

   printf("Filtered WAL files: %d\n", nfiles);
   printf("Total records filtered: %d\n", records_filtered);
   printf("Total XIDs deleted: %d\n", delete_xid_count);
   if (delete_xid_count > 0)
   {
      printf("Deleted XIDs:\n");
      for (int i = 0; i < delete_xid_count; i++)
      {
         printf("%u ", delete_xids[i]);
      }
      printf("\n");
   }

   free(delete_xids);

   *file_count = nfiles;

   return 0;
}

int
main(int argc, char* argv[])
{
   if (argc != 2)
   {
      usage();
      return 1;
   }

   config_t yaml_config;
   struct walfilter_configuration* config = NULL;
   char* logfile = NULL;
   int file_count = 0;
   char** files = NULL;
   char* file_path = malloc(MAX_PATH);
   char* wal_files_path = NULL;
   int loaded = 1;
   char* configuration_path = NULL;
   size_t size;
   struct backup* backup = NULL;
   char* backup_label = NULL;
   char* parent_dir = NULL;
   char* last_slash = NULL;
   char* tmp_wal = NULL;
   struct walfile* wf = NULL;
   char* decompressed_file_name = NULL;
   char* decrypted_file_name = NULL;
   char* wal_path = NULL;
   bool copy = true;
   struct walfile** walfiles = NULL;
   int walfile_count = 0;
   char* source_data_dir = NULL;
   char* target_pg_wal_dir = NULL;

   if (file_path == NULL)
   {
      warnx("Failed to allocate memory for file_path");
      return 1;
   }

   size = sizeof(struct walfilter_configuration);
   if (pgmoneta_create_shared_memory(size, HUGEPAGE_OFF, &shmem))
   {
      warnx("Error creating shared memory");
      goto error;
   }

   pgmoneta_init_walfilter_configuration(shmem);
   config = (struct walfilter_configuration*)shmem;

   if (configuration_path != NULL)
   {
      if (pgmoneta_exists(configuration_path))
      {
         loaded = pgmoneta_read_walfilter_configuration(shmem, configuration_path);
      }

      if (loaded)
      {
         warnx("Configuration not found: %s", configuration_path);
      }
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
         memcpy(&config->common.log_path[0], logfile, MIN(MISC_LENGTH - 1, strlen(logfile)));
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

   if (pgmoneta_parse_yaml_config(argv[1], &yaml_config) != 0)
   {
      pgmoneta_log_error("Failed to parse configuration\n");
      goto error;
   }

   last_slash = strrchr(yaml_config.source_dir, '/');
   if (last_slash != NULL && *(last_slash + 1) != '\0')
   {
      backup_label = last_slash + 1;
   }
   else
   {
      backup_label = yaml_config.source_dir;
   }

   parent_dir = pgmoneta_get_parent_dir(yaml_config.source_dir);
   if (parent_dir == NULL)
   {
      pgmoneta_log_error("Failed to allocate memory for parent_dir");
      goto error;
   }

   wal_files_path = pgmoneta_get_parent_dir(parent_dir);
   wal_files_path = pgmoneta_append(wal_files_path, "/wal");

   parent_dir = pgmoneta_append(parent_dir, "/");

   if (pgmoneta_load_info(parent_dir, backup_label, &backup))
   {
      pgmoneta_log_error("Failed to load backup information from %s\n", backup_label);
      goto error;
   }

   printf("Parent directory: %s\n", parent_dir);
   printf("Backup label: %s\n", backup_label);
   printf("WAL files path: %s\n", wal_files_path);
   printf("Backup END_WALPOS: %X/%X\n",
          backup->end_lsn_hi32, backup->end_lsn_lo32);

   if (pgmoneta_get_files(wal_files_path, &file_count, &files))
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
   }

   for (int i = 0; i < yaml_config.rules->exclude.operation_count; i++)
   {
      if (!strcmp(yaml_config.rules->exclude.operations[i], OPERATION_DELETE))
      {
         if (pgmoneta_filter_operation_delete(&walfile_count, &walfiles, backup) != 0)
         {
            pgmoneta_log_error("Failed to apply filter on operation %s", yaml_config.rules->exclude.operations[i]);
            goto error;
         }
      }
   }

   pgmoneta_process_walfiles(walfile_count, walfiles);

   source_data_dir = pgmoneta_format_and_append(source_data_dir, "%s/%s/data", parent_dir, backup_label);

   if (pgmoneta_mkdir(yaml_config.target_dir))
   {
      pgmoneta_log_error("Failed to create target data directory: %s", yaml_config.target_dir);
      goto error;
   }

   if (pgmoneta_copy_directory(source_data_dir, yaml_config.target_dir, NULL, NULL))
   {
      pgmoneta_log_error("Failed to copy backup data directory");
      goto error;
   }

   target_pg_wal_dir = pgmoneta_format_and_append(target_pg_wal_dir, "%s/pg_wal", yaml_config.target_dir);

   if (pgmoneta_exists(target_pg_wal_dir))
   {
      if (pgmoneta_delete_directory(target_pg_wal_dir))
      {
         pgmoneta_log_error("Failed to remove existing pg_wal directory: %s", target_pg_wal_dir);
         goto error;
      }
   }

   if (pgmoneta_mkdir(target_pg_wal_dir))
   {
      pgmoneta_log_error("Failed to create pg_wal directory: %s", target_pg_wal_dir);
      goto error;
   }

   pgmoneta_display_walfiles(walfiles, walfile_count);

   if (source_data_dir != NULL)
   {
      free(source_data_dir);
      source_data_dir = NULL;
   }
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
      free(partial_record);
      partial_record = NULL;
   }

   if (parent_dir != NULL)
   {
      free(parent_dir);
      parent_dir = NULL;
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
   if (backup != NULL)
   {
      free(backup);
      backup = NULL;
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

   if (source_data_dir != NULL)
   {
      free(source_data_dir);
      source_data_dir = NULL;
   }
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
      free(partial_record);
      partial_record = NULL;
   }

   if (parent_dir != NULL)
   {
      free(parent_dir);
      parent_dir = NULL;
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
   if (backup != NULL)
   {
      free(backup);
      backup = NULL;
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

   return 1;
}
