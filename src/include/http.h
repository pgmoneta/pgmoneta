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
 */

#ifndef PGMONETA_HTTP_H
#define PGMONETA_HTTP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>

#include <deque.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

/* HTTP method definitions */
#define PGMONETA_HTTP_GET  0
#define PGMONETA_HTTP_POST 1
#define PGMONETA_HTTP_PUT  2

/* HTTP status codes */
#define PGMONETA_HTTP_STATUS_OK    0
#define PGMONETA_HTTP_STATUS_ERROR 1

/** @struct http_payload
 * Defines shared HTTP message content
 */
struct http_payload
{
   struct deque* headers;  /**< HTTP headers as deque */
   void* data;             /**< Message data */
   size_t data_size;       /**< Size of message data */
};

/** @struct http_request
 * Defines a HTTP request
 */
struct http_request
{
   struct http_payload payload; /**< Request payload */
   int method;                  /**< HTTP method */
   char* path;                  /**< Request path */
};

/** @struct http_response
 * Defines a HTTP response
 */
struct http_response
{
   struct http_payload payload; /**< Response payload */
   int status_code;             /**< HTTP status code */
};

/** @struct http
 * Defines a HTTP connection
 */
struct http
{
   int socket;      /**< The socket descriptor */
   SSL* ssl;        /**< The SSL connection (NULL for non-secure) */
   char* hostname;  /**< The hostname */
   int port;        /**< The port number */
   bool secure;     /**< Use SSL if true */
};

/**
 * Create a connection to a HTTP/HTTPS server
 * @param hostname The host to connect to
 * @param port The port number
 * @param secure Use SSL if true
 * @param result The resulting HTTP connection
 * @return PGMONETA_HTTP_STATUS_OK upon success, otherwise PGMONETA_HTTP_STATUS_ERROR
 */
int
pgmoneta_http_create(char* hostname, int port, bool secure, struct http** result);

/**
 * Create a HTTP request
 * @param method The HTTP method
 * @param path The request path
 * @param result The resulting HTTP request
 * @return PGMONETA_HTTP_STATUS_OK upon success, otherwise PGMONETA_HTTP_STATUS_ERROR
 */
int
pgmoneta_http_request_create(int method, char* path, struct http_request** result);

/**
 * Add a header to the HTTP request
 * @param request The HTTP request
 * @param name The header name
 * @param value The header value
 * @return PGMONETA_HTTP_STATUS_OK upon success, otherwise PGMONETA_HTTP_STATUS_ERROR
 */
int
pgmoneta_http_request_add_header(struct http_request* request, char* name, char* value);

/**
 * Get a header value from the HTTP request
 * @param request The HTTP request
 * @param name The header name
 * @return The header value, or NULL if not found
 */
char*
pgmoneta_http_request_get_header(struct http_request* request, char* name);

/**
 * Update a header in the HTTP request
 * @param request The HTTP request
 * @param name The header name
 * @param value The new header value
 * @return PGMONETA_HTTP_STATUS_OK upon success, otherwise PGMONETA_HTTP_STATUS_ERROR
 */
int
pgmoneta_http_request_update_header(struct http_request* request, char* name, char* value);

/**
 * Remove a header from the HTTP request
 * @param request The HTTP request
 * @param name The header name
 * @return PGMONETA_HTTP_STATUS_OK upon success, otherwise PGMONETA_HTTP_STATUS_ERROR
 */
int
pgmoneta_http_request_remove_header(struct http_request* request, char* name);

/**
 * Set data for the HTTP request
 * @param request The HTTP request
 * @param data The data to set
 * @param size The size of the data
 * @return PGMONETA_HTTP_STATUS_OK upon success, otherwise PGMONETA_HTTP_STATUS_ERROR
 */
int
pgmoneta_http_set_data(struct http_request* request, void* data, size_t size);

/**
 * Get a header value from the HTTP response
 * @param response The HTTP response
 * @param name The header name
 * @return The header value, or NULL if not found
 */
char*
pgmoneta_http_get_response_header(struct http_response* response, char* name);

/**
 * Execute a HTTP request
 * @param connection The HTTP connection
 * @param request The HTTP request
 * @param response The resulting HTTP response
 * @return PGMONETA_HTTP_STATUS_OK upon success, otherwise PGMONETA_HTTP_STATUS_ERROR
 */
int
pgmoneta_http_invoke(struct http* connection, struct http_request* request, struct http_response** response);

/**
 * Destroy a HTTP request structure
 * @param request The HTTP request
 * @return PGMONETA_HTTP_STATUS_OK upon success, otherwise PGMONETA_HTTP_STATUS_ERROR
 */
int
pgmoneta_http_request_destroy(struct http_request* request);

/**
 * Destroy a HTTP response structure
 * @param response The HTTP response
 * @return PGMONETA_HTTP_STATUS_OK upon success, otherwise PGMONETA_HTTP_STATUS_ERROR
 */
int
pgmoneta_http_response_destroy(struct http_response* response);

/**
 * Destroy a HTTP connection structure
 * @param connection The HTTP connection
 * @return PGMONETA_HTTP_STATUS_OK upon success, otherwise PGMONETA_HTTP_STATUS_ERROR
 */
int
pgmoneta_http_destroy(struct http* connection);

#ifdef __cplusplus
}
#endif

#endif
