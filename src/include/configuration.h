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
 */

#ifndef PGMONETA_CONFIGURATION_H
#define PGMONETA_CONFIGURATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>
#include <json.h>

#include <stdlib.h>

#define PGMONETA_MAIN_INI_SECTION                  "pgmoneta"
#define PGMONETA_DEFAULT_CONFIG_FILE_PATH          "/etc/pgmoneta/pgmoneta.conf"
#define PGMONETA_WALINFO_DEFAULT_CONFIG_FILE_PATH  "/etc/pgmoneta/pgmoneta_walinfo.conf"
#define PGMONETA_DEFAULT_USERS_FILE_PATH           "/etc/pgmoneta/pgmoneta_users.conf"

/* Main configuration fields */
#define CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH         "admin_configuration_path"
#define CONFIGURATION_ARGUMENT_AZURE_BASE_DIR         "azure_base_dir"
#define CONFIGURATION_ARGUMENT_AZURE_CONTAINER        "azure_container"
#define CONFIGURATION_ARGUMENT_AZURE_SHARED_KEY       "azure_shared_key"
#define CONFIGURATION_ARGUMENT_AZURE_STORAGE_ACCOUNT  "azure_storage_account"
#define CONFIGURATION_ARGUMENT_BACKLOG                "backlog"
#define CONFIGURATION_ARGUMENT_BACKUP_MAX_RATE        "backup_max_rate"
#define CONFIGURATION_ARGUMENT_BASE_DIR               "base_dir"
#define CONFIGURATION_ARGUMENT_BLOCKING_TIMEOUT       "blocking_timeout"
#define CONFIGURATION_ARGUMENT_COMPRESSION            "compression"
#define CONFIGURATION_ARGUMENT_COMPRESSION_LEVEL      "compression_level"
#define CONFIGURATION_ARGUMENT_CREATE_SLOT            "create_slot"
#define CONFIGURATION_ARGUMENT_ENCRYPTION             "encryption"
#define CONFIGURATION_ARGUMENT_EXTRA                   "extra"
#define CONFIGURATION_ARGUMENT_FOLLOW                  "follow"
#define CONFIGURATION_ARGUMENT_HOST                   "host"
#define CONFIGURATION_ARGUMENT_HOT_STANDBY             "hot_standby"
#define CONFIGURATION_ARGUMENT_HOT_STANDBY_OVERRIDES   "hot_standby_overrides"
#define CONFIGURATION_ARGUMENT_HOT_STANDBY_TABLESPACES "hot_standby_tablespaces"
#define CONFIGURATION_ARGUMENT_HUGEPAGE               "hugepage"
#define CONFIGURATION_ARGUMENT_KEEP_ALIVE             "keep_alive"
#define CONFIGURATION_ARGUMENT_LIBEV                  "libev"
#define CONFIGURATION_ARGUMENT_LOG_LEVEL              "log_level"
#define CONFIGURATION_ARGUMENT_LOG_LINE_PREFIX        "log_line_prefix"
#define CONFIGURATION_ARGUMENT_LOG_MODE               "log_mode"
#define CONFIGURATION_ARGUMENT_LOG_PATH               "log_path"
#define CONFIGURATION_ARGUMENT_LOG_ROTATION_AGE       "log_rotation_age"
#define CONFIGURATION_ARGUMENT_LOG_ROTATION_SIZE      "log_rotation_size"
#define CONFIGURATION_ARGUMENT_LOG_TYPE               "log_type"
#define CONFIGURATION_ARGUMENT_MAIN_CONF_PATH          "main_configuration_path"
#define CONFIGURATION_ARGUMENT_MANAGEMENT             "management"
#define CONFIGURATION_ARGUMENT_MANIFEST               "manifest"
#define CONFIGURATION_ARGUMENT_METRICS                "metrics"
#define CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE  "metrics_cache_max_age"
#define CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE "metrics_cache_max_size"
#define CONFIGURATION_ARGUMENT_METRICS_CA_FILE        "metrics_ca_file"
#define CONFIGURATION_ARGUMENT_METRICS_CERT_FILE      "metrics_cert_file"
#define CONFIGURATION_ARGUMENT_METRICS_KEY_FILE       "metrics_key_file"
#define CONFIGURATION_ARGUMENT_NETWORK_MAX_RATE       "network_max_rate"
#define CONFIGURATION_ARGUMENT_NODELAY                "nodelay"
#define CONFIGURATION_ARGUMENT_NON_BLOCKING           "non_blocking"
#define CONFIGURATION_ARGUMENT_ONLINE                 "online"
#define CONFIGURATION_ARGUMENT_PIDFILE                "pidfile"
#define CONFIGURATION_ARGUMENT_PORT                    "port"
#define CONFIGURATION_ARGUMENT_RETENTION              "retention"
#define CONFIGURATION_ARGUMENT_S3_ACCESS_KEY_ID       "s3_access_key_id"
#define CONFIGURATION_ARGUMENT_S3_AWS_REGION          "s3_aws_region"
#define CONFIGURATION_ARGUMENT_S3_BASE_DIR            "s3_base_dir"
#define CONFIGURATION_ARGUMENT_S3_BUCKET              "s3_bucket"
#define CONFIGURATION_ARGUMENT_S3_SECRET_ACCESS_KEY   "s3_secret_access_key"
#define CONFIGURATION_ARGUMENT_SSH_BASE_DIR           "ssh_base_dir"
#define CONFIGURATION_ARGUMENT_SSH_CIPHERS            "ssh_ciphers"
#define CONFIGURATION_ARGUMENT_SSH_HOSTNAME           "ssh_hostname"
#define CONFIGURATION_ARGUMENT_SSH_USERNAME           "ssh_username"
#define CONFIGURATION_ARGUMENT_STORAGE_ENGINE         "storage_engine"
#define CONFIGURATION_ARGUMENT_TLS                    "tls"
#define CONFIGURATION_ARGUMENT_TLS_CA_FILE            "tls_ca_file"
#define CONFIGURATION_ARGUMENT_TLS_CERT_FILE          "tls_cert_file"
#define CONFIGURATION_ARGUMENT_TLS_KEY_FILE           "tls_key_file"
#define CONFIGURATION_ARGUMENT_UNIX_SOCKET_DIR        "unix_socket_dir"
#define CONFIGURATION_ARGUMENT_UPDATE_PROCESS_TITLE   "update_process_title"
#define CONFIGURATION_ARGUMENT_USER                    "user"
#define CONFIGURATION_ARGUMENT_USER_CONF_PATH          "users_configuration_path"
#define CONFIGURATION_ARGUMENT_VERIFICATION            "verification"
#define CONFIGURATION_ARGUMENT_WAL_SHIPPING            "wal_shipping"
#define CONFIGURATION_ARGUMENT_WAL_SLOT                "wal_slot"
#define CONFIGURATION_ARGUMENT_WORKERS                "workers"
#define CONFIGURATION_ARGUMENT_WORKSPACE               "workspace"
#define CONFIGURATION_ARGUMENT_SERVER                  "server"

#define CONFIGURATION_TYPE_MAIN 0
#define CONFIGURATION_TYPE_WALINFO 1

// Set configuration argument constants
#define CONFIGURATION_RESPONSE_STATUS                           "status"
#define CONFIGURATION_RESPONSE_MESSAGE                          "message"
#define CONFIGURATION_RESPONSE_CONFIG_KEY                       "config_key"
#define CONFIGURATION_RESPONSE_REQUESTED_VALUE                  "requested_value"
#define CONFIGURATION_RESPONSE_CURRENT_VALUE                    "current_value"
#define CONFIGURATION_RESPONSE_OLD_VALUE                        "old_value"
#define CONFIGURATION_RESPONSE_NEW_VALUE                        "new_value"
#define CONFIGURATION_RESPONSE_RESTART_REQUIRED                 "restart_required"
#define CONFIGURATION_STATUS_SUCCESS                            "success"
#define CONFIGURATION_STATUS_RESTART_REQUIRED                   "success_restart_required"
#define CONFIGURATION_MESSAGE_SUCCESS                           "Configuration change applied successfully"
#define CONFIGURATION_MESSAGE_RESTART_REQUIRED                  "Configuration change requires restart. Current values preserved."

/**
 * @struct config_key_info
 * @brief Parsed representation of a configuration key for runtime configuration changes.
 *
 * This structure is used internally to represent a configuration key as parsed from
 * user input (e.g., from the CLI or management API). It supports both main/global
 * configuration parameters and server-specific parameters.
 *
 * Example key formats:
 *   - "log_level"                  (main/global parameter)
 *   - "pgmoneta.log_level"         (main/global parameter, explicit section)
 *   - "server.primary.port"        (server-specific parameter)
 *
 * Fields:
 *   section      The top-level section ("pgmoneta" for main config, "server" for server config)
 *   context      The context identifier (e.g., server name for server configs, empty for main)
 *   key          The actual configuration parameter name (e.g., "port", "log_level")
 *   is_main_section True if this refers to the main/global configuration section
 *   section_type  Section type: 0=main, 1=server
 */
struct config_key_info
{
   char section[MISC_LENGTH];   /**< Section name: "pgmoneta" for main config, "server" for server config */
   char context[MISC_LENGTH];   /**< Context identifier: server name for server configs, empty for main config */
   char key[MISC_LENGTH];       /**< Configuration parameter name (e.g., "port", "log_level") */
   bool is_main_section;        /**< True if this is a main/global configuration parameter */
   int section_type;            /**< Section type: 0=main, 1=server */
};

/**
 * Initialize the configuration structure
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_init_main_configuration(void* shmem);

/**
 * Read the configuration from a file
 * @param shmem The shared memory segment
 * @param filename The file name
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_read_main_configuration(void* shmem, char* filename);

/**
 * Validate the configuration
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_validate_main_configuration(void* shmem);

/**
 * Initialize the WALINFO configuration structure
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_init_walinfo_configuration(void* shmem);

/**
 * Read the WALINFO configuration from a file
 * @param shmem The shared memory segment
 * @param filename The file name
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_read_walinfo_configuration(void* shmem, char* filename);

/**
 * Validate the WALINFO configuration
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_validate_walinfo_configuration(void);

/**
 * Read the USERS configuration from a file
 * @param shmem The shared memory segment
 * @param filename The file name
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_read_users_configuration(void* shmem, char* filename);

/**
 * Validate the USERS configuration from a file
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_validate_users_configuration(void* shmem);

/**
 * Read the ADMINS configuration from a file
 * @param shmem The shared memory segment
 * @param filename The file name
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_read_admins_configuration(void* shmem, char* filename);

/**
 * Validate the ADMINS configuration from a file
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_validate_admins_configuration(void* shmem);

/**
 * Reload the configuration
 * @param restart Should the server be restarted
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_reload_configuration(bool* restart);

/**
 * Get a configuration parameter value
 * @param ssl The SSL connection
 * @param client_fd The client
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The payload
 */
void
pgmoneta_conf_get(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * Set a configuration parameter value
 * @param ssl The SSL connection
 * @param client_fd The client
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The payload
 */
int
pgmoneta_conf_set(SSL* ssl __attribute__((unused)), int client_fd, uint8_t compression, uint8_t encryption, struct json* payload, bool* restart_required);

#ifdef __cplusplus
}
#endif

#endif
