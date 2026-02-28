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

#ifndef PGMONETA_ARCHIVE_H
#define PGMONETA_ARCHIVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>
#include <json.h>
#include <message.h>
#include <tablespace.h>

#include <stdlib.h>

/**
 * Create an archive
 * @param ssl The SSL connection
 * @param client_fd The client
 * @param server The server
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param request The request
 */
void
pgmoneta_archive(SSL* ssl, int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* request);

/**
 * Receive backup tar files from the copy stream and write to disk
 * This functionality is for server version < 15
 * @param srv The server
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
pgmoneta_receive_archive_files(int srv, SSL* ssl, int socket, struct stream_buffer* buffer, char* basedir, struct tablespace* tablespaces, struct token_bucket* bucket, struct token_bucket* network_bucket);

/**
 * Receive backup tar files from the copy stream and write to disk
 * This functionality is for server version >= 15
 * @param srv The server
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
pgmoneta_receive_archive_stream(int srv, SSL* ssl, int socket, struct stream_buffer* buffer, char* basedir, struct tablespace* tablespaces, struct token_bucket* bucket, struct token_bucket* network_bucket);

#ifdef __cplusplus
}
#endif

#endif
