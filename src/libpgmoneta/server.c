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
static int get_checksums(SSL* ssl, int socket, int server, bool* checksums);
static int get_version(SSL* ssl, int socket, int server, int* major, int* minor);
static int get_ext_version(SSL* ssl, int socket, char** version);
static int get_segment_size(SSL* ssl, int socket, int server, size_t* segsz);
static int get_block_size(SSL* ssl, int socket, int server, size_t* blocksz);

static bool is_valid_response(struct query_response* response);

void
pgmoneta_server_info(int srv)
{
   int usr;
   int auth;
   SSL* ssl = NULL;
   int socket = -1;
   int major = 0;
   int minor = 0;
   bool replica;
   bool checksums;
   int ws;
   size_t blocksz = 0;
   size_t segsz = 0;
   struct configuration* config;
   char* ext_version = NULL;

   config = (struct configuration*)shmem;

   config->servers[srv].valid = false;
   config->servers[srv].checksums = false;

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

   if (get_version(ssl, socket, srv, &major, &minor))
   {
      pgmoneta_log_error("Unable to get version for %s", config->servers[srv].name);
      config->servers[srv].valid = false;
      goto done;
   }
   else
   {
      config->servers[srv].version = major;
      config->servers[srv].minor_version = minor;
   }

   pgmoneta_log_debug("%s %d.%d", config->servers[srv].name, config->servers[srv].version, config->servers[srv].minor_version);

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

   pgmoneta_log_debug("%s/wal_level %s", config->servers[srv].name, config->servers[srv].valid ? "Yes" : "No");

   if (get_checksums(ssl, socket, srv, &checksums))
   {
      pgmoneta_log_error("Unable to get data_checksums for %s", config->servers[srv].name);
      config->servers[srv].checksums = false;
      goto done;
   }
   else
   {
      config->servers[srv].checksums = checksums;
   }

   pgmoneta_log_debug("%s/data_checksums %s", config->servers[srv].name, config->servers[srv].checksums ? "Yes" : "No");

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
   pgmoneta_log_debug("%s/wal_segment_size %d", config->servers[srv].name, config->servers[srv].wal_size);

   if (get_ext_version(ssl, socket, &ext_version))
   {
      pgmoneta_log_warn("Unable to get extionsion version for %s", config->servers[srv].name);
      config->servers[srv].ext_valid = false;
   }
   else
   {
      config->servers[srv].ext_valid = true;
      strcpy(config->servers[srv].ext_version, ext_version);
   }
   if (ext_version != NULL)
   {
      free(ext_version);
   }

   pgmoneta_log_debug("%s ext_valid: %s, ext_version: %s",
                      config->servers[srv].name,
                      config->servers[srv].ext_valid ? "true" : "false",
                      config->servers[srv].ext_valid ? config->servers[srv].ext_version : "N/A");
   if (get_segment_size(ssl, socket, srv, &segsz))
   {
      pgmoneta_log_error("Unable to get segment_size for %s", config->servers[srv].name);
      config->servers[srv].valid = false;
      goto done;
   }
   else
   {
      config->servers[srv].segment_size = segsz;
   }
   pgmoneta_log_debug("%s/segment_size %d", config->servers[srv].name, config->servers[srv].segment_size);

   if (get_block_size(ssl, socket, srv, &blocksz))
   {
      pgmoneta_log_error("Unable to get block_size for %s", config->servers[srv].name);
      config->servers[srv].valid = false;
      goto done;
   }
   else
   {
      config->servers[srv].block_size = blocksz;
   }
   pgmoneta_log_debug("%s/block_size %d", config->servers[srv].name, config->servers[srv].block_size);

   config->servers[srv].relseg_size = config->servers[srv].segment_size / config->servers[srv].block_size;

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

bool
pgmoneta_server_valid(int srv)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (!config->servers[srv].valid)
   {
      return false;
   }

   if (config->servers[srv].version == 0)
   {
      return false;
   }

   if (config->servers[srv].wal_size == 0)
   {
      return false;
   }

   if (config->servers[srv].segment_size == 0 || config->servers[srv].block_size == 0)
   {
      return false;
   }

   return true;
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

static int
get_checksums(SSL* ssl, int socket, int server, bool* checksums)
{
   int q = 0;
   int ret;
   char data_checksums[MISC_LENGTH];
   struct message* query_msg = NULL;
   struct query_response* response = NULL;

   *checksums = false;

   ret = pgmoneta_create_query_message("SHOW data_checksums;", &query_msg);
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

   memset(&data_checksums[0], 0, sizeof(data_checksums));

   snprintf(&data_checksums[0], sizeof(data_checksums), "%s", response->tuples->data[0]);

   if (!strcmp("on", data_checksums))
   {
      *checksums = true;
   }

   pgmoneta_free_query_response(response);
   pgmoneta_free_message(query_msg);

   return 0;
error:

   pgmoneta_log_error("Error getting data_checksums");

   pgmoneta_query_response_debug(response);
   pgmoneta_free_query_response(response);
   pgmoneta_free_message(query_msg);
   return 1;
}

static int
get_version(SSL* ssl, int socket, int server, int* major, int* minor)
{
   int q = 0;
   int ret;
   char maj[3];
   char min[3];
   struct message* query_msg = NULL;
   struct query_response* response = NULL;

   *major = 0;
   *minor = 0;

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

   memset(&maj[0], 0, sizeof(maj));
   memset(&min[0], 0, sizeof(min));

   snprintf(&maj[0], sizeof(maj), "%s", response->tuples->data[0]);
   if (response->tuples->data[1] != NULL && strlen(response->tuples->data[1]) > 0)
   {
      snprintf(&min[0], sizeof(min), "%s", response->tuples->data[1]);
   }

   *major = pgmoneta_atoi(maj);
   if (response->tuples->data[1] != NULL && strlen(response->tuples->data[1]) > 0)
   {
      *minor = pgmoneta_atoi(min);
   }
   else
   {
      *minor = 0;
   }

   pgmoneta_free_query_response(response);
   pgmoneta_free_message(query_msg);

   return 0;
error:

   pgmoneta_query_response_debug(response);
   pgmoneta_free_query_response(response);
   pgmoneta_free_message(query_msg);
   return 1;
}

static int
get_ext_version(SSL* ssl, int socket, char** version)
{
   struct query_response* qr = NULL;

   if (version == NULL)
   {
      goto error;
   }
   *version = NULL;

   pgmoneta_ext_version(ssl, socket, &qr);

   if (qr != NULL && qr->tuples != NULL && qr->tuples->data[0] != NULL)
   {
      size_t len = strlen(qr->tuples->data[0]) + 1;
      *version = (char*)malloc(len);

      if (*version == NULL)
      {
         pgmoneta_log_warn("get_ext_version: Memory allocation failed");
         goto error;
      }

      strcpy(*version, qr->tuples->data[0]);

      pgmoneta_free_query_response(qr);
      return 0;
   }
   else
   {
      pgmoneta_log_warn("get_ext_version: Query failed or invalid response");
      goto error;
   }

error:
   if (qr != NULL)
   {
      pgmoneta_free_query_response(qr);
   }
   return 1;
}

static int
get_segment_size(SSL* ssl, int socket, int server, size_t* segsz)
{
   int q = 0;
   bool mb = true;
   int ret;
   char seg_size[MISC_LENGTH];
   struct message* query_msg = NULL;
   struct query_response* response = NULL;

   ret = pgmoneta_create_query_message("SHOW segment_size;", &query_msg);
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

   memset(&seg_size[0], 0, sizeof(seg_size));

   snprintf(&seg_size[0], sizeof(seg_size), "%s", response->tuples->data[0]);

   if (pgmoneta_ends_with(&seg_size[0], "MB"))
   {
      mb = true;
   }
   else
   {
      mb = false;
   }

   seg_size[strlen(seg_size) - 2] = '\0';

   *segsz = pgmoneta_atoi(seg_size);

   if (mb)
   {
      *segsz = *segsz * 1024 * 1024;
   }
   else
   {
      *segsz = *segsz * 1024 * 1024 * 1024;
   }

   pgmoneta_free_query_response(response);
   pgmoneta_free_message(query_msg);

   return 0;
error:

   pgmoneta_log_error("Error getting segment_size");

   pgmoneta_query_response_debug(response);
   pgmoneta_free_query_response(response);
   pgmoneta_free_message(query_msg);
   return 1;
}

static int
get_block_size(SSL* ssl, int socket, int server, size_t* blocksz)
{
   int q = 0;
   int ret;
   char block_size[MISC_LENGTH];
   struct message* query_msg = NULL;
   struct query_response* response = NULL;

   ret = pgmoneta_create_query_message("SHOW block_size;", &query_msg);
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

   memset(&block_size[0], 0, sizeof(block_size));

   snprintf(&block_size[0], sizeof(block_size), "%s", response->tuples->data[0]);

   *blocksz = pgmoneta_atoi(block_size);

   pgmoneta_free_query_response(response);
   pgmoneta_free_message(query_msg);

   return 0;
error:

   pgmoneta_log_error("Error getting block_size");

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
         /* First column must be defined */
         if (tuple->data[0] == NULL)
         {
            return false;
         }
      }
      tuple = tuple->next;
   }

   return true;
}
