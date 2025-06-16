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

#include <pgmoneta.h>
#include <http.h>
#include <logging.h>
#include <tsclient.h>

int
pgmoneta_tsclient_execute_http()
{
   int status;
   struct http* h = NULL;

   pgmoneta_init_logging();

   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;

   if (pgmoneta_http_connect((char*)hostname, port, secure, &h))
   {
      return 1;
   }

   status = pgmoneta_http_get(h, (char*)hostname, "/get");

   pgmoneta_http_disconnect(h);
   pgmoneta_http_destroy(h);

   return (status == 0) ? 0 : 1;
}

int
pgmoneta_tsclient_execute_https()
{
   int status;
   struct http* h = NULL;

   pgmoneta_init_logging();

   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;

   if (pgmoneta_http_connect((char*)hostname, port, secure, &h))
   {
      return 1;
   }

   status = pgmoneta_http_get(h, (char*)hostname, "/get");

   pgmoneta_http_disconnect(h);
   pgmoneta_http_destroy(h);

   return (status == 0) ? 0 : 1;
}

int
pgmoneta_tsclient_execute_http_post()
{
   int status;
   struct http* h = NULL;

   pgmoneta_init_logging();

   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;
   const char* test_data = "name=pgmoneta&version=1.0";

   if (pgmoneta_http_connect((char*)hostname, port, secure, &h))
   {
      return 1;
   }

   status = pgmoneta_http_post(h, (char*)hostname, "/post", (char*)test_data, strlen(test_data));

   pgmoneta_http_disconnect(h);
   pgmoneta_http_destroy(h);

   return (status == 0) ? 0 : 1;
}

int
pgmoneta_tsclient_execute_http_put()
{
   int status;
   struct http* h = NULL;

   pgmoneta_init_logging();

   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;
   const char* test_data = "This is a test file content for PUT request";

   if (pgmoneta_http_connect((char*)hostname, port, secure, &h))
   {
      return 1;
   }

   status = pgmoneta_http_put(h, (char*)hostname, "/put", (void*)test_data, strlen(test_data));

   pgmoneta_http_disconnect(h);
   pgmoneta_http_destroy(h);

   return (status == 0) ? 0 : 1;
}

int
pgmoneta_tsclient_execute_http_put_file()
{
   int status;
   struct http* h = NULL;
   FILE* temp_file = NULL;

   pgmoneta_init_logging();

   const char* hostname = "localhost";
   int port = 9999;
   bool secure = false;
   const char* test_data = "This is a test file content for PUT file request\nSecond line of test data\nThird line with some numbers: 12345";
   size_t data_len = strlen(test_data);

   temp_file = tmpfile();
   if (temp_file == NULL)
   {
      return 1;
   }

   if (fwrite(test_data, 1, data_len, temp_file) != data_len)
   {
      fclose(temp_file);
      return 1;
   }

   rewind(temp_file);

   if (pgmoneta_http_connect((char*)hostname, port, secure, &h))
   {
      fclose(temp_file);
      return 1;
   }

   status = pgmoneta_http_put_file(h, (char*)hostname, "/put", temp_file, data_len, "text/plain");

   pgmoneta_http_disconnect(h);
   pgmoneta_http_destroy(h);
   fclose(temp_file);

   return (status == 0) ? 0 : 1;
}