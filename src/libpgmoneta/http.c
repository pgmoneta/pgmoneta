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

/* pgmoneta */
#include <pgmoneta.h>
#include <http.h>
#include "utils.h"

struct curl_slist*
pgmoneta_http_add_header(struct curl_slist* chunk, char* header, char* value)
{
   char* h = NULL;

   h = pgmoneta_append(h, header);
   h = pgmoneta_append(h, ": ");
   h = pgmoneta_append(h, value);

   chunk = curl_slist_append(chunk, h);

   free(h);

   return chunk;
}

int
pgmoneta_http_set_header_option(CURL* handle, struct curl_slist* chunk)
{
   CURLcode res;

   if (handle == NULL)
   {
      goto error;
   }

   res = curl_easy_setopt(handle, CURLOPT_HTTPHEADER, chunk);

   if (res != CURLE_OK)
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_http_set_request_option(CURL* handle, bool request_type)
{
   CURLcode res = -1;

   if (handle == NULL)
   {
      goto error;
   }

   if (request_type == HTTP_GET)
   {
      res = curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);
   }
   else if (request_type == HTTP_PUT)
   {
      res = curl_easy_setopt(handle, CURLOPT_UPLOAD, 1L);
   }

   if (res != CURLE_OK)
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_http_set_url_option(CURL* handle, char* url)
{
   CURLcode res;

   if (handle == NULL)
   {
      goto error;
   }

   res = curl_easy_setopt(handle, CURLOPT_URL, url);

   if (res != CURLE_OK)
   {
      goto error;
   }

   return 0;

error:

   return 1;
}