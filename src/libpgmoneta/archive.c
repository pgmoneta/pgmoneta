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

/* pgmoneta */
#include <pgmoneta.h>
#include <achv.h>
#include <deque.h>
#include <gzip_compression.h>
#include <info.h>
#include <json.h>
#include <logging.h>
#include <lz4_compression.h>
#include <management.h>
#include <network.h>
#include <restore.h>
#include <utils.h>
#include <workflow.h>
#include <zstandard_compression.h>

#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

static void write_tar_file(struct archive* a, char* current_real_path, char* current_save_path);

void
pgmoneta_archive(SSL* ssl, int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* backup_id = NULL;
   char* position = NULL;
   char* directory = NULL;
   char* elapsed = NULL;
   char real_directory[MAX_PATH];
   time_t start_time;
   time_t end_time;
   int total_seconds;
   char* to = NULL;
   char* id = NULL;
   char* d = NULL;
   char* output = NULL;
   char* filename = NULL;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   struct workflow* workflow = NULL;
   struct workflow* current = NULL;
   struct deque* nodes = NULL;
   struct json* req = NULL;
   struct json* response = NULL;
   struct configuration* config;

   pgmoneta_start_logging();

   config = (struct configuration*)shmem;

   start_time = time(NULL);

   atomic_fetch_add(&config->active_archives, 1);
   atomic_fetch_add(&config->servers[server].archiving, 1);

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   backup_id = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_BACKUP);
   position = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_POSITION);
   directory = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_DIRECTORY);

   // we used to get id after restore workflow, but now we need it first to create a wrapping directory, so...
   if (!strcmp(backup_id, "oldest"))
   {
      d = pgmoneta_get_server_backup(server);

      if (pgmoneta_get_backups(d, &number_of_backups, &backups))
      {
         goto error;
      }

      for (int i = 0; id == NULL && i < number_of_backups; i++)
      {
         if (backups[i]->valid == VALID_TRUE)
         {
            id = backups[i]->label;
         }
      }
   }
   else if (!strcmp(backup_id, "latest") || !strcmp(backup_id, "newest"))
   {
      d = pgmoneta_get_server_backup(server);

      if (pgmoneta_get_backups(d, &number_of_backups, &backups))
      {
         goto error;
      }

      for (int i = number_of_backups - 1; id == NULL && i >= 0; i--)
      {
         if (backups[i]->valid == VALID_TRUE)
         {
            id = backups[i]->label;
         }
      }
   }
   else
   {
      id = backup_id;
   }

   if (id == NULL)
   {
      pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_ARCHIVE_NOBACKUP, compression, encryption, payload);
      pgmoneta_log_error("Archive: No identifier for %s/%s", config->servers[server].name, backup_id);
      goto error;
   }

   memset(real_directory, 0, sizeof(real_directory));
   snprintf(real_directory, sizeof(real_directory), "%s/archive-%s-%s", directory, config->servers[server].name, id);

   pgmoneta_deque_create(false, &nodes);

   if (!pgmoneta_restore_backup(server, backup_id, position, real_directory, &output, &id))
   {
      if (pgmoneta_deque_add(nodes, "directory", (uintptr_t)real_directory, ValueString))
      {
         goto error;
      }

      if (pgmoneta_deque_add(nodes, "id", (uintptr_t)id, ValueString))
      {
         goto error;
      }

      if (pgmoneta_deque_add(nodes, "output", (uintptr_t)output, ValueString))
      {
         goto error;
      }

      if (pgmoneta_deque_add(nodes, "destination", (uintptr_t)directory, ValueString))
      {
         goto error;
      }

      workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_ARCHIVE);

      current = workflow;
      while (current != NULL)
      {
         if (current->setup(server, backup_id, nodes))
         {
            goto error;
         }
         current = current->next;
      }

      current = workflow;
      while (current != NULL)
      {
         if (current->execute(server, backup_id, nodes))
         {
            goto error;
         }
         current = current->next;
      }

      current = workflow;
      while (current != NULL)
      {
         if (current->teardown(server, backup_id, nodes))
         {
            goto error;
         }
         current = current->next;
      }

      if (pgmoneta_management_create_response(payload, server, &response))
      {
         pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_ALLOCATION, compression, encryption, payload);

         goto error;
      }

      filename = pgmoneta_append(filename, (char*)pgmoneta_deque_get(nodes, "tarfile"));
      if (config->compression_type == COMPRESSION_CLIENT_GZIP || config->compression_type == COMPRESSION_SERVER_GZIP)
      {
         filename = pgmoneta_append(filename, ".gz");
      }
      else if (config->compression_type == COMPRESSION_CLIENT_ZSTD || config->compression_type == COMPRESSION_SERVER_ZSTD)
      {
         filename = pgmoneta_append(filename, ".zstd");
      }
      else if (config->compression_type == COMPRESSION_CLIENT_LZ4 || config->compression_type == COMPRESSION_SERVER_LZ4)
      {
         filename = pgmoneta_append(filename, ".lz4");
      }
      else if (config->compression_type == COMPRESSION_CLIENT_BZIP2)
      {
         filename = pgmoneta_append(filename, ".bz2");
      }

      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->servers[server].name, ValueString);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)id, ValueString);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_FILENAME, (uintptr_t)filename, ValueString);

      end_time = time(NULL);

      if (pgmoneta_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload))
      {
         pgmoneta_management_response_error(NULL, client_fd, config->servers[server].name, MANAGEMENT_ERROR_ARCHIVE_NETWORK, compression, encryption, payload);
         pgmoneta_log_error("Archive: Error sending response for %s/%s", config->servers[server].name, backup_id);

         goto error;
      }

      elapsed = pgmoneta_get_timestamp_string(start_time, end_time, &total_seconds);

      pgmoneta_log_info("Archive: %s/%s (Elapsed: %s)", config->servers[server].name, id, elapsed);
   }

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   pgmoneta_deque_destroy(nodes);

   pgmoneta_json_destroy(payload);

   pgmoneta_workflow_delete(workflow);

   pgmoneta_disconnect(client_fd);

   atomic_fetch_sub(&config->servers[server].archiving, 1);
   atomic_fetch_sub(&config->active_archives, 1);

   pgmoneta_stop_logging();

   free(id);
   free(output);
   free(to);
   free(d);

   exit(0);

error:

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   pgmoneta_deque_destroy(nodes);

   pgmoneta_json_destroy(payload);

   pgmoneta_workflow_delete(workflow);

   pgmoneta_disconnect(client_fd);

   atomic_fetch_sub(&config->servers[server].archiving, 1);
   atomic_fetch_sub(&config->active_archives, 1);

   pgmoneta_stop_logging();

   free(id);
   free(output);
   free(to);
   free(d);

   exit(1);
}

int
pgmoneta_extract_tar_file(char* file_path, char* destination)
{
   char* archive_name = NULL;
   struct archive* a;
   struct archive_entry* entry;
   struct configuration* config;

   config = (struct configuration*)shmem;

   a = archive_read_new();
   archive_read_support_format_tar(a);

   if (config->compression_type == COMPRESSION_SERVER_GZIP)
   {
      archive_name = pgmoneta_append(archive_name, file_path);
      archive_name = pgmoneta_append(archive_name, ".gz");
      pgmoneta_move_file(file_path, archive_name);
      pgmoneta_gunzip_file(archive_name, file_path);
   }
   else if (config->compression_type == COMPRESSION_SERVER_ZSTD)
   {
      archive_name = pgmoneta_append(archive_name, file_path);
      archive_name = pgmoneta_append(archive_name, ".zstd");
      pgmoneta_move_file(file_path, archive_name);
      pgmoneta_zstandardd_file(archive_name, file_path);
   }
   else if (config->compression_type == COMPRESSION_SERVER_LZ4)
   {
      archive_name = pgmoneta_append(archive_name, file_path);
      archive_name = pgmoneta_append(archive_name, ".lz4");
      pgmoneta_move_file(file_path, archive_name);
      pgmoneta_lz4d_file(archive_name, file_path);
   }
   else
   {
      archive_name = pgmoneta_append(archive_name, file_path);
   }

   // open tar file in a suitable buffer size, I'm using 10240 here
   if (archive_read_open_filename(a, file_path, 10240) != ARCHIVE_OK)
   {
      pgmoneta_log_error("Failed to open the tar file for reading");
      goto error;
   }

   while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
   {
      char dst_file_path[MAX_PATH];
      memset(dst_file_path, 0, sizeof(dst_file_path));
      const char* entry_path = archive_entry_pathname(entry);
      if (pgmoneta_ends_with(destination, "/"))
      {
         snprintf(dst_file_path, sizeof(dst_file_path), "%s%s", destination, entry_path);
      }
      else
      {
         snprintf(dst_file_path, sizeof(dst_file_path), "%s/%s", destination, entry_path);
      }

      archive_entry_set_pathname(entry, dst_file_path);
      if (archive_read_extract(a, entry, 0) != ARCHIVE_OK)
      {
         pgmoneta_log_error("Failed to extract entry: %s", archive_error_string(a));
         goto error;
      }
   }

   free(archive_name);

   archive_read_close(a);
   archive_read_free(a);
   return 0;

error:
   free(archive_name);

   archive_read_close(a);
   archive_read_free(a);
   return 1;
}

int
pgmoneta_tar_directory(char* src_path, char* dst_path, char* save_path)
{
   struct archive* a = NULL;
   int status;

   a = archive_write_new();
   archive_write_set_format_ustar(a);  // Set tar format
   status = archive_write_open_filename(a, dst_path);

   if (status != ARCHIVE_OK)
   {
      pgmoneta_log_error("Could not create tar file %s", dst_path);
      goto error;
   }
   write_tar_file(a, src_path, save_path);

   archive_write_close(a);
   archive_write_free(a);

   return 0;

error:
   archive_write_close(a);
   archive_write_free(a);

   return 1;
}

static void
write_tar_file(struct archive* a, char* current_real_path, char* current_save_path)
{
   char real_path[MAX_PATH];
   char save_path[MAX_PATH];
   ssize_t size;
   struct archive_entry* entry;
   struct stat s;
   struct dirent* dent;

   DIR* dir = opendir(current_real_path);
   if (!dir)
   {
      pgmoneta_log_error("Could not open directory: %s", current_real_path);
      return;
   }
   while ((dent = readdir(dir)) != NULL)
   {
      char* entry_name = dent->d_name;

      if (pgmoneta_compare_string(entry_name, ".") || pgmoneta_compare_string(entry_name, ".."))
      {
         continue;
      }

      snprintf(real_path, sizeof(real_path), "%s/%s", current_real_path, entry_name);
      snprintf(save_path, sizeof(save_path), "%s/%s", current_save_path, entry_name);

      entry = archive_entry_new();
      archive_entry_copy_pathname(entry, save_path);

      lstat(real_path, &s);
      if (S_ISDIR(s.st_mode))
      {
         archive_entry_set_filetype(entry, AE_IFDIR);
         archive_entry_set_perm(entry, s.st_mode);
         archive_write_header(a, entry);
         write_tar_file(a, real_path, save_path);
      }
      else if (S_ISLNK(s.st_mode))
      {
         char target[MAX_PATH];
         memset(target, 0, sizeof(target));
         size = readlink(real_path, target, sizeof(target));
         if (size == -1)
         {
            return;
         }

         archive_entry_set_filetype(entry, AE_IFLNK);
         archive_entry_set_perm(entry, s.st_mode);
         archive_entry_set_symlink(entry, target);
         archive_write_header(a, entry);
      }
      else if (S_ISREG(s.st_mode))
      {
         FILE* file = NULL;

         archive_entry_set_filetype(entry, AE_IFREG);
         archive_entry_set_perm(entry, s.st_mode);
         archive_entry_set_size(entry, s.st_size);
         int status = archive_write_header(a, entry);
         if (status != ARCHIVE_OK)
         {
            pgmoneta_log_error("Could not write header: %s", archive_error_string(a));
            return;
         }

         file = fopen(real_path, "rb");

         if (file != NULL)
         {
            char buf[DEFAULT_BUFFER_SIZE];
            size_t bytes_read = 0;

            memset(buf, 0, sizeof(buf));
            while ((bytes_read = fread(buf, 1, sizeof(buf), file)) > 0)
            {
               archive_write_data(a, buf, bytes_read);
               memset(buf, 0, sizeof(buf));
            }
            fclose(file);
         }
      }

      archive_entry_free(entry);
   }

   closedir(dir);
}
