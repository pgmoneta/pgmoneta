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

/* pgmoneta */
#include <pgmoneta.h>
#include <brt.h>
#include <configuration.h>
#include <deque.h>
#include <logging.h>
#include <network.h>
#include <security.h>
#include <server.h>
#include <tsclient.h>
#include <tscommon.h>
#include <tssuite.h>
#include <tswalutils.h>
#include <utils.h>
#include <value.h>
#include <wal.h>
#include <walfile.h>
#include <walfile/wal_reader.h>
#include <walfile/wal_summary.h>
#include <stdio.h>

START_TEST(test_pgmoneta_wal_summary)
{
   fprintf(stderr, "TEST START: %s\n", __func__);
   char* summary_dir = NULL;
   struct main_configuration* config = NULL;
   SSL* srv_ssl = NULL;
   int srv_socket = -1;
   int srv_usr_index = -1;
   SSL* custom_user_ssl = NULL;
   int custom_user_socket = -1;
   xlog_rec_ptr s_lsn;
   xlog_rec_ptr e_lsn;
   xlog_rec_ptr tmp_lsn;
   uint32_t tli = 0;
   int ret;
   char* wal_dir = NULL;
   struct query_response* qr = NULL;
   block_ref_table* brt = NULL;

   config = (struct main_configuration*)shmem;

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

   /* establish a conection  to custom myuser */
   ret = !pgmoneta_server_authenticate(PRIMARY_SERVER, "mydb", "myuser", "password", false, &custom_user_ssl, &custom_user_socket);
   ck_assert_msg(ret, "failed to establish a connection to the user: myuser and database: postgres");

   pgmoneta_server_info(PRIMARY_SERVER, srv_ssl, srv_socket);

   /* get the starting lsn for summary */
   ret = !pgmoneta_server_checkpoint(PRIMARY_SERVER, srv_ssl, srv_socket, &s_lsn, &tli);
   ck_assert_msg(ret, "failed to create a checkpoint");

   /* Create a table  */
   ret = !pgmoneta_test_execute_query(PRIMARY_SERVER, custom_user_ssl, custom_user_socket, "CREATE TABLE t1 (id int);", &qr);
   ck_assert_msg(ret, "failed to execute a query: 'CREATE TABLE t1 (id int);'");

   pgmoneta_free_query_response(qr);
   qr = NULL;

   /* Insert some tuples */
   ret = !pgmoneta_test_execute_query(PRIMARY_SERVER, custom_user_ssl, custom_user_socket, "INSERT INTO t1 SELECT GENERATE_SERIES(1, 800);", &qr);
   ck_assert_msg(ret, "failed to execute a query: 'INSERT INTO t1 SELECT GENERATE_SERIES(1, 800);'");

   pgmoneta_free_query_response(qr);
   qr = NULL;

   /* get the ending lsn for summary */
   ret = !pgmoneta_server_checkpoint(PRIMARY_SERVER, srv_ssl, srv_socket, &e_lsn, &tli);
   ck_assert_msg(ret, "failed to create a checkpoint");

   /* Switch the wal segment so that records won't appear in partial segments */
   ret = !pgmoneta_test_execute_query(PRIMARY_SERVER, srv_ssl, srv_socket, "SELECT pg_switch_wal();", &qr);
   ck_assert_msg(ret, "failed to execute a query 'SELECT pg_switch_wal()'");

   pgmoneta_free_query_response(qr);
   qr = NULL;

   /* Append some more tuples just to ensure that a new wal segment is streamed */
   ret = !pgmoneta_server_checkpoint(PRIMARY_SERVER, srv_ssl, srv_socket, &tmp_lsn, &tli);
   ck_assert_msg(ret, "failed to create a checkpoint");

   pgmoneta_free_query_response(qr);
   qr = NULL;

   sleep(10);

   /* Create summary directory in the base_dir of a server if not already present */
   summary_dir = pgmoneta_get_server_summary(PRIMARY_SERVER);
   ck_assert_msg(!pgmoneta_mkdir(summary_dir), "failed to create %s directory", summary_dir);

   wal_dir = pgmoneta_get_server_wal(PRIMARY_SERVER);

   ck_assert_int_ge(e_lsn, s_lsn);
   ret = !pgmoneta_summarize_wal(PRIMARY_SERVER, wal_dir, s_lsn, e_lsn, &brt);
   ck_assert_msg(ret, "failed to summarize the wal");

   ret = !pgmoneta_wal_summary_save(PRIMARY_SERVER, s_lsn, e_lsn, brt);
   ck_assert_msg(ret, "failed to save the wal summary to disk");

   pgmoneta_brt_destroy(brt);
   pgmoneta_disconnect(srv_socket);
   pgmoneta_disconnect(custom_user_socket);
   free(summary_dir);
   free(wal_dir);
}
END_TEST

Suite*
pgmoneta_test_wal_summary_suite()
{
   Suite* s;
   TCase* tc_wal_summary;
   s = suite_create("pgmoneta_test_wal_summary");

   tc_wal_summary = tcase_create("test_wal_summary");
   tcase_set_timeout(tc_wal_summary, 60);
   tcase_add_checked_fixture(tc_wal_summary, pgmoneta_test_setup, pgmoneta_test_teardown);
   tcase_add_test(tc_wal_summary, test_pgmoneta_wal_summary);
   suite_add_tcase(s, tc_wal_summary);

   return s;
}