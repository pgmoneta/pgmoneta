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
#include <network.h>
#include <security.h>
#include <server.h>
#include <tscommon.h>
#include <tssuite.h>

#include <stdio.h>
#include <string.h>

int srv_socket = -1;
SSL* srv_ssl = NULL;

static void setup_server_connection(void);
static void teardown_server_connection(void);

START_TEST(test_server_api_info)
{
   int ret;
   struct main_configuration* config = NULL;
   struct server srv;

   config = (struct main_configuration*)shmem;
   pgmoneta_server_info(PRIMARY_SERVER, srv_ssl, srv_socket);

   srv = config->common.servers[PRIMARY_SERVER];
   ck_assert_msg(srv.primary, "server is not primary");
   ck_assert_msg(srv.version == 17, "server version mismatch");

   ret = pgmoneta_server_valid(PRIMARY_SERVER);
   ck_assert_msg(ret, "server is not valid");
}
END_TEST
START_TEST(test_server_api_checkpoint)
{
   int ret;
   uint64_t chkt;

   ret = !pgmoneta_server_checkpoint(PRIMARY_SERVER, srv_ssl, srv_socket, &chkt);
   ck_assert_msg(ret, "failed to perfom checkpoint");
}
END_TEST
START_TEST(test_server_api_read_file)
{
   int ret;
   uint8_t* data;
   int data_length;

   char file_path[] = "postgresql.conf";

   ret = !pgmoneta_server_read_binary_file(PRIMARY_SERVER, srv_ssl, file_path, 0, 100, srv_socket, &data, &data_length);
   ck_assert_msg(ret, "failed to read binary file: %s", file_path);

   free(data);
}
END_TEST
START_TEST(test_server_api_read_file_metadata)
{
   int ret;
   struct file_stats stat;
   char file_path[] = "postgresql.conf";

   ret = !pgmoneta_server_file_stat(PRIMARY_SERVER, srv_ssl, srv_socket, file_path, &stat);
   ck_assert_msg(ret, "failed to read metatdata of file: %s", file_path);
}
END_TEST

Suite*
pgmoneta_test_server_api_suite()
{
   Suite* s;
   TCase* tc_server_api;
   s = suite_create("pgmoneta_test_server_api");

   tc_server_api = tcase_create("test_server_api");

   tcase_set_timeout(tc_server_api, 60);
   tcase_add_checked_fixture(tc_server_api, setup_server_connection, teardown_server_connection);
   tcase_add_test(tc_server_api, test_server_api_info);
   tcase_add_test(tc_server_api, test_server_api_checkpoint);
   tcase_add_test(tc_server_api, test_server_api_read_file);
   tcase_add_test(tc_server_api, test_server_api_read_file_metadata);
   suite_add_tcase(s, tc_server_api);

   return s;
}

static void
setup_server_connection(void)
{
   int ret = 0;
   int srv_usr_index = -1;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   pgmoneta_test_setup();
   /* find the corresponding user's index of the given server */
   for (int i = 0; i < config->common.number_of_users; i++)
   {
      if (!strcmp(config->common.servers[PRIMARY_SERVER].username, config->common.users[i].username))
      {
         srv_usr_index = i;
      }
   }
   ck_assert_msg(srv_usr_index >= 0, "user associated with primary server not found");
   /* establish a connection to repl user, with replication flag not set */
   ret = !pgmoneta_server_authenticate(PRIMARY_SERVER, "postgres", config->common.users[srv_usr_index].username, config->common.users[srv_usr_index].password, false, &srv_ssl, &srv_socket);
   ck_assert_msg(ret, "failed to establish a connection to the user: %s and database: postgres", config->common.users[srv_usr_index].username);
}

static void
teardown_server_connection(void)
{
   if (srv_socket != -1)
   {
      pgmoneta_disconnect(srv_socket);
   }
   free(srv_ssl);
   pgmoneta_test_teardown();
}
