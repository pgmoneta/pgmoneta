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

/* pgmoneta */
#include <pgmoneta.h>
#include <achv.h>
#include <gzip_compression.h>
#include <logging.h>
#include <lz4_compression.h>
#include <management.h>
#include <manifest.h>
#include <network.h>
#include <restore.h>
#include <security.h>
#include <utils.h>
#include <workflow.h>
#include <zstandard_compression.h>

#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define NAME "archive"

static bool is_server_side_compression(void);

static void write_tar_file(struct archive* a, char* src, char* dst);

void
pgmoneta_archive(SSL* ssl __attribute__((unused)), int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* payload)
{
   bool active = false;
   char* identifier = NULL;
   char* position = NULL;
   char* directory = NULL;
   char* elapsed = NULL;
   char* real_directory = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   char* label = NULL;
   char* output = NULL;
   char* filename = NULL;
   struct backup* backup = NULL;
   struct workflow* workflow = NULL;
   struct art* nodes = NULL;
   struct json* req = NULL;
   struct json* response = NULL;
   struct main_configuration* config;

   pgmoneta_start_logging();

   config = (struct main_configuration*)shmem;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   if (!atomic_compare_exchange_strong(&config->common.servers[server].repository, &active, true))
   {
      pgmoneta_log_info("Archive: Server %s is active", config->common.servers[server].name);
      pgmoneta_management_response_error(NULL, client_fd, config->common.servers[server].name, MANAGEMENT_ERROR_ARCHIVE_ACTIVE, NAME, compression, encryption, payload);

      goto done;
   }

   config->common.servers[server].active_archive = true;

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   identifier = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_BACKUP);
   position = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_POSITION);
   directory = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_DIRECTORY);

   if (pgmoneta_art_create(&nodes))
   {
      goto error;
   }

   if (pgmoneta_art_insert(nodes, USER_POSITION, (uintptr_t)position, ValueString))
   {
      goto error;
   }

   if (pgmoneta_art_insert(nodes, NODE_TARGET_ROOT, (uintptr_t)directory, ValueString))
   {
      goto error;
   }

   if (pgmoneta_workflow_nodes(server, identifier, nodes, &backup))
   {
      pgmoneta_management_response_error(NULL, client_fd, config->common.servers[server].name,
                                         MANAGEMENT_ERROR_ARCHIVE_NOBACKUP, NAME, compression, encryption, payload);
      pgmoneta_log_warn("Archive: No identifier for %s/%s", config->common.servers[server].name, identifier);
      goto error;
   }

   if (backup == NULL)
   {
      pgmoneta_management_response_error(NULL, client_fd, config->common.servers[server].name, MANAGEMENT_ERROR_ARCHIVE_NOBACKUP, NAME, compression, encryption, payload);
      pgmoneta_log_warn("Archive: No identifier for %s/%s", config->common.servers[server].name, identifier);
      goto error;
   }

   real_directory = pgmoneta_append(real_directory, directory);
   if (!pgmoneta_ends_with(real_directory, "/"))
   {
      real_directory = pgmoneta_append_char(real_directory, '/');
   }
   real_directory = pgmoneta_append(real_directory, config->common.servers[server].name);
   real_directory = pgmoneta_append_char(real_directory, '-');
   real_directory = pgmoneta_append(real_directory, backup->label);

   if (pgmoneta_exists(real_directory))
   {
      pgmoneta_delete_directory(real_directory);
   }

   pgmoneta_mkdir(real_directory);

   if (pgmoneta_art_insert(nodes, NODE_TARGET_BASE, (uintptr_t)real_directory, ValueString))
   {
      goto error;
   }

   if (!pgmoneta_restore_backup(nodes))
   {
      workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_ARCHIVE, server, backup);

      if (pgmoneta_workflow_execute(workflow, nodes, server, client_fd, compression, encryption, payload))
      {
         goto error;
      }

      if (pgmoneta_management_create_response(payload, server, &response))
      {
         pgmoneta_management_response_error(NULL, client_fd, config->common.servers[server].name, MANAGEMENT_ERROR_ALLOCATION, NAME, compression, encryption, payload);

         goto error;
      }

      filename = pgmoneta_append(filename, (char*)pgmoneta_art_search(nodes, NODE_TARGET_FILE));
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

      if (config->encryption != ENCRYPTION_NONE)
      {
         filename = pgmoneta_append(filename, ".aes");
      }

      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->common.servers[server].name, ValueString);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)label, ValueString);
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_FILENAME, (uintptr_t)filename, ValueString);

#ifdef HAVE_FREEBSD
      clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
      clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

      if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
      {
         pgmoneta_management_response_error(NULL, client_fd, config->common.servers[server].name, MANAGEMENT_ERROR_ARCHIVE_NETWORK, NAME, compression, encryption, payload);
         pgmoneta_log_error("Archive: Error sending response for %s/%s", config->common.servers[server].name, identifier);

         goto error;
      }

      elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

      pgmoneta_log_info("Archive: %s/%s (Elapsed: %s)", config->common.servers[server].name, label, elapsed);
   }

   pgmoneta_art_destroy(nodes);

   pgmoneta_json_destroy(payload);

   pgmoneta_workflow_destroy(workflow);

   pgmoneta_disconnect(client_fd);

   config->common.servers[server].active_archive = false;
   atomic_store(&config->common.servers[server].repository, false);

done:

   pgmoneta_stop_logging();

   free(label);
   free(output);
   free(real_directory);

   exit(0);

error:

   pgmoneta_art_destroy(nodes);

   pgmoneta_json_destroy(payload);

   pgmoneta_workflow_destroy(workflow);

   pgmoneta_disconnect(client_fd);

   config->common.servers[server].active_archive = false;
   atomic_store(&config->common.servers[server].repository, false);

   pgmoneta_stop_logging();

   free(label);
   free(output);
   free(real_directory);

   exit(1);
}

int
pgmoneta_extract_tar_file(char* file_path, char* destination)
{
   char* archive_name = NULL;
   struct archive* a;
   struct archive_entry* entry;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

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
pgmoneta_tar_directory(char* src, char* dst, char* destination)
{
   struct archive* a = NULL;
   int status;

   a = archive_write_new();
   archive_write_set_format_ustar(a);  // Set tar format
   status = archive_write_open_filename(a, dst);

   if (status != ARCHIVE_OK)
   {
      pgmoneta_log_error("Could not create tar file %s", dst);
      goto error;
   }
   write_tar_file(a, src, destination);

   archive_write_close(a);
   archive_write_free(a);

   return 0;

error:
   archive_write_close(a);
   archive_write_free(a);

   return 1;
}

int
pgmoneta_receive_archive_files(SSL* ssl, int socket, struct stream_buffer* buffer, char* basedir, struct tablespace* tablespaces, struct token_bucket* bucket, struct token_bucket* network_bucket)
{
   char directory[MAX_PATH];
   char link_path[MAX_PATH];
   char null_buffer[2 * 512]; // 2 tar block size of terminator null bytes
   FILE* file = NULL;
   struct query_response* response = NULL;
   struct message* msg = (struct message*)malloc(sizeof (struct message));
   struct tuple* tup = NULL;

   memset(msg, 0, sizeof (struct message));

   // Receive the second result set
   if (pgmoneta_consume_data_row_messages(ssl, socket, buffer, &response))
   {
      goto error;
   }
   tup = response->tuples;
   while (tup != NULL)
   {
      char file_path[MAX_PATH];
      char directory[MAX_PATH];
      memset(file_path, 0, sizeof(file_path));
      memset(directory, 0, sizeof(directory));
      if (tup->data[1] == NULL)
      {
         // main data directory
         if (pgmoneta_ends_with(basedir, "/"))
         {
            snprintf(file_path, sizeof(file_path), "%sdata/%s", basedir, "base.tar");
            snprintf(directory, sizeof(directory), "%sdata/", basedir);
         }
         else
         {
            snprintf(file_path, sizeof(file_path), "%s/data/%s", basedir, "base.tar");
            snprintf(directory, sizeof(directory), "%s/data/", basedir);
         }
      }
      else
      {
         // user level tablespace
         struct tablespace* tblspc = tablespaces;
         while (tblspc != NULL)
         {
            if (pgmoneta_compare_string(tup->data[1], tblspc->path))
            {
               tblspc->oid = atoi(tup->data[0]);
               break;
            }
            tblspc = tblspc->next;
         }
         if (pgmoneta_ends_with(basedir, "/"))
         {
            snprintf(file_path, sizeof(file_path), "%stblspc_%s/%s.tar", basedir, tblspc->name, tblspc->name);
            snprintf(directory, sizeof(directory), "%stblspc_%s/", basedir, tblspc->name);
         }
         else
         {
            snprintf(file_path, sizeof(file_path), "%s/tblspc_%s/%s.tar", basedir, tblspc->name, tblspc->name);
            snprintf(directory, sizeof(directory), "%s/tblspc_%s/", basedir, tblspc->name);
         }
      }
      pgmoneta_mkdir(directory);
      file = fopen(file_path, "wb");
      if (file == NULL)
      {
         pgmoneta_log_error("Could not create archive tar file");
         goto error;
      }
      // get the copy out response
      while (msg == NULL || msg->kind != 'H')
      {
         pgmoneta_consume_copy_stream_start(ssl, socket, buffer, msg, NULL);
         if (msg->kind == 'E' || msg->kind == 'f')
         {
            pgmoneta_log_copyfail_message(msg);
            pgmoneta_log_error_response_message(msg);
            fflush(file);
            fclose(file);
            goto error;
         }
         pgmoneta_consume_copy_stream_end(buffer, msg);
      }
      while (msg->kind != 'c')
      {
         pgmoneta_consume_copy_stream_start(ssl, socket, buffer, msg, network_bucket);
         if (msg->kind == 'E' || msg->kind == 'f')
         {
            pgmoneta_log_copyfail_message(msg);
            pgmoneta_log_error_response_message(msg);
            fflush(file);
            fclose(file);
            goto error;
         }

         if (msg->kind == 'd' && msg->length > 0)
         {

            if (bucket)
            {
               while (1)
               {
                  if (!pgmoneta_token_bucket_consume(bucket, msg->length))
                  {
                     break;
                  }
                  else
                  {
                     SLEEP(500000000L)
                  }
               }
            }

            // copy data
            if (fwrite(msg->data, msg->length, 1, file) != 1)
            {
               pgmoneta_log_error("could not write to file %s", file_path);
               fflush(file);
               fclose(file);
               goto error;
            }
         }
         pgmoneta_consume_copy_stream_end(buffer, msg);
      }
      //append two blocks of null bytes to the end of the tar file
      memset(null_buffer, 0, 2 * 512);
      if (fwrite(null_buffer, 2 * 512, 1, file) != 1)
      {
         pgmoneta_log_error("could not write to file %s", file_path);
         fflush(file);
         fclose(file);
         goto error;
      }
      fflush(file);
      fclose(file);

      // extract the file
      pgmoneta_extract_tar_file(file_path, directory);
      remove(file_path);
      pgmoneta_free_message(msg);

      msg = NULL;
      tup = tup->next;
   }

   if (pgmoneta_receive_manifest_file(ssl, socket, buffer, basedir, bucket, network_bucket))
   {
      goto error;
   }

   // update symbolic link
   struct tablespace* tblspc = tablespaces;
   while (tblspc != NULL)
   {
      memset(link_path, 0, sizeof(link_path));
      memset(directory, 0, sizeof(directory));

      if (pgmoneta_ends_with(basedir, "/"))
      {
         snprintf(link_path, sizeof(link_path), "%sdata/pg_tblspc/%d", basedir, tblspc->oid);
         snprintf(directory, sizeof(directory), "%stblspc_%s/", basedir, tblspc->name);
      }
      else
      {
         snprintf(link_path, sizeof(link_path), "%s/data/pg_tblspc/%d", basedir, tblspc->oid);
         snprintf(directory, sizeof(directory), "%s/tblspc_%s/", basedir, tblspc->name);
      }

      unlink(link_path);
      pgmoneta_symlink_file(link_path, directory);
      tblspc = tblspc->next;
   }

   memset(directory, 0, sizeof(directory));

   if (pgmoneta_ends_with(basedir, "/"))
   {
      snprintf(directory, sizeof(directory), "%sdata", basedir);
   }
   else
   {
      snprintf(directory, sizeof(directory), "%s/data", basedir);
   }

   if (pgmoneta_manifest_checksum_verify(directory))
   {
      pgmoneta_log_error("Manifest verification failed");
      goto error;
   }

   pgmoneta_free_query_response(response);
   pgmoneta_free_message(msg);
   return 0;

error:
   pgmoneta_close_ssl(ssl);
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   pgmoneta_free_query_response(response);
   pgmoneta_free_message(msg);
   return 1;
}

int
pgmoneta_receive_archive_stream(SSL* ssl, int socket, struct stream_buffer* buffer, char* basedir, struct tablespace* tablespaces, struct token_bucket* bucket, struct token_bucket* network_bucket)
{
   struct query_response* response = NULL;
   struct message* msg = (struct message*)malloc(sizeof (struct message));
   struct tuple* tup = NULL;
   struct tablespace* tblspc = NULL;
   char null_buffer[2 * 512];
   char file_path[MAX_PATH];
   char directory[MAX_PATH];
   char link_path[MAX_PATH];
   char tmp_manifest_file_path[MAX_PATH];
   char manifest_file_path[MAX_PATH];
   memset(file_path, 0, sizeof(file_path));
   memset(directory, 0, sizeof(directory));
   memset(link_path, 0, sizeof(link_path));
   memset(manifest_file_path, 0, sizeof(manifest_file_path));
   memset(tmp_manifest_file_path, 0, sizeof(tmp_manifest_file_path));
   memset(null_buffer, 0, 2 * 512);
   char type;
   FILE* file = NULL;

   if (msg == NULL)
   {
      goto error;
   }

   memset(msg, 0, sizeof(struct message));

   // Receive the second result set
   if (pgmoneta_consume_data_row_messages(ssl, socket, buffer, &response))
   {
      goto error;
   }
   while (msg == NULL || msg->kind != 'H')
   {
      pgmoneta_consume_copy_stream_start(ssl, socket, buffer, msg, NULL);
      if (msg->kind == 'E' || msg->kind == 'f')
      {
         pgmoneta_log_copyfail_message(msg);
         pgmoneta_log_error_response_message(msg);
         goto error;
      }
      pgmoneta_consume_copy_stream_end(buffer, msg);
   }

   while (msg->kind != 'c')
   {
      pgmoneta_consume_copy_stream_start(ssl, socket, buffer, msg, network_bucket);
      if (msg->kind == 'E' || msg->kind == 'f')
      {
         pgmoneta_log_copyfail_message(msg);
         pgmoneta_log_error_response_message(msg);
         goto error;
      }
      if (msg->kind == 'd')
      {
         type = *((char*)msg->data);
         switch (type)
         {
            case 'n':
            {
               // append two blocks of null buffer and extract the tar file
               if (file != NULL)
               {
                  if ((!is_server_side_compression()) && fwrite(null_buffer, 2 * 512, 1, file) != 1)
                  {
                     pgmoneta_log_error("could not write to file %s", file_path);
                     fflush(file);
                     fclose(file);
                     file = NULL;
                     goto error;
                  }
                  fflush(file);
                  fclose(file);
                  file = NULL;
                  pgmoneta_extract_tar_file(file_path, directory);
                  remove(file_path);
               }
               // new tablespace or main directory tar file
               char* archive_name = pgmoneta_read_string(msg->data + 1);
               char* archive_path = pgmoneta_read_string(msg->data + 1 + strlen(archive_name) + 1);

               memset(file_path, 0, sizeof(file_path));
               memset(directory, 0, sizeof(directory));
               // The tablespace order in the second result set is presumably the same as the order in which the server sends tablespaces
               tblspc = tablespaces;
               if (tup == NULL)
               {
                  tup = response->tuples;
               }
               else
               {
                  tup = tup->next;
               }
               if (tup->data[1] == NULL)
               {
                  // main data directory
                  if (pgmoneta_ends_with(basedir, "/"))
                  {
                     snprintf(file_path, sizeof(file_path), "%sdata/%s", basedir, "base.tar");
                     snprintf(directory, sizeof(directory), "%sdata/", basedir);
                  }
                  else
                  {
                     snprintf(file_path, sizeof(file_path), "%s/data/%s", basedir, "base.tar");
                     snprintf(directory, sizeof(directory), "%s/data/", basedir);
                  }
               }
               else
               {
                  // user level tablespace
                  tblspc = tablespaces;
                  while (tblspc != NULL)
                  {
                     if (pgmoneta_compare_string(tblspc->path, archive_path))
                     {
                        tblspc->oid = atoi(tup->data[0]);
                        break;
                     }
                     tblspc = tblspc->next;
                  }
                  if (pgmoneta_ends_with(basedir, "/"))
                  {
                     snprintf(file_path, sizeof(file_path), "%stblspc_%s/%s.tar", basedir, tblspc->name, tblspc->name);
                     snprintf(directory, sizeof(directory), "%stblspc_%s/", basedir, tblspc->name);
                  }
                  else
                  {
                     snprintf(file_path, sizeof(file_path), "%s/tblspc_%s/%s.tar", basedir, tblspc->name, tblspc->name);
                     snprintf(directory, sizeof(directory), "%s/tblspc_%s/", basedir, tblspc->name);
                  }
               }
               pgmoneta_mkdir(directory);
               file = fopen(file_path, "wb");
               if (file == NULL)
               {
                  pgmoneta_log_error("Could not create archive tar file");
                  goto error;
               }
               break;
            }
            case 'm':
            {
               // start of manifest, finish off previous data archive receiving
               if (file != NULL)
               {
                  if ((!is_server_side_compression()) && fwrite(null_buffer, 2 * 512, 1, file) != 1)
                  {
                     pgmoneta_log_error("could not write to file %s", file_path);
                     fflush(file);
                     fclose(file);
                     file = NULL;
                     goto error;
                  }
                  fflush(file);
                  fclose(file);
                  file = NULL;
                  pgmoneta_extract_tar_file(file_path, directory);
                  remove(file_path);
               }
               if (pgmoneta_ends_with(basedir, "/"))
               {
                  snprintf(tmp_manifest_file_path, sizeof(tmp_manifest_file_path), "%sdata/%s", basedir, "backup_manifest.tmp");
                  snprintf(manifest_file_path, sizeof(manifest_file_path), "%sdata/%s", basedir, "backup_manifest");
               }
               else
               {
                  snprintf(tmp_manifest_file_path, sizeof(tmp_manifest_file_path), "%s/data/%s", basedir, "backup_manifest.tmp");
                  snprintf(manifest_file_path, sizeof(manifest_file_path), "%s/data/%s", basedir, "backup_manifest");
               }
               file = fopen(tmp_manifest_file_path, "wb");
               break;
            }
            case 'd':
            {
               // real data
               if (msg->length <= 1)
               {
                  break;
               }

               if (bucket)
               {
                  while (1)
                  {
                     if (!pgmoneta_token_bucket_consume(bucket, msg->length))
                     {
                        break;
                     }
                     else
                     {
                        SLEEP(500000000L)
                     }
                  }
               }

               if (fwrite(msg->data + 1, msg->length - 1, 1, file) != 1)
               {
                  pgmoneta_log_error("could not write to file %s", file_path);
                  goto error;
               }
               break;
            }
            case 'p':
            {
               // progress report, ignore this for now
               break;
            }
            default:
            {
               // should not happen, error
               pgmoneta_log_error("Invalid copy out data type");
               goto error;
            }
         }
      }
      pgmoneta_consume_copy_stream_end(buffer, msg);
   }

   if (file != NULL)
   {
      if (rename(tmp_manifest_file_path, manifest_file_path) != 0)
      {
         pgmoneta_log_error("could not rename file %s to %s", tmp_manifest_file_path, manifest_file_path);
         goto error;
      }
      fflush(file);
      fclose(file);
      file = NULL;
   }

   // update symlink
   tblspc = tablespaces;
   while (tblspc != NULL)
   {
      // update symlink
      memset(link_path, 0, sizeof(link_path));
      memset(directory, 0, sizeof(directory));
      if (pgmoneta_ends_with(basedir, "/"))
      {
         snprintf(link_path, sizeof(link_path), "%sdata/pg_tblspc/%d", basedir, tblspc->oid);
         snprintf(directory, sizeof(directory), "%stblspc_%s/", basedir, tblspc->name);
      }
      else
      {
         snprintf(link_path, sizeof(link_path), "%s/data/pg_tblspc/%d", basedir, tblspc->oid);
         snprintf(directory, sizeof(directory), "%s/tblspc_%s/", basedir, tblspc->name);
      }
      unlink(link_path);
      pgmoneta_symlink_file(link_path, directory);
      tblspc = tblspc->next;
   }

   // verify checksum if available
   char dir[MAX_PATH];
   memset(dir, 0, sizeof(dir));
   if (pgmoneta_ends_with(basedir, "/"))
   {
      snprintf(dir, sizeof(dir), "%sdata", basedir);
   }
   else
   {
      snprintf(dir, sizeof(dir), "%s/data", basedir);
   }
   if (pgmoneta_manifest_checksum_verify(dir))
   {
      pgmoneta_log_error("Manifest verification failed");
      goto error;
   }

   pgmoneta_free_query_response(response);
   pgmoneta_free_message(msg);
   return 0;

error:
   pgmoneta_close_ssl(ssl);
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   if (file != NULL)
   {
      fflush(file);
      fclose(file);
   }
   pgmoneta_free_query_response(response);
   pgmoneta_free_message(msg);
   return 1;
}

static void
write_tar_file(struct archive* a, char* src, char* dst)
{
   char real_path[MAX_PATH];
   char save_path[MAX_PATH];
   ssize_t size;
   struct archive_entry* entry;
   struct stat s;
   struct dirent* dent;

   DIR* dir = opendir(src);
   if (!dir)
   {
      pgmoneta_log_error("Could not open directory: %s", src);
      return;
   }
   while ((dent = readdir(dir)) != NULL)
   {
      char* entry_name = dent->d_name;

      if (pgmoneta_compare_string(entry_name, ".") || pgmoneta_compare_string(entry_name, ".."))
      {
         continue;
      }

      snprintf(real_path, sizeof(real_path), "%s/%s", src, entry_name);
      snprintf(save_path, sizeof(save_path), "%s/%s", dst, entry_name);

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

static bool
is_server_side_compression(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;
   return config->compression_type == COMPRESSION_SERVER_GZIP ||
          config->compression_type == COMPRESSION_SERVER_LZ4 ||
          config->compression_type == COMPRESSION_SERVER_ZSTD;
}
