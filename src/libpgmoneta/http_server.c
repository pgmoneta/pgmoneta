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
#include <pgmoneta.h>
#include <http_server.h>
#include <logging.h>
#include <message.h>
#include <shmem.h>
#include <utils.h>

/* system */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

static void fill_date(char* buf, size_t len);

static void
fill_date(char* buf, size_t len)
{
   time_t now = time(NULL);

   memset(buf, 0, len);
   ctime_r(&now, buf);
   buf[strlen(buf) - 1] = '\0'; /* strip trailing newline */
}

int
pgmoneta_http_server_ssl_accept(SSL* ssl, int fd)
{
   char buffer[5] = {0};

   if (ssl == NULL)
   {
      return MESSAGE_STATUS_OK;
   }

   recv(fd, buffer, 5, MSG_PEEK);

   if ((unsigned char)buffer[0] == 0x16 || (unsigned char)buffer[0] == 0x80)
   {
      if (SSL_accept(ssl) <= 0)
      {
         pgmoneta_log_error("http_server: SSL_accept failed");
         return MESSAGE_STATUS_ERROR;
      }
      return MESSAGE_STATUS_OK;
   }

   return MESSAGE_STATUS_ZERO; /* plain HTTP on a TLS-configured socket */
}

int
pgmoneta_http_server_parse(SSL* ssl, int fd, struct http_server_request** req)
{
   struct message* msg = NULL;
   struct main_configuration* config;
   struct http_server_request* r = NULL;
   char* from = NULL;
   int index;
   int status;

   *req = NULL;

   config = (struct main_configuration*)shmem;

   status = pgmoneta_read_timeout_message(ssl, fd,
                                          (int)pgmoneta_time_convert(config->authentication_timeout, FORMAT_TIME_S),
                                          &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      pgmoneta_log_debug("http_server: failed to read request");
      return MESSAGE_STATUS_ERROR;
   }

   if (msg == NULL || msg->data == NULL || msg->length < 3 ||
       strncmp((char*)msg->data, "GET", 3) != 0)
   {
      pgmoneta_log_debug("http_server: not a GET request");
      return MESSAGE_STATUS_ERROR;
   }

   index = 4;
   from = (char*)msg->data + index;

   while (index < (int)msg->length && pgmoneta_read_byte(msg->data + index) != ' ')
   {
      index++;
   }

   if (index >= (int)msg->length)
   {
      pgmoneta_log_debug("http_server: malformed request line");
      return MESSAGE_STATUS_ERROR;
   }

   pgmoneta_write_byte(msg->data + index, '\0');

   r = (struct http_server_request*)malloc(sizeof(struct http_server_request));
   if (r == NULL)
   {
      return MESSAGE_STATUS_ERROR;
   }

   memset(r, 0, sizeof(struct http_server_request));
   strncpy(r->path, from, sizeof(r->path) - 1);

   *req = r;
   return MESSAGE_STATUS_OK;
}

void
pgmoneta_http_server_request_destroy(struct http_server_request* req)
{
   free(req);
}

int
pgmoneta_http_server_dispatch(SSL* ssl, int fd, struct http_server_request* req,
                              struct http_route* routes, int n_routes)
{
   if (req == NULL)
   {
      return pgmoneta_http_respond_400(ssl, fd);
   }

   for (int i = 0; i < n_routes; i++)
   {
      if (strcmp(req->path, routes[i].path) == 0)
      {
         return routes[i].handler(ssl, fd);
      }
   }

   pgmoneta_log_debug("http_server: no route for path '%s'", req->path);
   return pgmoneta_http_respond_404(ssl, fd);
}

int
pgmoneta_http_respond_ok(SSL* ssl, int fd, const char* content_type,
                         const void* body, size_t len)
{
   char header[512];
   int header_len;
   struct message msg;
   int status;

   memset(&msg, 0, sizeof(struct message));

   header_len = pgmoneta_snprintf(header, sizeof(header),
                                  "HTTP/1.1 200 OK\r\n"
                                  "Content-Type: %s\r\n"
                                  "Content-Length: %zu\r\n"
                                  "Connection: close\r\n"
                                  "\r\n",
                                  content_type, len);

   msg.data = header;
   msg.length = header_len;
   status = pgmoneta_write_message(ssl, fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      return status;
   }

   if (len > 0 && body != NULL)
   {
      memset(&msg, 0, sizeof(struct message));
      msg.data = (void*)body;
      msg.length = len;
      status = pgmoneta_write_message(ssl, fd, &msg);
   }

   return status;
}

int
pgmoneta_http_respond_400(SSL* ssl, int fd)
{
   char* data = NULL;
   struct message msg;
   int status;

   memset(&msg, 0, sizeof(struct message));

   data = pgmoneta_append(data, "HTTP/1.1 400 Bad Request\r\n");
   data = pgmoneta_append(data, "Content-Length: 0\r\n");
   data = pgmoneta_append(data, "Connection: close\r\n");
   data = pgmoneta_append(data, "\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgmoneta_write_message(ssl, fd, &msg);

   free(data);
   return status;
}

int
pgmoneta_http_respond_404(SSL* ssl, int fd)
{
   char* data = NULL;
   char time_buf[32];
   struct message msg;
   int status;

   memset(&msg, 0, sizeof(struct message));
   fill_date(time_buf, sizeof(time_buf));

   data = pgmoneta_append(data, "HTTP/1.1 404 Not Found\r\n");
   data = pgmoneta_append(data, "Date: ");
   data = pgmoneta_append(data, time_buf);
   data = pgmoneta_append(data, "\r\n");
   data = pgmoneta_append(data, "Content-Length: 0\r\n");
   data = pgmoneta_append(data, "Connection: close\r\n\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgmoneta_write_message(ssl, fd, &msg);

   free(data);
   return status;
}

int
pgmoneta_http_respond_redirect(SSL* ssl, int fd, const char* location)
{
   char* data = NULL;
   char time_buf[32];
   struct message msg;
   int status;

   memset(&msg, 0, sizeof(struct message));
   fill_date(time_buf, sizeof(time_buf));

   data = pgmoneta_append(data, "HTTP/1.1 301 Moved Permanently\r\n");
   data = pgmoneta_append(data, "Location: ");
   data = pgmoneta_append(data, (char*)location);
   data = pgmoneta_append(data, "\r\n");
   data = pgmoneta_append(data, "Date: ");
   data = pgmoneta_append(data, time_buf);
   data = pgmoneta_append(data, "\r\n");
   data = pgmoneta_append(data, "Content-Length: 0\r\n");
   data = pgmoneta_append(data, "Connection: close\r\n");
   data = pgmoneta_append(data, "\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgmoneta_write_message(ssl, fd, &msg);

   free(data);
   return status;
}

int
pgmoneta_http_respond_chunked_start(SSL* ssl, int fd, const char* content_type)
{
   char* data = NULL;
   char time_buf[32];
   struct message msg;
   int status;

   memset(&msg, 0, sizeof(struct message));
   fill_date(time_buf, sizeof(time_buf));

   data = pgmoneta_append(data, "HTTP/1.1 200 OK\r\n");
   data = pgmoneta_append(data, "Content-Type: ");
   data = pgmoneta_append(data, content_type);
   data = pgmoneta_append(data, "\r\n");
   data = pgmoneta_append(data, "Date: ");
   data = pgmoneta_append(data, time_buf);
   data = pgmoneta_append(data, "\r\nTransfer-Encoding: chunked\r\n\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgmoneta_write_message(ssl, fd, &msg);

   free(data);
   return status;
}

int
pgmoneta_http_respond_chunked_write(SSL* ssl, int fd, const char* data)
{
   char* m = NULL;
   struct message msg;
   int status;

   memset(&msg, 0, sizeof(struct message));

   if (data == NULL)
   {
      return MESSAGE_STATUS_ERROR;
   }

   m = (char*)malloc(20);
   if (m == NULL)
   {
      return MESSAGE_STATUS_ERROR;
   }
   memset(m, 0, 20);

   pgmoneta_snprintf(m, 20, "%zX\r\n", strlen(data));

   m = pgmoneta_append(m, data);
   if (m == NULL)
   {
      return MESSAGE_STATUS_ERROR;
   }

   m = pgmoneta_append(m, "\r\n");
   if (m == NULL)
   {
      return MESSAGE_STATUS_ERROR;
   }

   msg.kind = 0;
   msg.length = strlen(m);
   msg.data = m;

   status = pgmoneta_write_message(ssl, fd, &msg);

   free(m);
   return status;
}

int
pgmoneta_http_respond_chunked_end(SSL* ssl, int fd)
{
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   msg.kind = 0;
   msg.data = "0\r\n\r\n";
   msg.length = 5;

   return pgmoneta_write_message(ssl, fd, &msg);
}