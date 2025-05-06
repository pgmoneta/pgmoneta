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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

/* HTTP method definitions */
#define PGMONETA_HTTP_GET  0
#define PGMONETA_HTTP_POST 1
#define PGMONETA_HTTP_PUT  2

/** @struct http
 * Defines a HTTP interaction
 */
struct http
{
   int endpoint;           /**< The endpoint */
   int socket;             /**< The socket descriptor */
   char* body;             /**< The HTTP response body */
   char* headers;          /**< The HTTP response headers */
   char* request_headers;  /**< The HTTP request headers */
   SSL* ssl;               /**< The SSL connection (NULL for non-secure) */
};

/**
 * Connect to an HTTP/HTTPS server
 * @param hostname The host to connect to
 * @param port The port number
 * @param secure Use SSL if true
 * @param result The resulting HTTP structure
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_http_connect(char* hostname, int port, bool secure, struct http** result);

/**
 * Disconnect and clean up HTTP resources
 * @param http The HTTP structure
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_http_disconnect(struct http* http);

/**
 * Add a header to the HTTP request
 * @param http The HTTP structure
 * @param name The header name
 * @param value The header value
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_http_add_header(struct http* http, char* name, char* value);

/**
 * Read response data directly from a socket
 * @param ssl SSL connection (can be NULL for non-secure connections)
 * @param socket The socket to read from
 * @param response_text Pointer to store the response text
 * @return MESSAGE_STATUS_OK on success, MESSAGE_STATUS_ERROR otherwise
 */
int
pgmoneta_http_read(SSL* ssl, int socket, char** response_text);

/**
 * Perform HTTP GET request
 * @param http The HTTP structure
 * @param hostname The hostname for the Host header
 * @param path The path for the request
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_http_get(struct http* http, char* hostname, char* path);

/**
 * Perform HTTP POST request
 * @param http The HTTP structure
 * @param hostname The hostname for the Host header
 * @param path The path for the request
 * @param data The data to send
 * @param length The length of the data
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_http_post(struct http* http, char* hostname, char* path, char* data, size_t length);

/**
 * Perform HTTP PUT request
 * @param http The HTTP structure
 * @param hostname The hostname for the Host header
 * @param path The path for the request
 * @param data The data to upload
 * @param length The length of the data
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_http_put(struct http* http, char* hostname, char* path, const void* data, size_t length);

/**
 * Perform HTTP PUT request with a file
 * @param http The HTTP structure
 * @param hostname The hostname for the Host header
 * @param path The path for the request
 * @param file The file to upload
 * @param file_size The size of the file
 * @param content_type The content type of the file (can be NULL for default)
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_http_put_file(struct http* http, char* hostname, char* path, FILE* file, size_t file_size, char* content_type);

#ifdef __cplusplus
}
#endif

#endif
