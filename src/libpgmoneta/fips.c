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

/* pgmoneta */
#include <pgmoneta.h>
#include <extension.h>
#include <fips.h>
#include <logging.h>
#include <message.h>
#include <utils.h>

/* OpenSSL */
#include <openssl/crypto.h>
#include <openssl/evp.h>

/* system */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static int query_fips_mode(SSL* ssl, int socket, struct query_response** qr);
static int query_fips_ext(SSL* ssl, int socket, struct query_response** qr);
static int query_execute(SSL* ssl, int socket, char* qs, struct query_response** qr);

bool
pgmoneta_fips_pgmoneta(void)
{
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
   return EVP_default_properties_is_fips_enabled(NULL) == 1;
#elif OPENSSL_VERSION_NUMBER >= 0x10100000L
   return FIPS_mode() == 1;
#endif
   return false;
}

int
pgmoneta_fips_server(SSL* ssl, int socket, int server, bool* status)
{
   int ret;
   struct query_response* qr = NULL;
   struct tuple* current = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (server < 0 || server >= config->common.number_of_servers)
   {
      pgmoneta_log_error("FIPS: Invalid server index: %d", server);
      *status = false;
      return 1;
   }

   /* Return cached value if already checked */
   if (config->common.servers[server].fips_enabled != SERVER_FIPS_UNKNOWN)
   {
      *status = config->common.servers[server].fips_enabled == SERVER_FIPS_ENABLED;
      return 0;
   }

   *status = false;

   /* PostgreSQL 18+: Use fips_mode() function from pgcrypto */
   if (config->common.servers[server].version >= 18)
   {
      if (!pgmoneta_extension_is_installed(server, "pgcrypto"))
      {
         goto done;
      }

      ret = query_fips_mode(ssl, socket, &qr);
      if (ret != 0)
      {
         pgmoneta_log_debug("FIPS: fips_mode() not available for server %s, assuming disabled",
                            config->common.servers[server].name);
         goto done;
      }

      current = qr->tuples;
      if (current == NULL)
      {
         goto done;
      }

      *status = strcmp(current->data[0], "t") == 0;
   }
   else if (config->common.servers[server].version >= 14)
   {
      /* PostgreSQL 14-17: Use pgmoneta_ext_fips() from pgmoneta_ext extension */
      if (!pgmoneta_extension_is_installed(server, "pgmoneta_ext"))
      {
         goto done;
      }

      ret = query_fips_ext(ssl, socket, &qr);
      if (ret != 0)
      {
         pgmoneta_log_debug("FIPS: pgmoneta_ext_fips() not available for server %s, FIPS status unknown",
                            config->common.servers[server].name);
         goto done;
      }

      current = qr->tuples;
      if (current == NULL)
      {
         goto done;
      }

      *status = strcmp(current->data[0], "t") == 0;
   }

done:
   config->common.servers[server].fips_enabled = *status ? SERVER_FIPS_ENABLED : SERVER_FIPS_DISABLED;

   pgmoneta_free_query_response(qr);
   return 0;
}

static int
query_fips_mode(SSL* ssl, int socket, struct query_response** qr)
{
   return query_execute(ssl, socket, "SELECT fips_mode();", qr);
}

static int
query_fips_ext(SSL* ssl, int socket, struct query_response** qr)
{
   return query_execute(ssl, socket, "SELECT pgmoneta_ext_fips();", qr);
}

static int
query_execute(SSL* ssl, int socket, char* qs, struct query_response** qr)
{
   struct message* query_msg = NULL;

   if (pgmoneta_create_query_message(qs, &query_msg) != MESSAGE_STATUS_OK || query_msg == NULL)
   {
      pgmoneta_log_debug("Failed to create query message");
      goto error;
   }

   if (pgmoneta_query_execute(ssl, socket, query_msg, qr) != 0 || qr == NULL)
   {
      pgmoneta_log_debug("Failed to execute query: %s", qs);
      goto error;
   }

   pgmoneta_free_message(query_msg);

   return 0;

error:

   pgmoneta_free_message(query_msg);

   return 1;
}
