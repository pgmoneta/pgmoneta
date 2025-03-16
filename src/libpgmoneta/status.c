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
#include <json.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <status.h>
#include <utils.h>

#define NAME "status"

void
pgmoneta_status(SSL* ssl, int client_fd, bool offline, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* d = NULL;
   int32_t retention_days;
   int32_t retention_weeks;
   int32_t retention_months;
   int32_t retention_years;
   uint64_t used_size;
   uint64_t free_size;
   uint64_t total_size;
   uint64_t hot_standby_size;
   int32_t number_of_directories = 0;
   char** array = NULL;
   uint64_t server_size;
   char* elapsed = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   int32_t number_of_backups = 0;
   struct backup** backups = NULL;
   struct json* response = NULL;
   struct json* servers = NULL;
   struct configuration* config;

   pgmoneta_start_logging();

   config = (struct configuration*)shmem;

   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);

   if (pgmoneta_management_create_response(payload, -1, &response))
   {
      goto error;
   }

   d = NULL;
   d = pgmoneta_append(d, config->base_dir);
   d = pgmoneta_append(d, "/");

   used_size = pgmoneta_directory_size(d);

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_USED_SPACE, (uintptr_t)used_size, ValueUInt64);

   free(d);
   d = NULL;

   free_size = pgmoneta_free_space(config->base_dir);
   total_size = pgmoneta_total_space(config->base_dir);

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_FREE_SPACE, (uintptr_t)free_size, ValueUInt64);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_TOTAL_SPACE, (uintptr_t)total_size, ValueUInt64);

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_OFFLINE, (uintptr_t)offline, ValueBool);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_WORKERS, (uintptr_t)config->workers, ValueInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_NUMBER_OF_SERVERS, (uintptr_t)config->number_of_servers, ValueInt32);

   pgmoneta_json_create(&servers);

   for (int i = 0; i < config->number_of_servers; i++)
   {
      struct json* js = NULL;

      pgmoneta_json_create(&js);

      retention_days = config->servers[i].retention_days;
      if (retention_days <= 0)
      {
         retention_days = config->retention_days;
      }

      retention_weeks = config->servers[i].retention_weeks;
      if (retention_weeks <= 0)
      {
         retention_weeks = config->retention_weeks;
      }

      retention_months = config->servers[i].retention_months;
      if (retention_months <= 0)
      {
         retention_months = config->retention_months;
      }

      retention_years = config->servers[i].retention_years;
      if (retention_years <= 0)
      {
         retention_years = config->retention_years;
      }

      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_RETENTION_DAYS, (uintptr_t)retention_days, ValueInt32);
      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_RETENTION_WEEKS, (uintptr_t)retention_weeks, ValueInt32);
      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_RETENTION_MONTHS, (uintptr_t)retention_months, ValueInt32);
      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_RETENTION_YEARS, (uintptr_t)retention_years, ValueInt32);

      d = pgmoneta_get_server_backup(i);

      pgmoneta_get_backups(d, &number_of_backups, &backups);
      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_NUMBER_OF_BACKUPS, (uintptr_t)number_of_backups, ValueInt32);

      free(d);
      d = NULL;

      d = pgmoneta_get_server(i);

      server_size = pgmoneta_directory_size(d);

      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_SERVER_SIZE, (uintptr_t)server_size, ValueUInt64);

      free(d);
      d = NULL;

      if (strlen(config->servers[i].hot_standby) > 0)
      {
         hot_standby_size = pgmoneta_directory_size(config->servers[i].hot_standby);
      }
      else
      {
         hot_standby_size = 0;
      }

      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_HOT_STANDBY_SIZE, (uintptr_t)hot_standby_size, ValueUInt64);

      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->servers[i].name, ValueString);

      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_WORKERS, (uintptr_t)(config->servers[i].workers != -1 ? config->servers[i].workers : config->workers), ValueInt32);

      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_CHECKSUMS, (uintptr_t)config->servers[i].checksums, ValueBool);

      pgmoneta_json_append(servers, (uintptr_t)js, ValueJSON);

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);
      backups = NULL;

      for (int j = 0; j < number_of_directories; j++)
      {
         free(array[j]);
      }
      free(array);
      array = NULL;

      free(d);
      d = NULL;
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVERS, (uintptr_t)servers, ValueJSON);

   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_STATUS_NETWORK, NAME, compression, encryption, payload);
      pgmoneta_log_error("Status: Error sending response");

      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("Status (Elapsed: %s)", elapsed);

   pgmoneta_json_destroy(payload);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(0);

error:

   for (int i = 0; i < number_of_directories; i++)
   {
      free(array[i]);
   }
   free(array);

   free(d);

   pgmoneta_json_destroy(payload);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(1);
}

void
pgmoneta_status_details(SSL* ssl, int client_fd, bool offline, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* d = NULL;
   int32_t retention_days;
   int32_t retention_weeks;
   int32_t retention_months;
   int32_t retention_years;
   uint64_t used_size;
   uint64_t free_size;
   uint64_t total_size;
   uint64_t hot_standby_size;
   int32_t number_of_directories = 0;
   char** array = NULL;
   uint64_t server_size;
   char* elapsed = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   uint64_t wal;
   uint64_t delta;
   int32_t number_of_backups = 0;
   struct backup** backups = NULL;
   struct json* response = NULL;
   struct json* servers = NULL;
   struct json* bcks = NULL;
   struct configuration* config;

   pgmoneta_start_logging();

   config = (struct configuration*)shmem;

   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);

   if (pgmoneta_management_create_response(payload, -1, &response))
   {
      goto error;
   }

   d = NULL;
   d = pgmoneta_append(d, config->base_dir);
   d = pgmoneta_append(d, "/");

   used_size = pgmoneta_directory_size(d);

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_USED_SPACE, (uintptr_t)used_size, ValueUInt64);

   free(d);
   d = NULL;

   free_size = pgmoneta_free_space(config->base_dir);
   total_size = pgmoneta_total_space(config->base_dir);

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_FREE_SPACE, (uintptr_t)free_size, ValueUInt64);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_TOTAL_SPACE, (uintptr_t)total_size, ValueUInt64);

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_OFFLINE, (uintptr_t)offline, ValueBool);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_WORKERS, (uintptr_t)config->workers, ValueInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_NUMBER_OF_SERVERS, (uintptr_t)config->number_of_servers, ValueInt32);

   pgmoneta_json_create(&servers);

   for (int i = 0; i < config->number_of_servers; i++)
   {
      char* wal_dir = NULL;
      struct json* js = NULL;

      wal_dir = pgmoneta_get_server_wal(i);

      pgmoneta_json_create(&js);

      retention_days = config->servers[i].retention_days;
      if (retention_days <= 0)
      {
         retention_days = config->retention_days;
      }

      retention_weeks = config->servers[i].retention_weeks;
      if (retention_weeks <= 0)
      {
         retention_weeks = config->retention_weeks;
      }

      retention_months = config->servers[i].retention_months;
      if (retention_months <= 0)
      {
         retention_months = config->retention_months;
      }

      retention_years = config->servers[i].retention_years;
      if (retention_years <= 0)
      {
         retention_years = config->retention_years;
      }

      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_RETENTION_DAYS, (uintptr_t)retention_days, ValueInt32);
      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_RETENTION_WEEKS, (uintptr_t)retention_weeks, ValueInt32);
      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_RETENTION_MONTHS, (uintptr_t)retention_months, ValueInt32);
      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_RETENTION_YEARS, (uintptr_t)retention_years, ValueInt32);

      d = pgmoneta_get_server(i);

      server_size = pgmoneta_directory_size(d);

      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_SERVER_SIZE, (uintptr_t)server_size, ValueUInt64);

      free(d);
      d = NULL;

      if (strlen(config->servers[i].hot_standby) > 0)
      {
         hot_standby_size = pgmoneta_directory_size(config->servers[i].hot_standby);
      }
      else
      {
         hot_standby_size = 0;
      }

      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_HOT_STANDBY_SIZE, (uintptr_t)hot_standby_size, ValueUInt64);

      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->servers[i].name, ValueString);

      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_WORKERS, (uintptr_t)(config->servers[i].workers != -1 ? config->servers[i].workers : config->workers), ValueInt32);

      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_CHECKSUMS, (uintptr_t)config->servers[i].checksums, ValueBool);

      free(d);
      d = NULL;

      d = pgmoneta_get_server_backup(i);

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_NUMBER_OF_BACKUPS, (uintptr_t)number_of_backups, ValueInt32);

      if (pgmoneta_json_create(&bcks))
      {
         goto error;
      }

      for (int j = 0; j < number_of_backups; j++)
      {
         struct json* bck = NULL;

         if (backups[j] != NULL)
         {
            if (pgmoneta_json_create(&bck))
            {
               goto error;
            }

            pgmoneta_json_put(bck, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)backups[j]->label, ValueString);
            pgmoneta_json_put(bck, MANAGEMENT_ARGUMENT_KEEP, (uintptr_t)backups[j]->keep, ValueBool);
            pgmoneta_json_put(bck, MANAGEMENT_ARGUMENT_VALID, (uintptr_t)backups[j]->valid, ValueInt8);
            pgmoneta_json_put(bck, MANAGEMENT_ARGUMENT_BACKUP_SIZE, (uintptr_t)backups[j]->backup_size, ValueUInt64);
            pgmoneta_json_put(bck, MANAGEMENT_ARGUMENT_RESTORE_SIZE, (uintptr_t)backups[j]->restore_size, ValueUInt64);
            pgmoneta_json_put(bck, MANAGEMENT_ARGUMENT_BIGGEST_FILE_SIZE, (uintptr_t)backups[j]->biggest_file_size, ValueUInt64);
            pgmoneta_json_put(bck, MANAGEMENT_ARGUMENT_COMMENTS, (uintptr_t)backups[j]->comments, ValueString);
            pgmoneta_json_put(bck, MANAGEMENT_ARGUMENT_COMPRESSION, (uintptr_t)backups[j]->compression, ValueInt32);
            pgmoneta_json_put(bck, MANAGEMENT_ARGUMENT_ENCRYPTION, (uintptr_t)backups[j]->encryption, ValueInt32);

            wal = pgmoneta_number_of_wal_files(wal_dir, &backups[j]->wal[0], NULL);
            wal *= config->servers[i].wal_size;

            pgmoneta_json_put(bck, MANAGEMENT_ARGUMENT_WAL, (uintptr_t)wal, ValueUInt64);

            delta = 0;
            if (j > 0)
            {
               delta = pgmoneta_number_of_wal_files(wal_dir, &backups[j - 1]->wal[0], &backups[j]->wal[0]);
               delta *= config->servers[i].wal_size;
            }

            pgmoneta_json_put(bck, MANAGEMENT_ARGUMENT_DELTA, (uintptr_t)delta, ValueUInt64);

            pgmoneta_json_append(bcks, (uintptr_t)bck, ValueJSON);
         }
      }

      pgmoneta_json_put(js, MANAGEMENT_ARGUMENT_BACKUPS, (uintptr_t)bcks, ValueJSON);

      pgmoneta_json_append(servers, (uintptr_t)js, ValueJSON);

      for (int j = 0; j < number_of_directories; j++)
      {
         free(array[j]);
      }
      free(array);
      array = NULL;

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);
      backups = NULL;

      free(wal_dir);
      wal_dir = NULL;

      free(d);
      d = NULL;
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVERS, (uintptr_t)servers, ValueJSON);

   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_STATUS_DETAILS_NETWORK, NAME, compression, encryption, payload);
      pgmoneta_log_error("Status details: Error sending response");

      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("Status details (Elapsed: %s)", elapsed);

   pgmoneta_json_destroy(payload);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(0);

error:

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   for (int i = 0; i < number_of_directories; i++)
   {
      free(array[i]);
   }
   free(array);

   free(d);

   pgmoneta_json_destroy(payload);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(1);
}
