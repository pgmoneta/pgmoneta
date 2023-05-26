/*
 * Copyright (C) 2023 Red Hat
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
#include <stdlib.h>
#include <time.h>
#if HAVE_OPENBSD
#include <sys/limits.h>
#endif
#include <sys/types.h>
#include <openssl/ssl.h>

#define VERSION "0.7.0"

#define PGMONETA_HOMEPAGE "https://pgmoneta.github.io/"
#define PGMONETA_ISSUES "https://github.com/pgmoneta/pgmoneta/issues"

#define MAIN_UDS ".s.pgmoneta"

#define MAX_PROCESS_TITLE_LENGTH 256

#define MAX_BUFFER_SIZE      65535
#define DEFAULT_BUFFER_SIZE  65535

#define MAX_USERNAME_LENGTH  128
#define MAX_PASSWORD_LENGTH 1024

#define MAX_PATH 1024
#define MISC_LENGTH 128
#define NUMBER_OF_SERVERS 64
#define NUMBER_OF_USERS    64
#define NUMBER_OF_ADMINS    8

#define MAX_NUMBER_OF_COLUMNS 8

#define STATE_FREE        0
#define STATE_IN_USE      1

#define AUTH_SUCCESS      0
#define AUTH_BAD_PASSWORD 1
#define AUTH_ERROR        2
#define AUTH_TIMEOUT      3

#define ENCRYPTION_NONE     0
#define ENCRYPTION_AES_256_CBC  1
#define ENCRYPTION_AES_192_CBC  2
#define ENCRYPTION_AES_128_CBC  3
#define ENCRYPTION_AES_256_CTR  4
#define ENCRYPTION_AES_192_CTR  5
#define ENCRYPTION_AES_128_CTR  6

#define HUGEPAGE_OFF 0
#define HUGEPAGE_TRY 1
#define HUGEPAGE_ON  2

#define COMPRESSION_NONE  0
#define COMPRESSION_GZIP  1
#define COMPRESSION_ZSTD  2
#define COMPRESSION_LZ4   3
#define COMPRESSION_BZIP2 4

#define STORAGE_ENGINE_LOCAL 0
#define STORAGE_ENGINE_SSH   1
#define STORAGE_ENGINE_S3    2
#define STORAGE_ENGINE_AZURE 3

#define UPDATE_PROCESS_TITLE_NEVER   0
#define UPDATE_PROCESS_TITLE_STRICT  1
#define UPDATE_PROCESS_TITLE_MINIMAL 2
#define UPDATE_PROCESS_TITLE_VERBOSE 3

#define likely(x)    __builtin_expect (!!(x), 1)
#define unlikely(x)  __builtin_expect (!!(x), 0)

#define MAX(a, b)               \
   ({ __typeof__ (a) _a = (a);  \
      __typeof__ (b) _b = (b);  \
      _a > _b ? _a : _b; })

#define MIN(a, b)               \
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
   } while (0);

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
#define SLEEP_AND_GOTO(zzz, goto_to)    \
   do                                   \
   {                                    \
      struct timespec ts_private;       \
      ts_private.tv_sec = 0;            \
      ts_private.tv_nsec = zzz;         \
      nanosleep(&ts_private, NULL);     \
      goto goto_to;                     \
   } while (0);

/**
 * The shared memory segment
 */
extern void* shmem;

/**
 * Shared memory used to contain the Prometheus
 * response cache.
 */
extern void* prometheus_cache_shmem;

/** @struct
 * Defines a server
 */
struct server
{
   char name[MISC_LENGTH];             /**< The name of the server */
   char host[MISC_LENGTH];             /**< The host name of the server */
   int port;                           /**< The port of the server */
   char username[MAX_USERNAME_LENGTH]; /**< The user name */
   char backup_slot[MISC_LENGTH];      /**< The backup slot name */
   char wal_slot[MISC_LENGTH];         /**< The WAL slot name */
   char follow[MISC_LENGTH];           /**< Follow a server */
   int retention_days;                 /**< The retention days for the server */
   int retention_weeks;                /**< The retention weeks for the server */
   int retention_months;               /**< The retention months for the server */
   int retention_years;                /**< The retention years for the server */
   bool synchronous;                   /**< Run in synchronous mode */
   atomic_bool backup;                 /**< Is there an active backup */
   atomic_bool delete;                 /**< Is there an active delete */
   atomic_bool wal;                    /**< Is there an active wal */
   int wal_size;                       /**< The size of the WAL files */
   bool wal_streaming;                 /**< Is WAL streaming active */
   bool valid;                         /**< Is the server valid */
   int version;                        /**< The major version of the server*/
} __attribute__ ((aligned (64)));

/** @struct
 * Defines a user
 */
struct user
{
   char username[MAX_USERNAME_LENGTH]; /**< The user name */
   char password[MAX_PASSWORD_LENGTH]; /**< The password */
} __attribute__ ((aligned (64)));

/**
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
   time_t valid_until;   /**< when the cache will become not valid */
   atomic_schar lock;    /**< lock to protect the cache */
   size_t size;          /**< size of the cache */
   char data[];          /**< the payload */
} __attribute__ ((aligned (64)));

/** @struct
 * Defines the Prometheus metrics
 */
struct prometheus
{
} __attribute__ ((aligned (64)));

/** @struct
 * Defines the configuration and state of pgmoneta
 */
struct configuration
{
   bool running; /**< Is pgmoneta running */

   char configuration_path[MAX_PATH]; /**< The configuration path */
   char users_path[MAX_PATH];         /**< The users path */
   char admins_path[MAX_PATH];        /**< The admins path */

   char host[MISC_LENGTH];     /**< The host */
   int metrics;                /**< The metrics port */
   int metrics_cache_max_age;  /**< Number of seconds to cache the Prometheus response */
   int metrics_cache_max_size; /**< Number of bytes max to cache the Prometheus response */
   int management;             /**< The management port */

   char base_dir[MAX_PATH];  /**< The base directory */
   char pgsql_dir[MAX_PATH]; /**< The PostgreSQL directory */

   int compression_type;  /**< The compression type */
   int compression_level; /**< The compression level */

   int storage_engine;  /**< The storage engine */

   int encryption; /**< The AES encryption mode */

   char ssh_hostname[MISC_LENGTH]; /**< The SSH hostname */
   char ssh_username[MISC_LENGTH]; /**< The SSH username */
   char ssh_base_dir[MAX_PATH];    /**< The SSH base directory */
   char ssh_ciphers[MISC_LENGTH];  /**< The SSH supported ciphers */

   char s3_aws_region[MISC_LENGTH];         /**< The AWS region */
   char s3_access_key_id[MISC_LENGTH];      /**< The IAM Access Key ID */
   char s3_secret_access_key[MISC_LENGTH];  /**< The IAM Secret Access Key */
   char s3_bucket[MISC_LENGTH];          /**< The S3 bucket */
   char s3_base_dir[MAX_PATH];           /**< The S3 base directory */

   char azure_storage_account[MISC_LENGTH];    /**< The Azure storage account name */
   char azure_container[MISC_LENGTH];          /**< The Azure container name */
   char azure_shared_key[MISC_LENGTH];         /**< The Azure storage account key */
   char azure_base_dir[MAX_PATH];              /**< The Azure base directory */

   int retention_days;                  /**< The retention days for the server */
   int retention_weeks;                 /**< The retention weeks for the server */
   int retention_months;                /**< The retention months for the server */
   int retention_years;                 /**< The retention years for the server */
   bool link;     /**< Use link */

   int log_type;                      /**< The logging type */
   int log_level;                     /**< The logging level */
   char log_path[MISC_LENGTH];        /**< The logging path */
   int log_mode;                      /**< The logging mode */
   int log_rotation_size;             /**< bytes to force log rotation */
   int log_rotation_age;              /**< minutes for log rotation */
   char log_line_prefix[MISC_LENGTH]; /**< The logging prefix */
   atomic_schar log_lock;             /**< The logging lock */

   bool tls;                        /**< Is TLS enabled */
   char tls_cert_file[MISC_LENGTH]; /**< TLS certificate path */
   char tls_key_file[MISC_LENGTH];  /**< TLS key path */
   char tls_ca_file[MISC_LENGTH];   /**< TLS CA certificate path */

   int blocking_timeout;       /**< The blocking timeout in seconds */
   int authentication_timeout; /**< The authentication timeout in seconds */
   char pidfile[MAX_PATH];     /**< File containing the PID */

   unsigned int update_process_title;  /**< Behaviour for updating the process title */

   char libev[MISC_LENGTH]; /**< Name of libev mode */
   int buffer_size;         /**< Socket buffer size */
   bool keep_alive;         /**< Use keep alive */
   bool nodelay;            /**< Use NODELAY */
   bool non_blocking;       /**< Use non blocking */
   int backlog;             /**< The backlog for listen */
   unsigned char hugepage;  /**< Huge page support */

   char unix_socket_dir[MISC_LENGTH]; /**< The directory for the Unix Domain Socket */

   int number_of_servers;        /**< The number of servers */
   int number_of_users;          /**< The number of users */
   int number_of_admins;         /**< The number of admins */

   struct server servers[NUMBER_OF_SERVERS];       /**< The servers */
   struct user users[NUMBER_OF_USERS];             /**< The users */
   struct user admins[NUMBER_OF_ADMINS];           /**< The admins */
   struct prometheus prometheus;                   /**< The Prometheus metrics */
} __attribute__ ((aligned (64)));

#ifdef __cplusplus
}
#endif

#endif
