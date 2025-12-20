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

#ifndef PGMONETA_EXTENSION_H
#define PGMONETA_EXTENSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>
#include <message.h>

#include <stdbool.h>
#include <stdlib.h>

#define MAX_QUERY_LENGTH    16384
#define PGMONETA_CHUNK_SIZE 8192

/**
 * Check if the server has the extension installed
 * @param ssl The SSL structure
 * @param socket The socket
 * @param qr The query result
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_ext_is_installed(SSL* ssl, int socket, struct query_response** qr);

/**
 * Trigger WAL switch operation
 * @param ssl The SSL structure
 * @param socket The socket
 * @param qr The query result
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_ext_switch_wal(SSL* ssl, int socket, struct query_response** qr);

/**
 * Force PostgreSQL to carry out an immediate checkpoint
 * @param ssl The SSL structure
 * @param socket The socket
 * @param qr The query result
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_ext_checkpoint(SSL* ssl, int socket, struct query_response** qr);

/**
 * Check if the current user is a superuser
 * @param ssl The SSL structure
 * @param socket The socket
 * @param qr The query result
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_ext_privilege(SSL* ssl, int socket, struct query_response** qr);

/**
 * Retrieve the bytes of the specified file
 * @param ssl The SSL structure
 * @param socket The socket
 * @param file_path The path to the file
 * @param qr The query result
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_ext_get_file(SSL* ssl, int socket, char* file_path, struct query_response** qr);

/**
 * Retrieve all file paths under the specified directory
 * @param ssl The SSL structure
 * @param socket The socket
 * @param file_path The path to the directory
 * @param qr The query result
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_ext_get_files(SSL* ssl, int socket, char* file_path, struct query_response** qr);

/**
 * Send a file chunk to the extension
 * @param ssl The SSL structure
 * @param socket The socket
 * @param dest_path The path where the file will be written
 * @param base64_data The encoded file chunk in base64
 * @param qr The query result
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_ext_send_file_chunk(SSL* ssl, int socket, char* dest_path, char* base64_data, struct query_response** qr);

/**
 * Promote a standby (replica) server to become the primary server
 * @param ssl The SSL structure
 * @param socket The socket
 * @param qr The query result
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_ext_promote(SSL* ssl, int socket, struct query_response** qr);

/**
 * Parse a semantic version string (e.g., "1.8.2" or "2.1") into version struct
 * @param version_str The version string to parse
 * @param version Output version struct
 * @return 0 on success, 1 on error
 */
int
pgmoneta_extension_parse_version(char* version_str, struct version* version);

/**
 * Convert a version struct to a string representation
 * @param version The version struct to convert
 * @param buffer Output buffer to write the version string
 * @param buffer_size Size of the output buffer
 * @return 0 on success, 1 on error
 */
int
pgmoneta_version_to_string(struct version* version, char* buffer, size_t buffer_size);

/**
 * Detect and populate all installed PostgreSQL extensions for a server
 * @param server The server index
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_detect_server_extensions(int server);

#ifdef __cplusplus
}
#endif

#endif
