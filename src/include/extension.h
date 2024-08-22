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

#ifndef PGMONETA_EXTENSION_H
#define PGMONETA_EXTENSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>
#include <message.h>

#include <stdbool.h>
#include <stdlib.h>

#define MAX_QUERY_LENGTH 1024

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
 * Return the extension version number
 * @param ssl The SSL structure
 * @param socket The socket
 * @param qr The query result
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_ext_version(SSL* ssl, int socket, struct query_response** qr);

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
pgmoneta_ext_priviledge(SSL* ssl, int socket, struct query_response** qr);

/**
 * Retrieve the bytes of the specified file
 * @param ssl The SSL structure
 * @param socket The socket
 * @param file_path The path to the file
 * @param qr The query result
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_ext_get_file(SSL* ssl, int socket, const char* file_path, struct query_response** qr);

/**
 * Retrieve all file paths under the specified directory
 * @param ssl The SSL structure
 * @param socket The socket
 * @param file_path The path to the directory
 * @param qr The query result
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_ext_get_files(SSL* ssl, int socket, const char* file_path, struct query_response** qr);

/**
 * Creates a table and inserts data from a manifest file
 * @param ssl The SSL structure used for secure communication
 * @param socket The socket file descriptor for the connection
 * @param file_path The path to the manifest file
 * @param qr The query result structure to store the result of the executed queries
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_ext_create_manifest_table(SSL* ssl, int socket, const char* file_path, struct query_response** qr);

/**
 * Delete backup_manifest table
 * @param ssl The SSL structure used for secure communication
 * @param socket The socket file descriptor for the connection
 * @param qr The query result structure to store the result of the executed queries
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_ext_delete_manifest_table(SSL* ssl, int socket, struct query_response** qr);

#ifdef __cplusplus
}
#endif

#endif