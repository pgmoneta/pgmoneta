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
#include <http.h>
#include <logging.h>
#include <network.h>
#include <deque.h>
#include <security.h>
#include <utils.h>

/* system */
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <openssl/err.h>

static int http_parse_header(char** header, struct http_response* http_response);
static int http_read_response_body(SSL* ssl, int socket, struct http_response* http_response);
static int http_read_response_header(SSL* ssl, int socket, char** header_text, struct http_response* http_response);
static int http_build_request(struct http* connection, struct http_request* request, char** full_request, size_t* full_request_size);
static char* http_method_to_string(int method);

int
pgmoneta_http_create(char* hostname, int port, bool secure, struct http** result)
{
   struct http* connection = NULL;
   int socket_fd = -1;
   SSL* ssl = NULL;
   SSL_CTX* ctx = NULL;

   if (hostname == NULL || result == NULL)
   {
      pgmoneta_log_error("Invalid parameters for HTTP connection");
      goto error;
   }

   pgmoneta_log_debug("Creating HTTP connection to %s:%d (secure: %d)", hostname, port, secure);

   connection = (struct http*)malloc(sizeof(struct http));
   if (connection == NULL)
   {
      pgmoneta_log_error("Failed to allocate HTTP connection structure");
      goto error;
   }

   memset(connection, 0, sizeof(struct http));

   if (pgmoneta_connect(hostname, port, &socket_fd))
   {
      pgmoneta_log_error("Failed to connect to %s:%d", hostname, port);
      goto error;
   }

   connection->socket = socket_fd;
   connection->hostname = strdup(hostname);
   connection->port = port;
   connection->secure = secure;

   if (secure)
   {
      if (pgmoneta_create_ssl_ctx(true, &ctx))
      {
         pgmoneta_log_error("Failed to create SSL context");
         goto error;
      }

      if (SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) == 0)
      {
         pgmoneta_log_error("Failed to set minimum TLS version");
         goto error;
      }

      ssl = SSL_new(ctx);
      if (ssl == NULL)
      {
         pgmoneta_log_error("Failed to create SSL structure");
         goto error;
      }

      if (SSL_set_fd(ssl, socket_fd) == 0)
      {
         pgmoneta_log_error("Failed to set SSL file descriptor");
         goto error;
      }

      if (SSL_set_tlsext_host_name(ssl, hostname) == 0)
      {
         pgmoneta_log_error("Failed to set SNI hostname");
         goto error;
      }

      int connect_result;
      do
      {
         connect_result = SSL_connect(ssl);

         if (connect_result != 1)
         {
            int err = SSL_get_error(ssl, connect_result);
            switch (err)
            {
               case SSL_ERROR_WANT_READ:
               case SSL_ERROR_WANT_WRITE:
                  continue;
               default:
                  pgmoneta_log_error("SSL connection failed: %s", ERR_error_string(err, NULL));
                  goto error;
            }
         }
      }
      while (connect_result != 1);

      connection->ssl = ssl;
   }

   *result = connection;

   return PGMONETA_HTTP_STATUS_OK;

error:
   if (ssl != NULL)
   {
      SSL_free(ssl);
   }
   if (ctx != NULL)
   {
      SSL_CTX_free(ctx);
   }
   if (socket_fd != -1)
   {
      pgmoneta_disconnect(socket_fd);
   }
   if (connection != NULL)
   {
      free(connection->hostname);
      free(connection);
   }

   return PGMONETA_HTTP_STATUS_ERROR;
}

int
pgmoneta_http_request_create(int method, char* path, struct http_request** result)
{
   struct http_request* request = NULL;

   if (path == NULL || result == NULL)
   {
      pgmoneta_log_error("Invalid parameters for HTTP request");
      goto error;
   }

   request = (struct http_request*)malloc(sizeof(struct http_request));
   if (request == NULL)
   {
      pgmoneta_log_error("Failed to allocate HTTP request structure");
      goto error;
   }

   memset(request, 0, sizeof(struct http_request));

   request->method = method;
   request->path = strdup(path);
   if (request->path == NULL)
   {
      pgmoneta_log_error("Failed to duplicate path string");
      goto error;
   }

   if (pgmoneta_deque_create(false, &request->payload.headers))
   {
      pgmoneta_log_error("Failed to create headers deque");
      goto error;
   }

   *result = request;

   return PGMONETA_HTTP_STATUS_OK;

error:
   if (request != NULL)
   {
      free(request->path);
      pgmoneta_deque_destroy(request->payload.headers);
      free(request);
   }
   return PGMONETA_HTTP_STATUS_ERROR;
}

int
pgmoneta_http_request_add_header(struct http_request* request, char* name, char* value)
{
   if (request == NULL || name == NULL || value == NULL)
   {
      pgmoneta_log_error("Invalid parameters for adding HTTP header");
      goto error;
   }

   if (request->payload.headers == NULL)
   {
      pgmoneta_log_error("Headers deque is NULL");
      goto error;
   }

   if (pgmoneta_deque_add(request->payload.headers, name, (uintptr_t)value, ValueString))
   {
      pgmoneta_log_error("Failed to add header to deque");
      goto error;
   }

   return PGMONETA_HTTP_STATUS_OK;

error:
   return PGMONETA_HTTP_STATUS_ERROR;
}

char*
pgmoneta_http_request_get_header(struct http_request* request, char* name)
{
   if (request == NULL || name == NULL)
   {
      return NULL;
   }

   if (request->payload.headers == NULL)
   {
      return NULL;
   }

   return (char*)pgmoneta_deque_get(request->payload.headers, name);
}

int
pgmoneta_http_request_update_header(struct http_request* request, char* name, char* value)
{
   if (request == NULL || name == NULL || value == NULL)
   {
      pgmoneta_log_error("Invalid parameters for updating HTTP header");
      goto error;
   }

   if (request->payload.headers == NULL)
   {
      pgmoneta_log_error("Headers deque is NULL");
      goto error;
   }

   if (pgmoneta_deque_remove(request->payload.headers, name) > 0)
   {
      pgmoneta_log_trace("Removed existing header: %s", name);
   }

   if (pgmoneta_deque_add(request->payload.headers, name, (uintptr_t)value, ValueString))
   {
      pgmoneta_log_error("Failed to add updated header to deque");
      goto error;
   }

   return PGMONETA_HTTP_STATUS_OK;

error:
   return PGMONETA_HTTP_STATUS_ERROR;
}

int
pgmoneta_http_request_remove_header(struct http_request* request, char* name)
{
   if (request == NULL || name == NULL)
   {
      pgmoneta_log_error("Invalid parameters for removing HTTP header");
      goto error;
   }

   if (request->payload.headers == NULL)
   {
      pgmoneta_log_error("Headers deque is NULL");
      goto error;
   }

   int removed = pgmoneta_deque_remove(request->payload.headers, name);
   if (removed == 0)
   {
      pgmoneta_log_debug("Header not found for removal: %s", name);
   }

   return PGMONETA_HTTP_STATUS_OK;

error:
   return PGMONETA_HTTP_STATUS_ERROR;
}

int
pgmoneta_http_set_data(struct http_request* request, void* data, size_t size)
{
   if (request == NULL)
   {
      pgmoneta_log_error("Invalid request parameter");
      goto error;
   }

   if (request->payload.data != NULL)
   {
      free(request->payload.data);
      request->payload.data = NULL;
      request->payload.data_size = 0;
   }

   if (data != NULL && size > 0)
   {
      request->payload.data = malloc(size);
      if (request->payload.data == NULL)
      {
         pgmoneta_log_error("Failed to allocate memory for request data");
         goto error;
      }

      memcpy(request->payload.data, data, size);
      request->payload.data_size = size;
   }

   return PGMONETA_HTTP_STATUS_OK;

error:
   return PGMONETA_HTTP_STATUS_ERROR;
}

char*
pgmoneta_http_get_response_header(struct http_response* response, char* name)
{
   if (response == NULL || name == NULL)
   {
      return NULL;
   }

   if (response->payload.headers == NULL)
   {
      return NULL;
   }

   char* key = strdup(name);
   if (key == NULL)
   {
      return NULL;
   }
   char* value = (char*)pgmoneta_deque_get(response->payload.headers, key);
   free(key);
   return value;
}

int
pgmoneta_http_invoke(struct http* connection, struct http_request* request, struct http_response** response)
{
   struct message* msg_request = NULL;
   char* full_request = NULL;
   size_t full_request_size = 0;
   char* header_text = NULL;
   struct http_response* http_response = NULL;
   int error = 0;
   int status;

   if (connection == NULL || request == NULL || response == NULL)
   {
      pgmoneta_log_error("Invalid parameters for HTTP invoke");
      goto error;
   }

   pgmoneta_log_trace("Invoking HTTP request");

   http_response = (struct http_response*)malloc(sizeof(struct http_response));
   if (http_response == NULL)
   {
      pgmoneta_log_error("Failed to allocate HTTP response structure");
      goto error;
   }
   memset(http_response, 0, sizeof(struct http_response));

   if (http_build_request(connection, request, &full_request, &full_request_size))
   {
      pgmoneta_log_error("Failed to build HTTP request");
      goto error;
   }

   msg_request = (struct message*)malloc(sizeof(struct message));
   if (msg_request == NULL)
   {
      pgmoneta_log_error("Failed to allocate message structure");
      goto error;
   }

   memset(msg_request, 0, sizeof(struct message));
   msg_request->data = full_request;
   msg_request->length = full_request_size;

   error = 0;
req:
   if (error < 5)
   {
      status = pgmoneta_write_message(connection->ssl, connection->socket, msg_request);
      if (status != MESSAGE_STATUS_OK)
      {
         error++;
         pgmoneta_log_debug("Write failed, retrying (%d/5)", error);
         goto req;
      }
   }
   else
   {
      pgmoneta_log_error("Failed to write after 5 attempts");
      goto error;
   }

   status = http_read_response_header(connection->ssl, connection->socket, &header_text, http_response);
   if (status != MESSAGE_STATUS_OK)
   {
      pgmoneta_log_error("Failed to read HTTP response header");
      goto error;
   }
   status = http_parse_header(&header_text, http_response);
   if (status != PGMONETA_HTTP_STATUS_OK)
   {
      pgmoneta_log_error("Failed to parse HTTP response header");
      goto error;
   }
   status = http_read_response_body(connection->ssl, connection->socket, http_response);
   if (status != MESSAGE_STATUS_OK)
   {
      pgmoneta_log_error("Failed to read HTTP response body");
      goto error;
   }

   *response = http_response;

   free(full_request);
   free(header_text);
   free(msg_request);

   return PGMONETA_HTTP_STATUS_OK;

error:
   free(full_request);
   free(header_text);
   free(msg_request);
   if (http_response != NULL)
   {
      pgmoneta_http_response_destroy(http_response);
   }

   return PGMONETA_HTTP_STATUS_ERROR;
}

int
pgmoneta_http_request_destroy(struct http_request* request)
{
   if (request != NULL)
   {
      free(request->path);
      pgmoneta_deque_destroy(request->payload.headers);
      free(request->payload.data);
      free(request);
   }

   return PGMONETA_HTTP_STATUS_OK;
}

int
pgmoneta_http_response_destroy(struct http_response* response)
{
   if (response != NULL)
   {
      pgmoneta_deque_destroy(response->payload.headers);
      free(response->payload.data);
      free(response);
   }

   return PGMONETA_HTTP_STATUS_OK;
}

int
pgmoneta_http_destroy(struct http* connection)
{
   if (connection != NULL)
   {
      if (connection->ssl != NULL)
      {
         pgmoneta_close_ssl(connection->ssl);
      }

      if (connection->socket != -1)
      {
         pgmoneta_disconnect(connection->socket);
      }

      free(connection->hostname);
      free(connection);
   }

   return PGMONETA_HTTP_STATUS_OK;
}

static ssize_t
http_read_bytes(SSL* ssl, int socket, char* buffer, size_t size)
{
   ssize_t bytes_read;

   while (1)
   {
      if (ssl)
      {
         bytes_read = SSL_read(ssl, buffer, size);
         if (bytes_read <= 0)
         {
            int err = SSL_get_error(ssl, bytes_read);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
               continue;
            if (err == SSL_ERROR_ZERO_RETURN)
               break;
            goto error;
         }
      }
      else
      {
         bytes_read = read(socket, buffer, size);
         if (bytes_read == 0)
            break;
         if (bytes_read < 0)
         {
            if (bytes_read < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
               continue;
            goto error;
         }
      }
      return bytes_read;
   }
   return 0;
error:
   return -1;
}

static int
http_read_response_header(SSL* ssl, int socket,
                          char** header_text,
                          struct http_response* http_response)
{
   char buffer[8192];
   ssize_t bytes_read;
   size_t total = 0;
   char* end = NULL;

   *header_text = NULL;

   // read to the buffer
   while (!end)
   {
      bytes_read = http_read_bytes(ssl, socket, buffer, sizeof(buffer) - 1);
      if (bytes_read < 0)
      {
         free(*header_text);
         goto error;
      }

      buffer[bytes_read] = '\0';
      *header_text = pgmoneta_append(*header_text, buffer);
      total += bytes_read;

      if (total > MAX_HEADER_SIZE)
      {
         free(*header_text);
         goto error;
      }

      end = strstr(*header_text, "\r\n\r\n");
   }

   // store the rest as body/data of the http request
   // add 4 bytes for the \r\n\r\n CLRF
   size_t header_len = (end - *header_text) + 4;
   size_t extra = total - header_len;

   if (extra > 0)
   {
      http_response->payload.data = malloc(extra + 1);
      if (!http_response->payload.data)
      {
         free(*header_text);
         goto error;
      }

      memcpy(http_response->payload.data, *header_text + header_len, extra);
      ((char*)http_response->payload.data)[extra] = '\0';
      http_response->payload.data_size = extra;
   }

   (*header_text)[header_len] = '\0';
   return MESSAGE_STATUS_OK;
error:
   return MESSAGE_STATUS_ERROR;
}

static int
http_read_chunked_body(SSL* ssl, int socket, struct http_response* http_response)
{
   char buffer[8192];
   ssize_t bytes_read;
   char* prefetched = (char*)http_response->payload.data;
   size_t prefetched_size = http_response->payload.data_size;
   size_t prefetched_pos = 0;

   // body bytes may already be buffered while reading headers; chunk decode
   // from those bytes first, then continue reading from the socket.
   http_response->payload.data = NULL;
   http_response->payload.data_size = 0;

   while (1)
   {
      // read chunk size line
      char chunk_size_line[32] = {0};
      int line_pos = 0;

      while (line_pos < (int)sizeof(chunk_size_line) - 1)
      {
         char c;
         // check the prefetched bytes for ordering them
         if (prefetched_pos < prefetched_size)
         {
            c = prefetched[prefetched_pos++];
            bytes_read = 1;
         }
         else
         {
            bytes_read = http_read_bytes(ssl, socket, &c, 1);
         }
         if (bytes_read <= 0)
            goto error;

         if (c == '\n')
            break;
         if (c != '\r')
            chunk_size_line[line_pos++] = c;
      }

      size_t chunk_size = strtoul(chunk_size_line, NULL, 16);

      // last chunk
      // we read 2 bytes here cause the \r\n
      if (chunk_size == 0)
      {
         char trailing[2];
         // check if prefetched bytes are available
         if (prefetched_pos < prefetched_size)
         {
            size_t remaining_prefetched = prefetched_size - prefetched_pos;
            if (remaining_prefetched >= 2)
            {
               memcpy(trailing, prefetched + prefetched_pos, 2);
               prefetched_pos += 2;
               bytes_read = 2;
            }
            else
            {
               bytes_read = http_read_bytes(ssl, socket, trailing, 2);
            }
         }
         else
         {
            bytes_read = http_read_bytes(ssl, socket, trailing, 2);
         }
         if (bytes_read != 2)
            goto error;
         break;
      }

      // read chucnk data
      size_t chunk_read = 0;
      while (chunk_read < chunk_size)
      {
         size_t to_read = chunk_size - chunk_read;
         if (to_read > sizeof(buffer) - 1)
            to_read = sizeof(buffer) - 1;

         // check if the prefetched bytes are available
         if (prefetched_pos < prefetched_size)
         {
            size_t remaining_prefetched = prefetched_size - prefetched_pos;
            size_t from_prefetched = to_read < remaining_prefetched ? to_read : remaining_prefetched;
            memcpy(buffer, prefetched + prefetched_pos, from_prefetched);
            prefetched_pos += from_prefetched;
            bytes_read = (ssize_t)from_prefetched;
         }
         else
         {
            bytes_read = http_read_bytes(ssl, socket, buffer, to_read);
         }
         if (bytes_read <= 0)
            goto error;

         buffer[bytes_read] = '\0';
         http_response->payload.data = pgmoneta_append(http_response->payload.data, buffer);
         http_response->payload.data_size += bytes_read;
         chunk_read += bytes_read;
      }
      // the chunk size must match
      // notice it does not include the \r\n
      if (chunk_read != chunk_size)
         goto error;

      // read the traling CLF or \r\n
      char trailing[2];
      if (prefetched_pos < prefetched_size)
      {
         size_t remaining_prefetched = prefetched_size - prefetched_pos;
         if (remaining_prefetched >= 2)
         {
            memcpy(trailing, prefetched + prefetched_pos, 2);
            prefetched_pos += 2;
            bytes_read = 2;
         }
         else
         {
            bytes_read = http_read_bytes(ssl, socket, trailing, 2);
         }
      }
      else
      {
         bytes_read = http_read_bytes(ssl, socket, trailing, 2);
      }
      if (bytes_read != 2)
         goto error;

      if (trailing[0] != '\r' || trailing[1] != '\n')
         goto error;
   }

   free(prefetched);
   return MESSAGE_STATUS_OK;
error:
   free(prefetched);
   return MESSAGE_STATUS_ERROR;
}

static int
http_read_content_length_body(SSL* ssl, int socket, struct http_response* http_response, size_t content_length)
{
   char buffer[8192];
   ssize_t bytes_read;
   size_t remaining = content_length - http_response->payload.data_size;

   while (remaining > 0)
   {
      size_t to_read = remaining > sizeof(buffer) - 1 ? sizeof(buffer) - 1 : remaining;

      bytes_read = http_read_bytes(ssl, socket, buffer, to_read);
      if (bytes_read <= 0)
         goto error;

      buffer[bytes_read] = '\0';
      http_response->payload.data = pgmoneta_append(http_response->payload.data, buffer);
      http_response->payload.data_size += bytes_read;
      remaining -= bytes_read;
   }

   return MESSAGE_STATUS_OK;
error:
   return MESSAGE_STATUS_ERROR;
}
static int
http_read_EOF_body(SSL* ssl, int socket, struct http_response* http_response)
{
   char buffer[8192];
   ssize_t bytes_read;

   while (1)
   {
      size_t to_read = sizeof(buffer) - 1;

      bytes_read = http_read_bytes(ssl, socket, buffer, to_read);
      if (bytes_read < 0)
         goto error;
      if (bytes_read == 0)
         break;

      buffer[bytes_read] = '\0';
      http_response->payload.data = pgmoneta_append(http_response->payload.data, buffer);
      http_response->payload.data_size += bytes_read;
   }

   return MESSAGE_STATUS_OK;
error:
   return MESSAGE_STATUS_ERROR;
}

static int
http_read_response_body(SSL* ssl, int socket, struct http_response* http_response)
{
   if (!http_response)
      return MESSAGE_STATUS_ERROR;

   char* transfer_encoding = (char*)pgmoneta_deque_get(http_response->payload.headers, "Transfer-Encoding");
   char* cl_str = (char*)pgmoneta_deque_get(http_response->payload.headers, "Content-Length");

   // handle chunked transfer_encoding
   if (transfer_encoding && strstr(transfer_encoding, "chunked"))
   {
      return http_read_chunked_body(ssl, socket, http_response);
   }

   // handle content length
   if (cl_str)
   {
      size_t content_length = strtoul(cl_str, NULL, 10);
      return http_read_content_length_body(ssl, socket, http_response, content_length);
   }

   return http_read_EOF_body(ssl, socket, http_response);
}

static int
http_parse_header(char** header_text, struct http_response* http_response)
{
   char* p = NULL;
   char* original = NULL;
   int result = PGMONETA_HTTP_STATUS_ERROR;

   if (!header_text || !*header_text || !http_response)
   {
      pgmoneta_log_error("Invalid parameters for parsing HTTP header");
      goto error;
   }

   if (pgmoneta_deque_create(false, &http_response->payload.headers))
   {
      pgmoneta_log_error("Failed to create headers deque for response");
      goto error;
   }

   // make a copy to work on
   original = p = strdup(*header_text);
   if (!p)
   {
      pgmoneta_log_error("Failed to duplicate header text");
      goto error;
   }

   // parse the status line
   char* line_end = strstr(p, "\r\n");
   if (!line_end)
      goto cleanup;

   *line_end = '\0';

   if (sscanf(p, "HTTP/%*s %d", &http_response->status_code) != 1)
      goto cleanup;

   p = line_end + 2;
   //parse headers
   while (1)
   {
      line_end = strstr(p, "\r\n");
      if (!line_end)
         goto cleanup;

      if (line_end == p)
      {
         result = PGMONETA_HTTP_STATUS_OK;
         break;
      }

      *line_end = '\0';

      char* colon = strchr(p, ':');
      if (!colon)
         goto cleanup;

      *colon = '\0';

      char* key = p;
      char* value = colon + 1;

      while (*value == ' ' || *value == '\t')
         value++;

      if (pgmoneta_deque_add(http_response->payload.headers, key, (uintptr_t)value, ValueString))
      {
         pgmoneta_log_warn("Failed to add header key %s with value %s", key, value);
      }

      p = line_end + 2;
   }

cleanup:
   free(original);

   if (result != PGMONETA_HTTP_STATUS_OK)
   {
      pgmoneta_deque_destroy(http_response->payload.headers);
      http_response->payload.headers = NULL;
   }

   return result;
error:
   pgmoneta_deque_destroy(http_response->payload.headers);
   return PGMONETA_HTTP_STATUS_ERROR;
}

static int
http_build_request(struct http* connection, struct http_request* request, char** full_request, size_t* full_request_size)
{
   char* method_str = NULL;
   char* request_line = NULL;
   char* headers = NULL;
   char* user_agent = NULL;
   char content_length[32];
   size_t header_len = 0;
   size_t total_len = 0;

   if (connection == NULL || request == NULL || full_request == NULL || full_request_size == NULL)
   {
      pgmoneta_log_error("Invalid parameters for HTTP build request");
      goto error;
   }

   method_str = http_method_to_string(request->method);
   if (method_str == NULL)
   {
      pgmoneta_log_error("Invalid HTTP method: %d", request->method);
      goto error;
   }

   request_line = pgmoneta_append(request_line, method_str);
   request_line = pgmoneta_append(request_line, " ");
   request_line = pgmoneta_append(request_line, request->path);
   request_line = pgmoneta_append(request_line, " HTTP/1.1\r\n");

   headers = pgmoneta_append(headers, "Host: ");
   headers = pgmoneta_append(headers, connection->hostname);
   headers = pgmoneta_append(headers, "\r\n");

   user_agent = pgmoneta_append(user_agent, "pgmoneta/");
   user_agent = pgmoneta_append(user_agent, VERSION);
   headers = pgmoneta_append(headers, "User-Agent: ");
   headers = pgmoneta_append(headers, user_agent);
   headers = pgmoneta_append(headers, "\r\n");

   headers = pgmoneta_append(headers, "Connection: close\r\n");

   sprintf(content_length, "%zu", request->payload.data_size);
   headers = pgmoneta_append(headers, "Content-Length: ");
   headers = pgmoneta_append(headers, content_length);
   headers = pgmoneta_append(headers, "\r\n");

   if (request->payload.headers != NULL && !pgmoneta_deque_empty(request->payload.headers))
   {
      struct deque_iterator* iter = NULL;
      if (pgmoneta_deque_iterator_create(request->payload.headers, &iter) == 0)
      {
         while (pgmoneta_deque_iterator_next(iter))
         {
            headers = pgmoneta_append(headers, iter->tag);
            headers = pgmoneta_append(headers, ": ");
            headers = pgmoneta_append(headers, (char*)pgmoneta_value_data(iter->value));
            headers = pgmoneta_append(headers, "\r\n");
         }
         pgmoneta_deque_iterator_destroy(iter);
      }
   }

   headers = pgmoneta_append(headers, "\r\n");

   header_len = strlen(request_line) + strlen(headers);
   total_len = header_len + request->payload.data_size;

   *full_request = malloc(total_len + 1);
   if (*full_request == NULL)
   {
      pgmoneta_log_error("Failed to allocate memory for full request");
      goto error;
   }

   memcpy(*full_request, request_line, strlen(request_line));
   memcpy(*full_request + strlen(request_line), headers, strlen(headers));

   if (request->payload.data_size > 0)
   {
      memcpy(*full_request + header_len, request->payload.data, request->payload.data_size);
   }

   (*full_request)[total_len] = '\0';
   *full_request_size = total_len;

   free(request_line);
   free(headers);
   free(user_agent);

   return PGMONETA_HTTP_STATUS_OK;

error:
   free(request_line);
   free(headers);
   free(user_agent);

   return PGMONETA_HTTP_STATUS_ERROR;
}

static char*
http_method_to_string(int method)
{
   switch (method)
   {
      case PGMONETA_HTTP_GET:
         return "GET";
      case PGMONETA_HTTP_POST:
         return "POST";
      case PGMONETA_HTTP_PUT:
         return "PUT";
      default:
         return NULL;
   }
}
