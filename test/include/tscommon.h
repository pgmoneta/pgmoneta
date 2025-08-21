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

#ifndef PGMONETA_TSCOMMON_H
#define PGMONETA_TSCOMMON_H

#ifdef __cplusplus
extern "C" {
#endif
#include "pgmoneta.h"

#define PRIMARY_SERVER 0
#define ENV_VAR_BASE_DIR "PGMONETA_TEST_BASE_DIR"

extern char TEST_CONFIG_SAMPLE_PATH[MAX_PATH];
extern char TEST_RESTORE_DIR[MAX_PATH];
extern char TEST_BASE_DIR[MAX_PATH];

/**
 * Create the testing environment
 */
void
pgmoneta_test_environment_create(void);

/**
 * Destroy the testing environment
 */
void
pgmoneta_test_environment_destroy(void);

/**
 * Add a backup for testing purpose
 */
void
pgmoneta_test_add_backup(void);

/**
 * Add a chain of 3 backups for testing purpose
 */
void
pgmoneta_test_add_backup_chain(void);

/**
 * Clean up the backup directory
 */
void
pgmoneta_test_basedir_cleanup(void);

/**
 * Basic setup before each forked unit test
 */
void
pgmoneta_test_setup(void);

/**
 * Basic teardown after each forked unit test
 */
void
pgmoneta_test_teardown(void);

#ifdef __cplusplus
}
#endif

#endif