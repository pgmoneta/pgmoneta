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

#ifndef PGMONETA_HTTP_SERVER_H
#define PGMONETA_HTTP_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <openssl/ssl.h>
#include <stddef.h>

/** @struct http_server_request
 * Parsed inbound HTTP request. Populated by pgmoneta_http_server_parse().
 */
struct http_server_request
{
   char path[256]; /**< Request path extracted from the GET line (e.g. "/metrics") */
};

/**
 * Handler function type for HTTP route handlers.
 * @param ssl The SSL connection, or NULL for plain HTTP
 * @param fd  The client socket file descriptor
 * @return MESSAGE_STATUS_OK on success, otherwise MESSAGE_STATUS_ERROR
 */
typedef int (*http_handler_fn)(SSL* ssl, int fd);

/** @struct http_route
 * Maps a URL path to a handler function.
 */
struct http_route
{
   const char* path;        /**< Exact path to match (e.g. "/metrics") */
   http_handler_fn handler; /**< Handler called when path matches */
};

/**
 * Perform TLS detection and SSL_accept on an incoming connection.
 *
 * Returns MESSAGE_STATUS_OK when the TLS handshake completed successfully, or
 * when @p ssl is NULL (no TLS configured — caller proceeds with plain HTTP).
 * Returns MESSAGE_STATUS_ZERO when the connection is plain HTTP on a TLS
 * socket — the caller decides what to do (e.g. redirect to HTTPS).
 * Returns MESSAGE_STATUS_ERROR when SSL_accept() failed.
 *
 * @param ssl The SSL object created for this connection
 * @param fd  The client socket file descriptor
 * @return MESSAGE_STATUS_OK, MESSAGE_STATUS_ZERO, or MESSAGE_STATUS_ERROR
 */
int
pgmoneta_http_server_ssl_accept(SSL* ssl, int fd);

/**
 * Read and parse an inbound HTTP request from the socket.
 *
 * Calls pgmoneta_read_timeout_message() using the configured authentication
 * timeout, then extracts the request path into a newly allocated
 * http_server_request. On failure (read error or malformed request) the
 * function returns MESSAGE_STATUS_ERROR and @p *req is set to NULL.
 *
 * The caller must free the returned struct with
 * pgmoneta_http_server_request_destroy() when done.
 *
 * @param ssl The SSL connection, or NULL for plain HTTP
 * @param fd  The client socket file descriptor
 * @param req Output: pointer to the allocated request struct
 * @return MESSAGE_STATUS_OK on success, otherwise MESSAGE_STATUS_ERROR
 */
int
pgmoneta_http_server_parse(SSL* ssl, int fd, struct http_server_request** req);

/**
 * Free an http_server_request allocated by pgmoneta_http_server_parse().
 * Safe to call with NULL.
 * @param req The request to free
 */
void
pgmoneta_http_server_request_destroy(struct http_server_request* req);

/**
 * Dispatch a parsed request against a route table.
 *
 * Walks @p routes for an exact path match and calls the matching handler.
 * Sends a 404 if no route matches. The dispatch always returns after the
 * first matching route (or after sending the 404).
 *
 * @param ssl      The SSL connection, or NULL for plain HTTP
 * @param fd       The client socket file descriptor
 * @param req      The parsed request from pgmoneta_http_server_parse()
 * @param routes   Route table array
 * @param n_routes Number of entries in @p routes
 * @return MESSAGE_STATUS_OK on success, otherwise MESSAGE_STATUS_ERROR
 */
int
pgmoneta_http_server_dispatch(SSL* ssl, int fd, struct http_server_request* req,
                              struct http_route* routes, int n_routes);

/**
 * Send an HTTP 200 OK response with a fixed-size body.
 * @param ssl          The SSL connection, or NULL for plain HTTP
 * @param fd           The client socket file descriptor
 * @param content_type The Content-Type header value
 * @param body         Response body data
 * @param len          Length of @p body in bytes
 * @return MESSAGE_STATUS_OK on success, otherwise MESSAGE_STATUS_ERROR
 */
int
pgmoneta_http_respond_ok(SSL* ssl, int fd, const char* content_type,
                         const void* body, size_t len);

/**
 * Send an HTTP 400 Bad Request response.
 * @param ssl The SSL connection, or NULL for plain HTTP
 * @param fd  The client socket file descriptor
 * @return MESSAGE_STATUS_OK on success, otherwise MESSAGE_STATUS_ERROR
 */
int
pgmoneta_http_respond_400(SSL* ssl, int fd);

/**
 * Send an HTTP 404 Not Found response.
 * @param ssl The SSL connection, or NULL for plain HTTP
 * @param fd  The client socket file descriptor
 * @return MESSAGE_STATUS_OK on success, otherwise MESSAGE_STATUS_ERROR
 */
int
pgmoneta_http_respond_404(SSL* ssl, int fd);

/**
 * Send an HTTP 301 Moved Permanently redirect.
 * @param ssl      The SSL connection, or NULL for plain HTTP
 * @param fd       The client socket file descriptor
 * @param location The target URL for the Location header
 * @return MESSAGE_STATUS_OK on success, otherwise MESSAGE_STATUS_ERROR
 */
int
pgmoneta_http_respond_redirect(SSL* ssl, int fd, const char* location);

/**
 * Begin a chunked HTTP 200 OK response.
 * Must be followed by one or more pgmoneta_http_respond_chunked_write() calls
 * and exactly one pgmoneta_http_respond_chunked_end() call.
 * @param ssl          The SSL connection, or NULL for plain HTTP
 * @param fd           The client socket file descriptor
 * @param content_type The Content-Type header value
 * @return MESSAGE_STATUS_OK on success, otherwise MESSAGE_STATUS_ERROR
 */
int
pgmoneta_http_respond_chunked_start(SSL* ssl, int fd, const char* content_type);

/**
 * Write one chunk of data in a chunked response.
 * Must be called after pgmoneta_http_respond_chunked_start().
 * @param ssl  The SSL connection, or NULL for plain HTTP
 * @param fd   The client socket file descriptor
 * @param data Null-terminated chunk data (must not be NULL)
 * @return MESSAGE_STATUS_OK on success, otherwise MESSAGE_STATUS_ERROR
 */
int
pgmoneta_http_respond_chunked_write(SSL* ssl, int fd, const char* data);

/**
 * Finish a chunked response by sending the terminal zero-length chunk.
 * Must be called exactly once after all pgmoneta_http_respond_chunked_write() calls.
 * @param ssl The SSL connection, or NULL for plain HTTP
 * @param fd  The client socket file descriptor
 * @return MESSAGE_STATUS_OK on success, otherwise MESSAGE_STATUS_ERROR
 */
int
pgmoneta_http_respond_chunked_end(SSL* ssl, int fd);

#ifdef __cplusplus
}
#endif

#endif