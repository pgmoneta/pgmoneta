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
#include <logging.h>
#include <management.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <prometheus.h>
#include <security.h>
#include <server.h>
#include <wal.h>
#include <workflow.h>
#include <utils.h>
#include <storage.h>
#include <io.h>

/* system */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <ev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <openssl/ssl.h>
#include <libssh/libssh.h>
#include <libssh/sftp.h>

static char* wal_file_name(uint32_t timeline, size_t segno, int segsize);
static int wal_fetch_history(char* basedir, int timeline, SSL* ssl, int socket);
static FILE* wal_open(char* root, char* filename, int segsize);
static int wal_close(char* root, char* filename, bool partial, FILE* file);
static int wal_prepare(FILE* file, int segsize);
static int wal_send_status_report(SSL* ssl, int socket, int64_t received, int64_t flushed, int64_t applied);
static int wal_xlog_offset(size_t xlogptr, int segsize);
static int wal_convert_xlogpos(char* xlogpos, uint32_t* high32, uint32_t* low32, int segsize);
static int wal_find_streaming_start(char* basedir, uint32_t* timeline, uint32_t* high32, uint32_t* low32, int segsize);
static int wal_read_replication_slot(SSL* ssl, int socket, char* slot, char* name, uint32_t* high32, uint32_t* low32, uint32_t* timeline, int segsize);
static int wal_shipping_setup(int srv, char** wal_shipping);
static void update_wal_lsn(int srv, size_t xlogptr);

void
pgmoneta_wal(int srv, char** argv)
{
   int usr;
   int auth;
   SSL* ssl = NULL;
   int socket = -1;
   uint32_t high32 = 0;
   uint32_t low32 = 0;
   char* d = NULL;
   char* wal_shipping = NULL;
   uint32_t timeline = 0;
   uint32_t cur_timeline = 0;
   int hdrlen = 1 + 8 + 8 + 8;
   int bytes_left = 0;
   char* xlogpos = NULL;
   char* remain_buffer = NULL;
   size_t remain_buffer_alloc_size = 0;
   char cmd[MISC_LENGTH];
   size_t xlogpos_size = 0;
   size_t xlogptr = 0;
   size_t segno;
   int xlogoff;
   int curr_xlogoff = 0;
   int segsize;
   int read_replication = 1;
   char* filename = NULL;
   signed char type;
   int ret;
   char date[128];
   time_t current_time;
   struct tm* time_info;
   FILE* wal_file = NULL;
   FILE* wal_shipping_file = NULL;
   sftp_file sftp_wal_file = NULL;
   struct message* identify_system_msg = NULL;
   struct query_response* identify_system_response = NULL;
   struct query_response* end_of_timeline_response = NULL;
   struct message* start_replication_msg = NULL;
   struct message* msg = (struct message*)malloc(sizeof (struct message));
   struct configuration* config;
   struct stream_buffer* buffer = NULL;
   struct workflow* head = NULL;
   struct workflow* current = NULL;
   struct node* i_nodes = NULL;
   struct node* o_nodes = NULL;

   config = (struct configuration*) shmem;

   if (msg == NULL)
   {
      goto error;
   }

   memset(msg, 0, sizeof (struct message));

   pgmoneta_start_logging();
   pgmoneta_memory_init();

   pgmoneta_set_proc_title(1, argv, "wal", config->servers[srv].name);

   memset(&date[0], 0, sizeof(date));
   time(&current_time);
   time_info = localtime(&current_time);
   strftime(&date[0], sizeof(date), "%Y%m%d%H%M%S", time_info);

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

   if (config->storage_engine & STORAGE_ENGINE_SSH)
   {
      head = pgmoneta_storage_create_ssh(WORKFLOW_TYPE_WAL_SHIPPING);
      current = head;
   }

   current = head;
   while (current != NULL)
   {
      if (current->setup(srv, &date[0], i_nodes, &o_nodes))
      {
         goto error;
      }
      current = current->next;
   }

   current = head;
   while (current != NULL)
   {
      if (current->execute(srv, &date[0], i_nodes, &o_nodes))
      {
         goto error;
      }
      current = current->next;
   }

   // Setup WAL shipping directory
   if (wal_shipping_setup(srv, &wal_shipping))
   {
      pgmoneta_log_warn("Unable to create WAL shipping directory");
   }

   auth = pgmoneta_server_authenticate(srv, "postgres", config->users[usr].username, config->users[usr].password, true, &ssl, &socket);

   if (auth != AUTH_SUCCESS)
   {
      pgmoneta_log_error("Authentication failed for user %s on %s", config->users[usr].username, config->servers[srv].name);
      goto error;
   }

   pgmoneta_memory_stream_buffer_init(&buffer);

   config->servers[srv].wal_streaming = true;
   pgmoneta_create_identify_system_message(&identify_system_msg);
   if (pgmoneta_query_execute(ssl, socket, identify_system_msg, &identify_system_response))
   {
      pgmoneta_log_error("Error occurred when executing IDENTIFY_SYSTEM");
      goto error;
   }

   cur_timeline = atoi(pgmoneta_query_response_get_data(identify_system_response, 1));
   if (cur_timeline < 1)
   {
      pgmoneta_log_error("identify system: timeline should at least be 1, getting %d", timeline);
      goto error;
   }
   config->servers[srv].cur_timeline = cur_timeline;

   wal_find_streaming_start(d, &timeline, &high32, &low32, segsize);
   if (timeline == 0)
   {
      read_replication = (config->servers[srv].version >= 15) ? 1 : 0;

      // query the replication slot to gey the starting LSN and timeline ID
      if (read_replication)
      {
         if (wal_read_replication_slot(ssl, socket, config->servers[srv].wal_slot, config->servers[srv].name, &high32, &low32, &timeline, segsize))
         {
            read_replication = 0;   // Fallback if not PostgreSQL 15+
         }
      }

      // use current xlogpos as last resort
      if (!read_replication)
      {
         timeline = cur_timeline;
         xlogpos_size = strlen(pgmoneta_query_response_get_data(identify_system_response, 2)) + 1;
         xlogpos = (char*)malloc(xlogpos_size);

         if (xlogpos == NULL)
         {
            goto error;
         }
         memset(xlogpos, 0, xlogpos_size);
         memcpy(xlogpos, pgmoneta_query_response_get_data(identify_system_response, 2), xlogpos_size);
         if (wal_convert_xlogpos(xlogpos, &high32, &low32, segsize))
         {
            goto error;
         }
         free(xlogpos);
         xlogpos = NULL;
      }
   }

   pgmoneta_free_query_response(identify_system_response);
   identify_system_response = NULL;

   while (config->running)
   {
      if (wal_fetch_history(d, timeline, ssl, socket))
      {
         pgmoneta_log_error("Error occurred when fetching .history file");
         goto error;
      }

      snprintf(cmd, sizeof(cmd), "%X/%X", high32, low32);

      pgmoneta_create_start_replication_message(cmd, timeline, config->servers[srv].wal_slot, &start_replication_msg);

      ret = pgmoneta_write_message(ssl, socket, start_replication_msg);

      if (ret != MESSAGE_STATUS_OK)
      {
         pgmoneta_log_error("Error during START_REPLICATION for server %s", config->servers[srv].name);
         goto error;
      }

      // assign xlogpos at the beginning of the streaming to LSN
      memset(config->servers[srv].current_wal_lsn, 0, MISC_LENGTH);
      snprintf(config->servers[srv].current_wal_lsn, MISC_LENGTH, "%s", cmd);

      type = 0;

      // wait for the CopyBothResponse message

      while (config->running && (msg == NULL || type != 'W'))
      {
         ret = pgmoneta_consume_copy_stream_start(ssl, socket, buffer, msg, NULL);
         if (ret != 1)
         {
            pgmoneta_log_error("Error occurred when starting stream replication");
            goto error;
         }
         type = msg->kind;
         if (type == 'E')
         {
            pgmoneta_log_error("Error occurred when starting stream replication");
            pgmoneta_log_error_response_message(msg);
            goto error;
         }
         pgmoneta_consume_copy_stream_end(buffer, msg);
      }

      type = 0;

      // start streaming current timeline's WAL segments
      while (config->running)
      {
         ret = pgmoneta_consume_copy_stream_start(ssl, socket, buffer, msg, NULL);
         if (ret == 0)
         {
            break;
         }
         if (ret != MESSAGE_STATUS_OK)
         {
            goto error;
         }

         if (msg == NULL)
         {
            pgmoneta_log_error("wal: received NULL message");
            goto error;
         }

         if (msg->kind == 'E' || msg->kind == 'f')
         {
            pgmoneta_log_info("received error response message");
            pgmoneta_log_copyfail_message(msg);
            pgmoneta_log_error_response_message(msg);
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
                        memset(config->servers[srv].current_wal_filename, 0, MISC_LENGTH);
                        snprintf(config->servers[srv].current_wal_filename, MISC_LENGTH, "%s.partial", filename);
                        if ((wal_shipping_file = wal_open(wal_shipping, filename, segsize)) == NULL)
                        {
                           if (wal_shipping != NULL)
                           {
                              pgmoneta_log_warn("Could not create or open WAL segment file at %s", wal_shipping);
                           }
                        }
                        if (config->storage_engine & STORAGE_ENGINE_SSH)
                        {
                           if (pgmoneta_sftp_wal_open(srv, filename, segsize, &sftp_wal_file) == 1)
                           {
                              pgmoneta_log_error("Could not create or open WAL segment file on remote ssh storage engine");
                              goto error;
                           }
                        }

                        if (bytes_left > 0)
                        {
                           curr_xlogoff += bytes_left;
                           pgmoneta_write_file(remain_buffer, 1, bytes_left, wal_file);
                           if (sftp_wal_file != NULL)
                           {
                              sftp_write(sftp_wal_file, remain_buffer, bytes_left);
                           }
                           if (wal_shipping_file != NULL)
                           {
                              pgmoneta_write_file(remain_buffer, 1, bytes_left, wal_shipping_file);
                           }
                           bytes_left = 0;
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
                     if (bytes_to_write != pgmoneta_write_file(msg->data + hdrlen + bytes_written, 1, bytes_to_write, wal_file))
                     {
                        pgmoneta_log_error("Could not write %d bytes to WAL file %s", bytes_to_write, filename);
                        goto error;
                     }
                     if (sftp_wal_file != NULL)
                     {
                        sftp_write(sftp_wal_file, msg->data + hdrlen + bytes_written, bytes_to_write);
                     }

                     if (wal_shipping_file != NULL)
                     {
                        pgmoneta_write_file(msg->data + hdrlen + bytes_written, 1, bytes_to_write, wal_shipping_file);
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
                        if (sftp_wal_file != NULL)
                        {
                           pgmoneta_sftp_wal_close(srv, filename, false, &sftp_wal_file);
                           sftp_wal_file = NULL;
                        }

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
                           /* Save the rest of the data for the next WAL segment */
                           if (remain_buffer == NULL)
                           {
                              remain_buffer = malloc(bytes_left);
                              remain_buffer_alloc_size = bytes_left;
                           }
                           else if (bytes_left > remain_buffer_alloc_size)
                           {
                              remain_buffer = realloc(remain_buffer, bytes_left);
                              remain_buffer_alloc_size = bytes_left;
                           }
                           memset(remain_buffer, 0, remain_buffer_alloc_size);
                           memcpy(remain_buffer, msg->data + bytes_written, bytes_left);
                        }
                        break;
                     }
                  }
                  // update LSN after a message data is written to the segment
                  update_wal_lsn(srv, xlogptr);

                  wal_send_status_report(ssl, socket, xlogptr, xlogptr, 0);
                  break;
               }
               case 'k':
               {
                  // keep alive request
                  wal_send_status_report(ssl, socket, xlogptr, xlogptr, 0);
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
            pgmoneta_send_copy_done_message(ssl, socket);
            if (wal_file != NULL)
            {
               // Next file would be at a new timeline, so we treat the current wal file completed
               wal_close(d, filename, false, wal_file);
               wal_file = NULL;
               wal_close(wal_shipping, filename, false, wal_shipping_file);
               wal_shipping_file = NULL;
               if (sftp_wal_file != NULL)
               {
                  pgmoneta_sftp_wal_close(srv, filename, false, &sftp_wal_file);
                  sftp_wal_file = NULL;
               }
            }
            pgmoneta_consume_copy_stream_end(buffer, msg);
            break;
         }
         pgmoneta_consume_copy_stream_end(buffer, msg);
      }
      // there should be a DataRow message followed by a CommandComplete messages,
      // receive them and parse the next timeline and xlogpos from it
      if (!config->running)
      {
         break;
      }
      pgmoneta_consume_data_row_messages(ssl, socket, buffer, &end_of_timeline_response);
      timeline = atoi(pgmoneta_query_response_get_data(end_of_timeline_response, 0));
      xlogpos = pgmoneta_query_response_get_data(end_of_timeline_response, 1);
      if (wal_convert_xlogpos(xlogpos, &high32, &low32, segsize))
      {
         goto error;
      }
      xlogpos = NULL;
      // receive the last command complete message
      msg->kind = '\0';
      while (config->running && msg->kind != 'C')
      {
         pgmoneta_consume_copy_stream_start(ssl, socket, buffer, msg, NULL);
         pgmoneta_consume_copy_stream_end(buffer, msg);
      }

      pgmoneta_free_query_response(end_of_timeline_response);
      end_of_timeline_response = NULL;
      pgmoneta_free_copy_message(start_replication_msg);
      start_replication_msg = NULL;
   }

   config->servers[srv].wal_streaming = false;
   pgmoneta_close_ssl(ssl);
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   if (wal_file != NULL)
   {
      bool partial = (wal_xlog_offset(xlogptr, segsize) != 0);
      wal_close(d, filename, partial, wal_file);
      wal_close(wal_shipping, filename, partial, wal_shipping_file);
      if (sftp_wal_file != NULL)
      {
         pgmoneta_sftp_wal_close(srv, filename, partial, &sftp_wal_file);
         sftp_wal_file = NULL;
      }
   }

   current = head;
   while (current != NULL)
   {
      current->teardown(srv, &date[0], i_nodes, &o_nodes);

      current = current->next;
   }

   pgmoneta_memory_destroy();
   pgmoneta_stop_logging();

   pgmoneta_free_copy_message(identify_system_msg);
   pgmoneta_free_copy_message(start_replication_msg);
   if (msg != NULL)
   {
      msg->data = NULL;
   }
   pgmoneta_free_copy_message(msg);
   pgmoneta_free_query_response(identify_system_response);
   pgmoneta_free_query_response(end_of_timeline_response);
   pgmoneta_memory_stream_buffer_free(buffer);

   free(remain_buffer);
   free(d);
   free(wal_shipping);
   free(filename);
   free(xlogpos);
   exit(0);

error:
   config->servers[srv].wal_streaming = false;
   pgmoneta_close_ssl(ssl);
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }

   if (wal_file != NULL)
   {
      wal_close(d, filename, true, wal_file);
      wal_close(wal_shipping, filename, true, wal_shipping_file);
   }
   if (sftp_wal_file != NULL)
   {
      pgmoneta_sftp_wal_close(srv, filename, true, &sftp_wal_file);
      sftp_wal_file = NULL;
   }
   pgmoneta_free_copy_message(identify_system_msg);
   pgmoneta_free_copy_message(start_replication_msg);
   if (msg != NULL)
   {
      msg->data = NULL;
   }
   pgmoneta_free_copy_message(msg);
   pgmoneta_free_query_response(identify_system_response);
   pgmoneta_free_query_response(end_of_timeline_response);
   pgmoneta_memory_stream_buffer_free(buffer);

   current = head;
   while (current != NULL)
   {
      current->teardown(srv, &date[0], i_nodes, &o_nodes);

      current = current->next;
   }

   pgmoneta_memory_destroy();
   pgmoneta_stop_logging();

   free(remain_buffer);
   free(d);
   free(wal_shipping);
   free(filename);
   free(xlogpos);
   exit(1);
}

static int
wal_read_replication_slot(SSL* ssl, int socket, char* slot, char* name, uint32_t* high32, uint32_t* low32, uint32_t* timeline, int segsize)
{
   struct message* read_slot_msg = NULL;
   struct query_response* read_slot_response = NULL;
   char* lsn = NULL;
   int status = 0;
   uint32_t local_timeline = 0;

   pgmoneta_create_read_replication_slot_message(slot, &read_slot_msg);
   status = pgmoneta_query_execute(ssl, socket, read_slot_msg, &read_slot_response);

   if (status != 0)
   {
      pgmoneta_log_error("Error occurred when executing READ_REPLICATION_SLOT for slot %s on server %s", slot, name);
      goto error;
   }

   if (read_slot_response->number_of_columns < 3)
   {
      pgmoneta_log_error("Invalid response from READ_REPLICATION_SLOT for slot %s on server %s", slot, name);
      goto error;
   }

   local_timeline = atoi(pgmoneta_query_response_get_data(read_slot_response, 2));
   if (local_timeline < 1)
   {
      pgmoneta_log_error("Error occurred when reading replication slot on server %s: timeline should at least be 1, but getting %d", name, local_timeline);
      goto error;
   }

   lsn = pgmoneta_query_response_get_data(read_slot_response, 1);
   if (wal_convert_xlogpos(lsn, high32, low32, segsize))
   {
      pgmoneta_log_error("Failed to convert LSN from replication slot %s on server %s", slot, name);
      goto error;
   }

   *timeline = local_timeline;

   pgmoneta_free_query_response(read_slot_response);
   pgmoneta_free_copy_message(read_slot_msg);

   return 0;

error:
   pgmoneta_free_query_response(read_slot_response);
   pgmoneta_free_copy_message(read_slot_msg);
   return 1;
}

static void
update_wal_lsn(int srv, size_t xlogptr)
{
   struct configuration* config = (struct configuration*) shmem;
   uint32_t low32 = xlogptr & 0xffffffff;
   uint32_t high32 = xlogptr >> 32 & 0xffffffff;
   memset(config->servers[srv].current_wal_lsn, 0, MISC_LENGTH);
   snprintf(config->servers[srv].current_wal_lsn, MISC_LENGTH, "%X/%X", high32, low32);
}

int
pgmoneta_get_timeline_history(int srv, uint32_t tli, struct timeline_history** history)
{
   struct timeline_history* h = NULL;
   struct timeline_history* curh = NULL;
   struct timeline_history* nexth = NULL;
   char filename[MISC_LENGTH];
   char* path = NULL;
   char buffer[MAX_PATH];
   int numfields = 0;
   FILE* file = NULL;

   if (tli == 1)
   {
      return 0;
   }

   snprintf(filename, sizeof(filename), "%08X.history", tli);
   path = pgmoneta_get_server_wal(srv);
   path = pgmoneta_append(path, filename);
   file = pgmoneta_open_file(path, "r");
   if (file == NULL)
   {
      pgmoneta_log_error("Unable to open history file: %s", strerror(errno));
      goto error;
   }
   memset(buffer, 0, sizeof(buffer));
   while (fgets(buffer, sizeof(buffer), file) != NULL)
   {
      char* ptr = buffer;
      // ignore empty spaces
      while (*ptr != '\0' && isspace(*ptr))
      {
         ptr++;
      }
      // ignore empty lines and comments
      if (*ptr == '\0' || *ptr == '#')
      {
         continue;
      }
      nexth = (struct timeline_history*) malloc(sizeof(struct timeline_history));

      if (nexth == NULL)
      {
         goto error;
      }

      memset(nexth, 0, sizeof(struct timeline_history));
      if (h == NULL)
      {
         curh = nexth;
         h = curh;
      }
      else
      {
         curh->next = nexth;
         curh = curh->next;
      }
      numfields = sscanf(ptr, "%u\t%X/%X", &curh->parent_tli, &curh->switchpos_hi, &curh->switchpos_lo);
      if (numfields != 3)
      {
         pgmoneta_log_error("error parsing history file %s", filename);
         goto error;
      }
      memset(buffer, 0, sizeof(buffer));
   }

   *history = h;

   free(path);
   if (file != NULL)
   {
      fclose(file);
   }
   return 0;

error:
   free(path);
   if (file != NULL)
   {
      fclose(file);
   }
   pgmoneta_free_timeline_history(h);
   return 1;
}

void
pgmoneta_free_timeline_history(struct timeline_history* history)
{
   struct timeline_history* h = history;
   while (h != NULL)
   {
      struct timeline_history* next = h->next;
      free(h);
      h = next;
   }
}

static char*
wal_file_name(uint32_t timeline, size_t segno, int segsize)
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

static int
wal_fetch_history(char* basedir, int timeline, SSL* ssl, int socket)
{
   struct message* timeline_history_msg = NULL;
   struct query_response* timeline_history_response = NULL;
   FILE* history_file = NULL;
   char* history_content = NULL;
   char path[MAX_PATH];

   if (basedir == NULL || strlen(basedir) == 0 || !pgmoneta_exists(basedir))
   {
      pgmoneta_log_error("base directory for history file does not exist");
      goto error;
   }

   memset(path, 0, sizeof(path));
   if (pgmoneta_ends_with(basedir, "/"))
   {
      snprintf(path, sizeof(path), "%s%08x.history", basedir, timeline);
   }

   // do nothing if the corresponding .history already exists, or current timeline is 1
   if (timeline == 1 || pgmoneta_exists(path))
   {
      return 0;
   }

   pgmoneta_create_timeline_history_message(timeline, &timeline_history_msg);
   if (pgmoneta_query_execute(ssl, socket, timeline_history_msg, &timeline_history_response))
   {
      pgmoneta_log_error("Error occurred when executing TIMELINE_HISTORY %d", timeline);
      goto error;
   }

   history_content = pgmoneta_query_response_get_data(timeline_history_response, 1);

   history_file = pgmoneta_open_file(path, "wb");
   if (history_file == NULL)
   {
      goto error;
   }
   pgmoneta_write_file(history_content, 1, strlen(history_content) + 1, history_file);
   fflush(history_file);

   pgmoneta_free_copy_message(timeline_history_msg);
   pgmoneta_free_query_response(timeline_history_response);
   if (history_file != NULL)
   {
      fclose(history_file);
   }
   return 0;

error:
   pgmoneta_free_copy_message(timeline_history_msg);
   pgmoneta_free_query_response(timeline_history_response);
   if (history_file != NULL)
   {
      fclose(history_file);
   }
   return 1;
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
         file = pgmoneta_open_file(path, "r+b");
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

   file = pgmoneta_open_file(path, "wb");

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
      written += pgmoneta_write_file(buffer, 1, sizeof(buffer), file);
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
wal_send_status_report(SSL* ssl, int socket, int64_t received, int64_t flushed, int64_t applied)
{
   struct message* status_report_msg = NULL;
   pgmoneta_create_standby_status_update_message(received, flushed, applied, &status_report_msg);

   if (pgmoneta_write_message(ssl, socket, status_report_msg) != MESSAGE_STATUS_OK)
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
wal_convert_xlogpos(char* xlogpos, uint32_t* high32, uint32_t* low32, int segsize)
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
   // discard in-segment offset
   *low32 = num & (~(segsize - 1));
   return 0;
}

// this function assumes basedir only contains wal segments and .history files
static int
wal_find_streaming_start(char* basedir, uint32_t* timeline, uint32_t* high32, uint32_t* low32, int segsize)
{
   char* segname = NULL;
   char* pos = NULL;
   DIR* dir;
   struct dirent* entry;
   bool high_is_partial = false;
   bool is_partial = false;

   dir = opendir(basedir);

   if (dir == NULL)
   {
      pgmoneta_log_error("Could not open wal base directory %s", basedir);
      goto error;
   }

   // read the wal directory, get the latest wal segment
   while ((entry = readdir(dir)) != NULL)
   {
      // we only care about files
      if (entry->d_type != DT_REG)
      {
         continue;
      }
      // ignore history files here
      if (pgmoneta_ends_with(entry->d_name, ".history"))
      {
         continue;
      }
      // find the latest wal segments
      // By latest we mean it has a larger xlogpos. If the xlogpos is the same(unlikely),
      // the one that's not partial is more up-to-date
      is_partial = pgmoneta_ends_with(entry->d_name, ".partial");
      if (segname == NULL || strcmp(entry->d_name, segname) > 0 || (strcmp(entry->d_name, segname) == 0 && !is_partial))
      {
         segname = entry->d_name;
         high_is_partial = is_partial;
      }
   }
   if (segname == NULL)
   {
      *timeline = 0;
      *high32 = 0;
      *low32 = 0;
      closedir(dir);
      return 0;
   }

   // remove the suffix
   pos = strtok(segname, ".");
   sscanf(pos, "%08X%08X%08X", timeline, high32, low32);

   // high32 is the segment id, low32 is the number of segments
   int segments_per_id = 0x100000000ULL / segsize;
   // if the latest wal segment is partial, we start with this one
   // otherwise we start with the next one
   if (!high_is_partial)
   {
      // handle possible overflow
      if (*low32 == segments_per_id)
      {
         *low32 = 0;
         *high32 = *high32 + 1;
      }
      else
      {
         *low32 = *low32 + 1;
      }
   }
   *low32 *= segsize;

   closedir(dir);
   return 0;

error:
   if (dir != NULL)
   {
      closedir(dir);
   }
   return 1;
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
