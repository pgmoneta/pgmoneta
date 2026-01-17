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
 *
 */
#include <http.h>
#include <tsclient.h>
#include <tscommon.h>
#include <mctf.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <stdio.h>

struct echo_server
{
   int socket_fd;
   int port;
   pthread_t thread;
   bool running;
};

static struct echo_server* test_server = NULL;

static void* echo_server_thread(void* arg);
static int start_echo_server(int port);
static int stop_echo_server(void);
static void setup_echo_server(void);
static void teardown_echo_server(void);

MCTF_TEST(test_pgmoneta_http_get)
{
   int status;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;

   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;

   setup_echo_server();

   MCTF_ASSERT(!pgmoneta_http_create((char*)hostname, port, secure, &connection), cleanup, "failed to establish connection");
   MCTF_ASSERT(!pgmoneta_http_request_create(PGMONETA_HTTP_GET, "/get", &request), cleanup, "failed to create request");

   status = pgmoneta_http_invoke(connection, request, &response);
   MCTF_ASSERT_INT_EQ(status, PGMONETA_HTTP_STATUS_OK, cleanup, "HTTP GET request failed");

cleanup:
   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);
   teardown_echo_server();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_http_post)
{
   int status;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;
   const char* test_data = "name=pgmoneta&version=1.0";

   setup_echo_server();

   MCTF_ASSERT(!pgmoneta_http_create((char*)hostname, port, secure, &connection), cleanup, "failed to establish connection");
   MCTF_ASSERT(!pgmoneta_http_request_create(PGMONETA_HTTP_POST, "/post", &request), cleanup, "failed to create request");
   MCTF_ASSERT(!pgmoneta_http_set_data(request, (void*)test_data, strlen(test_data)), cleanup, "failed to set request data");

   status = pgmoneta_http_invoke(connection, request, &response);
   MCTF_ASSERT_INT_EQ(status, PGMONETA_HTTP_STATUS_OK, cleanup, "HTTP POST request failed");

cleanup:
   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);
   teardown_echo_server();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_http_put)
{
   int status;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;
   const char* test_data = "This is a test file content for PUT request";

   setup_echo_server();

   MCTF_ASSERT(!pgmoneta_http_create((char*)hostname, port, secure, &connection), cleanup, "failed to establish connection");
   MCTF_ASSERT(!pgmoneta_http_request_create(PGMONETA_HTTP_PUT, "/put", &request), cleanup, "failed to create request");
   MCTF_ASSERT(!pgmoneta_http_set_data(request, (void*)test_data, strlen(test_data)), cleanup, "failed to set request data");

   status = pgmoneta_http_invoke(connection, request, &response);
   MCTF_ASSERT_INT_EQ(status, PGMONETA_HTTP_STATUS_OK, cleanup, "HTTP PUT request failed");

cleanup:
   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);
   teardown_echo_server();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_http_put_file)
{
   int status;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   FILE* temp_file = NULL;
   void* file_data = NULL;
   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;
   const char* test_data = "This is a test file content for PUT file request\nSecond line of test data\nThird line with some numbers: 12345";
   size_t data_len = strlen(test_data);

   setup_echo_server();

   temp_file = tmpfile();
   MCTF_ASSERT_PTR_NONNULL(temp_file, cleanup, "Failed to create temp file");

   MCTF_ASSERT(fwrite(test_data, 1, data_len, temp_file) == data_len, cleanup, "wrote file incomplete");

   rewind(temp_file);

   file_data = malloc(data_len);
   MCTF_ASSERT_PTR_NONNULL(file_data, cleanup, "malloc failed");

   MCTF_ASSERT(fread(file_data, 1, data_len, temp_file) == data_len, cleanup, "read file incomplete");

   MCTF_ASSERT(!pgmoneta_http_create((char*)hostname, port, secure, &connection), cleanup, "failed to establish connection");
   MCTF_ASSERT(!pgmoneta_http_request_create(PGMONETA_HTTP_PUT, "/put", &request), cleanup, "failed to create request");
   MCTF_ASSERT(!pgmoneta_http_request_add_header(request, "Content-Type", "text/plain"), cleanup, "failed to add content type header");
   MCTF_ASSERT(!pgmoneta_http_set_data(request, file_data, data_len), cleanup, "failed to set request data");

   status = pgmoneta_http_invoke(connection, request, &response);
   MCTF_ASSERT_INT_EQ(status, PGMONETA_HTTP_STATUS_OK, cleanup, "HTTP PUT file request failed");

cleanup:
   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);
   free(file_data);
   file_data = NULL;
   if (temp_file)
   {
      fflush(temp_file);
      fclose(temp_file);
   }
   teardown_echo_server();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_http_header_operations)
{
   struct http_request* request = NULL;
   char* header_value = NULL;

   pgmoneta_test_setup(); // No server needed for header ops, just memory init

   MCTF_ASSERT(!pgmoneta_http_request_create(PGMONETA_HTTP_GET, "/test", &request), cleanup, "failed to create request");

   MCTF_ASSERT(!pgmoneta_http_request_add_header(request, "Authorization", "Bearer token123"), cleanup, "failed to add Authorization header");
   MCTF_ASSERT(!pgmoneta_http_request_add_header(request, "Content-Type", "application/json"), cleanup, "failed to add Content-Type header");

   header_value = pgmoneta_http_request_get_header(request, "Authorization");
   MCTF_ASSERT_PTR_NONNULL(header_value, cleanup, "header Authorization should not be null");
   MCTF_ASSERT_STR_EQ(header_value, "Bearer token123", cleanup, "header Authorization mismatch");

   header_value = pgmoneta_http_request_get_header(request, "Content-Type");
   MCTF_ASSERT_PTR_NONNULL(header_value, cleanup, "header Content-Type should not be null");
   MCTF_ASSERT_STR_EQ(header_value, "application/json", cleanup, "header Content-Type mismatch");

   MCTF_ASSERT_PTR_NULL(pgmoneta_http_request_get_header(request, "NonExistent"), cleanup, "header NonExistent should be null");

   MCTF_ASSERT(!pgmoneta_http_request_update_header(request, "Authorization", "Bearer newtoken456"), cleanup, "failed to update Authorization header");

   header_value = pgmoneta_http_request_get_header(request, "Authorization");
   MCTF_ASSERT_PTR_NONNULL(header_value, cleanup, "updated header value is null");
   MCTF_ASSERT_STR_EQ(header_value, "Bearer newtoken456", cleanup, "updated header value mismatch");

   MCTF_ASSERT(!pgmoneta_http_request_remove_header(request, "Content-Type"), cleanup, "failed to remove Content-Type header");

   header_value = pgmoneta_http_request_get_header(request, "Content-Type");
   MCTF_ASSERT_PTR_NULL(header_value, cleanup, "removed header should be null");

   header_value = pgmoneta_http_request_get_header(request, "Authorization");
   MCTF_ASSERT_PTR_NONNULL(header_value, cleanup, "Authorization header should still be present");
   MCTF_ASSERT_STR_EQ(header_value, "Bearer newtoken456", cleanup, "Authorization header value check");

cleanup:
   pgmoneta_http_request_destroy(request);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

static void*
echo_server_thread(void* arg)
{
   struct echo_server* server = (struct echo_server*)arg;

   while (server->running)
   {
      fd_set read_fds;
      struct timeval timeout;

      FD_ZERO(&read_fds);
      FD_SET(server->socket_fd, &read_fds);

      timeout.tv_sec = 1;
      timeout.tv_usec = 0;

      int result = select(server->socket_fd + 1, &read_fds, NULL, NULL, &timeout);

      if (result < 0)
      {
         if (!server->running)
         {
            break;
         }
         continue;
      }
      else if (result == 0)
      {
         continue;
      }

      if (FD_ISSET(server->socket_fd, &read_fds))
      {
         struct sockaddr_in client_addr;
         socklen_t client_len = sizeof(client_addr);

         int client_fd = accept(server->socket_fd, (struct sockaddr*)&client_addr, &client_len);
         if (client_fd < 0)
         {
            if (!server->running)
            {
               break;
            }
            continue;
         }

         char buffer[4096];
         ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

         if (bytes_read > 0)
         {
            buffer[bytes_read] = '\0';

            char response[] = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: application/json\r\n"
                              "Connection: close\r\n"
                              "\r\n"
                              "{\"status\":\"ok\"}\n";

            send(client_fd, response, strlen(response), 0);
         }

         close(client_fd);
      }
   }

   return NULL;
}

static int
start_echo_server(int port)
{
   if (test_server != NULL)
   {
      return 0;
   }

   test_server = malloc(sizeof(struct echo_server));
   if (test_server == NULL)
   {
      return 1;
   }

   test_server->port = port;
   test_server->running = false;

   test_server->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
   if (test_server->socket_fd < 0)
   {
      free(test_server);
      test_server = NULL;
      return 1;
   }

   int opt = 1;
   setsockopt(test_server->socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

   struct sockaddr_in addr;
   memset(&addr, 0, sizeof(addr));
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = INADDR_ANY;
   addr.sin_port = htons(port);

   if (bind(test_server->socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
   {
      close(test_server->socket_fd);
      free(test_server);
      test_server = NULL;
      return 1;
   }

   if (listen(test_server->socket_fd, 5) < 0)
   {
      close(test_server->socket_fd);
      free(test_server);
      test_server = NULL;
      return 1;
   }

   test_server->running = true;

   if (pthread_create(&test_server->thread, NULL, echo_server_thread, test_server) != 0)
   {
      test_server->running = false;
      close(test_server->socket_fd);
      free(test_server);
      test_server = NULL;
      return 1;
   }

   usleep(100000);

   return 0;
}

static int
stop_echo_server(void)
{
   if (test_server == NULL)
   {
      return 0;
   }

   test_server->running = false;

   usleep(1100000);

   if (test_server->socket_fd >= 0)
   {
      close(test_server->socket_fd);
      test_server->socket_fd = -1;
   }

   pthread_detach(test_server->thread);

   free(test_server);
   test_server = NULL;

   return 0;
}

static void
setup_echo_server(void)
{
   pgmoneta_test_setup();
   start_echo_server(9999);
}

static void
teardown_echo_server(void)
{
   stop_echo_server();
   pgmoneta_test_teardown();
}
