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
#include <wal.h>
#include <tscommon.h>
#include <mctf.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * MUST MATCH tests: Real PostgreSQL walsender.c ErrorResponse messages.
 * Expected: pgmoneta_wal_is_history_start_error returns true.
 */

MCTF_TEST(test_wal_history_error_match_timeline_1)
{
   const char* msg = "requested starting point 0/8D000000 on timeline 1 is not in this server's history";

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_wal_is_history_start_error(msg), cleanup,
               "Expected history start error for timeline 1 message");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_wal_history_error_match_timeline_3)
{
   const char* msg = "requested starting point 1/2A000000 on timeline 3 is not in this server's history";

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_wal_is_history_start_error(msg), cleanup,
               "Expected history start error for timeline 3 message");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

/*
 * MUST NOT MATCH tests: NULL and empty string.
 * Expected: pgmoneta_wal_is_history_start_error returns false.
 */

MCTF_TEST(test_wal_history_error_null)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_wal_is_history_start_error(NULL), cleanup,
               "Expected false for NULL message");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_wal_history_error_empty)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_wal_is_history_start_error(""), cleanup,
               "Expected false for empty message");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

/*
 * MUST NOT MATCH tests: Other real PostgreSQL replication error messages.
 * Expected: pgmoneta_wal_is_history_start_error returns false.
 */

MCTF_TEST(test_wal_history_error_logical_slot)
{
   const char* msg = "cannot use a logical replication slot for physical replication";

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_wal_is_history_start_error(msg), cleanup,
               "Expected false for logical slot message");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_wal_history_error_auth_failed)
{
   const char* msg = "password authentication failed for user \"repl\"";

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_wal_is_history_start_error(msg), cleanup,
               "Expected false for auth failure message");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

/*
 * Regression guards with approximate wording.
 * These check against false-positive classifications on related PostgreSQL error conditions.
 * Expected: pgmoneta_wal_is_history_start_error returns false.
 */

MCTF_TEST(test_wal_history_error_slot_not_found)
{
   const char* msg = "replication slot \"pgmoneta\" does not exist";

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_wal_is_history_start_error(msg), cleanup,
               "Expected false for non-existent slot message");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_wal_history_error_ahead_of_wal_flush)
{
   const char* msg = "requested starting point 0/8D000000 is ahead of the WAL flush position of this server 0/10000000";

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_wal_is_history_start_error(msg), cleanup,
               "Expected false for ahead-of-flush message");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_wal_history_error_timeline_not_child)
{
   const char* msg = "requested timeline 2 is not a child of this server's history";

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_wal_is_history_start_error(msg), cleanup,
               "Expected false for timeline not child message");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

/*
 * MUST NOT MATCH tests: Near-misses and localized variants.
 * Expected: pgmoneta_wal_is_history_start_error returns false.
 */

MCTF_TEST(test_wal_history_error_near_miss_suffix_only)
{
   const char* msg = "relation is not in this server's history";

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_wal_is_history_start_error(msg), cleanup,
               "Expected false for message matching only suffix fragment");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_wal_history_error_near_miss_prefix_only)
{
   const char* msg = "requested starting point 0/8D000000 on timeline 1 is fine";

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_wal_is_history_start_error(msg), cleanup,
               "Expected false for message matching only prefix fragment");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_wal_history_error_german_locale)
{
   const char* msg = "angeforderter Startpunkt 0/8D000000 auf Zeitleiste 1 ist nicht in der Historie dieses Servers";

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_wal_is_history_start_error(msg), cleanup,
               "Expected false for German localized message");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}
