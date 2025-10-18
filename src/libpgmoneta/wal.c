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
#include <logging.h>
#include <network.h>
#include <security.h>
#include <server.h>
#include <storage.h>
#include <utils.h>
#include <wal.h>

/* system */
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <ev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <libssh/libssh.h>
#include <libssh/sftp.h>
#include <openssl/ssl.h>

int mappings_size = 0;
oid_mapping* oidMappings = NULL;
bool enable_translation = false;

static int wal_fetch_history(char* basedir, int timeline, SSL* ssl, int socket);
static FILE* wal_open(char* root, char* filename, int segsize);
static int wal_close(char* root, char* filename, bool partial, FILE* file);
static int wal_prepare(FILE* file, int segsize);
static int wal_send_status_report(SSL* ssl, int socket, int64_t received, int64_t flushed, int64_t applied);
static int wal_xlog_offset(size_t xlogptr, int segsize);
static int wal_convert_xlogpos(char* xlogpos, int segsize, uint32_t* high32, uint32_t* low32);
static int wal_find_streaming_start(char* basedir, int segsize, uint32_t* timeline, uint32_t* high32, uint32_t* low32);
static int wal_read_replication_slot(SSL* ssl, int socket, char* slot, char* name, int segsize, uint32_t* high32, uint32_t* low32, uint32_t* timeline);
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
   size_t bytes_left = 0;
   char* xlogpos = NULL;
   char cmd[MISC_LENGTH];
   size_t xlogpos_size = 0;
   size_t xlogptr = 0;
   size_t segno;
   size_t xlogoff;
   size_t curr_xlogoff = 0;
   size_t segsize;
   int read_replication = 1;
   char* filename = NULL;
   signed char type;
   int ret;
   FILE* wal_file = NULL;
   FILE* wal_shipping_file = NULL;
   sftp_file sftp_wal_file = NULL;
   struct message* identify_system_msg = NULL;
   struct query_response* identify_system_response = NULL;
   struct query_response* end_of_timeline_response = NULL;
   struct message* start_replication_msg = NULL;
   struct message* msg = (struct message*)malloc(sizeof (struct message));
   struct main_configuration* config;
   struct stream_buffer* buffer = NULL;
   struct workflow* head = NULL;
   struct workflow* current = NULL;
   struct art* nodes = NULL;

   config = (struct main_configuration*) shmem;

   pgmoneta_start_logging();
   pgmoneta_memory_init();

   pgmoneta_set_proc_title(1, argv, "wal", config->common.servers[srv].name);

   if (msg == NULL)
   {
      goto error;
   }

   memset(msg, 0, sizeof(struct message));

   if (!pgmoneta_server_is_online(srv))
   {
      goto error;
   }

   if (config->common.servers[srv].wal_streaming > 0)
   {
      goto error;
   }

   usr = -1;
   for (int i = 0; usr == -1 && i < config->common.number_of_users; i++)
   {
      if (!strcmp(config->common.servers[srv].username, config->common.users[i].username))
      {
         usr = i;
      }
   }

   if (usr == -1)
   {
      pgmoneta_log_trace("Invalid user for %s", config->common.servers[srv].name);
      goto error;
   }

   if (config->common.servers[srv].checksums)
   {
      pgmoneta_log_debug("Server %s has checksums enabled", config->common.servers[srv].name);
   }
   else
   {
      pgmoneta_log_warn("Server %s has checksums disabled. Use initdb -k or pg_checksums to enable", config->common.servers[srv].name);
   }

   segsize = config->common.servers[srv].wal_size;
   d = pgmoneta_get_server_wal(srv);
   pgmoneta_mkdir(d);

   if (pgmoneta_art_create(&nodes))
   {
      goto error;
   }

   if (pgmoneta_art_insert(nodes, NODE_SERVER_ID, (uintptr_t)srv,
                           ValueInt32))
   {
      goto error;
   }

   if (config->storage_engine & STORAGE_ENGINE_SSH)
   {
      head = pgmoneta_storage_create_ssh(WORKFLOW_TYPE_WAL_SHIPPING);
      current = head;
   }

   current = head;
   while (current != NULL)
   {
      if (current->setup(current->name(), nodes))
      {
         goto error;
      }
      current = current->next;
   }

   current = head;
   while (current != NULL)
   {
      if (current->execute(current->name(), nodes))
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

   auth = pgmoneta_server_authenticate(srv, "postgres", config->common.users[usr].username,
                                       config->common.users[usr].password, true, &ssl, &socket);

   if (auth != AUTH_SUCCESS)
   {
      pgmoneta_log_error("Authentication failed for user %s on %s",
                         config->common.users[usr].username,
                         config->common.servers[srv].name);
      goto error;
   }

   pgmoneta_memory_stream_buffer_init(&buffer);

   config->common.servers[srv].wal_streaming = getpid();

   pgmoneta_create_identify_system_message(&identify_system_msg);
   if (pgmoneta_query_execute(ssl, socket, identify_system_msg,
                              &identify_system_response))
   {
      pgmoneta_log_error("Error occurred when executing IDENTIFY_SYSTEM");
      goto error;
   }

   if (identify_system_response == NULL ||
       identify_system_response->number_of_columns < 4)
   {
      goto error;
   }

   cur_timeline = pgmoneta_atoi(
      pgmoneta_query_response_get_data(identify_system_response, 1));
   if (cur_timeline < 1)
   {
      pgmoneta_log_error("identify system: timeline should at least be 1, getting %d", timeline);
      goto error;
   }
   config->common.servers[srv].cur_timeline = cur_timeline;

   wal_find_streaming_start(d, segsize, &timeline, &high32, &low32);
   if (timeline == 0)
   {
      read_replication = (config->common.servers[srv].version >= 15) ? 1 : 0;

      // query the replication slot to get the starting LSN and timeline ID
      if (read_replication)
      {
         if (wal_read_replication_slot(ssl, socket,
                                       config->common.servers[srv].wal_slot,
                                       config->common.servers[srv].name,
                                       segsize, &high32, &low32, &timeline))
         {
            read_replication = 0;    // Fallback if not PostgreSQL 15+
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
         if (wal_convert_xlogpos(xlogpos, segsize, &high32, &low32))
         {
            goto error;
         }
         free(xlogpos);
         xlogpos = NULL;
      }
   }

   pgmoneta_free_query_response(identify_system_response);
   identify_system_response = NULL;

   while (config->running && pgmoneta_server_is_online(srv))
   {
      if (wal_fetch_history(d, timeline, ssl, socket))
      {
         pgmoneta_log_error("Error occurred when fetching .history file");
         goto error;
      }

      snprintf(cmd, sizeof(cmd), "%X/%X", high32, low32);

      pgmoneta_create_start_replication_message(cmd, timeline, config->common.servers[srv].wal_slot,
                                                &start_replication_msg);

      ret = pgmoneta_write_message(ssl, socket, start_replication_msg);

      if (ret != MESSAGE_STATUS_OK)
      {
         pgmoneta_log_error("Error during START_REPLICATION for server %s",
                            config->common.servers[srv].name);
         goto error;
      }

      // assign xlogpos at the beginning of the streaming to LSN
      memset(config->common.servers[srv].current_wal_lsn, 0, MISC_LENGTH);
      snprintf(config->common.servers[srv].current_wal_lsn, MISC_LENGTH, "%s", cmd);

      type = 0;

      // wait for the CopyBothResponse message

      while (config->running && pgmoneta_server_is_online(srv) && (msg == NULL || type != 'W'))
      {
         ret = pgmoneta_consume_copy_stream_start(srv, ssl, socket, buffer, msg, NULL);
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
      while (config->running && pgmoneta_server_is_online(srv))
      {
         ret = pgmoneta_consume_copy_stream_start(srv, ssl, socket, buffer, msg, NULL);
         // the streaming may have stopped because user terminated it
         if (ret == 0 || !config->running || !pgmoneta_server_is_online(srv))
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
            pgmoneta_log_copyfail_message(msg);
            pgmoneta_log_error_response_message(msg);
            goto error;
         }
         if (msg->kind == 'd')
         {
            type = *((char*)msg->data);
            switch (type)
            {
               case 'w': {
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
                     if (xlogoff != 0)
                     {
                        pgmoneta_log_error("Received WAL record of offset %d with no file open", xlogoff);
                        goto error;
                     }
                     // new wal file
                     segno = xlogptr / segsize;
                     curr_xlogoff = 0;
                     filename = pgmoneta_wal_file_name(timeline, segno, segsize);
                     if ((wal_file = wal_open(d, filename, segsize)) == NULL)
                     {
                        pgmoneta_log_error("Could not create or open WAL segment file at %s", d);
                        goto error;
                     }
                     memset(config->common.servers[srv].current_wal_filename, 0, MISC_LENGTH);
                     snprintf(config->common.servers[srv].current_wal_filename, MISC_LENGTH, "%s.partial", filename);
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
                  }
                  else if (curr_xlogoff != xlogoff)
                  {
                     pgmoneta_log_error("Received WAL record offset %08x, expected %08x", xlogoff, curr_xlogoff);
                     goto error;
                  }
                  bytes_left = msg->length - hdrlen;
                  size_t bytes_written = 0;
                  // write to the wal file
                  while (bytes_left > 0)
                  {
                     size_t bytes_to_write = 0;
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
                     fflush(wal_file);

                     if (sftp_wal_file != NULL)
                     {
                        sftp_write(sftp_wal_file, msg->data + hdrlen + bytes_written, bytes_to_write);
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
                           /* Write the rest of the data for the next WAL segment */
                           segno = xlogptr / segsize;
                           curr_xlogoff = 0;
                           filename = pgmoneta_wal_file_name(timeline, segno, segsize);
                           if ((wal_file = wal_open(d, filename, segsize)) == NULL)
                           {
                              pgmoneta_log_error("Could not create or open WAL segment file at %s", d);
                              goto error;
                           }
                           memset(config->common.servers[srv].current_wal_filename, 0, MISC_LENGTH);
                           snprintf(config->common.servers[srv].current_wal_filename, MISC_LENGTH, "%s.partial", filename);
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
                           curr_xlogoff += bytes_left;
                           fwrite(msg->data + hdrlen + bytes_written, 1, bytes_left, wal_file);
                           fflush(wal_file);
                           if (sftp_wal_file != NULL)
                           {
                              sftp_write(sftp_wal_file, msg->data + hdrlen + bytes_written, bytes_left);
                           }
                           if (wal_shipping_file != NULL)
                           {
                              fwrite(msg->data + hdrlen + bytes_written, 1, bytes_left, wal_shipping_file);
                           }
                           bytes_left = 0;
                        }
                        break;
                     }
                  }
                  // update LSN after a message data is written to the segment
                  update_wal_lsn(srv, xlogptr);

                  wal_send_status_report(ssl, socket, xlogptr, xlogptr, 0);
                  break;
               }
               case 'k': {
                  // keep alive request
                  update_wal_lsn(srv, xlogptr);
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
      if (!config->running || !pgmoneta_server_is_online(srv))
      {
         break;
      }
      pgmoneta_consume_data_row_messages(srv, ssl, socket, buffer, &end_of_timeline_response);
      if (end_of_timeline_response == NULL || end_of_timeline_response->number_of_columns < 2)
      {
         goto error;
      }
      timeline = pgmoneta_atoi(pgmoneta_query_response_get_data(end_of_timeline_response, 0));
      xlogpos = pgmoneta_query_response_get_data(end_of_timeline_response, 1);
      if (wal_convert_xlogpos(xlogpos, segsize, &high32, &low32))
      {
         goto error;
      }
      xlogpos = NULL;
      // receive the last command complete message
      msg->kind = '\0';
      while (config->running && pgmoneta_server_is_online(srv) && msg->kind != 'C')
      {
         pgmoneta_consume_copy_stream_start(srv, ssl, socket, buffer, msg, NULL);
         pgmoneta_consume_copy_stream_end(buffer, msg);
      }

      pgmoneta_free_query_response(end_of_timeline_response);
      end_of_timeline_response = NULL;
      pgmoneta_free_message(start_replication_msg);
      start_replication_msg = NULL;
   }

   if (pgmoneta_server_is_online(srv))
   {
      // Send CopyDone message to server to gracefully stop the streaming.
      // Normally we would receive a CopyDone from server as an acknowledgement,
      // but we opt not to wait for it as the system should no longer be running
      pgmoneta_send_copy_done_message(ssl, socket);
   }

   config->common.servers[srv].wal_streaming = -1;
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
      current->teardown(current->name(), nodes);

      current = current->next;
   }

   pgmoneta_server_set_online(srv, false);

   pgmoneta_memory_destroy();
   pgmoneta_stop_logging();

   pgmoneta_free_message(identify_system_msg);
   pgmoneta_free_message(start_replication_msg);
   if (msg != NULL)
   {
      msg->data = NULL;
   }
   pgmoneta_free_message(msg);
   pgmoneta_free_query_response(identify_system_response);
   pgmoneta_free_query_response(end_of_timeline_response);
   pgmoneta_memory_stream_buffer_free(buffer);

   pgmoneta_art_destroy(nodes);

   free(d);
   free(wal_shipping);
   free(filename);
   free(xlogpos);
   exit(0);

error:
   pgmoneta_server_set_online(srv, false);
   config->common.servers[srv].wal_streaming = -1;
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
   pgmoneta_free_message(identify_system_msg);
   pgmoneta_free_message(start_replication_msg);
   if (msg != NULL)
   {
      msg->data = NULL;
   }
   pgmoneta_free_message(msg);
   pgmoneta_free_query_response(identify_system_response);
   pgmoneta_free_query_response(end_of_timeline_response);
   pgmoneta_memory_stream_buffer_free(buffer);

   current = head;
   while (current != NULL)
   {
      current->teardown(current->name(), nodes);

      current = current->next;
   }

   pgmoneta_memory_destroy();
   pgmoneta_stop_logging();

   pgmoneta_art_destroy(nodes);

   free(d);
   free(wal_shipping);
   free(filename);
   free(xlogpos);
   exit(1);
}

static int
wal_read_replication_slot(SSL* ssl, int socket, char* slot, char* name, int segsize, uint32_t* high32, uint32_t* low32, uint32_t* timeline)
{
   struct message* read_slot_msg = NULL;
   struct query_response* read_slot_response = NULL;
   char* lsn = NULL;
   int status = 0;
   uint32_t tli = 0;

   *high32 = 0;
   *low32 = 0;
   *timeline = 0;

   pgmoneta_create_read_replication_slot_message(slot, &read_slot_msg);
   status = pgmoneta_query_execute(ssl, socket, read_slot_msg, &read_slot_response);

   if (status != 0)
   {
      pgmoneta_log_debug("Error occurred when executing READ_REPLICATION_SLOT for slot %s on server %s", slot, name);
      goto error;
   }

   if (read_slot_response == NULL || read_slot_response->number_of_columns < 3)
   {
      pgmoneta_log_debug("Invalid response from READ_REPLICATION_SLOT for slot %s on server %s", slot, name);
      goto error;
   }

   tli = pgmoneta_atoi(pgmoneta_query_response_get_data(read_slot_response, 2));
   if (tli < 1)
   {
      pgmoneta_log_debug("wal_read_replication_slot: timeline is %d, expecting 1 for server %s", tli, name);
      goto error;
   }

   lsn = pgmoneta_query_response_get_data(read_slot_response, 1);
   if (wal_convert_xlogpos(lsn, segsize, high32, low32))
   {
      pgmoneta_log_debug("wal_read_replication_slot: failed to convert LSN from replication slot %s on server %s", slot, name);
      goto error;
   }

   *timeline = tli;

   pgmoneta_free_query_response(read_slot_response);
   pgmoneta_free_message(read_slot_msg);

   return 0;

error:
   pgmoneta_free_query_response(read_slot_response);
   pgmoneta_free_message(read_slot_msg);
   return 1;
}

static void
update_wal_lsn(int srv, size_t xlogptr)
{
   struct main_configuration* config = (struct main_configuration*) shmem;
   uint32_t low32 = xlogptr & 0xffffffff;
   uint32_t high32 = xlogptr >> 32 & 0xffffffff;
   memset(config->common.servers[srv].current_wal_lsn, 0, MISC_LENGTH);
   snprintf(config->common.servers[srv].current_wal_lsn, MISC_LENGTH, "%X/%X", high32, low32);
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
   file = fopen(path, "r");
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

   history_file = fopen(path, "wb");
   if (history_file == NULL)
   {
      goto error;
   }
   fwrite(history_content, 1, strlen(history_content) + 1, history_file);
   fflush(history_file);

   pgmoneta_free_message(timeline_history_msg);
   pgmoneta_free_query_response(timeline_history_response);
   if (history_file != NULL)
   {
      fclose(history_file);
   }
   return 0;

error:
   pgmoneta_free_message(timeline_history_msg);
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
      if (size == (size_t)segsize)
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
      pgmoneta_log_info("Not renaming %s.partial, this segment is incomplete", filename);
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

   while (written < (size_t)segsize)
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
wal_send_status_report(SSL* ssl, int socket, int64_t received, int64_t flushed, int64_t applied)
{
   struct message* status_report_msg = NULL;
   pgmoneta_create_standby_status_update_message(received, flushed, applied, &status_report_msg);

   if (pgmoneta_write_message(ssl, socket, status_report_msg) != MESSAGE_STATUS_OK)
   {
      goto error;
   }
   pgmoneta_free_message(status_report_msg);
   return 0;

error:
   pgmoneta_free_message(status_report_msg);
   return 1;
}

static int
wal_xlog_offset(size_t xlogptr, int segsize)
{
   // this function assumes that segsize is a power of 2
   return xlogptr & (segsize - 1);
}

static int
wal_convert_xlogpos(char* xlogpos, int segsize, uint32_t* high32, uint32_t* low32)
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
wal_find_streaming_start(char* basedir, int segsize, uint32_t* timeline, uint32_t* high32, uint32_t* low32)
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
      if (*low32 == (uint32_t)segments_per_id)
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

int
pgmoneta_read_mappings_from_json(char* mappings_path)
{
   struct json* root = NULL;
   struct json* section = NULL;
   struct json_iterator* iter = NULL;
   int total_entries = 0;
   int index = 0;
   char* oid_str;
   int oid;
   object_type current_type;
   char* sections[] = {"tablespaces", "databases", "relations"};

   if (pgmoneta_json_read_file(mappings_path, &root))
   {
      pgmoneta_log_error("Failed to read mappings file: %s", mappings_path);
      goto error;
   }

   for (int i = 0; i < 3; i++)
   {
      section = (struct json*)pgmoneta_json_get(root, sections[i]);
      if (section && section->type == JSONItem)
      {
         struct art* art_tree = (struct art*)section->elements;
         total_entries += art_tree->size;
      }
   }

   oidMappings = (oid_mapping*)malloc(total_entries * sizeof(oid_mapping));
   if (!oidMappings)
   {
      pgmoneta_log_error("Memory allocation failed");
      goto error;
   }

   for (int i = 0; i < 3; i++)
   {
      current_type = (object_type)i;
      section = (struct json*)pgmoneta_json_get(root, sections[i]);
      if (section && section->type == JSONItem)
      {
         pgmoneta_json_iterator_create(section, &iter);
         while (pgmoneta_json_iterator_next(iter))
         {
            char* name = iter->key;
            oid_str = (char*)iter->value->data;
            oid = (int)strtol(oid_str, NULL, 10);

            oidMappings[index].oid = oid;
            oidMappings[index].type = current_type;
            oidMappings[index].name = strdup(name);
            index++;
         }
         pgmoneta_json_iterator_destroy(iter);
      }
   }

   mappings_size = total_entries;
   pgmoneta_json_destroy(root);
   enable_translation = true;

   return 0;

error:
   pgmoneta_json_destroy(root);
   return 1;
}

int
pgmoneta_read_mappings_from_server(int server_index)
{
   struct query_response* qr = NULL;
   struct message* query_msg = NULL;
   struct tuple* tuple = NULL;
   char* name = NULL;
   char* oid_str = NULL;
   unsigned int oid;
   int count;
   struct walinfo_configuration* config = NULL;
   int user_index = -1;
   int auth;
   SSL* ssl = NULL;
   int socket = -1;
   int len = 0;
   char* section_names[] = {"tablespaces", "databases", "relations"};
   object_type types[] = {OBJ_TABLESPACE, OBJ_DATABASE, OBJ_RELATION};
   char* queries[] = {
      "SELECT spcname, oid FROM pg_tablespace",
      "SELECT datname, oid FROM pg_database",
      "SELECT nspname || '.' || relname, c.oid FROM pg_class c JOIN pg_namespace n ON c.relnamespace = n.oid"
   };

   config = (struct walinfo_configuration*)shmem;
   pgmoneta_memory_init();

   for (int i = 0; i < config->common.number_of_users; i++)
   {
      if (strcmp(config->common.users[i].username, config->common.servers[server_index].username) == 0)
      {
         user_index = i;
         break;
      }
   }

   if (user_index == -1)
   {
      pgmoneta_log_error("User %s not found", config->common.servers[server_index].username);
      goto error;
   }

   auth = pgmoneta_server_authenticate(server_index, "postgres", config->common.users[user_index].username, config->common.users[user_index].password, false, &ssl, &socket);

   if (auth != AUTH_SUCCESS)
   {
      pgmoneta_log_error("Authentication failed for user %s on %s", config->common.users[user_index].username, config->common.servers[server_index].name);
      goto error;
   }

   len = sizeof(queries) / sizeof(queries[0]);

   for (int i = 0; i < len; i++)
   {
      query_msg = NULL;
      if (pgmoneta_create_query_message((char*)queries[i], &query_msg) != MESSAGE_STATUS_OK)
      {
         pgmoneta_log_error("Failed to create query message");
         goto error;
      }

      if (pgmoneta_query_execute(ssl, socket, query_msg, &qr) != 0 || qr == NULL)
      {
         pgmoneta_log_error("Failed to fetch %s", section_names[i]);
         goto error;
      }

      if (qr->number_of_columns < 2)
      {
         pgmoneta_log_error("Invalid response for %s", section_names[i]);
         qr = NULL;
         continue;
      }

      count = 0;
      tuple = qr->tuples;
      while (tuple != NULL)
      {
         count++;
         tuple = tuple->next;
      }

      oid_mapping* temp = realloc(oidMappings, (mappings_size + count) * sizeof(oid_mapping));
      if (temp == NULL)
      {
         pgmoneta_log_error("Memory allocation failed");
         goto error;
      }
      oidMappings = temp;

      tuple = qr->tuples;
      while (tuple != NULL)
      {
         name = tuple->data[0];
         oid_str = tuple->data[1];
         oid = (unsigned int)strtoul(oid_str, NULL, 10);

         oidMappings[mappings_size].type = types[i];
         oidMappings[mappings_size].oid = oid;
         oidMappings[mappings_size].name = strdup(name);

         if (oidMappings[mappings_size].name == NULL)
         {
            pgmoneta_log_error("Failed to duplicate name");
            goto error;
         }

         mappings_size++;
         tuple = tuple->next;
      }

      pgmoneta_free_query_response(qr);
      pgmoneta_free_message(query_msg);
      qr = NULL;
   }

   pgmoneta_close_ssl(ssl);
   pgmoneta_disconnect(socket);
   pgmoneta_memory_destroy();
   enable_translation = true;

   return 0;

error:
   pgmoneta_free_query_response(qr);
   pgmoneta_free_message(query_msg);
   pgmoneta_memory_destroy();
   qr = NULL;

   return 1;
}

int
pgmoneta_get_database_name(int oid, char** name)
{
   char* temp_name = NULL;
   int max_digits = 0;

   if (enable_translation)
   {
      for (int i = 0; i < mappings_size; i++)
      {
         if (oidMappings[i].oid == oid && oidMappings[i].type == OBJ_DATABASE)
         {
            temp_name = strdup(oidMappings[i].name);
            if (temp_name == NULL)
            {
               goto error;
            }
            break;
         }
      }
   }

   if (temp_name == NULL)
   {
      max_digits = snprintf(NULL, 0, "%d", oid) + 1;
      temp_name = malloc(max_digits);

      if (temp_name == NULL)
      {
         goto error;
      }

      snprintf(temp_name, max_digits, "%d", oid);
   }

   *name = temp_name;
   temp_name = NULL;

   return 0;

error:
   free(temp_name);
   temp_name = NULL;
   return 1;
}

int
pgmoneta_get_tablespace_name(int oid, char** name)
{
   char* temp_name = NULL;
   int max_digits = 0;

   if (enable_translation)
   {
      for (int i = 0; i < mappings_size; i++)
      {
         if (oidMappings[i].oid == oid && oidMappings[i].type == OBJ_TABLESPACE)
         {
            temp_name = strdup(oidMappings[i].name);
            if (temp_name == NULL)
            {
               goto error;
            }
            break;
         }
      }
   }

   if (temp_name == NULL)
   {
      max_digits = snprintf(NULL, 0, "%d", oid) + 1;
      temp_name = malloc(max_digits);
      if (temp_name == NULL)
      {
         goto error;
      }

      snprintf(temp_name, max_digits, "%d", oid);
   }

   *name = temp_name;
   temp_name = NULL;

   return 0;

error:
   free(temp_name);
   temp_name = NULL;
   return 1;
}

int
pgmoneta_get_relation_name(int oid, char** name)
{
   char* temp_name = NULL;
   int max_digits = 0;

   if (enable_translation)
   {
      for (int i = 0; i < mappings_size; i++)
      {
         if (oidMappings[i].oid == oid && oidMappings[i].type == OBJ_RELATION)
         {
            temp_name = strdup(oidMappings[i].name);
            if (temp_name == NULL)
            {
               goto error;
            }
            break;
         }
      }
   }

   if (temp_name == NULL)
   {
      max_digits = snprintf(NULL, 0, "%d", oid) + 1;
      temp_name = malloc(max_digits);
      if (temp_name == NULL)
      {
         goto error;
      }

      snprintf(temp_name, max_digits, "%d", oid);
   }

   *name = temp_name;
   temp_name = NULL;

   return 0;

error:
   free(temp_name);
   temp_name = NULL;
   return 1;
}

int
pgmoneta_get_tablespace_oid(char* name, char** oid)
{
   char* temp_oid = NULL;
   int max_digits = 0;

   if (enable_translation)
   {
      for (int i = 0; i < mappings_size; i++)
      {
         if (oidMappings[i].type == OBJ_TABLESPACE && !strcmp(oidMappings[i].name, name))
         {
            max_digits = snprintf(NULL, 0, "%d", oidMappings[i].oid) + 1;
            temp_oid = malloc(max_digits);
            if (temp_oid == NULL)
            {
               goto error;
            }
            snprintf(temp_oid, max_digits, "%d", oidMappings[i].oid);
            break;
         }
      }
   }

   if (temp_oid == NULL)
   {
      temp_oid = strdup(name);
      if (temp_oid == NULL)
      {
         goto error;
      }
   }

   *oid = temp_oid;
   temp_oid = NULL;

   return 0;

error:
   free(temp_oid);
   temp_oid = NULL;
   return 1;
}

int
pgmoneta_get_database_oid(char* name, char** oid)
{
   char* temp_oid = NULL;
   int max_digits = 0;

   if (enable_translation)
   {
      for (int i = 0; i < mappings_size; i++)
      {
         if (oidMappings[i].type == OBJ_DATABASE && !strcmp(oidMappings[i].name, name))
         {
            max_digits = snprintf(NULL, 0, "%d", oidMappings[i].oid) + 1;
            temp_oid = malloc(max_digits);
            if (temp_oid == NULL)
            {
               goto error;
            }
            snprintf(temp_oid, max_digits, "%d", oidMappings[i].oid);
            break;
         }
      }
   }

   if (temp_oid == NULL)
   {
      temp_oid = strdup(name);
      if (temp_oid == NULL)
      {
         goto error;
      }
   }

   *oid = temp_oid;
   temp_oid = NULL;

   return 0;

error:
   free(temp_oid);
   temp_oid = NULL;
   return 1;
}

int
pgmoneta_get_relation_oid(char* name, char** oid)
{
   char* temp_oid = NULL;
   int max_digits = 0;

   if (enable_translation)
   {
      for (int i = 0; i < mappings_size; i++)
      {
         if (oidMappings[i].type == OBJ_RELATION && !strcmp(oidMappings[i].name, name))
         {
            max_digits = snprintf(NULL, 0, "%d", oidMappings[i].oid) + 1;
            temp_oid = malloc(max_digits);
            if (temp_oid == NULL)
            {
               goto error;
            }
            snprintf(temp_oid, max_digits, "%d", oidMappings[i].oid);
            break;
         }
      }
   }

   if (temp_oid == NULL)
   {
      temp_oid = strdup(name);
      if (temp_oid == NULL)
      {
         goto error;
      }
   }

   *oid = temp_oid;
   temp_oid = NULL;

   return 0;

error:
   free(temp_oid);
   temp_oid = NULL;
   return 1;
}
