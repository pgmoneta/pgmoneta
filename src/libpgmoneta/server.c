/*
 * Copyright (C) 2021 Red Hat
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

static int get_wal_level(int socket, bool* replica);

void
pgmoneta_server_wal_level(int srv)
{
   int usr;
   int auth;
   int socket;
   bool replica;
   struct configuration* config;

   config = (struct configuration*)shmem;

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
      goto error;
   }

   auth = pgmoneta_server_authenticate(srv, "postgres", config->users[usr].username, config->users[usr].password, &socket);

   if (auth != AUTH_SUCCESS)
   {
      goto error;
   }

   if (get_wal_level(socket, &replica))
   {
      config->servers[srv].valid = false;
   }
   else
   {
      config->servers[srv].valid = replica;
   }

   pgmoneta_write_terminate(NULL, socket);

   pgmoneta_disconnect(socket);

error:

   pgmoneta_disconnect(socket);
}

static int
get_wal_level(int socket, bool* replica)
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

   status = pgmoneta_write_message(NULL, socket, &qmsg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgmoneta_read_block_message(NULL, socket, &tmsg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   pgmoneta_extract_message('D', tmsg, &dmsg);

   vlength = pgmoneta_read_int32(dmsg->data + 7);
   value = (char*)malloc(vlength + 1);
   memset(value, 0, vlength + 1);
   memcpy(value, dmsg->data + 11, vlength);
   
   if (!strcmp("replica", value) || !strcmp("logical", value))
   {
      *replica = true;
   }

   pgmoneta_free_copy_message(dmsg);
   pgmoneta_free_message(tmsg);
   free(value);

   return 0;

error:
   pgmoneta_log_trace("get_wal_level: socket %d status %d", socket, status);

   pgmoneta_free_copy_message(dmsg);
   pgmoneta_free_message(tmsg);
   free(value);

   return 1;
}
