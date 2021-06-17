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
#include <info.h>
#include <logging.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <prometheus.h>
#include <utils.h>

/* system */
#include <ev.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#define CHUNK_SIZE 32768

#define PAGE_UNKNOWN 0
#define PAGE_HOME    1
#define PAGE_METRICS 2

static int resolve_page(struct message* msg);
static int unknown_page(int client_fd);
static int home_page(int client_fd);
static int metrics_page(int client_fd);

static void general_information(int client_fd);
static void backup_information(int client_fd);
static void size_information(int client_fd);

static int send_chunk(int client_fd, char* data);

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
   else
   {
      unknown_page(client_fd);
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
}

static int
resolve_page(struct message* msg)
{
   char* from = NULL;
   int index;

   if (strncmp((char*)msg->data, "GET", 3) != 0)
   {
      pgmoneta_log_debug("Promethus: Not a GET request");
      return PAGE_UNKNOWN;
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
   data = pgmoneta_append(data, "  <table border=\"1\">\n");
   data = pgmoneta_append(data, "    <tbody>\n");
   data = pgmoneta_append(data, "      <tr>\n");
   data = pgmoneta_append(data, "        <td>value</td>\n");
   data = pgmoneta_append(data, "        <td>State\n");
   data = pgmoneta_append(data, "          <ol>\n");
   data = pgmoneta_append(data, "            <li>Running</li>\n");
   data = pgmoneta_append(data, "          </ol>\n");
   data = pgmoneta_append(data, "        </td>\n");
   data = pgmoneta_append(data, "      </tr>\n");
   data = pgmoneta_append(data, "    </tbody>\n");
   data = pgmoneta_append(data, "  </table>\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_retention</h2>\n");
   data = pgmoneta_append(data, "  The retention of pgmoneta in days\n");
   data = pgmoneta_append(data, "  <p>\n");
   data = pgmoneta_append(data, "  <h2>pgmoneta_retention_server</h2>\n");
   data = pgmoneta_append(data, "  The retention a server in days\n");
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

   memset(&msg, 0, sizeof(struct message));

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

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

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

   data = pgmoneta_append(data, "#HELP pgmoneta_retention The retention of pgmoneta\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_retention gauge\n");
   data = pgmoneta_append(data, "pgmoneta_retention ");
   data = pgmoneta_append_int(data, config->retention);
   data = pgmoneta_append(data, "\n\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_retention_server The retention of a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_retention_server gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      data = pgmoneta_append(data, "pgmoneta_retention_server{");

      data = pgmoneta_append(data, "name=\"");
      data = pgmoneta_append(data, config->servers[i].name);
      data = pgmoneta_append(data, "\"} ");

      retention = config->servers[i].retention;
      if (retention <= 0)
      {
         retention = config->retention;
      }

      data = pgmoneta_append_int(data, retention);

      data = pgmoneta_append(data, "\n");
   }
   data = pgmoneta_append(data, "\n");

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

   if (data != NULL)
   {
      send_chunk(client_fd, data);
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
      d = NULL;

      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[i].name);
      d = pgmoneta_append(d, "/backup/");

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
         if (backups[j] != NULL && backups[j]->valid)
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

      for (int i = 0; i < number_of_backups; i++)
      {
         free(backups[i]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_newest The newest backup for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_newest gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = NULL;

      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[i].name);
      d = pgmoneta_append(d, "/backup/");

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
         if (backups[j] != NULL && backups[j]->valid)
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

      for (int i = 0; i < number_of_backups; i++)
      {
         free(backups[i]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_count The number of valid backups for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_count gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = NULL;

      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[i].name);
      d = pgmoneta_append(d, "/backup/");

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
         if (backups[j] != NULL && backups[j]->valid)
         {
            valid_count++;
         }
      }

      data = pgmoneta_append_int(data, valid_count);

      data = pgmoneta_append(data, "\n");

      for (int i = 0; i < number_of_backups; i++)
      {
         free(backups[i]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_backup Is the backup valid for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = NULL;

      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[i].name);
      d = pgmoneta_append(d, "/backup/");

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

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

            data = pgmoneta_append_bool(data, backups[j]->valid);

            data = pgmoneta_append(data, "\n");
         }
      }

      for (int i = 0; i < number_of_backups; i++)
      {
         free(backups[i]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_elapsed_time The backup in seconds for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_elapsed_time gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = NULL;

      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[i].name);
      d = pgmoneta_append(d, "/backup/");

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      for (int j = 0; j < number_of_backups; j++)
      {
         if (backups[j] != NULL && backups[j]->valid)
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

      for (int i = 0; i < number_of_backups; i++)
      {
         free(backups[i]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
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
      d = NULL;

      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[i].name);
      d = pgmoneta_append(d, "/backup/");

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
         if (backups[j] != NULL && backups[j]->valid)
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

      for (int i = 0; i < number_of_backups; i++)
      {
         free(backups[i]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_newest_size The size of the newest backup for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_newest_size gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = NULL;

      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[i].name);
      d = pgmoneta_append(d, "/backup/");

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
         if (backups[j] != NULL && backups[j]->valid)
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

      for (int i = 0; i < number_of_backups; i++)
      {
         free(backups[i]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_restore_size The size of a restore for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_restore_size gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = NULL;

      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[i].name);
      d = pgmoneta_append(d, "/backup/");

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      for (int j = 0; j < number_of_backups; j++)
      {
         if (backups[j] != NULL && backups[j]->valid)
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

      for (int i = 0; i < number_of_backups; i++)
      {
         free(backups[i]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_size The size of a backup for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_size gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = NULL;

      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[i].name);
      d = pgmoneta_append(d, "/backup/");

      number_of_backups = 0;
      backups = NULL;

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      for (int j = 0; j < number_of_backups; j++)
      {
         if (backups[j] != NULL && backups[j]->valid)
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

      for (int i = 0; i < number_of_backups; i++)
      {
         free(backups[i]);
      }
      free(backups);

      free(d);
   }
   data = pgmoneta_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_backup_total_size The total size of the backups for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_backup_total_size gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = NULL;

      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[i].name);
      d = pgmoneta_append(d, "/backup/");

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
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_wal_total_size The total size of the WAL for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_wal_total_size gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = NULL;

      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[i].name);
      d = pgmoneta_append(d, "/wal/");

      size = pgmoneta_directory_size(d);

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
      free(data);
      data = NULL;
   }

   data = pgmoneta_append(data, "#HELP pgmoneta_total_size The total size for a server\n");
   data = pgmoneta_append(data, "#TYPE pgmoneta_total_size gauge\n");
   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = NULL;

      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[i].name);
      d = pgmoneta_append(d, "/");

      size = pgmoneta_directory_size(d);

      data = pgmoneta_append(data, "pgmoneta_total_size{");

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
   memset(m, 0, 20);

   sprintf(m, "%lX\r\n", strlen(data));

   m = pgmoneta_append(m, data);
   m = pgmoneta_append(m, "\r\n");
   
   msg.kind = 0;
   msg.length = strlen(m);
   msg.data = m;

   status = pgmoneta_write_message(NULL, client_fd, &msg);

   free(m);

   return status;
}
