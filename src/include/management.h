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

#ifndef PGMONETA_MANAGEMENT_H
#define PGMONETA_MANAGEMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>

#include <stdbool.h>
#include <stdlib.h>

#include <openssl/ssl.h>

#define MANAGEMENT_BACKUP          1
#define MANAGEMENT_LIST_BACKUP     2
#define MANAGEMENT_RESTORE         3
#define MANAGEMENT_ARCHIVE         4
#define MANAGEMENT_DELETE          5
#define MANAGEMENT_STOP            6
#define MANAGEMENT_STATUS          7
#define MANAGEMENT_STATUS_DETAILS  8
#define MANAGEMENT_ISALIVE         9
#define MANAGEMENT_RESET          10
#define MANAGEMENT_RELOAD         11
#define MANAGEMENT_RETAIN         12
#define MANAGEMENT_EXPUNGE        13
#define MANAGEMENT_DECRYPT        14
#define MANAGEMENT_ENCRYPT        15
#define MANAGEMENT_INFO           16

/**
 * Available command output formats
 */
#define COMMAND_OUTPUT_FORMAT_TEXT 'T'
#define COMMAND_OUTPUT_FORMAT_JSON 'J'

/**
 * Read the management header
 * @param socket The socket descriptor
 * @param id The resulting management identifier
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_read_header(int socket, signed char* id);

/**
 * Read the management payload
 * @param socket The socket descriptor
 * @param id The management identifier
 * @param payload_s1 The resulting string payload
 * @param payload_s2 The resulting string payload
 * @param payload_s3 The resulting string payload
 * @param payload_s4 The resulting string payload
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_read_payload(int socket, signed char id, char** payload_s1, char** payload_s2, char** payload_s3, char** payload_s4);

/**
 * Management operation: Backup a server
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server name
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_backup(SSL* ssl, int socket, char* server);

/**
 * Management operation: List backups for a server
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server name
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_list_backup(SSL* ssl, int socket, char* server);

/**
 * Management operation: List backups for a server (Read)
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server name
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_read_list_backup(SSL* ssl, int socket, char* server, char output_format);

/**
 * Management operation: List backups for a server (Write)
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server index
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_write_list_backup(SSL* ssl, int socket, int server);

/**
 * Management operation: Restore a server
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server name
 * @param backup_id The backup identifier
 * @param position The position
 * @param directory The directory
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_restore(SSL* ssl, int socket, char* server, char* backup_id, char* position, char* directory);

/**
 * Management operation: Archive a server
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server name
 * @param backup_id The backup identifier
 * @param position The position
 * @param directory The directory
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_archive(SSL* ssl, int socket, char* server, char* backup_id, char* position, char* directory);

/**
 * Management operation: Delete a backup for a server
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server name
 * @param backup_id The backup id
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_delete(SSL* ssl, int socket, char* server, char* backup_id);

/**
 * Management operation: Delete a backup for a server (Read)
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server name
 * @param backup_id The backup identifier
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_read_delete(SSL* ssl, int socket, char* server, char* backup_id, char output_format);

/**
 * Management operation: Delete a backup for a server (Write)
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server index
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_write_delete(SSL* ssl, int socket, int server);

/**
 * Management operation: Stop
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_stop(SSL* ssl, int socket);

/**
 * Management operation: Status
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_status(SSL* ssl, int socket);

/**
 * Management: Read status
 * @param ssl The SSL connection
 * @param socket The socket
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_read_status(SSL* ssl, int socket, char output_format);

/**
 * Management: Write status
 * @param ssl The SSL connection
 * @param socket The socket
 * @param offline Offline status
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_write_status(SSL* ssl, int socket, bool offline);

/**
 * Management operation: Details
 * @param ssl The SSL connection
 * @param socket The socket
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_details(SSL* ssl, int socket);

/**
 * Management: Read details
 * @param socket The socket
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_read_details(SSL* ssl, int socket, char output_format);

/**
 * Management: Write details
 * @param ssl The SSL connection
 * @param socket The socket
 * @param offline Offline status
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_write_details(SSL* ssl, int socket, bool offline);

/**
 * Management operation: isalive
 * @param socket The socket
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_isalive(SSL* ssl, int socket);

/**
 * Management: Read isalive
 * @param socket The socket
 * @param status The resulting status
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_read_isalive(SSL* ssl, int socket, int* status);

/**
 * Management: Write isalive
 * @param ssl The SSL connection
 * @param socket The socket
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_write_isalive(SSL* ssl, int socket);

/**
 * Management operation: Reset
 * @param ssl The SSL connection
 * @param socket The socket
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_reset(SSL* ssl, int socket);

/**
 * Management operation: Reload
 * @param ssl The SSL connection
 * @param socket The socket
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_reload(SSL* ssl, int socket);

/**
 * Management operation: Retain a backup for a server
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server name
 * @param backup_id The backup id
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_retain(SSL* ssl, int socket, char* server, char* backup_id);

/**
 * Management operation: Expunge a backup for a server
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server name
 * @param backup_id The backup id
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_expunge(SSL* ssl, int socket, char* server, char* backup_id);

/**
 * Management operation: Decrypt an archive file on server
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param path The file path
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_decrypt(SSL* ssl, int socket, char* path);

/**
 * Management operation: Encrypt a file on server
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param path The file path
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_encrypt(SSL* ssl, int socket, char* path);

/**
 * Management operation: Information about a back
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server name
 * @param backup The backup
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_info(SSL* ssl, int socket, char* server, char* backup);

/**
 * Management operation: Read information for a backup
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_read_info(SSL* ssl, int socket, char output_format);

/**
 * Management: Write info
 * @param ssl The SSL connection
 * @param socket The socket
 * @param server The server name
 * @param backup The backup
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_write_info(SSL* ssl, int socket, char* server, char* backup);

/**
 * Management: Read int32
 * @param ssl The SSL connection
 * @param socket The socket
 * @param status The resulting status
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_read_int32(SSL* ssl, int socket, int* status);

/**
 * Management: Write int32
 * @param ssl The SSL connection
 * @param socket The socket
 * @param code The code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_write_int32(SSL* ssl, int socket, int code);

/**
 * Management: Write result
 * @param ssl The SSL connection
 * @param socket The socket
 * @param srv The server index
 * @param server The server
 * @param code The code
 * @param send Whether to send
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_process_result(SSL* ssl, int socket, int srv, char* server, int code, bool send);

#ifdef __cplusplus
}
#endif

#endif
