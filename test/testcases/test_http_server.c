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

#include <http_server.h>
#include <message.h>
#include <pgmoneta.h>
#include <shmem.h>
#include <tsclient.h>
#include <tscommon.h>
#include <utils.h>
#include <mctf.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

static bool shmem_allocated = false;

static int write_all(int fd, const char* data, size_t len);
static int read_all(int fd, char* buf, size_t buflen);
static int send_raw_request(char* request, struct http_server_request** req);
static int handler_echo_marker(SSL* ssl, int fd);

MCTF_MODULE_SETUP(http_server)
{
   struct main_configuration* config = NULL;

   if (shmem == NULL)
   {
      pgmoneta_create_shared_memory(sizeof(struct main_configuration), HUGEPAGE_OFF, &shmem);
      memset(shmem, 0, sizeof(struct main_configuration));
      shmem_allocated = true;
   }

   config = (struct main_configuration*)shmem;
   config->authentication_timeout.s = 5;
}

MCTF_MODULE_TEARDOWN(http_server)
{
   if (shmem_allocated && shmem != NULL)
   {
      pgmoneta_destroy_shared_memory(shmem, sizeof(struct main_configuration));
      shmem = NULL;
      shmem_allocated = false;
   }
}

MCTF_TEST_SETUP(http_server)
{
   pgmoneta_test_setup();
}

MCTF_TEST_TEARDOWN(http_server)
{
   pgmoneta_test_teardown();
}

/**
 * Parses a raw GET request and returns the resulting request struct.
 * Uses a socketpair: writes @p request to one end, parses from the other.
 */
static int
send_raw_request(char* request, struct http_server_request** req)
{
   int sockets[2];

   if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets))
   {
      return -1;
   }

   write_all(sockets[0], request, strlen(request));
   close(sockets[0]);

   int status = pgmoneta_http_server_parse(NULL, sockets[1], req);

   close(sockets[1]);

   return status;
}

MCTF_TEST(test_parse_valid_get)
{
   struct http_server_request* req = NULL;
   int status = send_raw_request("GET /metrics HTTP/1.1\r\nHost: localhost\r\n\r\n", &req);

   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_OK, cleanup, "parse should succeed for GET /metrics");
   MCTF_ASSERT_PTR_NONNULL(req, cleanup, "req should be allocated");
   MCTF_ASSERT_STR_EQ(req->path, "/metrics", cleanup, "path should be /metrics");

cleanup:
   pgmoneta_http_server_request_destroy(req);
   MCTF_FINISH();
}

MCTF_TEST(test_parse_valid_root)
{
   struct http_server_request* req = NULL;
   int status = send_raw_request("GET / HTTP/1.1\r\n\r\n", &req);

   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_OK, cleanup, "parse should succeed for GET /");
   MCTF_ASSERT_PTR_NONNULL(req, cleanup, "req should be allocated");
   MCTF_ASSERT_STR_EQ(req->path, "/", cleanup, "path should be /");

cleanup:
   pgmoneta_http_server_request_destroy(req);
   MCTF_FINISH();
}

MCTF_TEST(test_parse_non_get)
{
   struct http_server_request* req = (struct http_server_request*)1;
   int status = send_raw_request("POST /metrics HTTP/1.1\r\n\r\n", &req);

   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_ERROR, cleanup, "non-GET should fail");
   MCTF_ASSERT_PTR_NULL(req, cleanup, "req should stay NULL on failure");

cleanup:
   pgmoneta_http_server_request_destroy(req);
   MCTF_FINISH();
}

MCTF_TEST(test_parse_too_short)
{
   struct http_server_request* req = (struct http_server_request*)1;
   int status = send_raw_request("GE", &req);

   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_ERROR, cleanup, "message shorter than 3 bytes should fail");
   MCTF_ASSERT_PTR_NULL(req, cleanup, "req should stay NULL on failure");

cleanup:
   pgmoneta_http_server_request_destroy(req);
   MCTF_FINISH();
}

MCTF_TEST(test_parse_no_terminating_space)
{
   struct http_server_request* req = (struct http_server_request*)1;
   int status = send_raw_request("GET /metrics", &req);

   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_ERROR, cleanup, "request line without trailing space must not read out of bounds");
   MCTF_ASSERT_PTR_NULL(req, cleanup, "req should stay NULL on failure");

cleanup:
   pgmoneta_http_server_request_destroy(req);
   MCTF_FINISH();
}

static int
handler_echo_marker(SSL* ssl, int fd)
{
   return pgmoneta_http_respond_ok(ssl, fd, "text/plain", "marker", 6);
}

MCTF_TEST(test_dispatch_route_match)
{
   int sockets[2];
   int status;
   char buffer[1024];
   struct http_server_request* req = NULL;
   struct http_route routes[] = {
      {"/test", handler_echo_marker},
   };

   MCTF_ASSERT(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), cleanup, "socketpair failed");

   req = (struct http_server_request*)malloc(sizeof(struct http_server_request));
   MCTF_ASSERT_PTR_NONNULL(req, cleanup, "failed to allocate req");
   memset(req, 0, sizeof(struct http_server_request));
   strncpy(req->path, "/test", sizeof(req->path) - 1);

   status = pgmoneta_http_server_dispatch(NULL, sockets[0], req,
                                          routes, sizeof(routes) / sizeof(routes[0]));
   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_OK, cleanup, "dispatch should succeed");

   read_all(sockets[1], buffer, sizeof(buffer));
   MCTF_ASSERT(strstr(buffer, "HTTP/1.1 200 OK") != NULL, cleanup, "handler response should be 200 OK");
   MCTF_ASSERT(strstr(buffer, "marker") != NULL, cleanup, "handler response should contain marker");

cleanup:
   pgmoneta_http_server_request_destroy(req);
   close(sockets[0]);
   close(sockets[1]);
   MCTF_FINISH();
}

MCTF_TEST(test_dispatch_unknown_404)
{
   int sockets[2];
   int status;
   char buffer[1024];
   struct http_server_request* req = NULL;
   struct http_route routes[] = {
      {"/test", handler_echo_marker},
   };

   MCTF_ASSERT(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), cleanup, "socketpair failed");

   req = (struct http_server_request*)malloc(sizeof(struct http_server_request));
   MCTF_ASSERT_PTR_NONNULL(req, cleanup, "failed to allocate req");
   memset(req, 0, sizeof(struct http_server_request));
   strncpy(req->path, "/nope", sizeof(req->path) - 1);

   status = pgmoneta_http_server_dispatch(NULL, sockets[0], req,
                                          routes, sizeof(routes) / sizeof(routes[0]));
   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_OK, cleanup, "404 response should be sent successfully");

   read_all(sockets[1], buffer, sizeof(buffer));
   MCTF_ASSERT(strstr(buffer, "HTTP/1.1 404 Not Found") != NULL, cleanup, "unknown route should return 404");
   MCTF_ASSERT(strstr(buffer, "Connection: close\r\n\r\n") != NULL, cleanup, "404 must be header-terminated");

cleanup:
   pgmoneta_http_server_request_destroy(req);
   close(sockets[0]);
   close(sockets[1]);
   MCTF_FINISH();
}

MCTF_TEST(test_dispatch_null_request_400)
{
   int sockets[2];
   int status;
   char buffer[1024];
   struct http_route routes[] = {
      {"/test", handler_echo_marker},
   };

   MCTF_ASSERT(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), cleanup, "socketpair failed");

   status = pgmoneta_http_server_dispatch(NULL, sockets[0], NULL,
                                          routes, sizeof(routes) / sizeof(routes[0]));
   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_OK, cleanup, "400 response should be sent successfully");

   read_all(sockets[1], buffer, sizeof(buffer));
   MCTF_ASSERT(strstr(buffer, "HTTP/1.1 400 Bad Request") != NULL, cleanup, "NULL request should return 400");
   MCTF_ASSERT(strstr(buffer, "Connection: close\r\n\r\n") != NULL, cleanup, "400 must be header-terminated");

cleanup:
   close(sockets[0]);
   close(sockets[1]);
   MCTF_FINISH();
}

MCTF_TEST(test_respond_ok)
{
   int sockets[2];
   int status;
   char buffer[2048];

   MCTF_ASSERT(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), cleanup, "socketpair failed");

   status = pgmoneta_http_respond_ok(NULL, sockets[0], "text/plain; version=0.0.1", "hello world", 11);
   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_OK, cleanup, "respond_ok should succeed");

   read_all(sockets[1], buffer, sizeof(buffer));
   MCTF_ASSERT(strstr(buffer, "HTTP/1.1 200 OK\r\n") != NULL, cleanup, "missing 200 OK status line");
   MCTF_ASSERT(strstr(buffer, "Content-Type: text/plain; version=0.0.1\r\n") != NULL, cleanup, "missing Content-Type");
   MCTF_ASSERT(strstr(buffer, "Content-Length: 11\r\n") != NULL, cleanup, "missing Content-Length");
   MCTF_ASSERT(strstr(buffer, "\r\n\r\nhello world") != NULL, cleanup, "body should follow headers");

cleanup:
   close(sockets[0]);
   close(sockets[1]);
   MCTF_FINISH();
}

MCTF_TEST(test_respond_400)
{
   int sockets[2];
   int status;
   char buffer[1024];

   MCTF_ASSERT(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), cleanup, "socketpair failed");

   status = pgmoneta_http_respond_400(NULL, sockets[0]);
   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_OK, cleanup, "respond_400 should succeed");

   read_all(sockets[1], buffer, sizeof(buffer));
   MCTF_ASSERT(strstr(buffer, "HTTP/1.1 400 Bad Request\r\n") != NULL, cleanup, "missing 400 status line");
   MCTF_ASSERT(strstr(buffer, "Content-Length: 0\r\n") != NULL, cleanup, "missing Content-Length");
   MCTF_ASSERT(strstr(buffer, "Connection: close\r\n\r\n") != NULL, cleanup, "400 must be header-terminated");

cleanup:
   close(sockets[0]);
   close(sockets[1]);
   MCTF_FINISH();
}

MCTF_TEST(test_respond_404)
{
   int sockets[2];
   int status;
   char buffer[1024];

   MCTF_ASSERT(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), cleanup, "socketpair failed");

   status = pgmoneta_http_respond_404(NULL, sockets[0]);
   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_OK, cleanup, "respond_404 should succeed");

   read_all(sockets[1], buffer, sizeof(buffer));
   MCTF_ASSERT(strstr(buffer, "HTTP/1.1 404 Not Found\r\n") != NULL, cleanup, "missing 404 status line");
   MCTF_ASSERT(strstr(buffer, "Content-Length: 0\r\n") != NULL, cleanup, "missing Content-Length");
   MCTF_ASSERT(strstr(buffer, "Connection: close\r\n\r\n") != NULL, cleanup, "404 must be header-terminated");

cleanup:
   close(sockets[0]);
   close(sockets[1]);
   MCTF_FINISH();
}

MCTF_TEST(test_respond_redirect)
{
   int sockets[2];
   int status;
   char buffer[2048];

   MCTF_ASSERT(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), cleanup, "socketpair failed");

   status = pgmoneta_http_respond_redirect(NULL, sockets[0], "https://example.com/metrics");
   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_OK, cleanup, "respond_redirect should succeed");

   read_all(sockets[1], buffer, sizeof(buffer));
   MCTF_ASSERT(strstr(buffer, "HTTP/1.1 301 Moved Permanently\r\n") != NULL, cleanup, "missing 301 status line");
   MCTF_ASSERT(strstr(buffer, "Location: https://example.com/metrics\r\n") != NULL, cleanup, "missing Location header");
   MCTF_ASSERT(strstr(buffer, "Connection: close\r\n\r\n") != NULL, cleanup, "redirect must be header-terminated");

cleanup:
   close(sockets[0]);
   close(sockets[1]);
   MCTF_FINISH();
}

MCTF_TEST(test_chunked_sequence)
{
   int sockets[2];
   int status;
   char buffer[4096];

   MCTF_ASSERT(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), cleanup, "socketpair failed");

   status = pgmoneta_http_respond_chunked_start(NULL, sockets[0], "text/plain");
   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_OK, cleanup, "chunked start should succeed");

   status = pgmoneta_http_respond_chunked_write(NULL, sockets[0], "abc");
   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_OK, cleanup, "chunked write should succeed");

   status = pgmoneta_http_respond_chunked_write(NULL, sockets[0], "defgh");
   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_OK, cleanup, "chunked write should succeed");

   status = pgmoneta_http_respond_chunked_end(NULL, sockets[0]);
   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_OK, cleanup, "chunked end should succeed");

   read_all(sockets[1], buffer, sizeof(buffer));
   MCTF_ASSERT(strstr(buffer, "HTTP/1.1 200 OK\r\n") != NULL, cleanup, "missing 200 OK status line");
   MCTF_ASSERT(strstr(buffer, "Transfer-Encoding: chunked\r\n") != NULL, cleanup, "missing Transfer-Encoding");
   MCTF_ASSERT(strstr(buffer, "Content-Length:") == NULL, cleanup, "chunked response must not have Content-Length");
   MCTF_ASSERT(strstr(buffer, "3\r\nabc\r\n") != NULL, cleanup, "missing abc chunk framing");
   MCTF_ASSERT(strstr(buffer, "5\r\ndefgh\r\n") != NULL, cleanup, "missing defgh chunk framing");
   MCTF_ASSERT(strstr(buffer, "0\r\n\r\n") != NULL, cleanup, "missing terminal chunk");

cleanup:
   close(sockets[0]);
   close(sockets[1]);
   MCTF_FINISH();
}

MCTF_TEST(test_chunked_write_null)
{
   int sockets[2];
   int status;

   MCTF_ASSERT(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), cleanup, "socketpair failed");

   status = pgmoneta_http_respond_chunked_write(NULL, sockets[0], NULL);
   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_ERROR, cleanup, "NULL chunk data should fail");

cleanup:
   close(sockets[0]);
   close(sockets[1]);
   MCTF_FINISH();
}

MCTF_TEST(test_ssl_accept_null)
{
   int sockets[2];
   int status;

   MCTF_ASSERT(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), cleanup, "socketpair failed");

   status = pgmoneta_http_server_ssl_accept(NULL, sockets[0]);
   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_OK, cleanup, "ssl_accept without SSL should pass through");

cleanup:
   close(sockets[0]);
   close(sockets[1]);
   MCTF_FINISH();
}

static int
write_all(int fd, const char* data, size_t len)
{
   size_t total = 0;
   ssize_t n;

   while (total < len)
   {
      n = write(fd, data + total, len - total);
      if (n <= 0)
      {
         return -1;
      }
      total += (size_t)n;
   }

   return 0;
}

static int
read_all(int fd, char* buf, size_t buflen)
{
   size_t total = 0;
   ssize_t n;
   int flags = fcntl(fd, F_GETFL, 0);

   fcntl(fd, F_SETFL, flags | O_NONBLOCK);

   while (total < buflen - 1)
   {
      n = read(fd, buf + total, buflen - 1 - total);
      if (n > 0)
      {
         total += (size_t)n;
      }
      else
      {
         break;
      }
   }

   buf[total] = '\0';

   return (int)total;
}