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

static int get_wal_level(SSL* ssl, int socket, int server, bool* replica);
static int get_wal_size(SSL* ssl, int socket, int server, int* ws);

static bool is_valid_response(struct query_response* response);

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

   pgmoneta_process_startup_message(ssl, socket, srv);

   if (get_wal_level(ssl, socket, srv, &replica))
   {
      pgmoneta_log_error("Unable to get wal_level for %s", config->servers[srv].name);
      config->servers[srv].valid = false;
      goto done;
   }
   else
   {
      config->servers[srv].valid = replica;
   }

   if (get_wal_size(ssl, socket, srv, &ws))
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

static int
get_wal_size(SSL* ssl, int socket, int server, int* ws)
{
   int q = 0;
   bool mb = true;
   int ret;
   char wal_size[MISC_LENGTH];
   struct message* query_msg = NULL;
   struct query_response* response = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   ret = pgmoneta_create_query_message("SHOW wal_segment_size;", &query_msg);
   if (ret != MESSAGE_STATUS_OK)
   {
      goto error;
   }

q:

   pgmoneta_query_execute(ssl, socket, query_msg, &response);

   if (!is_valid_response(response))
   {
      pgmoneta_free_query_response(response);
      response = NULL;

      SLEEP(5000000L);

      q++;

      if (q < 5)
      {
         goto q;
      }
      else
      {
         goto error;
      }
   }

   memset(&wal_size[0], 0, sizeof(wal_size));

   snprintf(&wal_size[0], sizeof(wal_size), "%s", response->tuples->data[0]);

   if (pgmoneta_ends_with(&wal_size[0], "MB"))
   {
      mb = true;
   }
   else
   {
      mb = false;
   }

   wal_size[strlen(wal_size)] = '\0';
   wal_size[strlen(wal_size)] = '\0';

   *ws = pgmoneta_atoi(wal_size);

   if (mb)
   {
      *ws = *ws * 1024 * 1024;
   }
   else
   {
      *ws = *ws * 1024 * 1024 * 1024;
   }

   pgmoneta_log_debug("%s/wal_segment_size %d", config->servers[server].name, *ws);

   pgmoneta_free_query_response(response);
   pgmoneta_free_message(query_msg);

   return 0;
error:

   pgmoneta_log_error("Error getting wal_segment_size");

   pgmoneta_query_response_debug(response);
   pgmoneta_free_query_response(response);
   pgmoneta_free_message(query_msg);
   return 1;
}

static int
get_wal_level(SSL* ssl, int socket, int server, bool* replica)
{
   int q = 0;
   int ret;
   char wal_level[MISC_LENGTH];
   struct message* query_msg = NULL;
   struct query_response* response = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   *replica = false;

   ret = pgmoneta_create_query_message("SHOW wal_level;", &query_msg);
   if (ret != MESSAGE_STATUS_OK)
   {
      goto error;
   }

q:

   pgmoneta_query_execute(ssl, socket, query_msg, &response);

   if (!is_valid_response(response))
   {
      pgmoneta_free_query_response(response);
      response = NULL;

      SLEEP(5000000L);

      q++;

      if (q < 5)
      {
         goto q;
      }
      else
      {
         goto error;
      }
   }

   memset(&wal_level[0], 0, sizeof(wal_level));

   snprintf(&wal_level[0], sizeof(wal_level), "%s", response->tuples->data[0]);

   if (!strcmp("replica", wal_level) || !strcmp("logical", wal_level))
   {
      *replica = true;
   }

   pgmoneta_log_debug("%s/wal_level %s", config->servers[server].name, *replica ? "Yes" : "No");

   pgmoneta_free_query_response(response);
   pgmoneta_free_message(query_msg);

   return 0;
error:

   pgmoneta_log_error("Error getting wal_level");

   pgmoneta_query_response_debug(response);
   pgmoneta_free_query_response(response);
   pgmoneta_free_message(query_msg);
   return 1;
}

int
pgmoneta_server_get_version(SSL* ssl, int socket, int server)
{
   int q = 0;
   int ret;
   char major[3];
   char minor[3];
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

q:

   pgmoneta_query_execute(ssl, socket, query_msg, &response);

   if (!is_valid_response(response))
   {
      pgmoneta_free_query_response(response);
      response = NULL;

      SLEEP(5000000L);

      q++;

      if (q < 5)
      {
         goto q;
      }
      else
      {
         goto error;
      }
   }

   memset(&major[0], 0, sizeof(major));
   memset(&minor[0], 0, sizeof(minor));

   snprintf(&major[0], sizeof(major), "%s", response->tuples->data[0]);
   snprintf(&minor[0], sizeof(minor), "%s", response->tuples->data[1]);

   config->servers[server].version = pgmoneta_atoi(major);
   config->servers[server].minor_version = pgmoneta_atoi(minor);

   pgmoneta_log_debug("%s %d.%d", config->servers[server].name, config->servers[server].version, config->servers[server].minor_version);

   pgmoneta_free_query_response(response);
   pgmoneta_free_message(query_msg);

   return 0;
error:

   pgmoneta_query_response_debug(response);
   pgmoneta_free_query_response(response);
   pgmoneta_free_message(query_msg);
   return 1;
}

static bool
is_valid_response(struct query_response* response)
{
   struct tuple* tuple = NULL;

   if (response == NULL)
   {
      return false;
   }

   if (response->number_of_columns == 0 || response->tuples == NULL)
   {
      return false;
   }

   tuple = response->tuples;
   while (tuple != NULL)
   {
      for (int i = 0; i < response->number_of_columns; i++)
      {
         if (tuple->data[i] == NULL)
         {
            return false;
         }
      }
      tuple = tuple->next;
   }

   return true;
}
