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
#include <shmem.h>
#include <tscommon.h>
#include <tssuite.h>
#include <configuration.h>

#include "logging.h"

int
main(int argc, char* argv[])
{
   int number_failed = 0;
   Suite* backup_suite;
   Suite* restore_suite;
   Suite* delete_suite;
   Suite* http_suite;
   Suite* wal_utils_suite;
   Suite* brt_io_suite;
   Suite* wal_summary_suite;
   Suite* art_suite;
   Suite* deque_suite;
   Suite* json_suite;
   Suite* server_api_suite;
   SRunner* sr;

   pgmoneta_test_environment_create();

   backup_suite = pgmoneta_test_backup_suite();
   restore_suite = pgmoneta_test_restore_suite();
   delete_suite = pgmoneta_test_delete_suite();
   http_suite = pgmoneta_test_http_suite();
   wal_utils_suite = pgmoneta_test_wal_utils_suite();
   brt_io_suite = pgmoneta_test_brt_io_suite();
   wal_summary_suite = pgmoneta_test_wal_summary_suite();
   art_suite = pgmoneta_test_art_suite();
   deque_suite = pgmoneta_test_deque_suite();
   json_suite = pgmoneta_test_json_suite();
   server_api_suite = pgmoneta_test_server_api_suite();

   sr = srunner_create(backup_suite);
   srunner_add_suite(sr, restore_suite);
   srunner_add_suite(sr, delete_suite);
   srunner_add_suite(sr, http_suite);
   srunner_add_suite(sr, wal_utils_suite);
   srunner_add_suite(sr, brt_io_suite);
   srunner_add_suite(sr, wal_summary_suite);
   srunner_add_suite(sr, art_suite);
   srunner_add_suite(sr, deque_suite);
   srunner_add_suite(sr, json_suite);
   srunner_add_suite(sr, server_api_suite);
   srunner_set_log (sr, "-");
   srunner_set_fork_status(sr, CK_NOFORK);
   srunner_run(sr, NULL, NULL, CK_VERBOSE);
   number_failed = srunner_ntests_failed(sr);
   srunner_free(sr);
   pgmoneta_test_environment_destroy();

   return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
