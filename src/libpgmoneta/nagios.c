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
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <art.h>
#include <deque.h>
#include <logging.h>
#include <memory.h>
#include <nagios.h>
#include <network.h>
#include <prometheus_client.h>
#include <utils.h>

/* system */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void nagios_page(SSL* client_ssl, int client_fd);
static int send_nagios_response(SSL* client_ssl, int client_fd, char* data);

void
pgmoneta_nagios(SSL* client_ssl, int client_fd)
{
   pgmoneta_start_logging();
   pgmoneta_memory_init();

   nagios_page(client_ssl, client_fd);

   pgmoneta_close_ssl(client_ssl);
   pgmoneta_disconnect(client_fd);
   pgmoneta_memory_destroy();
   pgmoneta_stop_logging();
   exit(0);
}

static void
nagios_page(SSL* client_ssl, int client_fd)
{
   struct prometheus_bridge* bridge = NULL;
   struct art_iterator* iter = NULL;
   char* output = NULL;
   char* perf_data = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (pgmoneta_prometheus_client_create_bridge(&bridge))
   {
      pgmoneta_log_error("Nagios: Failed to create Prometheus bridge");
      goto error;
   }

   if (pgmoneta_prometheus_client_get(config->metrics, bridge))
   {
      pgmoneta_log_error("Nagios: Failed to fetch metrics from Prometheus endpoint");
      goto error;
   }

   if (pgmoneta_art_iterator_create(bridge->metrics, &iter))
   {
      pgmoneta_log_error("Nagios: Failed to create metrics iterator");
      goto error;
   }

   perf_data = pgmoneta_append(perf_data, "");

   while (pgmoneta_art_iterator_next(iter))
   {
      struct prometheus_metric* metric = (struct prometheus_metric*)iter->value->data;
      if (metric == NULL)
      {
         continue;
      }

      struct deque_iterator* def_iter = NULL;
      if (pgmoneta_deque_iterator_create(metric->definitions, &def_iter))
      {
         continue;
      }

      while (pgmoneta_deque_iterator_next(def_iter))
      {
         struct prometheus_attributes* attrs = (struct prometheus_attributes*)def_iter->value->data;
         if (attrs == NULL || attrs->values == NULL)
         {
            continue;
         }

         struct deque_iterator* val_iter = NULL;
         if (pgmoneta_deque_iterator_create(attrs->values, &val_iter))
         {
            continue;
         }

         while (pgmoneta_deque_iterator_next(val_iter))
         {
            struct prometheus_value* val = (struct prometheus_value*)val_iter->value->data;
            if (val == NULL || val->value == NULL)
            {
               continue;
            }

            if (strlen(perf_data) > 0)
            {
               perf_data = pgmoneta_append(perf_data, " ");
            }
            perf_data = pgmoneta_append(perf_data, metric->name);
            perf_data = pgmoneta_append(perf_data, "=");
            perf_data = pgmoneta_append(perf_data, val->value);
         }
         pgmoneta_deque_iterator_destroy(val_iter);
      }
      pgmoneta_deque_iterator_destroy(def_iter);
   }

   pgmoneta_art_iterator_destroy(iter);
   iter = NULL;

   output = pgmoneta_append(output, "PGMONETA OK - pgmoneta is running");
   if (perf_data != NULL && strlen(perf_data) > 0)
   {
      output = pgmoneta_append(output, "|");
      output = pgmoneta_append(output, perf_data);
   }
   output = pgmoneta_append(output, "\n");

   send_nagios_response(client_ssl, client_fd, output);

   free(output);
   free(perf_data);
   pgmoneta_prometheus_client_destroy_bridge(bridge);
   return;

error:
   if (iter != NULL)
   {
      pgmoneta_art_iterator_destroy(iter);
   }
   if (bridge != NULL)
   {
      pgmoneta_prometheus_client_destroy_bridge(bridge);
   }
   free(perf_data);
   output = pgmoneta_append(NULL, "PGMONETA UNKNOWN - Failed to retrieve metrics\n");
   send_nagios_response(client_ssl, client_fd, output);
   free(output);
}

static int
send_nagios_response(SSL* client_ssl, int client_fd, char* data)
{
   int status;
   char* m = NULL;
   struct message msg;

   memset(&msg, 0, sizeof(struct message));

   m = malloc(20);
   if (m == NULL)
   {
      return 1;
   }
   memset(m, 0, 20);
   sprintf(m, "%zX\r\n", strlen(data));
   m = pgmoneta_append(m, data);
   m = pgmoneta_append(m, "\r\n");
   msg.kind = 0;
   msg.length = strlen(m);
   msg.data = m;
   status = pgmoneta_write_message(client_ssl, client_fd, &msg);
   free(m);
   return status;
}
