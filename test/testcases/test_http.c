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
 *
 */
#include <http.h>
#include <tsclient.h>
#include <tscommon.h>
#include <tssuite.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>

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

START_TEST(test_pgmoneta_http_get)
{
   int status;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;

   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;

   ck_assert_msg(!pgmoneta_http_create((char*)hostname, port, secure, &connection), "failed to establish connection");

   ck_assert_msg(!pgmoneta_http_request_create(PGMONETA_HTTP_GET, "/get", &request), "failed to create request");

   status = pgmoneta_http_invoke(connection, request, &response);
   ck_assert_msg(status == PGMONETA_HTTP_STATUS_OK, "HTTP GET request failed");

   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);
}
END_TEST
START_TEST(test_pgmoneta_http_post)
{
   int status;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;
   const char* test_data = "name=pgmoneta&version=1.0";

   ck_assert_msg(!pgmoneta_http_create((char*)hostname, port, secure, &connection), "failed to establish connection");

   ck_assert_msg(!pgmoneta_http_request_create(PGMONETA_HTTP_POST, "/post", &request), "failed to create request");

   ck_assert_msg(!pgmoneta_http_set_data(request, (void*)test_data, strlen(test_data)), "failed to set request data");

   status = pgmoneta_http_invoke(connection, request, &response);
   ck_assert_msg(status == PGMONETA_HTTP_STATUS_OK, "HTTP POST request failed");

   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);
}
END_TEST
START_TEST(test_pgmoneta_http_put)
{
   int status;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;
   const char* test_data = "This is a test file content for PUT request";

   ck_assert_msg(!pgmoneta_http_create((char*)hostname, port, secure, &connection), "failed to establish connection");

   ck_assert_msg(!pgmoneta_http_request_create(PGMONETA_HTTP_PUT, "/put", &request), "failed to create request");

   ck_assert_msg(!pgmoneta_http_set_data(request, (void*)test_data, strlen(test_data)), "failed to set request data");

   status = pgmoneta_http_invoke(connection, request, &response);
   ck_assert_msg(status == PGMONETA_HTTP_STATUS_OK, "HTTP PUT request failed");

   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);
}
END_TEST
START_TEST(test_pgmoneta_http_put_file)
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

   temp_file = tmpfile();
   ck_assert_ptr_nonnull(temp_file);

   ck_assert_msg(fwrite(test_data, 1, data_len, temp_file) == data_len, "wrote file incomplete");

   rewind(temp_file);

   file_data = malloc(data_len);
   ck_assert_ptr_nonnull(file_data);

   ck_assert_msg(fread(file_data, 1, data_len, temp_file) == data_len, "read file incomplete");

   ck_assert_msg(!pgmoneta_http_create((char*)hostname, port, secure, &connection), "failed to establish connection");

   ck_assert_msg(!pgmoneta_http_request_create(PGMONETA_HTTP_PUT, "/put", &request), "failed to create request");

   ck_assert_msg(!pgmoneta_http_request_add_header(request, "Content-Type", "text/plain"), "failed to add content type header");

   ck_assert_msg(!pgmoneta_http_set_data(request, file_data, data_len), "failed to set request data");

   status = pgmoneta_http_invoke(connection, request, &response);
   ck_assert_msg(status == PGMONETA_HTTP_STATUS_OK, "HTTP PUT file request failed");

   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);
   free(file_data);
   fclose(temp_file);
}
END_TEST
START_TEST(test_pgmoneta_http_header_operations)
{
   struct http_request* request = NULL;
   char* header_value = NULL;

   ck_assert_msg(!pgmoneta_http_request_create(PGMONETA_HTTP_GET, "/test", &request), "failed to create request");

   ck_assert_msg(!pgmoneta_http_request_add_header(request, "Authorization", "Bearer token123"), "failed to add Authorization header");
   ck_assert_msg(!pgmoneta_http_request_add_header(request, "Content-Type", "application/json"), "failed to add Content-Type header");

   header_value = pgmoneta_http_request_get_header(request, "Authorization");
   ck_assert_ptr_nonnull(header_value);
   ck_assert_str_eq(header_value, "Bearer token123");

   header_value = pgmoneta_http_request_get_header(request, "Content-Type");
   ck_assert_ptr_nonnull(header_value);
   ck_assert_str_eq(header_value, "application/json");

   ck_assert_ptr_null(pgmoneta_http_request_get_header(request, "NonExistent"));

   ck_assert_msg(!pgmoneta_http_request_update_header(request, "Authorization", "Bearer newtoken456"), "failed to update Authorization header");

   header_value = pgmoneta_http_request_get_header(request, "Authorization");
   ck_assert_ptr_nonnull(header_value);
   ck_assert_str_eq(header_value, "Bearer newtoken456");

   ck_assert_msg(!pgmoneta_http_request_remove_header(request, "Content-Type"), "failed to remove Content-Type header");

   header_value = pgmoneta_http_request_get_header(request, "Content-Type");
   ck_assert_ptr_null(header_value);

   header_value = pgmoneta_http_request_get_header(request, "Authorization");
   ck_assert_ptr_nonnull(header_value);
   ck_assert_str_eq(header_value, "Bearer newtoken456");

   pgmoneta_http_request_destroy(request);
}
END_TEST

Suite*
pgmoneta_test_http_suite()
{
   Suite* s;
   TCase* tc_http_basic;
   s = suite_create("pgmoneta_test_http");

   tc_http_basic = tcase_create("http_basic_test");

   tcase_set_timeout(tc_http_basic, 60);
   tcase_add_checked_fixture(tc_http_basic, setup_echo_server, teardown_echo_server);
   tcase_add_test(tc_http_basic, test_pgmoneta_http_get);
   tcase_add_test(tc_http_basic, test_pgmoneta_http_post);
   tcase_add_test(tc_http_basic, test_pgmoneta_http_put);
   tcase_add_test(tc_http_basic, test_pgmoneta_http_put_file);
   tcase_add_test(tc_http_basic, test_pgmoneta_http_header_operations);
   suite_add_tcase(s, tc_http_basic);

   return s;
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