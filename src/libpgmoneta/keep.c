/*
 * Copyright (C) 2026 The pgmoneta community
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
#include "backup.h"
#include <pgmoneta.h>
#include <info.h>
#include <logging.h>
#include <management.h>
#include <security.h>
#include <utils.h>

/* system */
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define NAME "keep"

static void keep(char* prefix, SSL* ssl, int client_fd, int srv, bool k, uint8_t compression, uint8_t encryption, struct json* payload);
static void keep_backup(int srv, char* label, bool k);

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
   char* en = NULL;
   int ec = -1;
   char* backup_id = NULL;
   char* d = NULL;
   int backup_index = -1;
   int number_of_backups = 0;
   bool kr = false;
   bool cascade = false;
   struct backup** backups = NULL;
   struct backup* bck = NULL;
   struct json* bcks = NULL;
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

   if (pgmoneta_load_infos(d, &number_of_backups, &backups))
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
   cascade = (bool)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_CASCADE);

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
         ec = MANAGEMENT_ERROR_RETAIN_NOBACKUP;
         pgmoneta_log_warn("Retain: No identifier for %s/%s", config->common.servers[srv].name, backup_id);
      }
      else
      {
         ec = MANAGEMENT_ERROR_EXPUNGE_NOBACKUP;
         pgmoneta_log_warn("Expunge: No identifier for %s/%s", config->common.servers[srv].name, backup_id);
      }

      goto error;
   }

   pgmoneta_json_create(&bcks);

   if (pgmoneta_is_backup_struct_valid(srv, backups[backup_index]))
   {
      keep_backup(srv, backups[backup_index]->label, k);
      pgmoneta_json_append(bcks, (uintptr_t)backups[backup_index]->label, ValueString);
      kr = k;
   }

   if (cascade)
   {
      struct backup* temp_bck = NULL;

      if (backups[backup_index]->type != TYPE_FULL)
      {
         bck = (struct backup*)malloc(sizeof(struct backup));
         memcpy(bck, backups[backup_index], sizeof(struct backup));

         while (!pgmoneta_get_backup_parent(srv, bck, &temp_bck))
         {
            free(bck);
            bck = temp_bck;
            temp_bck = NULL;

            if (pgmoneta_is_backup_struct_valid(srv, bck))
            {
               keep_backup(srv, bck->label, k);
               pgmoneta_json_append(bcks, (uintptr_t)bck->label, ValueString);
            }
         }

         free(bck);
         bck = NULL;
      }
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUPS, (uintptr_t)bcks, ValueJSON);

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_VALID, (uintptr_t)backups[backup_index]->valid, ValueInt8);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_COMMENTS, (uintptr_t)backups[backup_index]->comments, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_KEEP, (uintptr_t)kr, ValueBool);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_CASCADE, (uintptr_t)cascade, ValueBool);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(ssl, client_fd, start_t, end_t, compression, encryption, payload))
   {
      if (k)
      {
         ec = MANAGEMENT_ERROR_RETAIN_NETWORK;
         pgmoneta_log_error("Retain: Error sending response");
      }
      else
      {
         ec = MANAGEMENT_ERROR_EXPUNGE_NETWORK;
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
   free(bck);
   pgmoneta_json_destroy(bcks);

   free(d);
   free(elapsed);

   exit(0);

error:

   if (k)
   {
      pgmoneta_management_response_error(ssl, client_fd, config->common.servers[srv].name,
                                         ec != -1 ? ec : MANAGEMENT_ERROR_RETAIN_ERROR, en != NULL ? en : NAME,
                                         compression, encryption, payload);
   }
   else
   {
      pgmoneta_management_response_error(ssl, client_fd, config->common.servers[srv].name,
                                         ec != -1 ? ec : MANAGEMENT_ERROR_EXPUNGE_ERROR, en != NULL ? en : NAME,
                                         compression, encryption, payload);
   }

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);
   free(bck);
   pgmoneta_json_destroy(bcks);

   free(d);
   free(elapsed);

   exit(1);
}

static void
keep_backup(int srv, char* label, bool k)
{
   char* d = NULL;
   struct backup* backup = NULL;

   d = pgmoneta_get_server_backup(srv);

   if (pgmoneta_load_info(d, label, &backup))
   {
      pgmoneta_log_error("Unable to get backup for directory %s", d);
      return;
   }
   backup->keep = k;
   if (pgmoneta_save_info(d, backup))
   {
      pgmoneta_log_error("Unable to save backup info for directory %s", d);
      return;
   }

   free(d);
   free(backup);
}
