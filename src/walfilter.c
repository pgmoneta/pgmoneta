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
#define RM_HEAP_ID 13
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

struct walfile**
pgmoneta_filter_operation_delete(int file_count, struct walfile** walfiles, int* new_count)
{
   // 1. Collect all XIDs from HEAP DELETE records
   transaction_id* delete_xids = NULL;
   int delete_xid_count = 0;
   int delete_xid_capacity = 16;
   delete_xids = malloc(delete_xid_capacity * sizeof(transaction_id));
   if (!delete_xids)
   {
      return NULL;
   }

   for (int i = 0; i < file_count; i++)
   {
      struct walfile* wf = walfiles[i];
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
               // Store the XID (xmax)
               if (delete_xid_count == delete_xid_capacity)
               {
                  delete_xid_capacity *= 2;
                  delete_xids = realloc(delete_xids, delete_xid_capacity * sizeof(transaction_id));
               }
               delete_xids[delete_xid_count++] = del->xmax;
            }
         }
      }
      pgmoneta_deque_iterator_destroy(iter);
   }

   // 2. Helper: check if xid is in delete_xids
   #define XID_IS_DELETED(xid) \
           ({ int found = 0; \
              for (int k = 0; k < delete_xid_count; k++) { \
                 if (delete_xids[k] == (xid)) { found = 1; break; } \
              } found; })

   // 3. Build new walfile array
   struct walfile** new_walfiles = malloc(file_count * sizeof(struct walfile*));
   int nfiles = 0;
   for (int i = 0; i < file_count; i++)
   {
      struct walfile* wf = walfiles[i];
      struct walfile* new_wf = malloc(sizeof(struct walfile));

      if (!new_wf)
      {
         continue;
      }

      memset(new_wf, 0, sizeof(struct walfile));
      new_wf->magic_number = wf->magic_number;

      // Deep copy long_phd
      new_wf->long_phd = malloc(sizeof(struct xlog_long_page_header_data));
      memcpy(new_wf->long_phd, wf->long_phd, sizeof(struct xlog_long_page_header_data));

      // Copy page_headers deque (shallow, as headers are not filtered)
      pgmoneta_deque_create(false, &new_wf->page_headers);
      struct deque_iterator* ph_iter = NULL;
      pgmoneta_deque_iterator_create(wf->page_headers, &ph_iter);

      while (pgmoneta_deque_iterator_next(ph_iter))
      {
         pgmoneta_deque_add(new_wf->page_headers, NULL, (uintptr_t)ph_iter->value->data, ValueRef);
      }
      pgmoneta_deque_iterator_destroy(ph_iter);

      // Copy records, filtering out unwanted ones
      pgmoneta_deque_create(false, &new_wf->records);
      struct deque_iterator* rec_iter = NULL;
      pgmoneta_deque_iterator_create(wf->records, &rec_iter);

      while (pgmoneta_deque_iterator_next(rec_iter))
      {
         struct decoded_xlog_record* rec = (struct decoded_xlog_record*)rec_iter->value->data;
         int skip = 0;
         // 1. Skip if this is a HEAP DELETE record
         if (rec->header.xl_rmid == RM_HEAP_ID)
         {
            uint8_t info = rec->header.xl_info & ~XLR_INFO_MASK;
            info &= XLOG_HEAP_OPMASK;
            if (info == XLOG_HEAP_DELETE)
            {
               skip = 1;
            }
         }
         // 2. Skip if this record references a deleted XID
         if (!skip)
         {
            if (XID_IS_DELETED(rec->header.xl_xid) || XID_IS_DELETED(rec->toplevel_xid))
            {
               skip = 1;
            }
         }
         if (!skip)
         {
            // Deep copy the record
            struct decoded_xlog_record* rec_copy = malloc(sizeof(struct decoded_xlog_record));
            memcpy(rec_copy, rec, sizeof(struct decoded_xlog_record));

            // Deep copy main_data if present
            if (rec->main_data && rec->main_data_len > 0)
            {
               rec_copy->main_data = malloc(rec->main_data_len);
               memcpy(rec_copy->main_data, rec->main_data, rec->main_data_len);
            }
            pgmoneta_deque_add(new_wf->records, NULL, (uintptr_t)rec_copy, ValueRef);
         }
      }
      pgmoneta_deque_iterator_destroy(rec_iter);
      new_walfiles[nfiles++] = new_wf;
   }

   // Print deleted XIDs for debugging
   printf("Filtered WAL files: %d\n", nfiles);
   printf("Total XIDs deleted: %d\n", delete_xid_count);
   if (delete_xid_count > 0)
   {
      printf("Deleted XIDs:\n");
      for (int i = 0; i < delete_xid_count; i++)
      {
         printf("  %u\n", delete_xids[i]);
      }
   }

   free(delete_xids);
   *new_count = nfiles;
   return new_walfiles;
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

   // Create a new path that points to the parent directory of yaml_config.source_dir
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
      printf("Processing WAL file: %s\n", file_path);

      if (!pgmoneta_is_file(file_path))
      {
         pgmoneta_log_fatal("WAL file at %s does not exist", file_path);
         goto error;
      }

      free(wal_path);
      wal_path = NULL;
      wal_path = pgmoneta_append(wal_path, file_path);

      // Based on the file extension, if it's an encrypted file, decrypt it in /tmp
      if (pgmoneta_is_encrypted(wal_path))
      {
         free(tmp_wal);
         tmp_wal = NULL;
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
         decrypted_file_name = NULL;

         if (pgmoneta_decrypt_file(tmp_wal, wal_path))
         {
            pgmoneta_log_fatal("Failed to decrypt WAL file at %s", file_path);
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

   // ======================== Filtering on operations ========================
   for (int i = 0; i < yaml_config.rules->exclude.operation_count; i++)
   {
      if (strcmp(yaml_config.rules->exclude.operations[i], OPERATION_DELETE) == 0)
      {
         struct walfile** filtered_walfiles = NULL;
         int filtered_count = 0;
         filtered_walfiles = pgmoneta_filter_operation_delete(walfile_count, walfiles, &filtered_count);
         if (filtered_walfiles != NULL)
         {
            // Free the original walfiles before replacing
            for (int j = 0; j < walfile_count; j++)
            {
               if (walfiles[j] != NULL)
               {
                  pgmoneta_destroy_walfile(walfiles[j]);
                  walfiles[j] = NULL;
               }
            }
            free(walfiles);
            // Use the filtered walfiles for the rest of the program
            walfiles = filtered_walfiles;
            walfile_count = filtered_count;
         }
      }
   }
   // ======================== End of filtering on operations ========================

   // Print all walfiles, their records, and each record's LSN
   printf("\n=== Filtered WAL Files (Described) ===\n");
   for (int i = 0; i < walfile_count; i++)
   {
      if (files && i < file_count && files[i]) {
         printf("WAL file #%d: %s\n", i, files[i]);
         char file_full_path[MAX_PATH];
         snprintf(file_full_path, MAX_PATH, "%s/%s", wal_files_path, files[i]);
         pgmoneta_describe_walfile(file_full_path, ValueString, NULL, false, true, NULL, 0, 0, NULL, 0, NULL);
      } else {
         printf("WAL file #%d: [unknown file name]\n", i);
      }
   }
   printf("=== End of WAL Files ===\n\n");

   // Free all walfiles and related memory after printing
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

   if (partial_record != NULL)
   {
      free(partial_record);
   }
   if (tmp_wal != NULL)
   {
      free(tmp_wal);
   }
   if (wal_path != NULL)
   {
      free(wal_path);
   }
   if (decrypted_file_name != NULL)
   {
      free(decrypted_file_name);
   }
   if (decompressed_file_name != NULL)
   {
      free(decompressed_file_name);
   }
   if (wf != NULL)
   {
      pgmoneta_destroy_walfile(wf);
   }
   if (parent_dir != NULL)
   {
      free(parent_dir);
   }
   if (file_path != NULL)
   {
      free(file_path);
   }
   if (wal_files_path != NULL)
   {
      free(wal_files_path);
   }
   if (backup != NULL)
   {
      free(backup);
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
   }
   if (shmem != NULL)
   {
      pgmoneta_destroy_shared_memory(shmem, size);
   }

   // Clean up walfiles array in error case
   if (walfiles != NULL)
   {
      for (int i = 0; i < file_count; i++)  // Use file_count here since walfile_count might not be set
      {
         if (walfiles[i] != NULL)
         {
            pgmoneta_destroy_walfile(walfiles[i]);
         }
      }
      free(walfiles);
   }

   cleanup_config(&yaml_config);
   return 1;
}