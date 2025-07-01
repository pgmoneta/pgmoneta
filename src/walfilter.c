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

/* system */
#include <err.h>
#include <stdio.h>
#include <dirent.h>
#include <libgen.h>

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

int
main(int argc, char* argv[])
{
   if (argc != 2)
   {
      usage();
      return 1;
   }

   config_t yaml_config;
   struct walinfo_configuration* config = NULL;
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
   struct deque_iterator* record_iterator = NULL;
   struct decoded_xlog_record* record = NULL;
   char* decompressed_file_name = NULL;
   char* decrypted_file_name = NULL;
   char* wal_path = NULL;
   bool copy = true;

   if (file_path == NULL)
   {
      warnx("Failed to allocate memory for file_path");
      return 1;
   }

   size = sizeof(struct walinfo_configuration);
   if (pgmoneta_create_shared_memory(size, HUGEPAGE_OFF, &shmem))
   {
      warnx("Error creating shared memory");
      goto error;
   }

   pgmoneta_init_walinfo_configuration(shmem);
   config = (struct walinfo_configuration*)shmem;

   if (configuration_path != NULL)
   {
      if (pgmoneta_exists(configuration_path))
      {
         loaded = pgmoneta_read_walinfo_configuration(shmem, configuration_path);
      }

      if (loaded)
      {
         warnx("Configuration not found: %s", configuration_path);
      }
   }

   if (loaded && pgmoneta_exists(PGMONETA_WALINFO_DEFAULT_CONFIG_FILE_PATH))
   {
      loaded = pgmoneta_read_walinfo_configuration(shmem, PGMONETA_WALINFO_DEFAULT_CONFIG_FILE_PATH);
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

   if (pgmoneta_validate_walinfo_configuration())
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
   last_slash = NULL;

   parent_dir = strdup(yaml_config.source_dir);
   if (parent_dir == NULL)
   {
      pgmoneta_log_error("Failed to allocate memory for parent_dir");
      goto error;
   }

   last_slash = strrchr(parent_dir, '/');
   if (last_slash != NULL && last_slash != parent_dir)
   {
      *last_slash = '\0';
   }
   else if (last_slash == parent_dir)
   {
      // The path is like "/foo", so keep "/"
      parent_dir[1] = '\0';
   }
   // else: no slash found, parent_dir stays as is (probably ".")

   parent_dir = pgmoneta_append(parent_dir, "/");

   if (pgmoneta_load_info(parent_dir, backup_label, &backup))
   {
      pgmoneta_log_error("Failed to load backup information from %s\n", backup_label);
      goto error;
   }

   wal_files_path = pgmoneta_append(NULL, yaml_config.source_dir);
   wal_files_path = pgmoneta_append(wal_files_path, "/data/pg_wal");

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

      if (pgmoneta_deque_iterator_create(wf->records, &record_iterator))
      {
         pgmoneta_log_fatal("Failed to create deque iterator");
         goto error;
      }

      while (pgmoneta_deque_iterator_next(record_iterator))
      {
         record = (struct decoded_xlog_record*) record_iterator->value->data;
         printf("record->lsn 0x%lX\n", (unsigned long)record->lsn);
      }

      pgmoneta_deque_iterator_destroy(record_iterator);
      record_iterator = NULL;
      pgmoneta_destroy_walfile(wf);
      wf = NULL;
   }

   // ======================== Cleanup ========================

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
   if (record_iterator != NULL)
   {
      pgmoneta_deque_iterator_destroy(record_iterator);
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

   cleanup_config(&yaml_config);
   return 1;
}