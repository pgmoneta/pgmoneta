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
#include <message.h>
#include <network.h>
#include <security.h>
#include <server.h>
#include <utils.h>

/* system */
#include <ev.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

static int get_wal_level(SSL* ssl, int socket, bool* replica);

void
pgmoneta_server_info(int srv)
{
   int usr;
   int auth;
   SSL* ssl = NULL;
   int socket = -1;
   bool replica;
   int ws;
   struct configuration* config;

   config = (struct configuration*)shmem;

   config->servers[srv].valid = false;

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
      goto done;
   }

   auth = pgmoneta_server_authenticate(srv, "postgres", config->users[usr].username, config->users[usr].password, false, &ssl, &socket);

   if (auth != AUTH_SUCCESS)
   {
      pgmoneta_log_error("Authentication failed for user %s on %s", config->users[usr].username, config->servers[srv].name);
      goto done;
   }

   if (get_wal_level(ssl, socket, &replica))
   {
      pgmoneta_log_error("Unable to get wal_level for %s", config->servers[srv].name);
      config->servers[srv].valid = false;
      goto done;
   }
   else
   {
      config->servers[srv].valid = replica;
   }

   if (pgmoneta_server_get_wal_size(ssl, socket, &ws))
   {
      pgmoneta_log_error("Unable to get wal_segment_size for %s", config->servers[srv].name);
      config->servers[srv].valid = false;
      goto done;
   }
   else
   {
      config->servers[srv].wal_size = ws;
   }

   pgmoneta_write_terminate(ssl, socket);

done:

   pgmoneta_close_ssl(ssl);
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }

   if (!config->servers[srv].valid)
   {
      pgmoneta_log_error("Server %s need wal_level at replica or logical", config->servers[srv].name);
   }
}

int
pgmoneta_server_get_wal_size(SSL* ssl, int socket, int* ws)
{
   int status;
   size_t size = 28;
   char wal_segment_size[size];
   int vlength;
   char* value = NULL;
   char* number = NULL;
   struct message qmsg;
   struct message* tmsg = NULL;
   struct message* dmsg = NULL;

   *ws = 0;

   memset(&qmsg, 0, sizeof(struct message));
   memset(&wal_segment_size, 0, size);

   pgmoneta_write_byte(&wal_segment_size, 'Q');
   pgmoneta_write_int32(&(wal_segment_size[1]), size - 1);
   pgmoneta_write_string(&(wal_segment_size[5]), "SHOW wal_segment_size;");

   qmsg.kind = 'Q';
   qmsg.length = size;
   qmsg.data = &wal_segment_size;

   status = pgmoneta_write_message(ssl, socket, &qmsg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgmoneta_read_block_message(ssl, socket, &tmsg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   pgmoneta_log_message(tmsg);
   pgmoneta_extract_message('D', tmsg, &dmsg);

   if (dmsg == NULL)
   {
      goto error;
   }

   vlength = pgmoneta_read_int32(dmsg->data + 7);
   value = (char*)malloc(vlength + 1);
   memset(value, 0, vlength + 1);
   memcpy(value, dmsg->data + 11, vlength);

   number = (char*)malloc(strlen(value) - 2 + 1);
   memset(number, 0, strlen(value) - 2 + 1);
   memcpy(number, value, strlen(value) - 2);

   if (pgmoneta_ends_with(value, "MB"))
   {
      *ws = atoi(number) * 1024 * 1024;
   }
   else
   {
      *ws = atoi(number) * 1024 * 1024 * 1024;
   }

   pgmoneta_free_copy_message(dmsg);
   pgmoneta_free_message();
   free(value);
   free(number);

   return 0;

error:
   pgmoneta_log_trace("pgmoneta_server_get_wal_size: socket %d status %d", socket, status);

   pgmoneta_free_copy_message(dmsg);
   pgmoneta_free_message();
   free(value);
   free(number);

   return 1;
}

static int
get_wal_level(SSL* ssl, int socket, bool* replica)
{
   int status;
   size_t size = 21;
   char wal_level[size];
   int vlength;
   char* value = NULL;
   struct message qmsg;
   struct message* tmsg = NULL;
   struct message* dmsg = NULL;

   *replica = false;

   memset(&qmsg, 0, sizeof(struct message));
   memset(&wal_level, 0, size);

   pgmoneta_write_byte(&wal_level, 'Q');
   pgmoneta_write_int32(&(wal_level[1]), size - 1);
   pgmoneta_write_string(&(wal_level[5]), "SHOW wal_level;");

   qmsg.kind = 'Q';
   qmsg.length = size;
   qmsg.data = &wal_level;

   status = pgmoneta_write_message(ssl, socket, &qmsg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgmoneta_read_block_message(ssl, socket, &tmsg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   pgmoneta_log_message(tmsg);
   pgmoneta_extract_message('D', tmsg, &dmsg);

   if (dmsg == NULL)
   {
      goto error;
   }

   vlength = pgmoneta_read_int32(dmsg->data + 7);
   value = (char*)malloc(vlength + 1);
   memset(value, 0, vlength + 1);
   memcpy(value, dmsg->data + 11, vlength);

   if (!strcmp("replica", value) || !strcmp("logical", value))
   {
      *replica = true;
   }

   pgmoneta_free_copy_message(dmsg);
   pgmoneta_free_message();
   free(value);

   return 0;

error:
   pgmoneta_log_trace("get_wal_level: socket %d status %d", socket, status);

   pgmoneta_free_copy_message(dmsg);
   pgmoneta_free_message();
   free(value);

   return 1;
}

int
pgmoneta_server_get_version(SSL* ssl, int socket, int server)
{
   int ret;
   struct message* query_msg = NULL;
   struct query_response* response = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   ret = pgmoneta_create_query_message("SELECT split_part(split_part(version(), ' ', 2), '.', 1) AS major, "
                                       "split_part(split_part(version(), ' ', 2), '.', 2) AS minor;",
                                       &query_msg);
   if (ret != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (pgmoneta_query_execute(ssl, socket, query_msg, &response) || response == NULL)
   {
      goto error;
   }

   config->servers[server].version = atoi(response->tuples->data[0]);
   config->servers[server].minor_version = atoi(response->tuples->data[1]);

   pgmoneta_free_query_response(response);
   pgmoneta_free_copy_message(query_msg);

   return 0;
error:

   pgmoneta_query_response_debug(response);
   pgmoneta_free_query_response(response);
   return 1;
}
