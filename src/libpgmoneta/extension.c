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
#include <extension.h>
#include <logging.h>
#include <memory.h>
#include <network.h>
#include <security.h>
#include <utils.h>

/* system */
#include <stdlib.h>

static int query_execute(int server, char* qs, struct query_response** qr);

int
pgmoneta_ext_version(int server, struct query_response** qr)
{
   return query_execute(server, "SELECT pgmoneta_ext_version();", qr);
}

int
pgmoneta_ext_switch_wal(int server, struct query_response** qr)
{
   return query_execute(server, "SELECT pgmoneta_ext_switch_wal();", qr);
}

int
pgmoneta_ext_checkpoint(int server, struct query_response** qr)
{
   return query_execute(server, "SELECT pgmoneta_ext_checkpoint();", qr);
}

static int
query_execute(int server, char* qs, struct query_response** qr)
{
   int usr;
   int socket;
   SSL* ssl = NULL;
   struct configuration* config = NULL;
   struct message* query_msg = NULL;

   usr = -1;
   socket = 0;
   config = (struct configuration*)shmem;

   pgmoneta_memory_init();

   if (server < 0 || server >= config->number_of_servers)
   {
      pgmoneta_log_info("Invalid server index: %d", server);
      goto error;
   }

   for (int i = 0; usr == -1 && i < config->number_of_users; i++)
   {
      if (!strcmp(config->servers[server].username, config->users[i].username))
      {
         usr = i;
      }
   }

   if (usr == -1)
   {
      pgmoneta_log_info("User not found for server: %d", server);
      goto error;
   }

   if (pgmoneta_server_authenticate(server, "postgres", config->users[usr].username, config->users[usr].password, false, &ssl, &socket) != AUTH_SUCCESS)
   {
      pgmoneta_log_info("Invalid credentials for %s", config->users[usr].username);
      goto error;
   }

   if (pgmoneta_create_query_message(qs, &query_msg) != MESSAGE_STATUS_OK || query_msg == NULL)
   {
      pgmoneta_log_info("Failed to create query message");
      goto error;
   }

   if (pgmoneta_query_execute(ssl, socket, query_msg, qr) != 0 || qr == NULL)
   {
      pgmoneta_log_info("Failed to execute query");
      goto error;
   }

   pgmoneta_close_ssl(ssl);
   pgmoneta_disconnect(socket);
   pgmoneta_free_copy_message(query_msg);
   pgmoneta_memory_destroy();

   return 0;

error:
   if (ssl != NULL)
   {
      pgmoneta_close_ssl(ssl);
   }
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   if (query_msg != NULL)
   {
      pgmoneta_free_copy_message(query_msg);
   }

   pgmoneta_memory_destroy();
   return 1;
}
