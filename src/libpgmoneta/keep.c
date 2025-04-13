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
#include <info.h>
#include <logging.h>
#include <management.h>
#include <utils.h>

/* system */
#include <stdatomic.h>
#include <stdlib.h>

#define NAME "keep"

static void keep(char* prefix, SSL* ssl, int client_fd, int srv, bool k, uint8_t compression, uint8_t encryption, struct json* payload);

void
pgmoneta_retain_backup(SSL* ssl, int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* payload)
{
   keep("Retain", ssl, client_fd, server, true, compression, encryption, payload);
}

void
pgmoneta_expunge_backup(SSL* ssl, int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* payload)
{
   keep("Expunge", ssl, client_fd, server, false, compression, encryption, payload);
}

static void
keep(char* prefix, SSL* ssl, int client_fd, int srv, bool k, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* elapsed = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   char* backup_id = NULL;
   char* d = NULL;
   int backup_index = -1;
   int number_of_backups = 0;
   bool kr = false;
   struct backup** backups = NULL;
   struct json* req = NULL;
   struct json* response = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   d = pgmoneta_get_server_backup(srv);

   if (pgmoneta_get_backups(d, &number_of_backups, &backups))
   {
      goto error;
   }

   free(d);
   d = NULL;

   if (pgmoneta_management_create_response(payload, srv, &response))
   {
      goto error;
   }

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   backup_id = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_BACKUP);

   if (!strcmp(backup_id, "oldest"))
   {
      for (int i = 0; backup_index == -1 && i < number_of_backups; i++)
      {
         if (backups[i] != NULL)
         {
            backup_index = i;
         }
      }
   }
   else if (!strcmp(backup_id, "latest") || !strcmp(backup_id, "newest"))
   {
      for (int i = number_of_backups - 1; backup_index == -1 && i >= 0; i--)
      {
         if (backups[i] != NULL)
         {
            backup_index = i;
         }
      }
   }
   else
   {
      for (int i = 0; backup_index == -1 && i < number_of_backups; i++)
      {
         if (backups[i] != NULL && !strcmp(backups[i]->label, backup_id))
         {
            backup_index = i;
         }
      }
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->common.servers[srv].name, ValueString);

   if (backup_index == -1)
   {
      if (k)
      {
         pgmoneta_management_response_error(NULL, client_fd, config->common.servers[srv].name, MANAGEMENT_ERROR_RETAIN_NOBACKUP, NAME, compression, encryption, payload);
         pgmoneta_log_warn("Retain: No identifier for %s/%s", config->common.servers[srv].name, backup_id);
      }
      else
      {
         pgmoneta_management_response_error(NULL, client_fd, config->common.servers[srv].name, MANAGEMENT_ERROR_EXPUNGE_NOBACKUP, NAME, compression, encryption, payload);
         pgmoneta_log_warn("Expunge: No identifier for %s/%s", config->common.servers[srv].name, backup_id);
      }

      goto error;
   }

   if (backups[backup_index]->valid == VALID_TRUE && backups[backup_index]->type == TYPE_FULL)
   {
      d = pgmoneta_get_server_backup_identifier(srv, backups[backup_index]->label);

      pgmoneta_update_info_bool(d, INFO_KEEP, k);

      kr = k;

      free(d);
      d = NULL;
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)backups[backup_index]->label, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_VALID, (uintptr_t)backups[backup_index]->valid, ValueInt8);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_COMMENTS, (uintptr_t)backups[backup_index]->comments, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_KEEP, (uintptr_t)kr, ValueBool);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      if (k)
      {
         pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_RETAIN_NETWORK, NAME, compression, encryption, payload);
         pgmoneta_log_error("Retain: Error sending response");
      }
      else
      {
         pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_EXPUNGE_NETWORK, NAME, compression, encryption, payload);
         pgmoneta_log_error("Expunge: Error sending response");
      }

      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("%s: %s/%s (Elapsed: %s)", prefix, config->common.servers[srv].name, backups[backup_index]->label, elapsed);

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(d);
   free(elapsed);

   exit(0);

error:

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(d);
   free(elapsed);

   exit(1);
}
