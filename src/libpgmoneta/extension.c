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

/* pgmoneta */
#include <pgmoneta.h>
#include <extension.h>
#include <logging.h>
#include <memory.h>
#include <network.h>
#include <security.h>
#include <utils.h>

/* system */
#include <stdlib.h>

static int query_execute(SSL* ssl, int socket, char* qs, struct query_response** qr);

int
pgmoneta_ext_is_installed(SSL* ssl, int socket, struct query_response** qr)
{
   return query_execute(ssl, socket, "SELECT * FROM pg_available_extensions WHERE name = 'pgmoneta_ext';", qr);
}

int
pgmoneta_ext_version(SSL* ssl, int socket, struct query_response** qr)
{
   return query_execute(ssl, socket, "SELECT pgmoneta_ext_version();", qr);
}

int
pgmoneta_ext_switch_wal(SSL* ssl, int socket, struct query_response** qr)
{
   return query_execute(ssl, socket, "SELECT pgmoneta_ext_switch_wal();", qr);
}

int
pgmoneta_ext_checkpoint(SSL* ssl, int socket, struct query_response** qr)
{
   return query_execute(ssl, socket, "SELECT pgmoneta_ext_checkpoint();", qr);
}

int
pgmoneta_ext_privilege(SSL* ssl, int socket, struct query_response** qr)
{
   return query_execute(ssl, socket, "SELECT rolsuper FROM pg_roles WHERE rolname = current_user;", qr);
}

int
pgmoneta_ext_get_file(SSL* ssl, int socket, char* file_path, struct query_response** qr)
{
   char query[MAX_QUERY_LENGTH];
   snprintf(query, MAX_QUERY_LENGTH, "SELECT pgmoneta_ext_get_file('%s');", file_path);
   return query_execute(ssl, socket, query, qr);
}

int
pgmoneta_ext_get_files(SSL* ssl, int socket, char* file_path, struct query_response** qr)
{
   char query[MAX_QUERY_LENGTH];
   snprintf(query, MAX_QUERY_LENGTH, "SELECT pgmoneta_ext_get_files('%s');", file_path);
   return query_execute(ssl, socket, query, qr);
}

int
pgmoneta_ext_send_file_chunk(SSL* ssl, int socket, char* dest_path, char* base64_data, struct query_response** qr)
{
   char query[MAX_QUERY_LENGTH];
   snprintf(query, MAX_QUERY_LENGTH, "SELECT pgmoneta_ext_receive_file_chunk('%s', '%s');", base64_data, dest_path);
   return query_execute(ssl, socket, query, qr);
}

int
pgmoneta_ext_promote(SSL* ssl, int socket, struct query_response** qr)
{
   return query_execute(ssl, socket, "SELECT pgmoneta_ext_promote();", qr);
}

static int
query_execute(SSL* ssl, int socket, char* qs, struct query_response** qr)
{
   struct message* query_msg = NULL;

   if (pgmoneta_create_query_message(qs, &query_msg) != MESSAGE_STATUS_OK || query_msg == NULL)
   {
      pgmoneta_log_info("Failed to create query message");
      goto error;
   }

   if (pgmoneta_query_execute(ssl, socket, query_msg, qr) != 0 || qr == NULL)
   {
      pgmoneta_log_info("Failed to execute query");
      goto error;
   }

   pgmoneta_free_message(query_msg);

   return 0;

error:
   if (query_msg != NULL)
   {
      pgmoneta_free_message(query_msg);
   }

   return 1;
}
