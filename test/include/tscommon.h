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

#ifndef PGMONETA_TSCOMMON_H
#define PGMONETA_TSCOMMON_H

#ifdef __cplusplus
extern "C" {
#endif
#include <pgmoneta.h>
#include <message.h>

#include <stdbool.h>
#include <openssl/ssl.h>

#define PRIMARY_SERVER               0
#define ENV_VAR_BASE_DIR             "PGMONETA_TEST_BASE_DIR"
#define RESTORED_BACKUP_DEFAULT_PORT 15432

extern char TEST_CONFIG_SAMPLE_PATH[MAX_PATH];
extern char TEST_RESTORE_DIR[MAX_PATH];
extern char TEST_BASE_DIR[MAX_PATH];
extern char TEST_RETROSPECT_DIR[MAX_PATH];
extern char TEST_HOT_STANDBY_DIR[MAX_PATH];

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
 * Validate that main_configuration is valid
 * @param shmem The shared memory segment containing the configuration
 * @return 0 if valid, 1 if invalid
 */
int
pgmoneta_test_validate_configuration(void* shmem);

/**
 * Add a backup for testing purpose
 * @return 0 on success, 1 on failure
 */
int
pgmoneta_test_add_backup(void);

/**
 * Add a chain of 3 backups for testing purpose
 * @return 0 on success, 1 on failure
 */
int
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

/**
 * Snapshot the current shared-memory configuration.
 */
void
pgmoneta_test_config_save(void);

/**
 * Restore the shared-memory configuration from the last snapshot.
 */
void
pgmoneta_test_config_restore(void);

/**
 * Execute an SQL query on postgres database
 * @param srv The server index
 * @param ssl The SSL
 * @param skt The socket
 * @param query The query to be executed
 * @param qr [out] The query response
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_test_execute_query(int srv, SSL* ssl, int skt, char* query, struct query_response** qr);

/**
 * Free a query response
 * @param qr The pointer
 */
void
pgmoneta_test_cleanup_query_response(struct query_response** qr);

/**
 * Close an SSL connection and its socket
 * @param ssl The SSL pointer
 * @param socket The socket descriptor
 */
void
pgmoneta_test_cleanup_connection(SSL** ssl, int* socket);

/**
 * Connect to PG_DATABASE (mydb) as the regular test user (myuser/mypass).
 * @param ssl The resulting SSL structure (out)
 * @param socket The resulting socket descriptor (out)
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_test_connect_user(SSL** ssl, int* socket);

/**
 * Resolve an executable path under build/src from a test process.
 * @param binary_name The executable name
 * @param out Output path buffer (MAX_PATH bytes)
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_test_resolve_binary_path(const char* binary_name, char* out);

/**
 * Execute a shell command and capture combined stdout/stderr output.
 * @param command Command to execute
 * @param output [out] Captured output (caller must free)
 * @param exit_code [out] Process exit code
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_test_exec_command(const char* command, char** output, int* exit_code);

/**
 * Start a PostgreSQL server from a restored backup directory.
 * Ensures required subdirectories exist, updates pg_hba.conf for trust auth,
 * and waits for pg_isready to succeed.
 * @param restore_dir Path to restored PGDATA directory
 * @param port Host port to map to container 5432 (<=0 uses RESTORED_BACKUP_DEFAULT_PORT)
 * @return The port on success, otherwise -1
 */
int
start_restored_backup(const char* restore_dir, int port);

/**
 * Stop and remove the restored backup container, ignoring errors.
 */
void
stop_restored_backup(void);

/**
 * Load a configuration file into shared memory.
 * @param conf_path Path to the .conf file
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_test_load_conf(char* conf_path);

/**
 * State saved/restored by the mock encryption environment helpers.
 */
struct test_encryption_env
{
   char test_home[MAX_PATH];            /**< The test home directory path */
   char* original_home;                 /**< The original home environment variable value */
   char original_config_home[MAX_PATH]; /**< The original configuration home directory path */
   int original_encryption;             /**< The original encryption setting */
   bool shmem_locally_allocated;        /**< Flag indicating if shared memory was allocated locally */
};

/**
 * Set up a mock encryption environment for testing.
 * @param env [out] Environment state (caller provides storage)
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_test_setup_encryption_env(struct test_encryption_env* env);

/**
 * Tear down a mock encryption environment.
 * @param env The environment state from pgmoneta_test_setup_encryption_env()
 */
void
pgmoneta_test_teardown_encryption_env(struct test_encryption_env* env);

#ifdef __cplusplus
}
#endif

#endif