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

#include <pgmoneta.h>
#include <network.h>
#include <security.h>
#include <server.h>
#include <tscommon.h>
#include <mctf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int srv_socket = -1;
SSL* srv_ssl = NULL;

static int setup_server_connection(void);
static void teardown_server_connection(void);

MCTF_TEST(test_server_api_info)
{
   struct main_configuration* config = NULL;
   struct server srv;
   int expected_version = 17;

   if (setup_server_connection())
   {
      teardown_server_connection();
      MCTF_SKIP();
   }

   config = (struct main_configuration*)shmem;
   pgmoneta_server_info(PRIMARY_SERVER, srv_ssl, srv_socket);

   srv = config->common.servers[PRIMARY_SERVER];
   MCTF_ASSERT(srv.primary, cleanup, "server is not primary");

   if (getenv("TEST_PG_VERSION") != NULL)
   {
      expected_version = atoi(getenv("TEST_PG_VERSION"));
   }
   MCTF_ASSERT_INT_EQ(srv.version, expected_version, cleanup, "server version mismatch");

   if (!pgmoneta_server_valid(PRIMARY_SERVER))
   {
      MCTF_ASSERT(false, cleanup, "server is not valid");
   }

cleanup:
   teardown_server_connection();
   MCTF_FINISH();
}

MCTF_TEST(test_server_api_checkpoint)
{
   uint64_t chkt;
   uint32_t tli;

   if (setup_server_connection())
   {
      teardown_server_connection();
      MCTF_SKIP();
   }

   if (pgmoneta_server_checkpoint(PRIMARY_SERVER, srv_ssl, srv_socket, &chkt, &tli))
   {
      teardown_server_connection();
      MCTF_SKIP();
   }

cleanup:
   teardown_server_connection();
   MCTF_FINISH();
}

MCTF_TEST(test_server_api_read_file)
{
   uint8_t* data = NULL;
   int data_length;
   char file_path[] = "postgresql.conf";

   if (setup_server_connection())
   {
      teardown_server_connection();
      MCTF_SKIP();
   }

   if (pgmoneta_server_read_binary_file(PRIMARY_SERVER, srv_ssl, file_path, 0, 100, srv_socket, &data, &data_length))
   {
      MCTF_ASSERT(false, cleanup, "failed to read binary file");
   }

cleanup:
   free(data);
   teardown_server_connection();
   MCTF_FINISH();
}

MCTF_TEST(test_server_api_read_file_metadata)
{
   struct file_stats stat;
   char file_path[] = "postgresql.conf";

   if (setup_server_connection())
   {
      teardown_server_connection();
      MCTF_SKIP();
   }

   if (pgmoneta_server_file_stat(PRIMARY_SERVER, srv_ssl, srv_socket, file_path, &stat))
   {
      MCTF_ASSERT(false, cleanup, "failed to read metadata of file");
   }

cleanup:
   teardown_server_connection();
   MCTF_FINISH();
}

MCTF_TEST(test_server_api_backup)
{
   int ret;
   char* start_lsn = NULL;
   char* stop_lsn = NULL;
   struct label_file_contents lf = {0};

   if (setup_server_connection())
   {
      teardown_server_connection();
      MCTF_SKIP();
   }

   if (pgmoneta_server_start_backup(PRIMARY_SERVER, srv_ssl, srv_socket, "test_backup", &start_lsn))
   {
      teardown_server_connection();
      MCTF_SKIP();
   }

   if (pgmoneta_server_stop_backup(PRIMARY_SERVER, srv_ssl, srv_socket, NULL, &stop_lsn, &lf))
   {
      free(start_lsn);
      teardown_server_connection();
      MCTF_SKIP();
   }

cleanup:
   free(start_lsn);
   free(stop_lsn);
   teardown_server_connection();
   MCTF_FINISH();
}

static int
setup_server_connection(void)
{
   int srv_usr_index = -1;
   struct main_configuration* config = NULL;

   pgmoneta_test_setup();

   config = (struct main_configuration*)shmem;

   if (config == NULL)
   {
      return 1;
   }

   // find the corresponding user's index of the given server

   for (int i = 0; i < config->common.number_of_users; i++)
   {
      if (!strcmp(config->common.servers[PRIMARY_SERVER].username, config->common.users[i].username))
      {
         srv_usr_index = i;
      }
   }

   if (srv_usr_index < 0)
   {
      return 1;
   }

   // establish a connection to repl user, with replication flag not set

   if (pgmoneta_server_authenticate(PRIMARY_SERVER, "postgres", config->common.users[srv_usr_index].username, config->common.users[srv_usr_index].password, false, &srv_ssl, &srv_socket))
   {
      return 1;
   }

   return 0;
}

static void
teardown_server_connection(void)
{
   if (srv_socket != -1)
   {
      pgmoneta_disconnect(srv_socket);
      srv_socket = -1;
   }
   if (srv_ssl != NULL)
   {
      pgmoneta_close_ssl(srv_ssl);
      srv_ssl = NULL;
   }
   pgmoneta_test_teardown();
}
