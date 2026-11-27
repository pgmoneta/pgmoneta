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
#include <deque.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>

struct echo_server
{
   int socket_fd;
   int port;
   pthread_t thread;
   bool running;
   char* response;
   size_t response_len;
};

static struct echo_server* test_server = NULL;

static void* echo_server_thread(void* arg);
static int start_echo_server(int port, char* response, size_t response_len);
static int stop_echo_server(void);
static void setup_echo_server(char* response);
static void setup_echo_server_binary(char* response, size_t response_len);
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
   setup_echo_server(NULL);

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
MCTF_TEST(test_pgmoneta_http_get_content_length)
{
   int status;
   char* content_length;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;

   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;

   char* response_text =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: application/json\r\n"
      "Connection: close\r\n"
      "Content-Length: 16\r\n"
      "\r\n"
      "{\"status\":\"ok\"}\n";

   setup_echo_server(response_text);

   MCTF_ASSERT(!pgmoneta_http_create((char*)hostname, port, secure, &connection), cleanup, "failed to establish connection");
   MCTF_ASSERT(!pgmoneta_http_request_create(PGMONETA_HTTP_GET, "/get", &request), cleanup, "failed to create request");
   status = pgmoneta_http_invoke(connection, request, &response);
   MCTF_ASSERT_INT_EQ(status, PGMONETA_HTTP_STATUS_OK, cleanup, "HTTP GET request failed");
   MCTF_ASSERT(response->payload.headers, cleanup, "headers is null");
   content_length = (char*)pgmoneta_deque_get(response->payload.headers, "Content-Length");
   MCTF_ASSERT_STR_EQ(content_length, "16", cleanup, "Content-Length header mismatch");

cleanup:
   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);
   teardown_echo_server();
   MCTF_FINISH();
}
MCTF_TEST(test_pgmoneta_http_head)
{
   int status;
   char* content_length;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;

   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;

   /* A HEAD response has the GET headers but no body, even though it
    * advertises Content-Length. The client must not wait for those bytes. */
   char* response_text =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: application/octet-stream\r\n"
      "Connection: close\r\n"
      "Content-Length: 1234\r\n"
      "\r\n";

   setup_echo_server(response_text);

   MCTF_ASSERT(!pgmoneta_http_create((char*)hostname, port, secure, &connection), cleanup, "failed to establish connection");
   MCTF_ASSERT(!pgmoneta_http_request_create(PGMONETA_HTTP_HEAD, "/head", &request), cleanup, "failed to create request");
   status = pgmoneta_http_invoke(connection, request, &response);
   MCTF_ASSERT_INT_EQ(status, PGMONETA_HTTP_STATUS_OK, cleanup, "HTTP HEAD request failed");
   MCTF_ASSERT_INT_EQ(response->status_code, 200, cleanup, "unexpected HEAD status code");
   MCTF_ASSERT(response->payload.headers, cleanup, "headers is null");
   content_length = (char*)pgmoneta_deque_get(response->payload.headers, "Content-Length");
   MCTF_ASSERT_STR_EQ(content_length, "1234", cleanup, "Content-Length header mismatch");

cleanup:
   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);
   teardown_echo_server();
   MCTF_FINISH();
}
MCTF_TEST(test_pgmoneta_http_get_truncated_chunk)
{
   int status;
   char* truncated;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;

   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;
   char* response_text = "HTTP/1.1 200 OK\r\n"
                         "Content-Type: application/json\r\n"
                         "Transfer-Encoding: chunked\r\n"
                         "Connection: close\r\n"
                         "\r\n"
                         "10\r\n"
                         "{\"status\":\"ok\"}\n"
                         "\r\n"
                         "0\r\n"
                         "\r\n";
   setup_echo_server(response_text);

   MCTF_ASSERT(!pgmoneta_http_create((char*)hostname, port, secure, &connection), cleanup, "failed to establish connection");
   MCTF_ASSERT(!pgmoneta_http_request_create(PGMONETA_HTTP_GET, "/get", &request), cleanup, "failed to create request");

   status = pgmoneta_http_invoke(connection, request, &response);
   MCTF_ASSERT_INT_EQ(status, PGMONETA_HTTP_STATUS_OK, cleanup, "HTTP GET request failed");

   MCTF_ASSERT(response->payload.headers, cleanup, "headers is null");
   truncated = (char*)pgmoneta_deque_get(response->payload.headers, "Transfer-Encoding");
   MCTF_ASSERT(truncated, cleanup, "Truncated-Chunk is null");

cleanup:
   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);
   teardown_echo_server();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_http_get_binary_body)
{
   int status;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   char* raw_response = NULL;
   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;

   // binary payload starts with \0, has \0 throughout
   // mirrors what a real PostgreSQL data file chunk looks like
   // this is some sort of a real file from a backup
   unsigned char binary_payload[] = {
      0x00, 0x01, 0x02, 0x03,
      0x00, 0xFF, 0x00, 0xAB,
      0x50, 0x47, 0x00, 0x44,
      0x00, 0x00, 0x00, 0x00};
   size_t payload_size = sizeof(binary_payload);

   char header[] =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: application/octet-stream\r\n"
      "Connection: close\r\n"
      "Content-Length: 16\r\n"
      "\r\n";

   size_t header_len = strlen(header);
   size_t total_len = header_len + payload_size;

   raw_response = malloc(total_len);
   MCTF_ASSERT_PTR_NONNULL(raw_response, cleanup, "failed to allocate raw response");

   memcpy(raw_response, header, header_len);
   memcpy(raw_response + header_len, binary_payload, payload_size);

   setup_echo_server_binary(raw_response, total_len);

   MCTF_ASSERT(!pgmoneta_http_create((char*)hostname, port, secure, &connection),
               cleanup, "failed to establish connection");
   MCTF_ASSERT(!pgmoneta_http_request_create(PGMONETA_HTTP_GET, "/binary", &request),
               cleanup, "failed to create request");

   status = pgmoneta_http_invoke(connection, request, &response);
   MCTF_ASSERT_INT_EQ(status, PGMONETA_HTTP_STATUS_OK, cleanup,
                      "HTTP GET binary request failed");

   // if pgmoneta_append truncated at first \0, data_size will be 0 not 16
   MCTF_ASSERT_INT_EQ((int)response->payload.data_size, (int)payload_size, cleanup,
                      "binary body truncated — data_size mismatch, null byte handling broken");

   // memcmp is null-byte safe verifies every byte survived intact
   MCTF_ASSERT(!memcmp(response->payload.data, binary_payload, payload_size),
               cleanup, "binary body corrupted — byte content mismatch");

cleanup:
   free(raw_response);
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

   setup_echo_server(NULL);

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

   setup_echo_server(NULL);

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

   setup_echo_server(NULL);

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

   header_value = pgmoneta_http_request_get_header(request, "authorization");
   MCTF_ASSERT_PTR_NONNULL(header_value, cleanup, "lowercase header authorization should not be null");
   MCTF_ASSERT_STR_EQ(header_value, "Bearer token123", cleanup, "lowercase header authorization mismatch");

   header_value = pgmoneta_http_request_get_header(request, "Content-Type");
   MCTF_ASSERT_PTR_NONNULL(header_value, cleanup, "header Content-Type should not be null");
   MCTF_ASSERT_STR_EQ(header_value, "application/json", cleanup, "header Content-Type mismatch");

   header_value = pgmoneta_http_request_get_header(request, "CONTENT-TYPE");
   MCTF_ASSERT_PTR_NONNULL(header_value, cleanup, "uppercase header Content-Type should not be null");
   MCTF_ASSERT_STR_EQ(header_value, "application/json", cleanup, "uppercase header Content-Type mismatch");

   MCTF_ASSERT_PTR_NULL(pgmoneta_http_request_get_header(request, "NonExistent"), cleanup, "header NonExistent should be null");

   MCTF_ASSERT(!pgmoneta_http_request_update_header(request, "authorization", "Bearer newtoken456"), cleanup, "failed to update Authorization header with lowercase name");

   header_value = pgmoneta_http_request_get_header(request, "Authorization");
   MCTF_ASSERT_PTR_NONNULL(header_value, cleanup, "updated header value is null");
   MCTF_ASSERT_STR_EQ(header_value, "Bearer newtoken456", cleanup, "updated header value mismatch");

   MCTF_ASSERT(!pgmoneta_http_request_remove_header(request, "content-type"), cleanup, "failed to remove Content-Type header with lowercase name");

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

struct write_cb_context
{
   char data[64];
   size_t data_size;
};

static size_t
collect_write_cb(void* buffer, size_t size, void* userdata)
{
   struct write_cb_context* context = (struct write_cb_context*)userdata;

   if (context == NULL || context->data_size + size > sizeof(context->data))
   {
      return 0;
   }

   memcpy(context->data + context->data_size, buffer, size);
   context->data_size += size;

   return size;
}

MCTF_TEST(test_pgmoneta_http_cb_fields_initialize)
{
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   pgmoneta_test_setup();
   MCTF_ASSERT(!pgmoneta_http_request_create(PGMONETA_HTTP_GET, "/test", &request), cleanup, "failed to create request");
   response = (struct http_response*)calloc(1, sizeof(struct http_response));
   MCTF_ASSERT_PTR_NONNULL(response, cleanup, "failed to alloc response");
   MCTF_ASSERT_PTR_NULL((void*)request->read_cb, cleanup, "read_cb should be NULL initially");
   MCTF_ASSERT_PTR_NULL((void*)response->write_cb, cleanup, "write_cb should be NULL initially");
   MCTF_ASSERT_PTR_NULL(request->read_userdata, cleanup, "read_userdata should be NULL initially");
   MCTF_ASSERT_PTR_NULL(response->write_userdata, cleanup, "write_userdata should be NULL initially");
   response->write_cb = collect_write_cb;
   response->write_userdata = response;
   MCTF_ASSERT_PTR_NONNULL((void*)response->write_cb, cleanup, "write_cb should be set");
   MCTF_ASSERT(response->write_userdata == response, cleanup, "write_userdata should be set");
cleanup:
   pgmoneta_http_request_destroy(request);
   free(response);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_http_write_cb_content_length)
{
   int status;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   struct write_cb_context write_context = {0};

   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;

   char* response_text =
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 13\r\n"
      "\r\n"
      "Hello, World!";

   setup_echo_server(response_text);

   response = (struct http_response*)calloc(1, sizeof(struct http_response));
   MCTF_ASSERT(response != NULL, cleanup, "failed to allocate response");

   response->write_cb = collect_write_cb;
   response->write_userdata = &write_context;

   MCTF_ASSERT(!pgmoneta_http_create((char*)hostname, port, secure, &connection), cleanup, "failed to establish connection");
   MCTF_ASSERT(!pgmoneta_http_request_create(PGMONETA_HTTP_GET, "/test", &request), cleanup, "failed to create request");

   status = pgmoneta_http_invoke(connection, request, &response);
   MCTF_ASSERT_INT_EQ(status, PGMONETA_HTTP_STATUS_OK, cleanup, "HTTP request failed");
   MCTF_ASSERT_INT_EQ(write_context.data_size, 13, cleanup, "write_cb should receive content-length body");
   MCTF_ASSERT_STR_EQ(write_context.data, "Hello, World!", cleanup, "write_cb content-length body mismatch");
   MCTF_ASSERT(response->payload.data_size == 0, cleanup, "write_cb should prevent buffering in payload");

cleanup:
   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);
   teardown_echo_server();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_http_write_cb_chunked)
{
   int status;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   struct write_cb_context write_context = {0};

   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;

   char* response_text =
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "5\r\n"
      "Hello\r\n"
      "8\r\n"
      ", World!\r\n"
      "0\r\n"
      "\r\n";

   setup_echo_server(response_text);

   response = (struct http_response*)calloc(1, sizeof(struct http_response));
   MCTF_ASSERT(response != NULL, cleanup, "failed to allocate response");

   response->write_cb = collect_write_cb;
   response->write_userdata = &write_context;

   MCTF_ASSERT(!pgmoneta_http_create((char*)hostname, port, secure, &connection), cleanup, "failed to establish connection");
   MCTF_ASSERT(!pgmoneta_http_request_create(PGMONETA_HTTP_GET, "/test", &request), cleanup, "failed to create request");

   status = pgmoneta_http_invoke(connection, request, &response);
   MCTF_ASSERT_INT_EQ(status, PGMONETA_HTTP_STATUS_OK, cleanup, "HTTP chunked request failed");
   MCTF_ASSERT_INT_EQ(write_context.data_size, 13, cleanup, "write_cb should receive chunked body");
   MCTF_ASSERT_STR_EQ(write_context.data, "Hello, World!", cleanup, "write_cb chunked body mismatch");
   MCTF_ASSERT(response->payload.data_size == 0, cleanup, "write_cb should prevent buffering for chunked response");

cleanup:
   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);
   teardown_echo_server();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_http_response_header_case_insensitive)
{
   int status;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   char* header_val = NULL;

   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;

   char* response_text =
      "HTTP/1.1 200 OK\r\n"
      "content-type: application/json\r\n"
      "X-Custom-Header: CustomVal123\r\n"
      "etag: \"abcdef123456\"\r\n"
      "content-length: 16\r\n"
      "connection: close\r\n"
      "\r\n"
      "{\"status\":\"ok\"}\n";

   setup_echo_server(response_text);

   MCTF_ASSERT(!pgmoneta_http_create((char*)hostname, port, secure, &connection), cleanup, "failed to establish connection");
   MCTF_ASSERT(!pgmoneta_http_request_create(PGMONETA_HTTP_GET, "/test", &request), cleanup, "failed to create request");

   status = pgmoneta_http_invoke(connection, request, &response);
   MCTF_ASSERT_INT_EQ(status, PGMONETA_HTTP_STATUS_OK, cleanup, "HTTP GET request failed");

   /* Exact casing check */
   header_val = pgmoneta_http_get_response_header(response, "content-type");
   MCTF_ASSERT_PTR_NONNULL(header_val, cleanup, "content-type header is null");
   MCTF_ASSERT_STR_EQ(header_val, "application/json", cleanup, "content-type mismatch");

   /* Title-case query on lowercase response header */
   header_val = pgmoneta_http_get_response_header(response, "Content-Type");
   MCTF_ASSERT_PTR_NONNULL(header_val, cleanup, "Content-Type header is null");
   MCTF_ASSERT_STR_EQ(header_val, "application/json", cleanup, "Content-Type mismatch");

   /* Uppercase query on lowercase response header */
   header_val = pgmoneta_http_get_response_header(response, "CONTENT-TYPE");
   MCTF_ASSERT_PTR_NONNULL(header_val, cleanup, "CONTENT-TYPE header is null");
   MCTF_ASSERT_STR_EQ(header_val, "application/json", cleanup, "CONTENT-TYPE mismatch");

   /* Lowercase query on mixed-case response header */
   header_val = pgmoneta_http_get_response_header(response, "x-custom-header");
   MCTF_ASSERT_PTR_NONNULL(header_val, cleanup, "x-custom-header is null");
   MCTF_ASSERT_STR_EQ(header_val, "CustomVal123", cleanup, "x-custom-header mismatch");

   /* Uppercase query on mixed-case response header */
   header_val = pgmoneta_http_get_response_header(response, "X-CUSTOM-HEADER");
   MCTF_ASSERT_PTR_NONNULL(header_val, cleanup, "X-CUSTOM-HEADER is null");
   MCTF_ASSERT_STR_EQ(header_val, "CustomVal123", cleanup, "X-CUSTOM-HEADER mismatch");

   /* Title-case query on lowercase etag */
   header_val = pgmoneta_http_get_response_header(response, "ETag");
   MCTF_ASSERT_PTR_NONNULL(header_val, cleanup, "ETag header is null");
   MCTF_ASSERT_STR_EQ(header_val, "\"abcdef123456\"", cleanup, "ETag mismatch");

   /* Non-existent header returns NULL */
   header_val = pgmoneta_http_get_response_header(response, "X-Non-Existent");
   MCTF_ASSERT_PTR_NULL(header_val, cleanup, "non-existent header should be null");

cleanup:
   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);
   teardown_echo_server();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_http_get_chunked_case_insensitive)
{
   int status;
   char* header_val = NULL;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;

   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;

   /* Lowercase header name and mixed-case coding value "Chunked" */
   char* response_text =
      "HTTP/1.1 200 OK\r\n"
      "content-type: text/plain\r\n"
      "transfer-encoding: Chunked\r\n"
      "connection: close\r\n"
      "\r\n"
      "5\r\n"
      "Hello\r\n"
      "8\r\n"
      ", World!\r\n"
      "0\r\n"
      "\r\n";

   setup_echo_server(response_text);

   MCTF_ASSERT(!pgmoneta_http_create((char*)hostname, port, secure, &connection), cleanup, "failed to establish connection");
   MCTF_ASSERT(!pgmoneta_http_request_create(PGMONETA_HTTP_GET, "/chunked", &request), cleanup, "failed to create request");

   status = pgmoneta_http_invoke(connection, request, &response);
   MCTF_ASSERT_INT_EQ(status, PGMONETA_HTTP_STATUS_OK, cleanup, "HTTP GET chunked request failed");

   /* Chunk framing must be decoded: body size must be 13 ("Hello, World!"), not 28 raw chunk bytes */
   MCTF_ASSERT_INT_EQ((int)response->payload.data_size, 13, cleanup, "chunked body size mismatch");
   MCTF_ASSERT(!memcmp(response->payload.data, "Hello, World!", 13), cleanup, "chunked body content mismatch");

   header_val = pgmoneta_http_get_response_header(response, "Transfer-Encoding");
   MCTF_ASSERT_PTR_NONNULL(header_val, cleanup, "Transfer-Encoding header is null");
   MCTF_ASSERT_STR_EQ(header_val, "Chunked", cleanup, "Transfer-Encoding header mismatch");

   header_val = pgmoneta_http_get_response_header(response, "transfer-encoding");
   MCTF_ASSERT_PTR_NONNULL(header_val, cleanup, "transfer-encoding header is null");
   MCTF_ASSERT_STR_EQ(header_val, "Chunked", cleanup, "transfer-encoding header mismatch");

cleanup:
   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);
   teardown_echo_server();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_http_get_content_length_case_insensitive)
{
   int status;
   char* header_val = NULL;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;

   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;

   /* Lowercase content-length header */
   char* response_text =
      "HTTP/1.1 200 OK\r\n"
      "content-type: application/json\r\n"
      "content-length: 16\r\n"
      "connection: close\r\n"
      "\r\n"
      "{\"status\":\"ok\"}\n";

   setup_echo_server(response_text);

   MCTF_ASSERT(!pgmoneta_http_create((char*)hostname, port, secure, &connection), cleanup, "failed to establish connection");
   MCTF_ASSERT(!pgmoneta_http_request_create(PGMONETA_HTTP_GET, "/cl", &request), cleanup, "failed to create request");

   status = pgmoneta_http_invoke(connection, request, &response);
   MCTF_ASSERT_INT_EQ(status, PGMONETA_HTTP_STATUS_OK, cleanup, "HTTP GET content-length request failed");

   MCTF_ASSERT_INT_EQ((int)response->payload.data_size, 16, cleanup, "content-length body size mismatch");
   MCTF_ASSERT(!memcmp(response->payload.data, "{\"status\":\"ok\"}\n", 16), cleanup, "content-length body mismatch");

   header_val = pgmoneta_http_get_response_header(response, "Content-Length");
   MCTF_ASSERT_PTR_NONNULL(header_val, cleanup, "Content-Length header is null");
   MCTF_ASSERT_STR_EQ(header_val, "16", cleanup, "Content-Length mismatch");

   header_val = pgmoneta_http_get_response_header(response, "content-length");
   MCTF_ASSERT_PTR_NONNULL(header_val, cleanup, "content-length header is null");
   MCTF_ASSERT_STR_EQ(header_val, "16", cleanup, "content-length mismatch");

cleanup:
   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);
   teardown_echo_server();
   MCTF_FINISH();
}

struct split_chunk_server
{
   int listen_fd;
   const char* part1;
   const char* part2;
};

static void*
split_chunk_server_thread(void* arg)
{
   struct split_chunk_server* srv = (struct split_chunk_server*)arg;
   struct sockaddr_in client_addr;
   socklen_t client_len = sizeof(client_addr);
   int client_fd = accept(srv->listen_fd, (struct sockaddr*)&client_addr, &client_len);

   if (client_fd >= 0)
   {
      char buffer[4096];
      ssize_t n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
      if (n > 0)
      {
         send(client_fd, srv->part1, strlen(srv->part1), 0);
         usleep(50000);
         send(client_fd, srv->part2, strlen(srv->part2), 0);
      }
      close(client_fd);
   }

   return NULL;
}

static int
run_split_chunk_case(const char* part1, const char* part2, struct http_response** out_response)
{
   int listen_fd = -1;
   struct sockaddr_in addr;
   socklen_t addr_len = sizeof(addr);
   struct split_chunk_server srv;
   pthread_t thread;
   bool started = false;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   int status = PGMONETA_HTTP_STATUS_ERROR;

   listen_fd = socket(AF_INET, SOCK_STREAM, 0);
   if (listen_fd < 0)
   {
      goto done;
   }

   memset(&addr, 0, sizeof(addr));
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   addr.sin_port = 0;

   if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
       listen(listen_fd, 1) < 0 ||
       getsockname(listen_fd, (struct sockaddr*)&addr, &addr_len) < 0)
   {
      goto done;
   }

   srv.listen_fd = listen_fd;
   srv.part1 = part1;
   srv.part2 = part2;

   if (pthread_create(&thread, NULL, split_chunk_server_thread, &srv) != 0)
   {
      goto done;
   }
   started = true;

   if (pgmoneta_http_create("127.0.0.1", ntohs(addr.sin_port), false, &connection) ||
       pgmoneta_http_request_create(PGMONETA_HTTP_GET, "/chunked", &request))
   {
      goto done;
   }

   status = pgmoneta_http_invoke(connection, request, out_response);

done:
   if (started)
   {
      pthread_join(thread, NULL);
   }
   if (listen_fd >= 0)
   {
      close(listen_fd);
   }
   pgmoneta_http_request_destroy(request);
   pgmoneta_http_destroy(connection);
   return status;
}

MCTF_TEST(test_pgmoneta_http_get_chunked_split_crlf)
{
   struct http_response* response = NULL;
   int status;

   pgmoneta_test_setup();

   /* Data chunk trailing CRLF split: '\r' prefetched, '\n' arrives in next read */
   status = run_split_chunk_case(
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n"
      "Connection: close\r\n"
      "\r\n"
      "5\r\n"
      "Hello\r",
      "\n0\r\n\r\n",
      &response);
   MCTF_ASSERT_INT_EQ(status, PGMONETA_HTTP_STATUS_OK, cleanup, "split data trailing CRLF failed");
   MCTF_ASSERT_PTR_NONNULL(response, cleanup, "response is null for data trailing split");
   MCTF_ASSERT_INT_EQ((int)response->payload.data_size, 5, cleanup, "payload size mismatch for data trailing split");
   MCTF_ASSERT(!memcmp(response->payload.data, "Hello", 5), cleanup, "payload body mismatch for data trailing split");
   pgmoneta_http_response_destroy(response);
   response = NULL;

   /* Terminating chunk trailing CRLF split: '\r' prefetched, '\n' arrives in next read */
   status = run_split_chunk_case(
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n"
      "Connection: close\r\n"
      "\r\n"
      "5\r\n"
      "Hello\r\n"
      "0\r\n"
      "\r",
      "\n",
      &response);
   MCTF_ASSERT_INT_EQ(status, PGMONETA_HTTP_STATUS_OK, cleanup, "split terminating trailing CRLF failed");
   MCTF_ASSERT_PTR_NONNULL(response, cleanup, "response is null for terminating split");
   MCTF_ASSERT_INT_EQ((int)response->payload.data_size, 5, cleanup, "payload size mismatch for terminating split");
   MCTF_ASSERT(!memcmp(response->payload.data, "Hello", 5), cleanup, "payload body mismatch for terminating split");

cleanup:
   pgmoneta_http_response_destroy(response);
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
            send(client_fd, server->response, server->response_len, 0);
         }

         close(client_fd);
      }
   }

   return NULL;
}

static int
start_echo_server(int port, char* response, size_t response_len)
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
   if (response == NULL)
   {
      response = "HTTP/1.1 200 OK\r\n"
                 "Content-Type: application/json\r\n"
                 "Connection: close\r\n"
                 "\r\n"
                 "{\"status\":\"ok\"}\n";
      response_len = strlen(response);
   }
   test_server->response = response;
   test_server->response_len = response_len;

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
setup_echo_server(char* response)
{
   pgmoneta_test_setup();
   size_t len = response != NULL ? strlen(response) : 0;
   start_echo_server(9999, response, len);
}
static void
setup_echo_server_binary(char* response, size_t response_len)
{
   pgmoneta_test_setup();
   start_echo_server(9999, response, response_len);
}

static void
teardown_echo_server(void)
{
   stop_echo_server();
   pgmoneta_test_teardown();
}
