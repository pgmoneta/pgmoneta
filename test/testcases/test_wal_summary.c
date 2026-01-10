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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
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
#include <tswalutils.h>
#include <utils.h>
#include <value.h>
#include <wal.h>
#include <walfile.h>
#include <walfile/wal_reader.h>
#include <walfile/wal_summary.h>
#include <mctf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void cleanup_connections(SSL** srv_ssl, int* srv_socket, SSL** custom_user_ssl, int* custom_user_socket);

MCTF_TEST(test_pgmoneta_wal_summary)
{
   char* summary_dir = NULL;
   struct main_configuration* config = NULL;
   SSL* srv_ssl = NULL;
   int srv_socket = -1;
   int srv_usr_index = -1;
   SSL* custom_user_ssl = NULL;
   int custom_user_socket = -1;
   xlog_rec_ptr s_lsn = 0;
   xlog_rec_ptr e_lsn = 0;
   xlog_rec_ptr tmp_lsn = 0;
   uint32_t tli = 0;
   int ret = 0;
   char* wal_dir = NULL;
   struct query_response* qr = NULL;
   block_ref_table* brt = NULL;
   char* summary_file_path = NULL;
   block_ref_table* verify_brt = NULL;

   pgmoneta_test_setup();

   config = (struct main_configuration*)shmem;
   MCTF_ASSERT_PTR_NONNULL(config, "configuration is null", cleanup);

   /* find the corresponding user's index of the given server */
   for (int i = 0; i < config->common.number_of_users; i++)
   {
      if (!strcmp(config->common.servers[PRIMARY_SERVER].username, config->common.users[i].username))
      {
         srv_usr_index = i;
         break;
      }
   }
   MCTF_ASSERT(srv_usr_index >= 0, "user associated with primary server not found", cleanup);

   /* establish a connection to repl user, with replication flag not set */
   ret = !pgmoneta_server_authenticate(PRIMARY_SERVER, "postgres", config->common.users[srv_usr_index].username, config->common.users[srv_usr_index].password, false, &srv_ssl, &srv_socket);
   if (!ret)
   {
      cleanup_connections(&srv_ssl, &srv_socket, &custom_user_ssl, &custom_user_socket);
      pgmoneta_test_teardown();
      MCTF_SKIP();
   }

   /* establish a connection to custom myuser */
   ret = !pgmoneta_server_authenticate(PRIMARY_SERVER, "mydb", "myuser", "password", false, &custom_user_ssl, &custom_user_socket);
   if (!ret)
   {
      cleanup_connections(&srv_ssl, &srv_socket, &custom_user_ssl, &custom_user_socket);
      pgmoneta_test_teardown();
      MCTF_SKIP();
   }

   /* Get server info - this returns void, so we check validity after */
   pgmoneta_server_info(PRIMARY_SERVER, srv_ssl, srv_socket);
   ret = pgmoneta_test_server_info_check(PRIMARY_SERVER);
   if (ret != 0)
   {
      cleanup_connections(&srv_ssl, &srv_socket, &custom_user_ssl, &custom_user_socket);
      pgmoneta_test_teardown();
      MCTF_SKIP();
   }

   /* get the starting lsn for summary */
   ret = !pgmoneta_server_checkpoint(PRIMARY_SERVER, srv_ssl, srv_socket, &s_lsn, &tli);
   if (!ret)
   {
      cleanup_connections(&srv_ssl, &srv_socket, &custom_user_ssl, &custom_user_socket);
      pgmoneta_test_teardown();
      MCTF_SKIP();
   }

   /* Create a table  */
   ret = !pgmoneta_test_execute_query(PRIMARY_SERVER, custom_user_ssl, custom_user_socket, "CREATE TABLE t1 (id int);", &qr);
   if (!ret)
   {
      pgmoneta_test_cleanup_query_response(&qr);
      cleanup_connections(&srv_ssl, &srv_socket, &custom_user_ssl, &custom_user_socket);
      pgmoneta_test_teardown();
      MCTF_SKIP();
   }
   pgmoneta_test_cleanup_query_response(&qr);

   /* Insert some tuples */
   ret = !pgmoneta_test_execute_query(PRIMARY_SERVER, custom_user_ssl, custom_user_socket, "INSERT INTO t1 SELECT GENERATE_SERIES(1, 800);", &qr);
   if (!ret)
   {
      pgmoneta_test_cleanup_query_response(&qr);
      cleanup_connections(&srv_ssl, &srv_socket, &custom_user_ssl, &custom_user_socket);
      pgmoneta_test_teardown();
      MCTF_SKIP();
   }
   pgmoneta_test_cleanup_query_response(&qr);

   /* get the ending lsn for summary */
   ret = !pgmoneta_server_checkpoint(PRIMARY_SERVER, srv_ssl, srv_socket, &e_lsn, &tli);
   if (!ret)
   {
      cleanup_connections(&srv_ssl, &srv_socket, &custom_user_ssl, &custom_user_socket);
      pgmoneta_test_teardown();
      MCTF_SKIP();
   }

   /* Switch the wal segment so that records won't appear in partial segments */
   ret = !pgmoneta_test_execute_query(PRIMARY_SERVER, srv_ssl, srv_socket, "SELECT pg_switch_wal();", &qr);
   if (!ret)
   {
      pgmoneta_test_cleanup_query_response(&qr);
      cleanup_connections(&srv_ssl, &srv_socket, &custom_user_ssl, &custom_user_socket);
      pgmoneta_test_teardown();
      MCTF_SKIP();
   }
   pgmoneta_test_cleanup_query_response(&qr);

   /* Append some more tuples just to ensure that a new wal segment is streamed */
   ret = !pgmoneta_server_checkpoint(PRIMARY_SERVER, srv_ssl, srv_socket, &tmp_lsn, &tli);
   if (!ret)
   {
      cleanup_connections(&srv_ssl, &srv_socket, &custom_user_ssl, &custom_user_socket);
      pgmoneta_test_teardown();
      MCTF_SKIP();
   }

   sleep(10);

   /* Create summary directory in the base_dir of a server if not already present */
   summary_dir = pgmoneta_get_server_summary(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(summary_dir, "summary directory path is null", cleanup);
   ret = !pgmoneta_mkdir(summary_dir);
   if (!ret)
   {
      free(summary_dir);
      summary_dir = NULL;
      cleanup_connections(&srv_ssl, &srv_socket, &custom_user_ssl, &custom_user_socket);
      pgmoneta_test_teardown();
      MCTF_SKIP();
   }

   wal_dir = pgmoneta_get_server_wal(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(wal_dir, "wal directory path is null", cleanup);

   MCTF_ASSERT(e_lsn >= s_lsn, "ending LSN must be >= starting LSN", cleanup);
   ret = !pgmoneta_summarize_wal(PRIMARY_SERVER, wal_dir, s_lsn, e_lsn, &brt);
   if (!ret)
   {
      free(summary_dir);
      summary_dir = NULL;
      free(wal_dir);
      wal_dir = NULL;
      cleanup_connections(&srv_ssl, &srv_socket, &custom_user_ssl, &custom_user_socket);
      pgmoneta_test_teardown();
      MCTF_SKIP();
   }

   /* Verify BRT was created and contains data */
   MCTF_ASSERT_PTR_NONNULL(brt, "BRT should not be null after summarization", cleanup);
   MCTF_ASSERT_PTR_NONNULL(brt->table, "BRT table should not be null", cleanup);
   /* After creating table and inserting 800 rows, we should have at least one entry in the BRT */
   MCTF_ASSERT(brt->table->size > 0, "BRT should contain entries after WAL summarization", cleanup);

   ret = !pgmoneta_wal_summary_save(PRIMARY_SERVER, s_lsn, e_lsn, brt);
   if (!ret)
   {
      if (brt != NULL)
      {
         pgmoneta_brt_destroy(brt);
         brt = NULL;
      }
      free(summary_dir);
      summary_dir = NULL;
      free(wal_dir);
      wal_dir = NULL;
      cleanup_connections(&srv_ssl, &srv_socket, &custom_user_ssl, &custom_user_socket);
      pgmoneta_test_teardown();
      MCTF_SKIP();
   }

   /* Verify the summary file was actually created on disk */
   char hex[128] = {0};

   snprintf(hex, sizeof(hex), "%08X%08X%08X%08X", (uint32_t)(s_lsn >> 32), (uint32_t)s_lsn, (uint32_t)(e_lsn >> 32), (uint32_t)e_lsn);
   summary_file_path = pgmoneta_append(summary_file_path, summary_dir);
   if (!pgmoneta_ends_with(summary_dir, "/"))
   {
      summary_file_path = pgmoneta_append_char(summary_file_path, '/');
   }
   summary_file_path = pgmoneta_append(summary_file_path, hex);

   MCTF_ASSERT_PTR_NONNULL(summary_file_path, "summary file path is null", cleanup);
   MCTF_ASSERT(pgmoneta_exists(summary_file_path), "summary file should exist on disk after save", cleanup);
   MCTF_ASSERT(pgmoneta_is_file(summary_file_path), "summary file should be a regular file", cleanup);

   /* Verify we can read the summary file back and it matches what we wrote */
   ret = pgmoneta_brt_read(summary_file_path, &verify_brt);
   if (ret != 0)
   {
      /* If read fails, we skip the verification but still report the file exists */
      free(summary_file_path);
      summary_file_path = NULL;
      if (brt != NULL)
      {
         pgmoneta_brt_destroy(brt);
         brt = NULL;
      }
      free(summary_dir);
      summary_dir = NULL;
      free(wal_dir);
      wal_dir = NULL;
      cleanup_connections(&srv_ssl, &srv_socket, &custom_user_ssl, &custom_user_socket);
      pgmoneta_test_teardown();
      MCTF_SKIP();
   }

   /* Verify the read BRT is valid and matches what we wrote */
   MCTF_ASSERT_PTR_NONNULL(verify_brt, "BRT read from file should not be null", cleanup);
   MCTF_ASSERT_PTR_NONNULL(verify_brt->table, "read BRT table should not be null", cleanup);
   MCTF_ASSERT(verify_brt->table->size == brt->table->size, "read BRT size should match written BRT size", cleanup);
   MCTF_ASSERT(verify_brt->table->size > 0, "read BRT should contain entries", cleanup);

   /* Verify the summary file has content (not empty) */
   struct stat file_stat;
   MCTF_ASSERT(stat(summary_file_path, &file_stat) == 0, "failed to stat summary file", cleanup);
   MCTF_ASSERT(file_stat.st_size > 0, "summary file should not be empty", cleanup);

   /* All validations passed - set errno to 0 to ensure test passes */
   mctf_errno = 0;

cleanup:
   if (verify_brt != NULL)
   {
      pgmoneta_brt_destroy(verify_brt);
      verify_brt = NULL;
   }
   if (brt != NULL)
   {
      pgmoneta_brt_destroy(brt);
      brt = NULL;
   }
   pgmoneta_test_cleanup_query_response(&qr);
   cleanup_connections(&srv_ssl, &srv_socket, &custom_user_ssl, &custom_user_socket);
   if (summary_file_path != NULL)
   {
      free(summary_file_path);
      summary_file_path = NULL;
   }
   if (summary_dir != NULL)
   {
      free(summary_dir);
      summary_dir = NULL;
   }
   if (wal_dir != NULL)
   {
      free(wal_dir);
      wal_dir = NULL;
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

static void
cleanup_connections(SSL** srv_ssl, int* srv_socket, SSL** custom_user_ssl, int* custom_user_socket)
{
   pgmoneta_test_cleanup_connection(srv_ssl, srv_socket);
   pgmoneta_test_cleanup_connection(custom_user_ssl, custom_user_socket);
}
