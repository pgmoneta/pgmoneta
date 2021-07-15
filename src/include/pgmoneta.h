/*
 * Copyright (C) 2021 Red Hat
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
#include <sys/types.h>
#include <openssl/ssl.h>

#define VERSION "0.3.2"

#define PGMONETA_HOMEPAGE "https://pgmoneta.github.io/"
#define PGMONETA_ISSUES "https://github.com/pgmoneta/pgmoneta/issues"

#define MAIN_UDS ".s.pgmoneta"

#define MAX_BUFFER_SIZE      65535
#define DEFAULT_BUFFER_SIZE  65535

#define MAX_USERNAME_LENGTH  128
#define MAX_PASSWORD_LENGTH 1024

#define MAX_PATH 1024
#define MISC_LENGTH 128
#define NUMBER_OF_SERVERS 64
#define NUMBER_OF_USERS    64
#define NUMBER_OF_ADMINS    8

#define STATE_FREE        0
#define STATE_IN_USE      1

#define AUTH_SUCCESS      0
#define AUTH_BAD_PASSWORD 1
#define AUTH_ERROR        2
#define AUTH_TIMEOUT      3

#define HUGEPAGE_OFF 0
#define HUGEPAGE_TRY 1
#define HUGEPAGE_ON  2

#define COMPRESSION_NONE 0
#define COMPRESSION_GZIP 1
#define COMPRESSION_ZSTD 2

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

/**
 * The shared memory segment
 */
extern void* shmem;

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
   int retention;                      /**< The retention for the server */
   bool synchronous;                   /**< Run in synchronous mode */
   atomic_bool backup;                 /**< Is there an active backup */
   atomic_bool delete;                 /**< Is there an active delete */
   bool valid;                         /**< Is the server valid */
} __attribute__ ((aligned (64)));

/** @struct
 * Defines a user
 */
struct user
{
   char username[MAX_USERNAME_LENGTH]; /**< The user name */
   char password[MAX_PASSWORD_LENGTH]; /**< The password */
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
   char configuration_path[MAX_PATH]; /**< The configuration path */
   char users_path[MAX_PATH];         /**< The users path */
   char admins_path[MAX_PATH];        /**< The admins path */

   char host[MISC_LENGTH]; /**< The host */
   int metrics;            /**< The metrics port */
   int management;         /**< The management port */

   char base_dir[MAX_PATH];  /**< The base directory */
   char pgsql_dir[MAX_PATH]; /**< The PostgreSQL directory */

   int compression_type;  /**< The compression type */
   int compression_level; /**< The compression level */

   int retention; /**< The retention */
   bool link;     /**< Use link */

   int log_type;               /**< The logging type */
   int log_level;              /**< The logging level */
   char log_path[MISC_LENGTH]; /**< The logging path */
   atomic_schar log_lock;      /**< The logging lock */

   bool tls;                        /**< Is TLS enabled */
   char tls_cert_file[MISC_LENGTH]; /**< TLS certificate path */
   char tls_key_file[MISC_LENGTH];  /**< TLS key path */
   char tls_ca_file[MISC_LENGTH];   /**< TLS CA certificate path */

   int blocking_timeout;       /**< The blocking timeout in seconds */
   int authentication_timeout; /**< The authentication timeout in seconds */
   char pidfile[MISC_LENGTH];  /**< File containing the PID */

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
