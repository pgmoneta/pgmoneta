/*
 * Copyright (C) 2023 Red Hat
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
#include <logging.h>
#include <management.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <prometheus.h>
#include <security.h>
#include <server.h>
#include <wal.h>
#include <utils.h>

/* system */
#include <errno.h>
#include <ev.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <openssl/ssl.h>

static char* wal_file_name(int timeline, size_t segno, int segsize);
static FILE* wal_open(char* root, char* filename, int segsize);
static int wal_close(char* root, char* filename, bool partial, FILE* file);
static int wal_prepare(FILE* file, int segsize);
static int wal_send_status_report(int socket, int64_t received, int64_t flushed, int64_t applied);
static int wal_xlog_offset(size_t xlogptr, int segsize);
static int wal_convert_xlogpos(char* xlogpos, int* high32, int* low32, int segsize);
static int wal_shipping_setup(int srv, char** wal_shipping);

void
pgmoneta_wal(int srv, char** argv)
{
   int usr;
   int auth;
   int cnt = 0;
   int socket = -1;
   int high32 = 0;
   int low32 = 0;
   char* d = NULL;
   char* wal_shipping = NULL;
   int timeline = -1;
   int hdrlen = 1 + 8 + 8 + 8;
   int bytes_left = 0;
   char* xlogpos = NULL;
   char* remain_buffer = NULL;
   char cmd[MISC_LENGTH];
   size_t xlogpos_size = 0;
   size_t xlogptr = 0;
   size_t segno;
   int xlogoff;
   int curr_xlogoff = 0;
   int segsize;
   char* filename = NULL;
   signed char type;
   int ret;
   FILE* wal_file = NULL;
   FILE* wal_shipping_file = NULL;
   struct message* identify_system_msg = NULL;
   struct query_response* identify_system_response = NULL;
   struct message* start_replication_msg = NULL;
   struct message* msg = NULL;
   struct configuration* config;
   struct stream_buffer* buffer = NULL;

   pgmoneta_start_logging();
   pgmoneta_memory_init();

   config = (struct configuration*) shmem;

   pgmoneta_set_proc_title(1, argv, "wal", config->servers[srv].name);

   usr = -1;
   for (int i = 0; usr == -1 && i < config->number_of_users; i++)
   {
      if (!strcmp(config->servers[srv].username, config->users[i].username))
      {
         usr = i;
      }
   }

   if (usr == -1)
   {
      pgmoneta_log_trace("Invalid user for %s", config->servers[srv].name);
      goto error;
   }
   pgmoneta_server_info(srv);

   segsize = config->servers[srv].wal_size;
   d = pgmoneta_get_server_wal(srv);
   pgmoneta_mkdir(d);

   // Setup WAL shipping directory
   if (wal_shipping_setup(srv, &wal_shipping))
   {
      pgmoneta_log_warn("Unable to create WAL shipping directory");
   }

   auth = pgmoneta_server_authenticate(srv, "postgres", config->users[usr].username, config->users[usr].password, true, &socket);

   if (auth != AUTH_SUCCESS)
   {
      pgmoneta_log_trace("Invalid credentials for %s", config->users[usr].username);
      goto error;
   }

   pgmoneta_create_identify_system_message(&identify_system_msg);
   pgmoneta_query_execute(socket, identify_system_msg, &identify_system_response);

   timeline = atoi(pgmoneta_query_response_get_data(identify_system_response, 1));
   xlogpos_size = strlen(pgmoneta_query_response_get_data(identify_system_response, 2)) + 1;
   xlogpos = (char*)malloc(xlogpos_size);
   memset(xlogpos, 0, xlogpos_size);
   memcpy(xlogpos, pgmoneta_query_response_get_data(identify_system_response, 2), xlogpos_size);

   if (wal_convert_xlogpos(xlogpos, &high32, &low32, segsize))
   {
      goto error;
   }

   snprintf(cmd, sizeof(cmd), "%X/%X", high32, low32);

   pgmoneta_memory_stream_buffer_init(&buffer);

   config->servers[srv].wal_streaming = true;

   pgmoneta_create_start_replication_message(cmd, timeline, config->servers[srv].wal_slot, &start_replication_msg);

   ret = pgmoneta_write_message(NULL, socket, start_replication_msg);

   if (ret != MESSAGE_STATUS_OK)
   {
      pgmoneta_log_error("Error during START_REPLICATION for server %s", config->servers[srv].name);
      goto error;
   }

   type = 0;

   // wait for the CopyBothResponse message
   while (config->running && (msg == NULL || type != 'W'))
   {
      ret = pgmoneta_consume_copy_stream(socket, buffer, &msg);
      if (ret != 1)
      {
         pgmoneta_log_error("Error occurred when starting stream replication");
         goto error;
      }
      type = msg->kind;
      if (type == 'E')
      {
         pgmoneta_log_error("Error occurred when starting stream replication");
         goto error;
      }
   }

   type = 0;

   while (config->running)
   {
      ret = pgmoneta_consume_copy_stream(socket, buffer, &msg);
      if (ret == 0)
      {
         break;
      }
      if (ret != MESSAGE_STATUS_OK)
      {
         goto error;
      }

      if (msg == NULL || msg->kind == 'E' || msg->kind == 'f')
      {
         pgmoneta_log_message(msg);
         goto error;
      }
      if (msg->kind == 'd')
      {
         type = *((char*)msg->data);
         switch (type)
         {
            case 'w':
            {
               // wal data
               if (msg->length < hdrlen)
               {
                  pgmoneta_log_error("Incomplete CopyData payload");
                  goto error;
               }
               xlogptr = pgmoneta_read_int64(msg->data + 1);
               xlogoff = wal_xlog_offset(xlogptr, segsize);

               if (wal_file == NULL)
               {
                  if (xlogoff != 0 && bytes_left != xlogoff)
                  {
                     pgmoneta_log_error("Received WAL record of offset %d with no file open", xlogoff);
                     goto error;
                  }
                  else
                  {
                     // new wal file
                     segno = xlogptr / segsize;
                     curr_xlogoff = 0;
                     filename = wal_file_name(timeline, segno, segsize);
                     if ((wal_file = wal_open(d, filename, segsize)) == NULL)
                     {
                        pgmoneta_log_error("Could not create or open WAL segment file at %s", d);
                        goto error;
                     }
                     if ((wal_shipping_file = wal_open(wal_shipping, filename, segsize)) == NULL)
                     {
                        if (wal_shipping != NULL)
                        {
                           pgmoneta_log_warn("Could not create or open WAL segment file at %s", wal_shipping);
                        }
                     }
                     if (bytes_left > 0)
                     {
                        curr_xlogoff += bytes_left;
                        fwrite(remain_buffer, 1, bytes_left, wal_file);
                        if (wal_shipping_file != NULL)
                        {
                           fwrite(remain_buffer, 1, bytes_left, wal_shipping_file);
                        }
                        bytes_left = 0;
                        free(remain_buffer);
                     }
                  }
               }
               else if (curr_xlogoff != xlogoff)
               {
                  pgmoneta_log_error("Received WAL record offset %08x, expected %08x", xlogoff, curr_xlogoff);
                  goto error;
               }
               bytes_left = msg->length - hdrlen;
               int bytes_written = 0;
               // write to the wal file
               while (bytes_left > 0)
               {
                  int bytes_to_write = 0;
                  if (xlogoff + bytes_left > segsize)
                  {
                     // do not write across the segment boundary
                     bytes_to_write = segsize - xlogoff;
                  }
                  else
                  {
                     bytes_to_write = bytes_left;
                  }
                  if (bytes_to_write != fwrite(msg->data + hdrlen + bytes_written, 1, bytes_to_write, wal_file))
                  {
                     pgmoneta_log_error("Could not write %d bytes to WAL file %s", bytes_to_write, filename);
                     goto error;
                  }
                  if (wal_shipping_file != NULL)
                  {
                     fwrite(msg->data + hdrlen + bytes_written, 1, bytes_to_write, wal_shipping_file);
                  }

                  bytes_written += bytes_to_write;
                  bytes_left -= bytes_to_write;
                  xlogptr += bytes_written;
                  xlogoff += bytes_written;
                  curr_xlogoff += bytes_written;

                  if (wal_xlog_offset(xlogptr, segsize) == 0)
                  {
                     // the end of WAL segment
                     fflush(wal_file);
                     wal_close(d, filename, false, wal_file);
                     wal_file = NULL;
                     if (wal_shipping_file != NULL)
                     {
                        fflush(wal_shipping_file);
                        wal_close(wal_shipping, filename, false, wal_shipping_file);
                        wal_shipping_file = NULL;
                     }
                     free(filename);
                     filename = NULL;

                     xlogoff = 0;
                     curr_xlogoff = 0;

                     if (bytes_left > 0)
                     {
                        // save the rest of the data for the next wal segment
                        remain_buffer = malloc(bytes_left);
                        memset(remain_buffer, 0, bytes_left);
                        memcpy(remain_buffer, msg->data + bytes_written, bytes_left);
                     }
                     break;
                  }
               }
               wal_send_status_report(socket, xlogptr, xlogptr, 0);
               break;
            }
            case 'k':
            {
               // keep alive request
               wal_send_status_report(socket, xlogptr, xlogptr, 0);
               break;
            }
            default:
               // shouldn't be here
               pgmoneta_log_error("Unrecognized CopyData type %c", type);
               goto error;
         }
      }
      else if (msg->kind == 'c')
      {
         // handle CopyDone
         pgmoneta_send_copy_done_message(socket);
         if (wal_file != NULL)
         {
            // Next file would be at a new timeline, so we treat the current wal file completed
            wal_close(d, filename, false, wal_file);
            wal_file = NULL;
            wal_close(wal_shipping, filename, false, wal_shipping_file);
            wal_shipping_file = NULL;
         }
         break;
      }
   }
   // there should be two CommandComplete messages, receive them
   while (config->running && cnt < 2)
   {
      if (pgmoneta_consume_copy_stream(socket, buffer, &msg) != MESSAGE_STATUS_OK || msg->kind == 'E' || msg->kind == 'f')
      {
         goto error;
      }
      if (msg->kind == 'C')
      {
         cnt++;
      }
   }

   config->servers[srv].wal_streaming = false;
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   if (wal_file != NULL)
   {
      bool partial = (wal_xlog_offset(xlogptr, segsize) != 0);
      wal_close(d, filename, partial, wal_file);
      wal_close(wal_shipping, filename, partial, wal_shipping_file);
   }

   pgmoneta_memory_destroy();
   pgmoneta_stop_logging();

   pgmoneta_free_copy_message(identify_system_msg);
   pgmoneta_free_copy_message(start_replication_msg);
   pgmoneta_free_copy_message(msg);
   pgmoneta_free_query_response(identify_system_response);
   pgmoneta_memory_stream_buffer_free(buffer);

   free(d);
   free(wal_shipping);
   free(filename);
   free(xlogpos);
   exit(0);

error:
   config->servers[srv].wal_streaming = false;
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }

   if (wal_file != NULL)
   {
      wal_close(d, filename, true, wal_file);
      wal_close(wal_shipping, filename, true, wal_shipping_file);
   }
   pgmoneta_free_copy_message(identify_system_msg);
   pgmoneta_free_copy_message(start_replication_msg);
   pgmoneta_free_query_response(identify_system_response);
   pgmoneta_memory_stream_buffer_free(buffer);

   pgmoneta_memory_destroy();
   pgmoneta_stop_logging();

   free(d);
   free(wal_shipping);
   free(filename);
   free(xlogpos);
   exit(1);
}

static char*
wal_file_name(int timeline, size_t segno, int segsize)
{
   char hex[128];
   char* f = NULL;

   memset(&hex[0], 0, sizeof(hex));
   int segments_per_id = 0x100000000ULL / segsize;
   int seg_id = segno / segments_per_id;
   int seg_offset = segno % segments_per_id;

   snprintf(&hex[0], sizeof(hex), "%08X%08X%08X", timeline, seg_id, seg_offset);
   f = pgmoneta_append(f, hex);
   return f;
}

static FILE*
wal_open(char* root, char* filename, int segsize)
{
   if (root == NULL || strlen(root) == 0 || !pgmoneta_exists(root))
   {
      return NULL;
   }
   char* path = NULL;
   FILE* file = NULL;
   path = pgmoneta_append(path, root);
   if (!pgmoneta_ends_with(path, "/"))
   {
      path = pgmoneta_append(path, "/");
   }

   path = pgmoneta_append(path, filename);
   path = pgmoneta_append(path, ".partial");

   if (pgmoneta_exists(path))
   {
      // file alreay exists, check if it's padded already
      size_t size = pgmoneta_get_file_size(path);
      if (size == segsize)
      {
         file = fopen(path, "r+b");
         if (file == NULL)
         {
            pgmoneta_log_error("WAL error: %s", strerror(errno));
            errno = 0;
            goto error;
         }
         pgmoneta_permission(path, 6, 0, 0);

         free(path);
         return file;
      }
      if (size != 0)
      {
         // corrupted file
         pgmoneta_log_error("WAL file corrupted: %s", path);
         goto error;
      }
   }

   file = fopen(path, "wb");

   if (file == NULL)
   {
      pgmoneta_log_error("WAL error: %s", strerror(errno));
      errno = 0;
      goto error;
   }

   if (wal_prepare(file, segsize))
   {
      goto error;
   }

   pgmoneta_permission(path, 6, 0, 0);

   free(path);
   return file;

error:
   if (file != NULL)
   {
      fclose(file);
   }
   free(path);
   return NULL;
}

static int
wal_close(char* root, char* filename, bool partial, FILE* file)
{
   if (file == NULL || root == NULL || filename == NULL || strlen(root) == 0 || strlen(filename) == 0)
   {
      return 1;
   }
   char tmp_file_path[MAX_PATH] = {0};
   char file_path[MAX_PATH] = {0};

   if (partial)
   {
      pgmoneta_log_warn("Not renaming %s.partial, this segment is incomplete", filename);
      fclose(file);
      return 0;
   }

   if (pgmoneta_ends_with(root, "/"))
   {
      snprintf(tmp_file_path, sizeof(tmp_file_path), "%s%s.partial", root, filename);
      snprintf(file_path, sizeof(file_path), "%s%s", root, filename);
   }
   else
   {
      snprintf(tmp_file_path, sizeof(tmp_file_path), "%s/%s.partial", root, filename);
      snprintf(file_path, sizeof(file_path), "%s/%s", root, filename);
   }
   if (rename(tmp_file_path, file_path) != 0)
   {
      pgmoneta_log_error("could not rename file %s to %s", tmp_file_path, file_path);
      goto error;
   }

   fclose(file);

   return 0;

error:
   fclose(file);
   return 1;
}

static int
wal_prepare(FILE* file, int segsize)
{
   char buffer[8192] = {0};
   size_t written = 0;

   if (file == NULL)
   {
      return 1;
   }

   while (written < segsize)
   {
      written += fwrite(buffer, 1, sizeof(buffer), file);
   }

   fflush(file);
   if (fseek(file, 0, SEEK_SET) != 0)
   {
      pgmoneta_log_error("WAL error: %s", strerror(errno));
      errno = 0;
      return 1;
   }
   return 0;
}

static int
wal_send_status_report(int socket, int64_t received, int64_t flushed, int64_t applied)
{
   struct message* status_report_msg = NULL;
   pgmoneta_create_standby_status_update_message(received, flushed, applied, &status_report_msg);

   if (pgmoneta_write_message(NULL, socket, status_report_msg) != MESSAGE_STATUS_OK)
   {
      goto error;
   }
   pgmoneta_free_copy_message(status_report_msg);
   return 0;

error:
   pgmoneta_free_copy_message(status_report_msg);
   return 1;
}

static int
wal_xlog_offset(size_t xlogptr, int segsize)
{
   // this function assumes that segsize is a power of 2
   return xlogptr & (segsize - 1);
}

static int
wal_convert_xlogpos(char* xlogpos, int* high32, int* low32, int segsize)
{
   char* ptr = NULL;
   int num = 0;
   if (xlogpos == NULL || !pgmoneta_contains(xlogpos, "/"))
   {
      pgmoneta_log_error("WAL unable to convert xlogpos");
      return 1;
   }
   ptr = strtok(xlogpos, "/");
   sscanf(ptr, "%x", &num);
   *high32 = num;

   ptr = strtok(NULL, "/");
   sscanf(ptr, "%x", &num);
   *low32 = num & (~(segsize - 1));
   return 0;
}

static int
wal_shipping_setup(int srv, char** wal_shipping)
{
   char* ws = NULL;
   ws = pgmoneta_get_server_wal_shipping_wal(srv);
   if (ws != NULL)
   {
      if (pgmoneta_mkdir(ws))
      {
         *wal_shipping = NULL;
         return 1;
      }
      *wal_shipping = ws;
      return 0;
   }
   *wal_shipping = NULL;
   return 0;
}