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
#include <extension.h>
#include <logging.h>
#include <message.h>
#include <network.h>
#include <security.h>
#include <utils.h>
#include <workflow.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int extra_setup(int, char*, struct deque*);
static int extra_execute(int, char*, struct deque*);
static int extra_teardown(int, char*, struct deque*);

struct workflow*
pgmoneta_create_extra(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->setup = &extra_setup;
   wf->execute = &extra_execute;
   wf->teardown = &extra_teardown;
   wf->next = NULL;

   return wf;
}

static int
extra_setup(int server, char* identifier, struct deque* nodes)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Extra (setup): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   return 0;
}

static int
extra_execute(int server, char* identifier, struct deque* nodes)
{
   int usr;
   int socket = -1;
   int seconds;
   int minutes;
   int hours;
   int total_seconds;
   char elapsed[128];
   char* root = NULL;
   char* info_root = NULL;
   char* info_extra = NULL;
   time_t start_time;
   SSL* ssl = NULL;
   struct configuration* config;
   struct query_response* qr = NULL;

   config = (struct configuration*)shmem;

   if (config->servers[server].number_of_extra == 0)
   {
      pgmoneta_log_debug("No extra parameter are set for server: %s", config->servers[server].name);
      return 0;
   }

   pgmoneta_log_debug("Extra (execute): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   // Create the root directory
   root = pgmoneta_get_server_extra_identifier(server, identifier);

   start_time = time(NULL);

   pgmoneta_memory_init();

   usr = -1;
   // find the corresponding user's index of the given server
   for (int i = 0; usr == -1 && i < config->number_of_users; i++)
   {
      if (!strcmp(config->servers[server].username, config->users[i].username))
      {
         usr = i;
      }
   }

   if (usr == -1)
   {
      pgmoneta_log_error("User not found for server: %d", server);
      goto error;
   }

   // establish a connection, with replication flag set
   if (pgmoneta_server_authenticate(server, "postgres", config->users[usr].username, config->users[usr].password, false, &ssl, &socket) != AUTH_SUCCESS)
   {
      pgmoneta_log_error("Authentication failed for user %s on %s", config->users[usr].username, config->servers[server].name);
      goto error;
   }

   pgmoneta_ext_is_installed(ssl, socket, &qr);
   if (qr == NULL || qr->tuples == NULL || qr->tuples->data == NULL || qr->tuples->data[0] == NULL || qr->tuples->data[2] == NULL || strcmp(qr->tuples->data[0], "pgmoneta_ext") != 0)
   {
      pgmoneta_log_warn("extra failed: Server %s does not have the pgmoneta_ext extension installed.", config->servers[server].name);
      goto error;
   }
   pgmoneta_free_query_response(qr);
   qr = NULL;

   for (int i = 0; i < config->servers[server].number_of_extra; i++)
   {
      if (pgmoneta_receive_extra_files(ssl, socket, config->servers[server].name, config->servers[server].extra[i], root, &info_extra) != 0)
      {
         pgmoneta_log_warn("extra failed: Server %s failed to retrieve extra files %s", config->servers[server].name, config->servers[server].extra[i]);
      }
   }

   total_seconds = (int) difftime(time(NULL), start_time);
   hours = total_seconds / 3600;
   minutes = (total_seconds % 3600) / 60;
   seconds = total_seconds % 60;

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

   pgmoneta_log_debug("Extra: %s/%s (Elapsed: %s)", config->servers[server].name, identifier, &elapsed[0]);

   info_root = pgmoneta_get_server_backup_identifier(server, identifier);

   if (info_extra == NULL)
   {
      pgmoneta_update_info_string(info_root, INFO_EXTRA, "");
   }
   else
   {
      pgmoneta_update_info_string(info_root, INFO_EXTRA, info_extra);
   }

   free(root);
   free(info_root);
   if (info_extra != NULL)
   {
      free(info_extra);
   }
   pgmoneta_close_ssl(ssl);
   pgmoneta_disconnect(socket);
   pgmoneta_memory_destroy();

   return 0;

error:
   if (root != NULL)
   {
      free(root);
   }
   if (info_root != NULL)
   {
      free(info_root);
   }
   if (info_extra != NULL)
   {
      free(info_extra);
   }
   if (ssl != NULL)
   {
      pgmoneta_close_ssl(ssl);
   }
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   pgmoneta_memory_destroy();

   return 1;
}

static int
extra_teardown(int server, char* identifier, struct deque* nodes)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Extra (teardown): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   return 0;
}
