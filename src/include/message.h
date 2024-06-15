/*
 * Copyright (C) 2024 The pgmoneta community
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

#ifndef PGMONETA_MESSAGE_H
#define PGMONETA_MESSAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <memory.h>
#include <pgmoneta.h>
#include <tablespace.h>

#include <stdbool.h>
#include <stdlib.h>

#include <openssl/ssl.h>

#define MESSAGE_STATUS_ZERO  0
#define MESSAGE_STATUS_OK    1
#define MESSAGE_STATUS_ERROR 2

extern struct token_bucket bucket;

/** @struct
 * Defines a message
 */
struct message
{
   signed char kind;  /**< The kind of the message */
   ssize_t length;    /**< The length of the message */
   size_t max_length; /**< The maximum size of the message */
   void* data;        /**< The message data */
} __attribute__ ((aligned (64)));

/** @struct
 * Defines a tuple
 */
struct tuple
{
   char** data;                   /**< The data */
   struct tuple* next;            /**< The next tuple */
} __attribute__ ((aligned (64)));

/** @struct
 * Defines the response to a query
 */
struct query_response
{
   char names[MAX_NUMBER_OF_COLUMNS][MISC_LENGTH]; /**< The column names */
   int number_of_columns;                          /**< The number of columns */

   struct tuple* tuples;
} __attribute__ ((aligned (64)));

/**
 * Read a message in blocking mode
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @param msg The resulting message
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgmoneta_read_block_message(SSL* ssl, int socket, struct message** msg);

/**
 * Read a message with a timeout
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @param timeout The timeout in seconds
 * @param msg The resulting message
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgmoneta_read_timeout_message(SSL* ssl, int socket, int timeout, struct message** msg);

/**
 * Write a message using a socket
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @param msg The message
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgmoneta_write_message(SSL* ssl, int socket, struct message* msg);

/**
 * Free a message
 * @param msg The resulting message
 */
void
pgmoneta_free_message(struct message* msg);

/**
 * Copy a message
 * @param msg The resulting message
 * @return The copy
 */
struct message*
pgmoneta_copy_message(struct message* msg);

/**
 * Free a copy message
 * @param msg The resulting message
 */
void
pgmoneta_free_copy_message(struct message* msg);

/**
 * Log a message
 * @param msg The message
 */
void
pgmoneta_log_message(struct message* msg);

/**
 * Log a COPY-failure message
 * @param msg The copyfail message
 */
void
pgmoneta_log_copyfail_message(struct message* msg);

/**
 * Log an Error Response message
 * @param msg The Error Response message
 */
void
pgmoneta_log_error_response_message(struct message* msg);

/**
 * Log a Notice Response message
 * @param msg The Notice Response message
 */
void
pgmoneta_log_notice_response_message(struct message* msg);

/**
 * Write a notice message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_write_notice(SSL* ssl, int socket);

/**
 * Write a terminate message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_write_terminate(SSL* ssl, int socket);

/**
 * Write an empty message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_write_empty(SSL* ssl, int socket);

/**
 * Write a connection refused message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_write_connection_refused(SSL* ssl, int socket);

/**
 * Write a connection refused message (protocol 1 or 2)
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_write_connection_refused_old(SSL* ssl, int socket);

/**
 * Write TLS response
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_write_tls(SSL* ssl, int socket);

/**
 * Create an auth password response message
 * @param password The password
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_auth_password_response(char* password, struct message** msg);

/**
 * Create an auth MD5 response message
 * @param md5 The md5
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_auth_md5_response(char* md5, struct message** msg);

/**
 * Write an auth SCRAM-SHA-256 message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_write_auth_scram256(SSL* ssl, int socket);

/**
 * Create an auth SCRAM-SHA-256 response message
 * @param nounce The nounce
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_auth_scram256_response(char* nounce, struct message** msg);

/**
 * Create an auth SCRAM-SHA-256/Continue message
 * @param cn The client nounce
 * @param sn The server nounce
 * @param salt The salt
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_auth_scram256_continue(char* cn, char* sn, char* salt, struct message** msg);

/**
 * Create an auth SCRAM-SHA-256/Continue response message
 * @param wp The without proff
 * @param p The proff
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_auth_scram256_continue_response(char* wp, char* p, struct message** msg);

/**
 * Create an auth SCRAM-SHA-256/Final message
 * @param ss The server signature (BASE64)
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_auth_scram256_final(char* ss, struct message** msg);

/**
 * Write an auth success message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_write_auth_success(SSL* ssl, int socket);

/**
 * Create a SSL message
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_ssl_message(struct message** msg);

/**
 * Create a startup message
 * @param username The user name
 * @param database The database
 * @param replication Should replication be enabled
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_startup_message(char* username, char* database, bool replication, struct message** msg);

/**
 * Create an IDENTIFY SYSTEM message
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_identify_system_message(struct message** msg);

/**
 * Create a TIMELINE_HISTORY message
 * @param timeline The timeline
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_timeline_history_message(int timeline, struct message** msg);

/**
 * Create a READ_REPLICATION_SLOT message
 * @param slot The slot
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_read_replication_slot_message(char* slot, struct message** msg);

/**
 * Create a START_REPLICATION message
 * @param xlogpos The WAL position (can be NULL)
 * @param timeline The timeline (can be -1)
 * @param slot The slot
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_start_replication_message(char* xlogpos, int timeline, char* slot, struct message** msg);

/**
 * Create a standby status update message
 * @param received The received position
 * @param flushed The flushed position
 * @param applied The applied position
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_standby_status_update_message(int64_t received, int64_t flushed, int64_t applied, struct message** msg);

/**
 * Create a base backup message
 * @param server_version The version of the PostgreSQL server to backup
 * @param label The label of the backup
 * @param include_wal The indication of whether to also include WAL
 * @param checksum_algorithm The checksum algorithm to be applied to backup manifest
 * @param compression The compression type
 * @param compression_level The compression level
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_base_backup_message(int server_version, char* label, bool include_wal, int checksum_algorithm,
                                    int compression, int compression_level,
                                    struct message** msg);

/**
 * Create a replication slot
 * @param create_slot_name The name of the slot
 * @param msg The resulting message
 * @param version The server version
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_replication_slot_message(char* create_slot_name, struct message** msg, int version);

/**
 * Create a message to search if a replication slot exists
 * @param slot_name The name of the slot
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_search_replication_slot_message(char* slot_name, struct message** msg);

/**
 * Send a CopyDone message
 * @param ssl The SSL structure
 * @param socket The socket
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_send_copy_done_message(SSL* ssl, int socket);

/**
 * Create a query message for a simple query
 * @param query The query to be executed on server
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_query_message(char* query, struct message** msg);

/**
 * Has a message
 * @param type The message type to be extracted
 * @param data The data
 * @param data_size The data size
 * @return true if found, otherwise false
 */
bool
pgmoneta_has_message(char type, void* data, size_t data_size);

/**
 * Query execute
 * @param ssl The SSL structure
 * @param socket The socket
 * @param msg The query message
 * @param query The query response
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_query_execute(SSL* ssl, int socket, struct message* msg, struct query_response** response);

/**
 * Get data from a query response
 * @param response The response
 * @param column The column
 * @return The data
 */
char*
pgmoneta_query_response_get_data(struct query_response* response, int column);

/**
 * Free query
 * @param query The query
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_free_query_response(struct query_response* response);

/**
 * Debug query
 * @param query The resulting query
 * @return 0 upon success, otherwise 1
 */
void
pgmoneta_query_response_debug(struct query_response* response);

/**
 * Read the copy stream into the streaming buffer in blocking mode
 * @param ssl The SSL structure
 * @param socket The socket
 * @param buffer The streaming buffer
 * @return 1 upon success, 0 if no data received, otherwise 2
 */
int
pgmoneta_read_copy_stream(SSL* ssl, int socket, struct stream_buffer* buffer);

/**
 * Consume the data in copy stream buffer, get the next valid message in the copy stream buffer
 * Recognized message types are DataRow, CopyOutResponse, CopyInResponse, CopyData, CopyDone, CopyFail, RowDescription, CommandComplete and ErrorResponse
 * Other message will be ignored
 * @param ssl The SSL structure
 * @param socket The socket
 * @param buffer The stream buffer
 * @param message [out] The message
 * @return 1 upon success, 0 if no data to consume, otherwise 2
 */
int
pgmoneta_consume_copy_stream(SSL* ssl, int socket, struct stream_buffer* buffer, struct message** message);

/**
 * Consume the data in copy stream buffer similar to pgmoneta_consume_copy_stream.
 * Instead of creating a new message each time, reuse the same message buffer each time
 * Must be used with pgmoneta_consume_copy_stream_end
 * @param ssl The SSL structure
 * @param socket The socket
 * @param buffer The stream buffer
 * @param message The message buffer
 * @param network_bucket The network rate limit bucket
 * @return 1 upon success, 0 if no data to consume, otherwise 2
 */
int
pgmoneta_consume_copy_stream_start(SSL* ssl, int socket, struct stream_buffer* buffer, struct message* message, struct token_bucket* network_bucket);

/**
 * Finish consuming the buffer, prepare for the next message to be consumed
 * @param buffer The stream buffer
 * @param message The message buffer
 */
void
pgmoneta_consume_copy_stream_end(struct stream_buffer* buffer, struct message* message);

/**
 * Receive and parse the DataRow messages into tuples
 * @param ssl The SSL structure
 * @param socket The socket
 * @param buffer The stream buffer holding the messages
 * @param response The query response
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_consume_data_row_messages(SSL* ssl, int socket, struct stream_buffer* buffer, struct query_response** response);

/**
 * Receive backup tar files from the copy stream and write to disk
 * This functionality is for server version < 15
 * @param ssl The SSL structure
 * @param socket The socket
 * @param buffer The stream buffer
 * @param basedir The base directory for the backup data
 * @param tablespaces The user level tablespaces
 * @param version The server version
 * @param bucket The rate limit bucket
 * @param network_bucket The network rate limit bucket
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_receive_archive_files(SSL* ssl, int socket, struct stream_buffer* buffer, char* basedir, struct tablespace* tablespaces, int version, struct token_bucket* bucket, struct token_bucket* network_bucket);

/**
 * Receive backup tar files from the copy stream and write to disk
 * This functionality is for server version >= 15
 * @param ssl The SSL structure
 * @param socket The socket
 * @param buffer The stream buffer
 * @param basedir The base directory for the backup data
 * @param tablespaces The user level tablespaces
 * @param bucket The rate limit bucket
 * @param network_bucket The network rate limit bucket
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_receive_archive_stream(SSL* ssl, int socket, struct stream_buffer* buffer, char* basedir, struct tablespace* tablespaces, struct token_bucket* bucket, struct token_bucket* network_bucket);

/**
 * Receive mainfest file from the copy stream and write to disk
 * @param ssl The SSL structure
 * @param socket The socket
 * @param buffer The stream buffer
 * @param basedir The base directory for the manifest
 * @param bucket The rate limit bucket
 * @param network_bucket The network rate limit bucket
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_receive_manifest_file(SSL* ssl, int socket, struct stream_buffer* buffer, char* basedir, struct token_bucket* bucket, struct token_bucket* network_bucket);

#ifdef __cplusplus
}
#endif

#endif
