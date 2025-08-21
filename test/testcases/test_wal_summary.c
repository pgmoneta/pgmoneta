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
#include <configuration.h>
#include <deque.h>
#include <logging.h>
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

static int do_checkpoint_and_get_lsn(int server, xlog_rec_ptr* checkpoint_lsn);

START_TEST(test_pgmoneta_wal_summary)
{
   char* summary_dir = NULL;
   struct main_configuration* config = NULL;
   xlog_rec_ptr s_lsn;
   xlog_rec_ptr e_lsn;
   int ret;
   char* wal_dir = NULL;
   struct query_response* qr = NULL;

   config = (struct main_configuration*)shmem;

   /* get the starting lsn for summary */
   ret = !do_checkpoint_and_get_lsn(PRIMARY_SERVER, &s_lsn);
   ck_assert_msg(ret, "failed to create a checkpoint");

   /* Create a table  */
   ret = !pgmoneta_test_execute_query(PRIMARY_SERVER, "CREATE TABLE t1 (id int);", 1, &qr);
   ck_assert_msg(ret, "failed to execute a query: 'CREATE TABLE t1 (id int);'");

   pgmoneta_free_query_response(qr);
   qr = NULL;

   /* Insert some tuples */
   ret = !pgmoneta_test_execute_query(PRIMARY_SERVER, "INSERT INTO t1 SELECT  GENERATE_SERIES(1, 800);", 1, &qr);
   ck_assert_msg(ret, "failed to execute a query: 'INSERT INTO t1 SELECT  GENERATE_SERIES(1, 800);'");

   pgmoneta_free_query_response(qr);
   qr = NULL;

   /* get the ending lsn for summary */
   ret = !do_checkpoint_and_get_lsn(PRIMARY_SERVER, &e_lsn);
   ck_assert_msg(ret, "failed to create a checkpoint");

   /* Switch the wal segment so that records won't appear in partial segments */
   ret = !pgmoneta_test_execute_query(PRIMARY_SERVER, "SELECT pg_switch_wal();", 0, &qr);
   ck_assert_msg(ret, "failed to execute a query 'SELECT pg_switch_wal()'");

   pgmoneta_free_query_response(qr);
   qr = NULL;

   /* Append some more tuples just to ensure that a new wal segment is streamed */
   ret = !do_checkpoint_and_get_lsn(PRIMARY_SERVER, NULL);
   ck_assert_msg(ret, "failed to create a checkpoint");

   pgmoneta_free_query_response(qr);
   qr = NULL;

   sleep(10);

   /* Create summary directory in the base_dir of a server if not already present */
   summary_dir = pgmoneta_get_server_summary(PRIMARY_SERVER);
   ck_assert_msg(!pgmoneta_mkdir(summary_dir), "failed to create %s directory", summary_dir);

   wal_dir = pgmoneta_get_server_wal(PRIMARY_SERVER);

   ck_assert_int_ge(e_lsn, s_lsn);
   ret = !pgmoneta_summarize_wal(PRIMARY_SERVER, wal_dir, s_lsn, e_lsn);
   ck_assert_msg(ret, "failed to summarize the wal");

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

static int
do_checkpoint_and_get_lsn(int srv, xlog_rec_ptr* checkpoint_lsn)
{
   char* chkpt_lsn = NULL;
   uint32_t chkpt_hi = 0;
   uint32_t chkpt_lo = 0;
   int ret;
   struct query_response* qr = NULL;

   /* Assuming the user associated with this server has 'pg_checkpoint' role */
   ret = !pgmoneta_test_execute_query(srv, "CHECKPOINT;", 0, &qr);
   ck_assert_msg(ret, "failed to execute 'CHECKPOINT;' query");

   // Ignore this result
   pgmoneta_free_query_response(qr);
   qr = NULL;

   ret = !pgmoneta_test_execute_query(srv, "SELECT checkpoint_lsn FROM pg_control_checkpoint();", 0, &qr);
   ck_assert_msg(ret, "failed to execute 'SELECT checkpoint_lsn FROM pg_control_checkpoint();' query");

   /* Extract the checkpoint lsn */
   ck_assert_ptr_nonnull(qr);
   ck_assert_int_ge(qr->number_of_columns, 1);

   chkpt_lsn = pgmoneta_query_response_get_data(qr, 0);
   ck_assert_ptr_nonnull(chkpt_lsn);

   sscanf(chkpt_lsn, "%X/%X", &chkpt_hi, &chkpt_lo);

   if (checkpoint_lsn)
   {
      *checkpoint_lsn = ((uint64_t)chkpt_hi << 32) + (uint64_t)chkpt_lo;
   }

   pgmoneta_free_query_response(qr);
   return 0;
}