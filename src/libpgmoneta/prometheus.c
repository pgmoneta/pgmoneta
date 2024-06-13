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
#include <info.h>
#include <logging.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <prometheus.h>
#include <shmem.h>
#include <utils.h>
#include <wal.h>

/* system */
#include <ev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#define CHUNK_SIZE 32768

#define PAGE_UNKNOWN 0
#define PAGE_HOME    1
#define PAGE_METRICS 2
#define BAD_REQUEST  3

static int resolve_page(struct message* msg);
static int unknown_page(int client_fd);
static int home_page(int client_fd);
static int metrics_page(int client_fd);
static int bad_request(int client_fd);

static void general_information(int client_fd);
static void backup_information(int client_fd);
static void size_information(int client_fd);

static int send_chunk(int client_fd, char* data);

static bool is_metrics_cache_configured(void);
static bool is_metrics_cache_valid(void);
static bool metrics_cache_append(char* data);
static bool metrics_cache_finalize(void);
static size_t metrics_cache_size_to_alloc(void);
static void metrics_cache_invalidate(void);

void
pgmoneta_prometheus(int client_fd)
{
   int status;
   int page;
   struct message* msg = NULL;
   struct configuration* config;

   pgmoneta_start_logging();
   pgmoneta_memory_init();

   config = (struct configuration*)shmem;

   status = pgmoneta_read_timeout_message(NULL, client_fd, config->authentication_timeout, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   page = resolve_page(msg);

   if (page == PAGE_HOME)
   {
      home_page(client_fd);
   }
   else if (page == PAGE_METRICS)
   {
      metrics_page(client_fd);
   }
   else if (page == PAGE_UNKNOWN)
   {
      unknown_page(client_fd);
   }
   else
   {
      bad_request(client_fd);
   }

   pgmoneta_disconnect(client_fd);

   pgmoneta_memory_destroy();
   pgmoneta_stop_logging();

   exit(0);

error:

   pgmoneta_disconnect(client_fd);

   pgmoneta_memory_destroy();
   pgmoneta_stop_logging();

   exit(1);
}

void
pgmoneta_prometheus_reset(void)
{
   signed char cache_is_free;
   struct configuration* config;
   struct prometheus_cache* cache;

   config = (struct configuration*)shmem;
   cache = (struct prometheus_cache*)prometheus_cache_shmem;

retry_cache_locking:
   cache_is_free = STATE_FREE;
   if (atomic_compare_exchange_strong(&cache->lock, &cache_is_free, STATE_IN_USE))
   {
      metrics_cache_invalidate();

      atomic_store(&config->prometheus.logging_info, 0);
      atomic_store(&config->prometheus.logging_warn, 0);
      atomic_store(&config->prometheus.logging_error, 0);
      atomic_store(&config->prometheus.logging_fatal, 0);

      atomic_store(&cache->lock, STATE_FREE);
   }
   else
   {
      /* Sleep for 1ms */
      SLEEP_AND_GOTO(1000000L, retry_cache_locking);
   }
}

void
pgmoneta_prometheus_logging(int type)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   switch (type)
   {
      case PGMONETA_LOGGING_LEVEL_INFO:
         atomic_fetch_add(&config->prometheus.logging_info, 1);
         break;
      case PGMONETA_LOGGING_LEVEL_WARN:
         atomic_fetch_add(&config->prometheus.logging_warn, 1);
         break;
      case PGMONETA_LOGGING_LEVEL_ERROR:
         atomic_fetch_add(&config->prometheus.logging_error, 1);
         break;
      case PGMONETA_LOGGING_LEVEL_FATAL:
         atomic_fetch_add(&config->prometheus.logging_fatal, 1);
         break;
      default:
         break;
   }
}

static int
resolve_page(struct message* msg)
{
   char* from = NULL;
   int index;

   if (msg->length < 3 || strncmp((char*)msg->data, "GET", 3) != 0)
   {
      pgmoneta_log_debug("Promethus: Not a GET request");
      return BAD_REQUEST;
   }

   index = 4;
   from = (char*)msg->data + index;

   while (pgmoneta_read_byte(msg->data + index) != ' ')
   {
      index++;
   }

   pgmoneta_write_byte(msg->data + index, '\0');

   if (strcmp(from, "/") == 0 || strcmp(from, "/index.html") == 0)
   {
      return PAGE_HOME;
   }
   else if (strcmp(from, "/metrics") == 0)
   {
      return PAGE_METRICS;
   }

   return PAGE_UNKNOWN;
}

static int
unknown_page(int client_fd)
{
   char* data = NULL;
   time_t now;
   char time_buf[32];
   int status;
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&data, 0, sizeof(data));

   now = time(NULL);

   memset(&time_buf, 0, sizeof(time_buf));
   ctime_r(&now, &time_buf[0]);
   time_buf[strlen(time_buf) - 1] = 0;

   data = pgmoneta_append(data, "HTTP/1.1 403 Forbidden\r\n");
   data = pgmoneta_append(data, "Date: ");
   data = pgmoneta_append(data, &time_buf[0]);
   data = pgmoneta_append(data, "\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgmoneta_write_message(NULL, client_fd, &msg);

   free(data);

   return status;
}

static int
home_page(int client_fd)
{
   char* data = NULL;
   time_t now;
   char time_buf[32];
   int status;
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&data, 0, sizeof(data));

   now = time(NULL);

   memset(&time_buf, 0, sizeof(time_buf));
   ctime_r(&now, &time_buf[0]);
   time_buf[strlen(time_buf) - 1] = 0;

   data = pgmoneta_append(data, "HTTP/1.1 200 OK\r\n");
   data = pgmoneta_append(data, "Content-Type: text/html; charset=utf-8\r\n");
   data = pgmoneta_append(data, "Date: ");
   data = pgmoneta_append(data, &time_buf[0]);
   data = pgmoneta_append(data, "\r\n");
   data = pgmoneta_append(data, "Transfer-Encoding: chunked\r\n");
   data = pgmoneta_append(data, "\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgmoneta_write_message(NULL, client_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto done;
   }

   free(data);
   data = NULL;

   data = pgmoneta_append(data, "<html>\n");
   data = pgmoneta_append(data, "<head>\n");
   data = pgmoneta_append(data, "  <title>pgmoneta exporter</title>\n");
   data = pgmoneta_append(data, "</head>\n");
   data = pgmoneta_append(data, "<body>\n");
   data = pgmoneta_append(data, "  <h1>pgmoneta exporter</h1>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <a href=\"/metrics\">Metrics</a>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_state</h2>\n");
   data = pgmoneta_append(data, "  The state of pgmoneta\n");
   data = pgmoneta_append(data, "  <ul>\n");
   data = pgmoneta_append(data, "    <li>1 = Running</li>\n");
   data = pgmoneta_append(data, "  </ul>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_version</h2>\n");
   data = pgmoneta_append(data, "  The version of pgmoneta\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_logging_info</h2>\n");
   data = pgmoneta_append(data, "  The number of INFO logging statements\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_logging_warn</h2>\n");
   data = pgmoneta_append(data, "  The number of WARN logging statements\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_logging_error</h2>\n");
   data = pgmoneta_append(data, "  The number of ERROR logging statements\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_logging_fatal</h2>\n");
   data = pgmoneta_append(data, "  The number of FATAL logging statements\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_retention_days</h2>\n");
   data = pgmoneta_append(data, "  The retention of pgmoneta in days\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_retention_weeks</h2>\n");
   data = pgmoneta_append(data, "  The retention of pgmoneta in weeks\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_retention_months</h2>\n");
   data = pgmoneta_append(data, "  The retention of pgmoneta in months\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_retention_years</h2>\n");
   data = pgmoneta_append(data, "  The retention of pgmoneta in years\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_retention_server</h2>\n");
   data = pgmoneta_append(data, "  The retention of a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>parameter</td>\n");
   data = pgmoneta_append(data, "        <td>days|weeks|months|years</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_compression</h2>\n");
   data = pgmoneta_append(data, "  The compression used\n");
   data = pgmoneta_append(data, "  <ul>\n");
   data = pgmoneta_append(data, "    <li>0 = None</li>\n");
   data = pgmoneta_append(data, "    <li>1 = GZip</li>\n");
   data = pgmoneta_append(data, "    <li>2 = ZSTD</li>\n");
   data = pgmoneta_append(data, "    <li>3 = LZ4</li>\n");
   data = pgmoneta_append(data, "    <li>4 = BZIP2</li>\n");
   data = pgmoneta_append(data, "  </ul>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_used_space</h2>\n");
   data = pgmoneta_append(data, "  The disk space used for pgmoneta\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_free_space</h2>\n");
   data = pgmoneta_append(data, "  The free disk space for pgmoneta\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_total_space</h2>\n");
   data = pgmoneta_append(data, "  The total disk space for pgmoneta\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_server_valid</h2>\n");
   data = pgmoneta_append(data, "  Is the server in a valid state\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_wal_streaming</h2>\n");
   data = pgmoneta_append(data, "  The WAL streaming status of a server\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_server_operation_count</h2>\n");
   data = pgmoneta_append(data, "  The count of client operations of a server\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_server_failed_operation_count</h2>\n");
   data = pgmoneta_append(data, "  The count of failed client operations of a server\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_server_last_operation_time</h2>\n");
   data = pgmoneta_append(data, "  The time of the latest client operation of a server \n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_server_last_failed_operation_time</h2>\n");
   data = pgmoneta_append(data, "  The time of the latest failed client operation of a server \n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_wal_shipping</h2>\n");
   data = pgmoneta_append(data, "  The disk space used for WAL shipping for a server\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_wal_shipping_used_space</h2>\n");
   data = pgmoneta_append(data, "  The disk space used for everything under the WAL shipping directory of a server\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_wal_shipping_free_space</h2>\n");
   data = pgmoneta_append(data, "  The free disk space for the WAL shipping directory of a server\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_wal_shipping_total_space</h2>\n");
   data = pgmoneta_append(data, "  The total disk space for the WAL shipping directory of a server\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_hot_standby</h2>\n");
   data = pgmoneta_append(data, "  The disk space used for hot standby for a server\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_hot_standby_free_space</h2>\n");
   data = pgmoneta_append(data, "  The free disk space for the hot standby directory of a server\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_hot_standby_total_space</h2>\n");
   data = pgmoneta_append(data, "  The total disk space for the hot standby directory of a server\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_server_timeline</h2>\n");
   data = pgmoneta_append(data, "  The current timeline a server is on\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_server_parent_tli</h2>\n");
   data = pgmoneta_append(data, "  The parent timeline of a timeline on a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>tli</td>\n");
   data = pgmoneta_append(data, "        <td>The current/previous timeline ID in the server history</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_server_timeline_switchpos</h2>\n");
   data = pgmoneta_append(data, "  The WAL switch position of a timeline on a server (showed in hex as a parameter)\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>tli</td>\n");
   data = pgmoneta_append(data, "        <td>The current/previous timeline ID in the server history</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>walpos</td>\n");
   data = pgmoneta_append(data, "        <td>The WAL switch position of this timeline</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_server_workers</h2>\n");
   data = pgmoneta_append(data, "  The number of workers for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_backup_oldest</h2>\n");
   data = pgmoneta_append(data, "  The oldest backup for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_backup_newest</h2>\n");
   data = pgmoneta_append(data, "  The newest backup for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_backup_count</h2>\n");
   data = pgmoneta_append(data, "  The number of valid backups for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_backup</h2>\n");
   data = pgmoneta_append(data, "  Is the backup valid for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>label</td>\n");
   data = pgmoneta_append(data, "        <td>The backup label</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_backup_version</h2>\n");
   data = pgmoneta_append(data, "  The version of PostgreSQL for a backup\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>label</td>\n");
   data = pgmoneta_append(data, "        <td>The backup label</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>major</td>\n");
   data = pgmoneta_append(data, "        <td>The backup PostgreSQL major version</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>minor</td>\n");
   data = pgmoneta_append(data, "        <td>The backup PostgreSQL minor version</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_backup_throughput</h2>\n");
   data = pgmoneta_append(data, "  The throughput of the backup for a server (bytes/s)\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>label</td>\n");
   data = pgmoneta_append(data, "        <td>The backup label</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_backup_elapsed_time</h2>\n");
   data = pgmoneta_append(data, "  The backup in seconds for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>label</td>\n");
   data = pgmoneta_append(data, "        <td>The backup label</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_backup_start_timeline</h2>\n");
   data = pgmoneta_append(data, "  The starting timeline of a backup for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>label</td>\n");
   data = pgmoneta_append(data, "        <td>The backup label</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_backup_end_timeline</h2>\n");
   data = pgmoneta_append(data, "  The ending timeline of a backup for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>label</td>\n");
   data = pgmoneta_append(data, "        <td>The backup label</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_backup_start_walpos</h2>\n");
   data = pgmoneta_append(data, "  The starting WAL position of a backup for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>label</td>\n");
   data = pgmoneta_append(data, "        <td>The backup label</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>walpos</td>\n");
   data = pgmoneta_append(data, "        <td>The backup starting WAL position</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_backup_checkpoint_walpos</h2>\n");
   data = pgmoneta_append(data, "  The checkpoint WAL pos of a backup for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>label</td>\n");
   data = pgmoneta_append(data, "        <td>The backup label</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>walpos</td>\n");
   data = pgmoneta_append(data, "        <td>The backup checkpoint WAL position</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_backup_end_walpos</h2>\n");
   data = pgmoneta_append(data, "  The ending WAL pos of a backup for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>label</td>\n");
   data = pgmoneta_append(data, "        <td>The backup label</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>walpos</td>\n");
   data = pgmoneta_append(data, "        <td>The backup ending WAL position</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_restore_newest_size</h2>\n");
   data = pgmoneta_append(data, "  The size of the newest restore for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_backup_newest_size</h2>\n");
   data = pgmoneta_append(data, "  The size of the newest backup for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_restore_size</h2>\n");
   data = pgmoneta_append(data, "  The size of a restore for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>label</td>\n");
   data = pgmoneta_append(data, "        <td>The backup label</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_restore_size_increment</h2>\n");
   data = pgmoneta_append(data, "  The increment size of a restore for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>label</td>\n");
   data = pgmoneta_append(data, "        <td>The backup label</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_backup_size</h2>\n");
   data = pgmoneta_append(data, "  The size of a backup for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>label</td>\n");
   data = pgmoneta_append(data, "        <td>The backup label</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_backup_compression_ratio</h2>\n");
   data = pgmoneta_append(data, "  The ratio of backup size to restore size for each backup\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>label</td>\n");
   data = pgmoneta_append(data, "        <td>The backup label</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_backup_retain</h2>\n");
   data = pgmoneta_append(data, "  Retain a backup for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>label</td>\n");
   data = pgmoneta_append(data, "        <td>The backup label</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_backup_total_size</h2>\n");
   data = pgmoneta_append(data, "  The total size of the backups for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_wal_total_size</h2>\n");
   data = pgmoneta_append(data, "  The total size of the WAL for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_total_size</h2>\n");
   data = pgmoneta_append(data, "  The total size for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_active_backup</h2>\n");
   data = pgmoneta_append(data, "  Is there an active backup for a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_current_wal_file</h2>\n");
   data = pgmoneta_append(data, "  The current streaming WAL filename of a server\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>file</td>\n");
   data = pgmoneta_append(data, "        <td>The current WAL filename for this server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_current_wal_lsn</h2>\n");
   data = pgmoneta_append(data, "  The current WAL log sequence number\n");
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>name</td>\n");
   data = pgmoneta_append(data, "        <td>The identifier for the server</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>lsn</td>\n");
   data = pgmoneta_append(data, "        <td>The current WAL log sequence number</td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <a href=\"https://pgmoneta.github.io/\">pgmoneta.github.io/</a>\n");
   data = pgmoneta_append(data, "</body>\n");
   data = pgmoneta_append(data, "</html>\n");

   send_chunk(client_fd, data);
   free(data);
   data = NULL;

   /* Footer */
   data = pgmoneta_append(data, "0\r\n\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgmoneta_write_message(NULL, client_fd, &msg);

done:
   if (data != NULL)
   {
      free(data);
   }

   return status;
}

static int
metrics_page(int client_fd)
{
   char* data = NULL;
   time_t now;
   char time_buf[32];
   int status;
   struct message msg;
   struct prometheus_cache* cache;
   signed char cache_is_free;

   cache = (struct prometheus_cache*)prometheus_cache_shmem;

   memset(&msg, 0, sizeof(struct message));

retry_cache_locking:
   cache_is_free = STATE_FREE;
   if (atomic_compare_exchange_strong(&cache->lock, &cache_is_free, STATE_IN_USE))
   {
      // can serve the message out of cache?
      if (is_metrics_cache_configured() && is_metrics_cache_valid())
      {
         // serve the message directly out of the cache
         pgmoneta_log_debug("Serving metrics out of cache (%d/%d bytes valid until %lld)",
                            strlen(cache->data),
                            cache->size,
                            cache->valid_until);

         msg.kind = 0;
         msg.length = strlen(cache->data);
         msg.data = cache->data;
      }
      else
      {
         // build the message without the cache
         metrics_cache_invalidate();

         now = time(NULL);

         memset(&time_buf, 0, sizeof(time_buf));
         ctime_r(&now, &time_buf[0]);
         time_buf[strlen(time_buf) - 1] = 0;

         data = pgmoneta_append(data, "HTTP/1.1 200 OK\r\n");
         data = pgmoneta_append(data, "Content-Type: text/plain; version=0.0.1; charset=utf-8\r\n");
         data = pgmoneta_append(data, "Date: ");
         data = pgmoneta_append(data, &time_buf[0]);
         data = pgmoneta_append(data, "\r\n");
         data = pgmoneta_append(data, "Transfer-Encoding: chunked\r\n");
         data = pgmoneta_append(data, "\r\n");
         metrics_cache_append(data);

         msg.kind = 0;
         msg.length = strlen(data);
         msg.data = data;

         status = pgmoneta_write_message(NULL, client_fd, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }

         free(data);
         data = NULL;

         general_information(client_fd);
         backup_information(client_fd);
         size_information(client_fd);

         /* Footer */
         data = pgmoneta_append(data, "0\r\n\r\n");
         metrics_cache_append(data);

         msg.kind = 0;
         msg.length = strlen(data);
         msg.data = data;

         metrics_cache_finalize();
      }

      // free the cache
      atomic_store(&cache->lock, STATE_FREE);
   }
   else
   {
      /* Sleep for 1ms */
      SLEEP_AND_GOTO(1000000L, retry_cache_locking)
   }

   status = pgmoneta_write_message(NULL, client_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   free(data);

   return 0;

error:

   free(data);

   return 1;
}

static int
bad_request(int client_fd)
{
   char* data = NULL;
   time_t now;
   char time_buf[32];
   int status;
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&data, 0, sizeof(data));

   now = time(NULL);

   memset(&time_buf, 0, sizeof(time_buf));
   ctime_r(&now, &time_buf[0]);
   time_buf[strlen(time_buf) - 1] = 0;

   data = pgmoneta_append(data, "HTTP/1.1 400 Bad Request\r\n");
   data = pgmoneta_append(data, "Date: ");
   data = pgmoneta_append(data, &time_buf[0]);
   data = pgmoneta_append(data, "\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgmoneta_write_message(NULL, client_fd, &msg);

   free(data);

   return status;
}

static void
general_information(int client_fd)
{
   char* d;
   unsigned long size;
   int retention;
   char* data = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   data = pgmoneta_append(data, "#HELP pgmoneta_state The state of pgmoneta\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_state gauge\n");
   data = pgmoneta_append(data, "pgmoneta_state ");
   data = pgmoneta_append(data, "1");
   data = pgmoneta_append(data, "\n\n");
   data = pgmoneta_append(data, "#HELP pgmoneta_version The version of pgmoneta\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_version gauge\n");
   data = pgmoneta_append(data, "pgmoneta_version{version=\"");
   data = pgmoneta_append(data, VERSION);
   data = pgmoneta_append(data, "\"} 1");
   data = pgmoneta_append(data, "\n\n");
   data = pgmoneta_append(data, "#HELP pgmoneta_logging_info The number of INFO logging statements\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_logging_info gauge\n");
   data = pgmoneta_append(data, "pgmoneta_logging_info ");
   data = pgmoneta_append_ulong(data, atomic_load(&config->prometheus.logging_info));
   data = pgmoneta_append(data, "\n\n");
   data = pgmoneta_append(data, "#HELP pgmoneta_logging_warn The number of WARN logging statements\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_logging_warn gauge\n");
   data = pgmoneta_append(data, "pgmoneta_logging_warn ");
   data = pgmoneta_append_ulong(data, atomic_load(&config->prometheus.logging_warn));
   data = pgmoneta_append(data, "\n\n");
   data = pgmoneta_append(data, "#HELP pgmoneta_logging_error The number of ERROR logging statements\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_logging_error gauge\n");
   data = pgmoneta_append(data, "pgmoneta_logging_error ");
   data = pgmoneta_append_ulong(data, atomic_load(&config->prometheus.logging_error));
   data = pgmoneta_append(data, "\n\n");
   data = pgmoneta_append(data, "#HELP pgmoneta_logging_fatal The number of FATAL logging statements\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_logging_fatal gauge\n");
   data = pgmoneta_append(data, "pgmoneta_logging_fatal ");
   data = pgmoneta_append_ulong(data, atomic_load(&config->prometheus.logging_fatal));
   data = pgmoneta_append(data, "\n\n");
   data = pgmoneta_append(data, "#HELP pgmoneta_retention_days The retention days of pgmoneta\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_retention_days gauge\n");
   data = pgmoneta_append(data, "pgmoneta_retention_days ");
   data = pgmoneta_append_int(data, config->retention_days <= 0 ? 0 : config->retention_days);
   data = pgmoneta_append(data, "\n\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_retention_weeks The retention weeks of pgmoneta\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_retention_weeks gauge\n");
   data = pgmoneta_append(data, "pgmoneta_retention_weeks ");
   data = pgmoneta_append_int(data, config->retention_weeks <= 0 ? 0 : config->retention_weeks);
   data = pgmoneta_append(data, "\n\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_retention_months The retention months of pgmoneta\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_retention_months gauge\n");
   data = pgmoneta_append(data, "pgmoneta_retention_months ");
   data = pgmoneta_append_int(data, config->retention_months <= 0 ? 0 : config->retention_months);
   data = pgmoneta_append(data, "\n\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_retention_years The retention years of pgmoneta\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_retention_years gauge\n");
   data = pgmoneta_append(data, "pgmoneta_retention_years ");
   data = pgmoneta_append_int(data, config->retention_years <= 0 ? 0 : config->retention_years);
   data = pgmoneta_append(data, "\n\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_retention_server The retention of a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_retention_server gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      data = pgmoneta_append(data, "pgmoneta_retention_server{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"");
      data = pgmoneta_append(data, ", ");
      data = pgmoneta_append(data, "parameter= \"days\"");
      data = pgmoneta_append(data, "} ");
      retention = config->servers[i].retention_days;
      if (retention <= 0)
      {
         retention = config->retention_days;
      }
      data = pgmoneta_append_int(data, retention <= 0 ? 0 : retention);
      data = pgmoneta_append(data, "\n");

      data = pgmoneta_append(data, "pgmoneta_retention_server{");
      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"");
      data = pgmoneta_append(data, ", ");
      data = pgmoneta_append(data, "parameter= \"weeks\"");
      data = pgmoneta_append(data, "} ");
      retention = config->servers[i].retention_weeks;
      if (retention <= 0)
      {
         retention = config->retention_weeks;
      }
      data = pgmoneta_append_int(data, retention <= 0 ? 0 : retention);
      data = pgmoneta_append(data, "\n");

      data = pgmoneta_append(data, "pgmoneta_retention_server{");
      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"");
      data = pgmoneta_append(data, ", ");
      data = pgmoneta_append(data, "parameter= \"months\"");
      data = pgmoneta_append(data, "} ");
      retention = config->servers[i].retention_months;
      if (retention <= 0)
      {
         retention = config->retention_months;
      }
      data = pgmoneta_append_int(data, retention <= 0 ? 0 : retention);
      data = pgmoneta_append(data, "\n");

      data = pgmoneta_append(data, "pgmoneta_retention_server{");
      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"");
      data = pgmoneta_append(data, ", ");
      data = pgmoneta_append(data, "parameter= \"years\"");
      data = pgmoneta_append(data, "} ");
      retention = config->servers[i].retention_years;
      if (retention <= 0)
      {
         retention = config->retention_years;
      }
      data = pgmoneta_append_int(data, retention <= 0 ? 0 : retention);
      data = pgmoneta_append(data, "\n");
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_compression The compression used\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_compression gauge\n");
   data = pgmoneta_append(data, "pgmoneta_compression ");
   data = pgmoneta_append_int(data, config->compression_type);
   data = pgmoneta_append(data, "\n\n");

   d = NULL;

   d = pgmoneta_append(d, config->base_dir);
   d = pgmoneta_append(d, "/");

   size = pgmoneta_directory_size(d);

   data = pgmoneta_append(data, "#HELP pgmoneta_used_space The disk space used for pgmoneta\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_used_space gauge\n");
   data = pgmoneta_append(data, "pgmoneta_used_space ");
   data = pgmoneta_append_ulong(data, size);
   data = pgmoneta_append(data, "\n\n");

   free(d);

   d = NULL;

   d = pgmoneta_append(d, config->base_dir);
   d = pgmoneta_append(d, "/");

   size = pgmoneta_free_space(d);

   data = pgmoneta_append(data, "#HELP pgmoneta_free_space The free disk space for pgmoneta\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_free_space gauge\n");
   data = pgmoneta_append(data, "pgmoneta_free_space ");
   data = pgmoneta_append_ulong(data, size);
   data = pgmoneta_append(data, "\n\n");

   free(d);

   d = NULL;

   d = pgmoneta_append(d, config->base_dir);
   d = pgmoneta_append(d, "/");

   size = pgmoneta_total_space(d);

   data = pgmoneta_append(data, "#HELP pgmoneta_total_space The total disk space for pgmoneta\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_total_space gauge\n");
   data = pgmoneta_append(data, "pgmoneta_total_space ");
   data = pgmoneta_append_ulong(data, size);
   data = pgmoneta_append(data, "\n\n");

   free(d);

   d = NULL;

   data = pgmoneta_append(data, "#HELP pgmoneta_wal_shipping The disk space used for WAL shipping for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_wal_shipping gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      data = pgmoneta_append(data, "pgmoneta_wal_shipping{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      d = pgmoneta_get_server_wal_shipping_wal(i);

      if (d != NULL)
      {
         size = pgmoneta_directory_size(d);
         data = pgmoneta_append_ulong(data, size);
      }
      else
      {
         data = pgmoneta_append_ulong(data, 0);
      }

      data = pgmoneta_append(data, "\n");

      free(d);
      d = NULL;
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_wal_shipping_used_space The disk space used for WAL shipping of a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_wal_shipping_used_space gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      data = pgmoneta_append(data, "pgmoneta_wal_shipping_used_space{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      d = pgmoneta_get_server_wal_shipping(i);
      if (d != NULL)
      {
         size = pgmoneta_directory_size(d);
         data = pgmoneta_append_ulong(data, size);
      }
      else
      {
         data = pgmoneta_append_ulong(data, 0);
      }

      data = pgmoneta_append(data, "\n");

      free(d);
      d = NULL;
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_wal_shipping_free_space The free disk space for WAL shipping of a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_wal_shipping_free_space gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      data = pgmoneta_append(data, "pgmoneta_wal_shipping_free_space{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      d = pgmoneta_get_server_wal_shipping(i);

      if (d != NULL)
      {
         size = pgmoneta_free_space(d);
         data = pgmoneta_append_ulong(data, size);
      }
      else
      {
         data = pgmoneta_append_ulong(data, 0);
      }

      data = pgmoneta_append(data, "\n");

      free(d);
      d = NULL;
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_wal_shipping_total_space The total disk space for WAL shipping of a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_wal_shipping_total_space gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      data = pgmoneta_append(data, "pgmoneta_wal_shipping_total_space{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      d = pgmoneta_get_server_wal_shipping(i);

      if (d != NULL)
      {
         size = pgmoneta_total_space(d);
         data = pgmoneta_append_ulong(data, size);
      }
      else
      {
         data = pgmoneta_append_ulong(data, 0);
      }

      data = pgmoneta_append(data, "\n");

      free(d);
      d = NULL;
   }
   data = pgmoneta_append(data, "\n");

   free(d);

   d = NULL;

   data = pgmoneta_append(data, "#HELP pgmoneta_hot_standby The disk space used for hot standby for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_hot_standby gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      data = pgmoneta_append(data, "pgmoneta_hot_standby{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      d = pgmoneta_get_server_hot_standby(i);

      if (d != NULL)
      {
         size = pgmoneta_directory_size(d);
         data = pgmoneta_append_ulong(data, size);
      }
      else
      {
         data = pgmoneta_append_ulong(data, 0);
      }

      data = pgmoneta_append(data, "\n");

      free(d);
      d = NULL;
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_hot_standby_free_space The free disk space for hot standby of a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_hot_standby_free_space gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      data = pgmoneta_append(data, "pgmoneta_hot_standby_free_space{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      d = pgmoneta_get_server_hot_standby(i);

      if (d != NULL)
      {
         size = pgmoneta_free_space(d);
         data = pgmoneta_append_ulong(data, size);
      }
      else
      {
         data = pgmoneta_append_ulong(data, 0);
      }

      data = pgmoneta_append(data, "\n");

      free(d);
      d = NULL;
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_hot_standby_total_space The total disk space for hot standby of a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_hot_standby_total_space gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      data = pgmoneta_append(data, "pgmoneta_hot_standby_total_space{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      d = pgmoneta_get_server_hot_standby(i);

      if (d != NULL)
      {
         size = pgmoneta_total_space(d);
         data = pgmoneta_append_ulong(data, size);
      }
      else
      {
         data = pgmoneta_append_ulong(data, 0);
      }

      data = pgmoneta_append(data, "\n");

      free(d);
      d = NULL;
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_server_timeline The current timeline a server is on\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_server_timeline counter\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      data = pgmoneta_append(data, "pgmoneta_server_timeline{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      data = pgmoneta_append_int(data, config->servers[i].cur_timeline);

      data = pgmoneta_append(data, "\n");
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_server_parent_tli The parent timeline of a timeline on a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_server_parent_tli gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      struct timeline_history* history = NULL;
      struct timeline_history* curh = NULL;
      int tli = 2;

      data = pgmoneta_append(data, "pgmoneta_server_parent_tli{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\", ");

      data = pgmoneta_append(data, "tli=\"");
      data = pgmoneta_append_int(data, 1);
      data = pgmoneta_append(data, "\"} ");

      data = pgmoneta_append_int(data, 0);

      data = pgmoneta_append(data, "\n");

      pgmoneta_get_timeline_history(i, config->servers[i].cur_timeline, &history);
      curh = history;
      while (curh != NULL)
      {
         data = pgmoneta_append(data, "pgmoneta_server_parent_tli{");

         data = pgmoneta_append(data, "name=\"");
         data = pgmoneta_append(data, config->servers[i].name);
         data = pgmoneta_append(data, "\", ");

         data = pgmoneta_append(data, "tli=\"");
         data = pgmoneta_append_int(data, tli);
         data = pgmoneta_append(data, "\"} ");

         data = pgmoneta_append_int(data, curh->parent_tli);

         data = pgmoneta_append(data, "\n");

         curh = curh->next;
         tli++;
      }
      pgmoneta_free_timeline_history(history);
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_server_timeline_switchpos The WAL switch position of a timeline on a server (showed in hex as a parameter)\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_server_timeline_switchpos gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      struct timeline_history* history = NULL;
      struct timeline_history* curh = NULL;
      int tli = 2;

      data = pgmoneta_append(data, "pgmoneta_server_timeline_switchpos{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\", ");

      data = pgmoneta_append(data, "tli=\"1\", ");

      data = pgmoneta_append(data, "walpos=\"0/0\"} ");

      data = pgmoneta_append(data, "1");

      data = pgmoneta_append(data, "\n");

      pgmoneta_get_timeline_history(i, config->servers[i].cur_timeline, &history);
      curh = history;
      while (curh != NULL)
      {
         char xlogpos[MISC_LENGTH];
         memset(xlogpos, 0, MISC_LENGTH);
         snprintf(xlogpos, MISC_LENGTH, "%X/%X", curh->switchpos_hi, curh->switchpos_lo);

         data = pgmoneta_append(data, "pgmoneta_server_timeline_switchpos{");

         data = pgmoneta_append(data, "name=\"");
         data = pgmoneta_append(data, config->servers[i].name);
         data = pgmoneta_append(data, "\", ");

         data = pgmoneta_append(data, "tli=\"");
         data = pgmoneta_append_int(data, tli);
         data = pgmoneta_append(data, "\", ");

         data = pgmoneta_append(data, "walpos=\"");
         data = pgmoneta_append(data, xlogpos);
         data = pgmoneta_append(data, "\"} ");

         data = pgmoneta_append_int(data, 1);

         data = pgmoneta_append(data, "\n");

         curh = curh->next;
         tli++;
      }
      pgmoneta_free_timeline_history(history);
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_server_workers The numbeer of workers for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_server_workers gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      int workers = config->servers[i].workers != -1 ? config->servers[i].workers : config->workers;

      data = pgmoneta_append(data, "pgmoneta_server_workers{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      data = pgmoneta_append_int(data, workers);

      data = pgmoneta_append(data, "\n");
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_server_valid Is the server in a valid state\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_server_valid gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      data = pgmoneta_append(data, "pgmoneta_server_valid{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      data = pgmoneta_append_bool(data, config->servers[i].valid);

      data = pgmoneta_append(data, "\n");
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_wal_streaming The WAL streaming status of a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_wal_streaming gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      data = pgmoneta_append(data, "pgmoneta_wal_streaming{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      data = pgmoneta_append_bool(data, config->servers[i].wal_streaming);

      data = pgmoneta_append(data, "\n");
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_server_operation_count The count of client operations of a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_server_operation_count gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      data = pgmoneta_append(data, "pgmoneta_server_operation_count{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      data = pgmoneta_append_int(data, config->servers[i].operation_count);

      data = pgmoneta_append(data, "\n");
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_server_failed_operation_count The count of failed client operations of a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_server_failed_operation_count gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      data = pgmoneta_append(data, "pgmoneta_server_failed_operation_count{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      data = pgmoneta_append_int(data, config->servers[i].failed_operation_count);

      data = pgmoneta_append(data, "\n");
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_server_last_operation_time The time of the latest client operation of a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_server_last_operation_time gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      data = pgmoneta_append(data, "pgmoneta_server_last_operation_time{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      if (config->servers[i].operation_count > 0)
      {
         data = pgmoneta_append(data, config->servers[i].last_operation_time);
      }
      else
      {
         data = pgmoneta_append_int(data, 0);
      }

      data = pgmoneta_append(data, "\n");
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_server_last_failed_operation_time The time of the latest failed client operation of a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_server_last_failed_operation_time gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      data = pgmoneta_append(data, "pgmoneta_server_last_failed_operation_time{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      if (config->servers[i].failed_operation_count > 0)
      {
         data = pgmoneta_append(data, config->servers[i].last_failed_operation_time);
      }
      else
      {
         data = pgmoneta_append_int(data, 0);
      }

      data = pgmoneta_append(data, "\n");
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      metrics_cache_append(data);
      free(data);
      data = NULL;
   }
}

static void
backup_information(int client_fd)
{
   char* d;
   int number_of_backups;
   struct backup** backups;
   bool valid;
   int valid_count;
   char* data = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_oldest The oldest backup for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_oldest gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_backup(i);

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      data = pgmoneta_append(data, "pgmoneta_backup_oldest{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      valid = false;
      for (int j = 0; !valid && j < number_of_backups; j++)
      {
         if (backups[j]->valid == VALID_TRUE)
         {
            data = pgmoneta_append(data, backups[j]->label);
            valid = true;
         }
      }

      if (!valid)
      {
         data = pgmoneta_append(data, "0");
      }

      data = pgmoneta_append(data, "\n");

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      metrics_cache_append(data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_newest The newest backup for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_newest gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_backup(i);

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      data = pgmoneta_append(data, "pgmoneta_backup_newest{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      valid = false;
      for (int j = number_of_backups - 1; !valid && j >= 0; j--)
      {
         if (backups[j]->valid == VALID_TRUE)
         {
            data = pgmoneta_append(data, backups[j]->label);
            valid = true;
         }
      }

      if (!valid)
      {
         data = pgmoneta_append(data, "0");
      }

      data = pgmoneta_append(data, "\n");

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_count The number of valid backups for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_count gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_backup(i);

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      data = pgmoneta_append(data, "pgmoneta_backup_count{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      valid_count = 0;
      for (int j = 0; j < number_of_backups; j++)
      {
         if (backups[j]->valid == VALID_TRUE)
         {
            valid_count++;
         }
      }

      data = pgmoneta_append_int(data, valid_count);

      data = pgmoneta_append(data, "\n");

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      metrics_cache_append(data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_backup Is the backup valid for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_backup(i);

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      if (number_of_backups > 0)
      {
         for (int j = 0; j < number_of_backups; j++)
         {
            if (backups[j] != NULL)
            {
               data = pgmoneta_append(data, "pgmoneta_backup{");

               data = pgmoneta_append(data, "name=\"");
               data = pgmoneta_append(data, config->servers[i].name);
               data = pgmoneta_append(data, "\",label=\"");
               data = pgmoneta_append(data, backups[j]->label);
               data = pgmoneta_append(data, "\"} ");

               data = pgmoneta_append_int(data, backups[j]->valid);

               data = pgmoneta_append(data, "\n");
            }
         }
      }
      else
      {
         data = pgmoneta_append(data, "pgmoneta_backup{");

         data = pgmoneta_append(data, "name=\"");
         data = pgmoneta_append(data, config->servers[i].name);
         data = pgmoneta_append(data, "\",label=\"0\"} 0");

         data = pgmoneta_append(data, "\n");
      }

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      metrics_cache_append(data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_version The version of postgresql for a backup\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_version gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_backup(i);

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      if (number_of_backups > 0)
      {
         for (int j = 0; j < number_of_backups; j++)
         {
            if (backups[j]->valid == VALID_TRUE)
            {
               data = pgmoneta_append(data, "pgmoneta_backup_version{");

               data = pgmoneta_append(data, "name=\"");
               data = pgmoneta_append(data, config->servers[i].name);
               data = pgmoneta_append(data, "\",label=\"");
               data = pgmoneta_append(data, backups[j]->label);
               data = pgmoneta_append(data, "\", major=\"");
               data = pgmoneta_append_int(data, backups[j]->version);
               data = pgmoneta_append(data, "\", minor=\"");
               data = pgmoneta_append_int(data, backups[j]->minor_version);
               data = pgmoneta_append(data, "\"} 1");

               data = pgmoneta_append(data, "\n");
            }
         }
      }
      else
      {
         data = pgmoneta_append(data, "pgmoneta_backup_version{");

         data = pgmoneta_append(data, "name=\"");
         data = pgmoneta_append(data, config->servers[i].name);
         data = pgmoneta_append(data, "\",label=\"0\"} 0");

         data = pgmoneta_append(data, "\n");
      }

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      metrics_cache_append(data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_elapsed_time The backup in seconds for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_elapsed_time gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_backup(i);

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      if (number_of_backups > 0)
      {
         for (int j = 0; j < number_of_backups; j++)
         {
            if (backups[j]->valid == VALID_TRUE)
            {
               data = pgmoneta_append(data, "pgmoneta_backup_elapsed_time{");

               data = pgmoneta_append(data, "name=\"");
               data = pgmoneta_append(data, config->servers[i].name);
               data = pgmoneta_append(data, "\",label=\"");
               data = pgmoneta_append(data, backups[j]->label);
               data = pgmoneta_append(data, "\"} ");

               data = pgmoneta_append_int(data, backups[j]->elapsed_time);

               data = pgmoneta_append(data, "\n");
            }
         }
      }
      else
      {
         data = pgmoneta_append(data, "pgmoneta_backup_elapsed_time{");

         data = pgmoneta_append(data, "name=\"");
         data = pgmoneta_append(data, config->servers[i].name);
         data = pgmoneta_append(data, "\",label=\"0\"} 0");

         data = pgmoneta_append(data, "\n");
      }

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_start_timeline The starting timeline of a backup for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_start_timeline gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_backup(i);

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      if (number_of_backups > 0)
      {
         for (int j = 0; j < number_of_backups; j++)
         {
            if (backups[j]->valid == VALID_TRUE)
            {
               data = pgmoneta_append(data, "pgmoneta_backup_start_timeline{");

               data = pgmoneta_append(data, "name=\"");
               data = pgmoneta_append(data, config->servers[i].name);
               data = pgmoneta_append(data, "\",label=\"");
               data = pgmoneta_append(data, backups[j]->label);
               data = pgmoneta_append(data, "\"} ");

               data = pgmoneta_append_int(data, backups[j]->start_timeline);

               data = pgmoneta_append(data, "\n");
            }
         }
      }
      else
      {
         data = pgmoneta_append(data, "pgmoneta_backup_start_timeline{");

         data = pgmoneta_append(data, "name=\"");
         data = pgmoneta_append(data, config->servers[i].name);
         data = pgmoneta_append(data, "\",label=\"0\"} 0");

         data = pgmoneta_append(data, "\n");
      }

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_end_timeline The ending timeline of a backup for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_end_timeline gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_backup(i);

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      if (number_of_backups > 0)
      {
         for (int j = 0; j < number_of_backups; j++)
         {
            if (backups[j]->valid == VALID_TRUE)
            {
               data = pgmoneta_append(data, "pgmoneta_backup_end_timeline{");

               data = pgmoneta_append(data, "name=\"");
               data = pgmoneta_append(data, config->servers[i].name);
               data = pgmoneta_append(data, "\",label=\"");
               data = pgmoneta_append(data, backups[j]->label);
               data = pgmoneta_append(data, "\"} ");

               data = pgmoneta_append_int(data, backups[j]->end_timeline);

               data = pgmoneta_append(data, "\n");
            }
         }
      }
      else
      {
         data = pgmoneta_append(data, "pgmoneta_backup_end_timeline{");

         data = pgmoneta_append(data, "name=\"");
         data = pgmoneta_append(data, config->servers[i].name);
         data = pgmoneta_append(data, "\",label=\"0\"} 0");

         data = pgmoneta_append(data, "\n");
      }

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_start_walpos The starting WAL position of a backup for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_start_walpos gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_backup(i);

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      if (number_of_backups > 0)
      {
         for (int j = 0; j < number_of_backups; j++)
         {
            if (backups[j]->valid == VALID_TRUE)
            {
               char walpos[MISC_LENGTH];
               memset(walpos, 0, MISC_LENGTH);
               data = pgmoneta_append(data, "pgmoneta_backup_start_walpos{");

               data = pgmoneta_append(data, "name=\"");
               data = pgmoneta_append(data, config->servers[i].name);
               data = pgmoneta_append(data, "\",label=\"");
               data = pgmoneta_append(data, backups[j]->label);
               data = pgmoneta_append(data, "\", ");

               snprintf(walpos, MISC_LENGTH, "%X/%X", backups[j]->start_lsn_hi32, backups[j]->start_lsn_lo32);
               data = pgmoneta_append(data, "walpos=\"");
               data = pgmoneta_append(data, walpos);
               data = pgmoneta_append(data, "\"} ");

               data = pgmoneta_append_int(data, 1);

               data = pgmoneta_append(data, "\n");
            }
         }
      }
      else
      {
         data = pgmoneta_append(data, "pgmoneta_backup_start_walpos{");

         data = pgmoneta_append(data, "name=\"");
         data = pgmoneta_append(data, config->servers[i].name);
         data = pgmoneta_append(data, "\",label=\"0\", ");
         data = pgmoneta_append(data, "walpos=\"0/0\"} 0");

         data = pgmoneta_append(data, "\n");
      }
      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_checkpoint_walpos The checkpoint WAL position of a backup for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_checkpoint_walpos gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_backup(i);

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      if (number_of_backups > 0)
      {
         for (int j = 0; j < number_of_backups; j++)
         {
            if (backups[j]->valid == VALID_TRUE)
            {
               char walpos[MISC_LENGTH];
               memset(walpos, 0, MISC_LENGTH);
               data = pgmoneta_append(data, "pgmoneta_backup_checkpoint_walpos{");

               data = pgmoneta_append(data, "name=\"");
               data = pgmoneta_append(data, config->servers[i].name);
               data = pgmoneta_append(data, "\",label=\"");
               data = pgmoneta_append(data, backups[j]->label);
               data = pgmoneta_append(data, "\", ");

               snprintf(walpos, MISC_LENGTH, "%X/%X", backups[j]->checkpoint_lsn_hi32, backups[j]->checkpoint_lsn_lo32);
               data = pgmoneta_append(data, "walpos=\"");
               data = pgmoneta_append(data, walpos);
               data = pgmoneta_append(data, "\"} ");

               data = pgmoneta_append_int(data, 1);

               data = pgmoneta_append(data, "\n");
            }
         }
      }
      else
      {
         data = pgmoneta_append(data, "pgmoneta_backup_checkpoint_walpos{");

         data = pgmoneta_append(data, "name=\"");
         data = pgmoneta_append(data, config->servers[i].name);
         data = pgmoneta_append(data, "\",label=\"0\", ");
         data = pgmoneta_append(data, "walpos=\"0/0\"} 0");

         data = pgmoneta_append(data, "\n");
      }

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_end_walpos The ending WAL position of a backup for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_end_walpos gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_backup(i);

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      if (number_of_backups > 0)
      {
         for (int j = 0; j < number_of_backups; j++)
         {
            if (backups[j]->valid == VALID_TRUE)
            {
               char walpos[MISC_LENGTH];
               memset(walpos, 0, MISC_LENGTH);
               data = pgmoneta_append(data, "pgmoneta_backup_end_walpos{");

               data = pgmoneta_append(data, "name=\"");
               data = pgmoneta_append(data, config->servers[i].name);
               data = pgmoneta_append(data, "\",label=\"");
               data = pgmoneta_append(data, backups[j]->label);
               data = pgmoneta_append(data, "\", ");

               snprintf(walpos, MISC_LENGTH, "%X/%X", backups[j]->end_lsn_hi32, backups[j]->end_lsn_lo32);
               data = pgmoneta_append(data, "walpos=\"");
               data = pgmoneta_append(data, walpos);
               data = pgmoneta_append(data, "\"} ");

               data = pgmoneta_append_int(data, 1);

               data = pgmoneta_append(data, "\n");
            }
         }
      }
      else
      {
         data = pgmoneta_append(data, "pgmoneta_backup_end_walpos{");

         data = pgmoneta_append(data, "name=\"");
         data = pgmoneta_append(data, config->servers[i].name);
         data = pgmoneta_append(data, "\",label=\"0\", ");
         data = pgmoneta_append(data, "walpos=\"0/0\"} 0");

         data = pgmoneta_append(data, "\n");
      }

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      metrics_cache_append(data);
      free(data);
      data = NULL;
   }
}

static void
size_information(int client_fd)
{
   char* d;
   int number_of_backups;
   struct backup** backups;
   unsigned long size;
   bool valid;
   char* data = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   data = pgmoneta_append(data, "#HELP pgmoneta_restore_newest_size The size of the newest restore for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_restore_newest_size gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_backup(i);

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      data = pgmoneta_append(data, "pgmoneta_restore_newest_size{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      valid = false;
      for (int j = number_of_backups - 1; !valid && j >= 0; j--)
      {
         if (backups[j]->valid == VALID_TRUE)
         {
            data = pgmoneta_append_ulong(data, backups[j]->restore_size);
            valid = true;
         }
      }

      if (!valid)
      {
         data = pgmoneta_append(data, "0");
      }

      data = pgmoneta_append(data, "\n");

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      metrics_cache_append(data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_newest_size The size of the newest backup for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_newest_size gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_backup(i);

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      data = pgmoneta_append(data, "pgmoneta_backup_newest_size{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      valid = false;
      for (int j = number_of_backups - 1; !valid && j >= 0; j--)
      {
         if (backups[j]->valid == VALID_TRUE)
         {
            data = pgmoneta_append_ulong(data, backups[j]->backup_size);
            valid = true;
         }
      }

      if (!valid)
      {
         data = pgmoneta_append(data, "0");
      }

      data = pgmoneta_append(data, "\n");

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      metrics_cache_append(data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_restore_size The size of a restore for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_restore_size gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_backup(i);

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      if (number_of_backups > 0)
      {
         for (int j = 0; j < number_of_backups; j++)
         {
            if (backups[j]->valid == VALID_TRUE)
            {
               data = pgmoneta_append(data, "pgmoneta_restore_size{");

               data = pgmoneta_append(data, "name=\"");
               data = pgmoneta_append(data, config->servers[i].name);
               data = pgmoneta_append(data, "\",label=\"");
               data = pgmoneta_append(data, backups[j]->label);
               data = pgmoneta_append(data, "\"} ");

               data = pgmoneta_append_ulong(data, backups[j]->restore_size);

               data = pgmoneta_append(data, "\n");
            }
         }
      }
      else
      {
         data = pgmoneta_append(data, "pgmoneta_restore_size{");

         data = pgmoneta_append(data, "name=\"");
         data = pgmoneta_append(data, config->servers[i].name);
         data = pgmoneta_append(data, "\",label=\"0\"} 0");

         data = pgmoneta_append(data, "\n");
      }

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      metrics_cache_append(data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_restore_size_increment The size increment of a restore for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_restore_size_increment gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_backup(i);

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      if (number_of_backups > 0)
      {
         for (int j = 0; j < number_of_backups; j++)
         {
            if (backups[j] != NULL)
            {
               data = pgmoneta_append(data, "pgmoneta_restore_size_increment{");

               data = pgmoneta_append(data, "name=\"");
               data = pgmoneta_append(data, config->servers[i].name);
               data = pgmoneta_append(data, "\",label=\"");
               data = pgmoneta_append(data, backups[j]->label);
               data = pgmoneta_append(data, "\"} ");

               if (j == 0)
               {
                  data = pgmoneta_append_int(data, backups[0]->restore_size);
               }
               else
               {
                  data = pgmoneta_append_int(data, backups[j]->restore_size - backups[j - 1]->restore_size);
               }

               data = pgmoneta_append(data, "\n");
            }
         }
      }
      else
      {
         data = pgmoneta_append(data, "pgmoneta_restore_size_increment{");

         data = pgmoneta_append(data, "name=\"");
         data = pgmoneta_append(data, config->servers[i].name);
         data = pgmoneta_append(data, "\",label=\"0\"} 0");

         data = pgmoneta_append(data, "\n");
      }

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      metrics_cache_append(data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_size The size of a backup for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_size gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_backup(i);

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      if (number_of_backups > 0)
      {
         for (int j = 0; j < number_of_backups; j++)
         {
            if (backups[j]->valid == VALID_TRUE)
            {
               data = pgmoneta_append(data, "pgmoneta_backup_size{");

               data = pgmoneta_append(data, "name=\"");
               data = pgmoneta_append(data, config->servers[i].name);
               data = pgmoneta_append(data, "\",label=\"");
               data = pgmoneta_append(data, backups[j]->label);
               data = pgmoneta_append(data, "\"} ");

               data = pgmoneta_append_ulong(data, backups[j]->backup_size);

               data = pgmoneta_append(data, "\n");
            }
         }
      }
      else
      {
         data = pgmoneta_append(data, "pgmoneta_backup_size{");

         data = pgmoneta_append(data, "name=\"");
         data = pgmoneta_append(data, config->servers[i].name);
         data = pgmoneta_append(data, "\",label=\"0\"} 0");

         data = pgmoneta_append(data, "\n");
      }

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      metrics_cache_append(data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_compression_ratio The ratio of backup size to restore size for each backup\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_compression_ratio gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_backup(i);

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      if (number_of_backups > 0)
      {
         for (int j = 0; j < number_of_backups; j++)
         {
            if (backups[j] != NULL)
            {
               data = pgmoneta_append(data, "pgmoneta_backup_compression_ratio{");

               data = pgmoneta_append(data, "name=\"");
               data = pgmoneta_append(data, config->servers[i].name);
               data = pgmoneta_append(data, "\",label=\"");
               data = pgmoneta_append(data, backups[j]->label);
               data = pgmoneta_append(data, "\"} ");

               if (backups[j]->restore_size)
               {
                  data = pgmoneta_append_double(data, 1.0 * backups[j]->backup_size / backups[j]->restore_size);
               }
               else
               {
                  data = pgmoneta_append_int(data, 0);
               }

               data = pgmoneta_append(data, "\n");
            }
         }
      }
      else
      {
         data = pgmoneta_append(data, "pgmoneta_backup_compression_ratio{");

         data = pgmoneta_append(data, "name=\"");
         data = pgmoneta_append(data, config->servers[i].name);
         data = pgmoneta_append(data, "\",label=\"0\"} 0");

         data = pgmoneta_append(data, "\n");
      }

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      metrics_cache_append(data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_throughput The throughput of the backup for a server (bytes/s)\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_throughput gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_backup(i);

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      if (number_of_backups > 0)
      {
         for (int j = 0; j < number_of_backups; j++)
         {
            if (backups[j] != NULL)
            {
               data = pgmoneta_append(data, "pgmoneta_backup_throughput{");

               data = pgmoneta_append(data, "name=\"");
               data = pgmoneta_append(data, config->servers[i].name);
               data = pgmoneta_append(data, "\",label=\"");
               data = pgmoneta_append(data, backups[j]->label);
               data = pgmoneta_append(data, "\"} ");

               if (backups[j]->elapsed_time)
               {
                  data = pgmoneta_append_double(data, 1.0 * backups[j]->backup_size / backups[j]->elapsed_time);
               }
               else
               {
                  data = pgmoneta_append_int(data, 0);
               }
               data = pgmoneta_append(data, "\n");
            }
         }
      }
      else
      {
         data = pgmoneta_append(data, "pgmoneta_backup_throughput{");

         data = pgmoneta_append(data, "name=\"");
         data = pgmoneta_append(data, config->servers[i].name);
         data = pgmoneta_append(data, "\",label=\"0\"} 0");

         data = pgmoneta_append(data, "\n");
      }

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      metrics_cache_append(data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_retain Retain backup for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_retain gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_backup(i);

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      if (number_of_backups > 0)
      {
         for (int j = 0; j < number_of_backups; j++)
         {
            if (backups[j] != NULL)
            {
               data = pgmoneta_append(data, "pgmoneta_backup_retain{");

               data = pgmoneta_append(data, "name=\"");
               data = pgmoneta_append(data, config->servers[i].name);
               data = pgmoneta_append(data, "\",label=\"");
               data = pgmoneta_append(data, backups[j]->label);
               data = pgmoneta_append(data, "\"} ");

               data = pgmoneta_append_bool(data, backups[j]->keep);

               data = pgmoneta_append(data, "\n");
            }
         }
      }
      else
      {
         data = pgmoneta_append(data, "pgmoneta_backup_retain{");

         data = pgmoneta_append(data, "name=\"");
         data = pgmoneta_append(data, config->servers[i].name);
         data = pgmoneta_append(data, "\",label=\"0\"} 0");

         data = pgmoneta_append(data, "\n");
      }

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      metrics_cache_append(data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_total_size The total size of the backups for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_total_size gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_backup(i);

      size = pgmoneta_directory_size(d);

      data = pgmoneta_append(data, "pgmoneta_backup_total_size{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      data = pgmoneta_append_ulong(data, size);

      data = pgmoneta_append(data, "\n");

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      metrics_cache_append(data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_wal_total_size The total size of the WAL for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_wal_total_size gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server_wal(i);

      size = pgmoneta_directory_size(d);

      free(d);

      d = pgmoneta_get_server_wal_shipping_wal(i);

      if (d != NULL)
      {

         size += pgmoneta_directory_size(d);
      }

      data = pgmoneta_append(data, "pgmoneta_wal_total_size{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      data = pgmoneta_append_ulong(data, size);

      data = pgmoneta_append(data, "\n");

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      metrics_cache_append(data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_total_size The total size for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_total_size gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = pgmoneta_get_server(i);

      size = pgmoneta_directory_size(d);

      free(d);

      d = pgmoneta_get_server_wal_shipping(i);

      if (d != NULL)
      {
         size += pgmoneta_directory_size(d);
      }

      data = pgmoneta_append(data, "pgmoneta_total_size{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      data = pgmoneta_append_ulong(data, size);

      data = pgmoneta_append(data, "\n");

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_active_backup Is there an active backup for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_active_backup gauge\n");

   for (int i = 0; i < config->number_of_servers; i++)
   {
      data = pgmoneta_append(data, "pgmoneta_active_backup{");
      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      data = pgmoneta_append_bool(data, atomic_load(&config->servers[i].backup));

      data = pgmoneta_append(data, "\n");
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_current_wal_file The current streaming WAL filename of a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_current_wal_file gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      data = pgmoneta_append(data, "pgmoneta_current_wal_file{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\", ");

      data = pgmoneta_append(data, "file=\"");
      data = pgmoneta_append(data, config->servers[i].current_wal_filename);
      data = pgmoneta_append(data, "\"} ");

      data = pgmoneta_append_bool(data, config->servers[i].wal_streaming);

      data = pgmoneta_append(data, "\n");
   }
   data = pgmoneta_append(data, "\n");

   // Append the WAL LSN of every server
   data = pgmoneta_append(data, "#HELP pgmoneta_current_wal_lsn The current WAL log sequence number\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_current_wal_lsn gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      data = pgmoneta_append(data, "pgmoneta_current_wal_lsn{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\", ");

      data = pgmoneta_append(data, "lsn=\"");
      if (!strcmp(config->servers[i].current_wal_lsn, ""))
      {
         data = pgmoneta_append(data, "0/0");
      }
      else
      {
         data = pgmoneta_append(data, config->servers[i].current_wal_lsn);
      }
      data = pgmoneta_append(data, "\"} ");

      data = pgmoneta_append_bool(data, config->servers[i].wal_streaming);

      data = pgmoneta_append(data, "\n");
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      metrics_cache_append(data);
      free(data);
      data = NULL;
   }
}

static int
send_chunk(int client_fd, char* data)
{
   int status;
   char* m = NULL;
   struct message msg;

   memset(&msg, 0, sizeof(struct message));

   m = malloc(20);

   if (m == NULL)
   {
      goto error;
   }

   memset(m, 0, 20);

   sprintf(m, "%zX\r\n", strlen(data));

   m = pgmoneta_append(m, data);
   m = pgmoneta_append(m, "\r\n");

   msg.kind = 0;
   msg.length = strlen(m);
   msg.data = m;

   status = pgmoneta_write_message(NULL, client_fd, &msg);

   free(m);

   return status;

error:

   return MESSAGE_STATUS_ERROR;
}

/**
 * Checks if the Prometheus cache configuration setting
 * (`metrics_cache`) has a non-zero value, that means there
 * are seconds to cache the response.
 *
 * @return true if there is a cache configuration,
 *         false if no cache is active
 */
static bool
is_metrics_cache_configured(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   // cannot have caching if not set metrics!
   if (config->metrics == 0)
   {
      return false;
   }

   return config->metrics_cache_max_age != PGMONETA_PROMETHEUS_CACHE_DISABLED;
}

/**
 * Checks if the cache is still valid, and therefore can be
 * used to serve as a response.
 * A cache is considred valid if it has non-empty payload and
 * a timestamp in the future.
 *
 * @return true if the cache is still valid
 */
static bool
is_metrics_cache_valid(void)
{
   time_t now;

   struct prometheus_cache* cache;

   cache = (struct prometheus_cache*)prometheus_cache_shmem;

   if (cache->valid_until == 0 || strlen(cache->data) == 0)
   {
      return false;
   }

   now = time(NULL);
   return now <= cache->valid_until;
}

int
pgmoneta_init_prometheus_cache(size_t* p_size, void** p_shmem)
{
   struct prometheus_cache* cache;
   struct configuration* config;
   size_t cache_size = 0;
   size_t struct_size = 0;

   config = (struct configuration*)shmem;

   // first of all, allocate the overall cache structure
   cache_size = metrics_cache_size_to_alloc();
   struct_size = sizeof(struct prometheus_cache);

   if (pgmoneta_create_shared_memory(struct_size + cache_size, config->hugepage, (void*) &cache))
   {
      goto error;
   }

   memset(cache, 0, struct_size + cache_size);
   cache->valid_until = 0;
   cache->size = cache_size;
   atomic_init(&cache->lock, STATE_FREE);

   // success! do the memory swap
   *p_shmem = cache;
   *p_size = cache_size + struct_size;
   return 0;

error:
   // disable caching
   config->metrics_cache_max_age = config->metrics_cache_max_size = PGMONETA_PROMETHEUS_CACHE_DISABLED;
   pgmoneta_log_error("Cannot allocate shared memory for the Prometheus cache!");
   *p_size = 0;
   *p_shmem = NULL;

   return 1;
}

/**
 * Provides the size of the cache to allocate.
 *
 * It checks if the metrics cache is configured, and
 * computers the right minimum value between the
 * user configured requested size and the default
 * cache size.
 *
 * @return the cache size to allocate
 */
static size_t
metrics_cache_size_to_alloc(void)
{
   struct configuration* config;
   size_t cache_size = 0;

   config = (struct configuration*)shmem;

   // which size to use ?
   // either the configured (i.e., requested by user) if lower than the max size
   // or the default value
   if (is_metrics_cache_configured())
   {
      cache_size = config->metrics_cache_max_size > 0
            ? MIN(config->metrics_cache_max_size, PROMETHEUS_MAX_CACHE_SIZE)
            : PROMETHEUS_DEFAULT_CACHE_SIZE;
   }

   return cache_size;
}

/**
 * Invalidates the cache.
 *
 * Requires the caller to hold the lock on the cache!
 *
 * Invalidating the cache means that the payload is zero-filled
 * and that the valid_until field is set to zero too.
 */
static void
metrics_cache_invalidate(void)
{
   struct prometheus_cache* cache;

   cache = (struct prometheus_cache*)prometheus_cache_shmem;

   memset(cache->data, 0, cache->size);
   cache->valid_until = 0;
}

/**
 * Appends data to the cache.
 *
 * Requires the caller to hold the lock on the cache!
 *
 * If the input data is empty, nothing happens.
 * The data is appended only if the cache does not overflows, that
 * means the current size of the cache plus the size of the data
 * to append does not exceed the current cache size.
 * If the cache overflows, the cache is flushed and marked
 * as invalid.
 * This makes safe to call this method along the workflow of
 * building the Prometheus response.
 *
 * @param data the string to append to the cache
 * @return true on success
 */
static bool
metrics_cache_append(char* data)
{
   int origin_length = 0;
   int append_length = 0;
   struct prometheus_cache* cache;

   cache = (struct prometheus_cache*)prometheus_cache_shmem;

   if (!is_metrics_cache_configured())
   {
      return false;
   }

   origin_length = strlen(cache->data);
   append_length = strlen(data);
   // need to append the data to the cache
   if (origin_length + append_length >= cache->size)
   {
      // cannot append new data, so invalidate cache
      pgmoneta_log_debug("Cannot append %d bytes to the Prometheus cache because it will overflow the size of %d bytes (currently at %d bytes). HINT: try adjusting `metrics_cache_max_size`",
                         append_length,
                         cache->size,
                         origin_length);
      metrics_cache_invalidate();
      return false;
   }

   // append the data to the data field
   memcpy(cache->data + origin_length, data, append_length);
   cache->data[origin_length + append_length + 1] = '\0';
   return true;
}

/**
 * Finalizes the cache.
 *
 * Requires the caller to hold the lock on the cache!
 *
 * This method should be invoked when the cache is complete
 * and therefore can be served.
 *
 * @return true if the cache has a validity
 */
static bool
metrics_cache_finalize(void)
{
   struct configuration* config;
   struct prometheus_cache* cache;
   time_t now;

   cache = (struct prometheus_cache*)prometheus_cache_shmem;
   config = (struct configuration*)shmem;

   if (!is_metrics_cache_configured())
   {
      return false;
   }

   now = time(NULL);
   cache->valid_until = now + config->metrics_cache_max_age;
   return cache->valid_until > now;
}
