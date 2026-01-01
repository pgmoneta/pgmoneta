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

#ifndef PGMONETA_TEST_SUITE_H
#define PGMONETA_TEST_SUITE_H

#include <check.h>

/**
 * Set up a retore test suite for pgmoneta
 * @return The result
 */
Suite*
pgmoneta_test_restore_suite();

/**
 * Set up a backup test suite for pgmoneta
 * @return The result
 */
Suite*
pgmoneta_test_backup_suite();

/**
 * Set up delete test suite for pgmoneta
 * @return The result
 */
Suite*
pgmoneta_test_delete_suite();

/**
 * Set up http test suite for pgmoneta
 * @return The result
 */
Suite*
pgmoneta_test_http_suite();

/**
 * Set up server API test suite for pgmoneta
 * @return The result
 */
Suite*
pgmoneta_test_server_api_suite();

/**
 * Set up a brt input/output suite for pgmoneta
 * @return The result
 */
Suite*
pgmoneta_test_brt_io_suite();

/**
 * Set up a wal utils test suite for pgmoneta
 * @return The result
 */
Suite*
pgmoneta_test_wal_utils_suite();

/**
 * Set up a wal summary test suite for pgmoneta
 * @return The result
 */
Suite*
pgmoneta_test_wal_summary_suite();

/**
 * Set up an art suite for pgmoneta
 * @return The result
 */
Suite*
pgmoneta_test_art_suite();

/**
 * Set up a deque suite for pgmoneta
 * @return The result
 */
Suite*
pgmoneta_test_deque_suite();

/**
 * Set up a json suite for pgmoneta
 * @return The result
 */
Suite*
pgmoneta_test_json_suite();

/**
 * Set up a utils suite for pgmoneta
 * @return The result
 */
Suite*
pgmoneta_test_utils_suite();

#endif