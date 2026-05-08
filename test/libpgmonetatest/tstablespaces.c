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

#include <tstablespaces.h>
#include <network.h>
#include <security.h>
#include <server.h>
#include <tsclient.h>
#include <tsclient_helpers.h>
#include <tscommon.h>

#include <stdlib.h>
#include <string.h>

int
pgmoneta_test_tablespace_pg_version(void)
{
   char* v = getenv("TEST_PG_VERSION");

   if (v != NULL)
   {
      return atoi(v);
   }

   return 0;
}

char*
pgmoneta_test_tablespace_newest_backup_label(void)
{
   struct json* response = NULL;
   struct json* backup = NULL;
   char* l = NULL;
   char* result = NULL;
   int count = 0;

   if (pgmoneta_tsclient_list_backup("primary", NULL, &response, 0))
   {
      goto error;
   }

   count = pgmoneta_tsclient_get_backup_count(response);
   if (count <= 0)
   {
      goto error;
   }

   backup = pgmoneta_tsclient_get_backup(response, count - 1);
   if (backup == NULL)
   {
      goto error;
   }

   l = pgmoneta_tsclient_get_backup_label(backup);
   if (l != NULL)
   {
      result = strdup(l);
   }

   pgmoneta_json_destroy(response);

   return result;

error:

   if (response != NULL)
   {
      pgmoneta_json_destroy(response);
   }

   return NULL;
}

int
pgmoneta_test_tablespace_index(struct backup* bck)
{
   for (uint64_t i = 0; i < bck->number_of_tablespaces; i++)
   {
      if (!strcmp(bck->tablespaces[i], TBLSPC_NAME))
      {
         return (int)i;
      }
   }

   return -1;
}

int
pgmoneta_test_tablespace_create(void)
{
   static int tablespace_created = 0;
   SSL* ssl = NULL;
   int socket = -1;
   struct query_response* qr = NULL;
   int ret = 1;

   if (tablespace_created)
   {
      return 0;
   }

   if (pgmoneta_server_authenticate(PRIMARY_SERVER, "postgres",
                                    "postgres", "",
                                    false, &ssl, &socket))
   {
      goto error;
   }

   if (pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                   "CREATE TABLESPACE test_ts LOCATION '/conf/tblspc_test';", &qr))
   {
      goto error;
   }
   pgmoneta_test_cleanup_query_response(&qr);

   if (pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket,
                                   "GRANT CREATE ON TABLESPACE test_ts TO myuser;", &qr))
   {
      goto error;
   }
   pgmoneta_test_cleanup_query_response(&qr);

   tablespace_created = 1;
   ret = 0;

   goto cleanup;

error:

   ret = 1;

cleanup:
   pgmoneta_test_cleanup_query_response(&qr);
   if (ssl != NULL)
   {
      pgmoneta_close_ssl(ssl);
   }
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }

   return ret;
}
