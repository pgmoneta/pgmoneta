/*
 * Copyright (C) 2024 The pgmoneta community
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

#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif

#include <curl/curl.h>
#include <stdbool.h>
#include <stdio.h>

#define HTTP_GET 0
#define HTTP_PUT 1

/**
 * Add a header
 * @param chunk A linked list of strings
 * @param header The header
 * @param value The header's value
 * @return A new list pointer
 */
struct curl_slist*
pgmoneta_http_add_header(struct curl_slist* chunk, char* header, char* value);

/**
 * set HTTP headers
 * @param handle A CURL easy handle
 * @param headers A pointer to a linked list of HTTP headers
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_http_set_header_option(CURL* handle, struct curl_slist* chunk);

/**
 * set HTTP request
 * @param handle A CURL easy handle
 * @param request_type A http request type
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_http_set_request_option(CURL* handle, bool request_type);

/**
 * set the URL
 * @param handle A CURL easy handle
 * @param url A URL for the transfer
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_http_set_url_option(CURL* handle, char* url);

#ifdef __cplusplus
}
#endif

#endif
