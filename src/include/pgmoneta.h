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
 */

#ifndef PGMONETA_H
#define PGMONETA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ev.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#if HAVE_OPENBSD
#include <sys/limits.h>
#endif
#include <sys/types.h>
#include <openssl/ssl.h>

#define VERSION                      "0.20.0"

#define PGMONETA_HOMEPAGE            "https://pgmoneta.github.io/"
#define PGMONETA_ISSUES              "https://github.com/pgmoneta/pgmoneta/issues"

#define POSTGRESQL_MIN_VERSION       13

#define MAIN_UDS                     ".s.pgmoneta"

#define MAX_PROCESS_TITLE_LENGTH     256

#define ALIGNMENT_SIZE               512
#define DEFAULT_BUFFER_SIZE          131072

#define DEFAULT_BURST                65536
#define DEFAULT_EVERY                1

#define MAX_USERNAME_LENGTH          128
#define MAX_PASSWORD_LENGTH          1024
#define MAX_PASSWORD_CHARS           256
#define MIN_MASTER_KEY_CHARS         8

#define MAX_PATH                     1024
#define MISC_LENGTH                  128
#define MAX_COMMENT                  2048
#define MAX_EXTRA_PATH               8192

#define MAX_EXTRA                    64
#define NUMBER_OF_SERVERS            64
#define NUMBER_OF_USERS              64
#define NUMBER_OF_ADMINS             8
#define NUMBER_OF_HOT_STANDBY        8
#define NUMBER_OF_EXTENSIONS         64

#define MAX_NUMBER_OF_COLUMNS        8
#define MAX_NUMBER_OF_TABLESPACES    64

#define STATE_FREE                   0
#define STATE_IN_USE                 1

#define AUTH_SUCCESS                 0
#define AUTH_BAD_PASSWORD            1
#define AUTH_ERROR                   2
#define AUTH_TIMEOUT                 3

#define ENCRYPTION_NONE              0
#define ENCRYPTION_AES_256_CBC       1
#define ENCRYPTION_AES_192_CBC       2
#define ENCRYPTION_AES_128_CBC       3
#define ENCRYPTION_AES_256_CTR       4
#define ENCRYPTION_AES_192_CTR       5
#define ENCRYPTION_AES_128_CTR       6

#define HUGEPAGE_OFF                 0
#define HUGEPAGE_TRY                 1
#define HUGEPAGE_ON                  2

#define COMPRESSION_NONE             0
#define COMPRESSION_CLIENT_GZIP      1
#define COMPRESSION_CLIENT_ZSTD      2
#define COMPRESSION_CLIENT_LZ4       3
#define COMPRESSION_CLIENT_BZIP2     4
#define COMPRESSION_SERVER_GZIP      5
#define COMPRESSION_SERVER_ZSTD      6
#define COMPRESSION_SERVER_LZ4       7

#define STORAGE_ENGINE_LOCAL         1 << 0
#define STORAGE_ENGINE_SSH           1 << 1
#define STORAGE_ENGINE_S3            1 << 2
#define STORAGE_ENGINE_AZURE         1 << 3

#define DEFAULT_BLOCKING_TIMEOUT     30

#define UPDATE_PROCESS_TITLE_NEVER   0
#define UPDATE_PROCESS_TITLE_STRICT  1
#define UPDATE_PROCESS_TITLE_MINIMAL 2
#define UPDATE_PROCESS_TITLE_VERBOSE 3

#define CREATE_SLOT_UNDEFINED        0
#define CREATE_SLOT_YES              1
#define CREATE_SLOT_NO               2

#define VALID_SLOT                   0
#define SLOT_NOT_FOUND               1
#define INCORRECT_SLOT_TYPE          2

#define INDENT_PER_LEVEL             2
#define FORMAT_JSON                  0
#define FORMAT_TEXT                  1
#define FORMAT_JSON_COMPACT          2
#define BULLET_POINT                 "- "

#define INCREMENTAL_PREFIX           "INCREMENTAL."

#define likely(x)                    __builtin_expect(!!(x), 1)
#define unlikely(x)                  __builtin_expect(!!(x), 0)

#define EMPTY_STR(_s)                (_s[0] == 0)

#define MAX(a, b) \
   ({ __typeof__ (a) _a = (a);  \
           __typeof__ (b) _b = (b);  \
           _a > _b ? _a : _b; })

#define MIN(a, b) \
   ({ __typeof__ (a) _a = (a);  \
           __typeof__ (b) _b = (b);  \
           _a < _b ? _a : _b; })

/*
 * Common piece of code to perform a sleeping.
 *
 * @param zzz the amount of time to
 * sleep, expressed as nanoseconds.
 *
 * Example
   SLEEP(5000000L)
 *
 */
#define SLEEP(zzz)                  \
   do                               \
   {                                \
      struct timespec ts_private;   \
      ts_private.tv_sec = 0;        \
      ts_private.tv_nsec = zzz;     \
      nanosleep(&ts_private, NULL); \
   }                                \
   while (0);

/*
 * Commonly used block of code to sleep
 * for a specified amount of time and
 * then jump back to a specified label.
 *
 * @param zzz how much time to sleep (as long nanoseconds)
 * @param goto_to the label to which jump to
 *
 * Example:
 *
     ...
     else
       SLEEP_AND_GOTO(100000L, retry)
 */
#define SLEEP_AND_GOTO(zzz, goto_to) \
   do                                \
   {                                 \
      struct timespec ts_private;    \
      ts_private.tv_sec = 0;         \
      ts_private.tv_nsec = zzz;      \
      nanosleep(&ts_private, NULL);  \
      goto goto_to;                  \
   }                                 \
   while (0);

/**
 * The shared memory segment
 */
extern void* shmem;

/**
 * Shared memory used to contain the Prometheus
 * response cache.
 */
extern void* prometheus_cache_shmem;

/**
 * @struct version
 * Semantic version structure for extensions (major.minor.patch format)
 */
struct version
{
   int major; /**< Major version number */
   int minor; /**< Minor version number (-1 if not specified) */
   int patch; /**< Patch version number (-1 if not specified) */
} __attribute__((aligned(64)));

/** @struct extension_info
 * Defines information about a PostgreSQL extension
 */
struct extension_info
{
   char name[MISC_LENGTH];           /**< The extension name */
   char comment[MISC_LENGTH];        /**< The extension description/comment */
   int server;                       /**< The server index */
   bool enabled;                     /**< Is extension enabled */
   struct version installed_version; /**< The installed version */
} __attribute__((aligned(64)));

/** @struct s3_configuration
 * Defines the S3 configuration
 */
struct s3_configuration
{
   int port;                            /**< The S3 port */
   bool use_tls;                        /**< Use TLS for S3 */
   char storage_class[MISC_LENGTH];     /**< The S3 storage class */
   char endpoint[MISC_LENGTH];          /**< The S3 endpoint */
   char region[MISC_LENGTH];            /**< The AWS region */
   char access_key_id[MISC_LENGTH];     /**< The IAM Access Key ID */
   char secret_access_key[MISC_LENGTH]; /**< The IAM Secret Access Key */
   char bucket[MISC_LENGTH];            /**< The S3 bucket */
   char base_dir[MAX_PATH];             /**< The S3 base directory */
} __attribute__((aligned(64)));

/** @struct server
 * Defines a server
 */
struct server
{
   char name[MISC_LENGTH];                                        /**< The name of the server */
   char host[MISC_LENGTH];                                        /**< The host name of the server */
   int port;                                                      /**< The port of the server */
   bool primary;                                                  /**< Is the server a primary ? */
   char username[MAX_USERNAME_LENGTH];                            /**< The user name */
   char wal_slot[MISC_LENGTH];                                    /**< The WAL slot name */
   bool online;                                                   /**< Is the server online ? */
   char current_wal_filename[MISC_LENGTH];                        /**< The current WAL filename*/
   char current_wal_lsn[MISC_LENGTH];                             /**< The current WAL log sequence number*/
   char follow[MISC_LENGTH];                                      /**< Follow a server */
   char workspace[MAX_PATH];                                      /**< A workspace for combining incremental backups */
   int retention_days;                                            /**< The retention days for the server */
   int retention_weeks;                                           /**< The retention weeks for the server */
   int retention_months;                                          /**< The retention months for the server */
   int retention_years;                                           /**< The retention years for the server */
   int create_slot;                                               /**< Create a slot */
   atomic_bool repository;                                        /**< Repository lock */
   bool active_backup;                                            /**< Is there an active backup */
   bool active_restore;                                           /**< Is there an active restore */
   bool active_archive;                                           /**< Is there an active archive */
   bool active_delete;                                            /**< Is there an active delete */
   bool active_retention;                                         /**< Is there an active retention */
   int wal_size;                                                  /**< The size of the WAL files */
   size_t block_size;                                             /**< The size of a block in relation files*/
   size_t segment_size;                                           /**< The max size of a relation file segment*/
   size_t relseg_size;                                            /**< The max number of blocks in a relation file segment */
   pid_t wal_streaming;                                           /**< WAL streaming process id */
   bool checksums;                                                /**< Are checksums enabled */
   bool summarize_wal;                                            /**< Is summarize_wal enabled */
   bool valid;                                                    /**< Is the server valid */
   int version;                                                   /**< The major version of the server*/
   int minor_version;                                             /**< The minor version of the server*/
   atomic_ulong operation_count;                                  /**< Operation count of the server */
   atomic_ulong failed_operation_count;                           /**< Failed operation count of the server */
   uint32_t cur_timeline;                                         /**< Current timeline the server is on*/
   atomic_llong last_operation_time;                              /**< Last operation time of the server */
   atomic_llong last_failed_operation_time;                       /**< Last failed operation time of the server */
   char wal_shipping[MAX_PATH];                                   /**< The WAL shipping directory */
   int number_of_hot_standbys;                                    /**< The number of hot standby directories */
   int number_of_extensions;                                      /**< The number of extensions */
   char hot_standby[NUMBER_OF_HOT_STANDBY][MAX_PATH];             /**< The hot standby directories */
   char hot_standby_overrides[NUMBER_OF_HOT_STANDBY][MAX_PATH];   /**< The hot standby overrides directory */
   char hot_standby_tablespaces[NUMBER_OF_HOT_STANDBY][MAX_PATH]; /**< The hot standby tablespaces mappings */
   char tls_cert_file[MAX_PATH];                                  /**< TLS certificate path */
   char tls_key_file[MAX_PATH];                                   /**< TLS key path */
   char tls_ca_file[MAX_PATH];                                    /**< TLS CA certificate path */
   int workers;                                                   /**< The number of workers */
   int backup_max_rate;                                           /**< Number of tokens added to the bucket with each replenishment for backup. */
   int network_max_rate;                                          /**< Number of bytes of tokens added every one second to limit the netowrk backup rate */
   int number_of_extra;                                           /**< The number of source directory*/
   char extra[MAX_EXTRA][MAX_EXTRA_PATH];                         /**< Source directory*/
   bool has_extension;                                            /**< Does this have pgmoneta_ext */
   char ext_version[MISC_LENGTH];                                 /**< The major version of the extension*/
   struct extension_info extensions[NUMBER_OF_EXTENSIONS];        /**< The extensions */
   struct s3_configuration s3;                                    /**< The S3 configuration */
} __attribute__((aligned(64)));

/** @struct user
 * Defines a user
 */
struct user
{
   char username[MAX_USERNAME_LENGTH]; /**< The user name */
   char password[MAX_PASSWORD_LENGTH]; /**< The password */
} __attribute__((aligned(64)));

/** @struct prometheus_cache
 * A structure to handle the Prometheus response
 * so that it is possible to serve the very same
 * response over and over depending on the cache
 * settings.
 *
 * The `valid_until` field stores the result
 * of `time(2)`.
 *
 * The cache is protected by the `lock` field.
 *
 * The `size` field stores the size of the allocated
 * `data` payload.
 */
struct prometheus_cache
{
   time_t valid_until; /**< when the cache will become not valid */
   atomic_schar lock;  /**< lock to protect the cache */
   size_t size;        /**< size of the cache */
   char data[];        /**< the payload */
} __attribute__((aligned(64)));

/** @struct prometheus
 * Defines the Prometheus metrics
 */
struct prometheus
{
   atomic_ulong logging_info;  /**< Logging: INFO */
   atomic_ulong logging_warn;  /**< Logging: WARN */
   atomic_ulong logging_error; /**< Logging: ERROR */
   atomic_ulong logging_fatal; /**< Logging: FATAL */
} __attribute__((aligned(64)));

/** @struct common_configuration
 * Defines configurations that are common between all tools
 */
struct common_configuration
{
   char home_dir[MAX_PATH]; /**< The home directory */

   int log_type;                      /**< The logging type */
   int log_level;                     /**< The logging level */
   char log_path[MISC_LENGTH];        /**< The logging path */
   int log_mode;                      /**< The logging mode */
   int log_rotation_size;             /**< bytes to force log rotation */
   int log_rotation_age;              /**< minutes for log rotation */
   char log_line_prefix[MISC_LENGTH]; /**< The logging prefix */
   atomic_schar log_lock;             /**< The logging lock */

   struct server servers[NUMBER_OF_SERVERS]; /**< The servers */
   struct user users[NUMBER_OF_USERS];       /**< The users */
   struct user admins[NUMBER_OF_ADMINS];     /**< The admins */

   int number_of_servers; /**< The number of servers */
   int number_of_users;   /**< The number of users */
   int number_of_admins;  /**< The number of admins */

   char configuration_path[MAX_PATH]; /**< The configuration path */
   char users_path[MAX_PATH];         /**< The users path */
   char admins_path[MAX_PATH];        /**< The admins path */

   bool keep_alive;   /**< Use keep alive */
   bool nodelay;      /**< Use NODELAY */
   bool non_blocking; /**< Use non blocking */

   struct prometheus prometheus; /**< The Prometheus metrics */
} __attribute__((aligned(64)));

/** @struct main_configuration
 * Defines the main configuration list
 */
struct main_configuration
{
   struct common_configuration common; /**< Common configurations that are shared across multiple tools */

   bool running; /**< Is pgmoneta running */

   char host[MISC_LENGTH];     /**< The host */
   int metrics;                /**< The metrics port */
   int metrics_cache_max_age;  /**< Number of seconds to cache the Prometheus response */
   int metrics_cache_max_size; /**< Number of bytes max to cache the Prometheus response */
   int management;             /**< The management port */

   char base_dir[MAX_PATH]; /**< The base directory */

   int compression_type;  /**< The compression type */
   int compression_level; /**< The compression level */

   int create_slot; /**< Create a slot */

   int storage_engine; /**< The storage engine */

   int encryption; /**< The AES encryption mode */

   char ssh_hostname[MISC_LENGTH];      /**< The SSH hostname */
   char ssh_username[MISC_LENGTH];      /**< The SSH username */
   char ssh_base_dir[MAX_PATH];         /**< The SSH base directory */
   char ssh_ciphers[MISC_LENGTH];       /**< The SSH supported ciphers */
   char ssh_public_key_file[MAX_PATH];  /**< The SSH public key path */
   char ssh_private_key_file[MAX_PATH]; /**< The SSH private key path */

   struct s3_configuration s3; /**< The S3 configuration */

   char azure_storage_account[MISC_LENGTH]; /**< The Azure storage account name */
   char azure_container[MISC_LENGTH];       /**< The Azure container name */
   char azure_shared_key[MISC_LENGTH];      /**< The Azure storage account key */
   char azure_base_dir[MAX_PATH];           /**< The Azure base directory */

   int retention_days;     /**< The retention days for the server */
   int retention_weeks;    /**< The retention weeks for the server */
   int retention_months;   /**< The retention months for the server */
   int retention_years;    /**< The retention years for the server */
   int retention_interval; /**< The retention interval */

   char workspace[MAX_PATH]; /**< A workspace for combining incremental backups */

   bool tls;                     /**< Is TLS enabled */
   char tls_cert_file[MAX_PATH]; /**< TLS certificate path */
   char tls_key_file[MAX_PATH];  /**< TLS key path */
   char tls_ca_file[MAX_PATH];   /**< TLS CA certificate path */

   char metrics_cert_file[MAX_PATH]; /**< Metrics TLS certificate path */
   char metrics_key_file[MAX_PATH];  /**< Metrics TLS key path */
   char metrics_ca_file[MAX_PATH];   /**< Metrics TLS CA certificate path */

   int blocking_timeout;       /**< The blocking timeout in seconds */
   int authentication_timeout; /**< The authentication timeout in seconds */
   char pidfile[MAX_PATH];     /**< File containing the PID */

   int workers; /**< The number of workers */

   unsigned int update_process_title; /**< Behaviour for updating the process title */

   char libev[MISC_LENGTH]; /**< Name of libev mode */
   int backlog;             /**< The backlog for listen */
   unsigned char hugepage;  /**< Huge page support */

   char unix_socket_dir[MISC_LENGTH]; /**< The directory for the Unix Domain Socket */

   int backup_max_rate;  /**< Number of tokens added to the bucket with each replenishment for backup. */
   int network_max_rate; /**< Number of bytes of tokens added every one second to limit the netowrk backup rate */

   int verification; /**< The sha512 verification interval */

#ifdef DEBUG
   bool link; /**< Do linking */
#endif
} __attribute__((aligned(64)));

/** @struct walinfo_configuration
 * Defines the walinfo configuration list
 */
struct walinfo_configuration
{
   struct common_configuration common; /**< Common configurations that are shared across multiple tools */
} __attribute__((aligned(64)));

/** @struct walfilter_configuration
 * Defines the walfilter configuration list
 */
struct walfilter_configuration
{
   struct common_configuration common; /**< Common configurations that are shared across multiple tools */
} __attribute__((aligned(64)));

#ifdef __cplusplus
}
#endif

#endif
