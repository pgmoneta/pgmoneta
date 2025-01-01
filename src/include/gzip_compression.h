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

#ifndef PGMONETA_GZIP_H
#define PGMONETA_GZIP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <json.h>
#include <workers.h>

#include <stdlib.h>

/**
 * GZip a data directory
 * @param directory The directory
 * @param workers The optional workers
 */
void
pgmoneta_gzip_data(char* directory, struct workers* workers);

/**
 * GZip tablespace directories
 * @param root The root directory
 * @param workers The optional workers
 */
void
pgmoneta_gzip_tablespaces(char* root, struct workers* workers);

/**
 * GZip a WAL directory
 * @param directory The directory
 */
void
pgmoneta_gzip_wal(char* directory);

/**
 * GZip a single file, also remove the original file
 * @param ssl The SSL
 * @param client_fd The client descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The payload of the request
 */
void
pgmoneta_gzip_request(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * GZip a file
 * @param from The from name
 * @param to The to name
 */
int
pgmoneta_gzip_file(char* from, char* to);

/**
 * GUNZip a single file, also remove the original file
 * @param ssl The SSL
 * @param client_fd The client descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The payload of the request
 */
void
pgmoneta_gunzip_request(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * GUNZip a file
 * @param from The from file
 * @param to The to file
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_gunzip_file(char* from, char* to);

/**
 * GUNZip a directory
 * @param directory The directory
 * @param workers The optional workers
 */
void
pgmoneta_gunzip_data(char* directory, struct workers* workers);

/**
 * GZip a string
 * @param s The original string
 * @param buffer The point to the compressed data buffer
 * @param buffer_size The size of the compressed buffer will be stored.
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_gzip_string(char* s, unsigned char** buffer, size_t* buffer_size);

/**
 * GUNZip a buffer to string
 * @param compressed_buffer The buffer containing the GZIP compressed data
 * @param compressed_size The size of the compressed buffer
 * @param output_string The pointer to a string where the decompressed data will be stored
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_gunzip_string(unsigned char* compressed_buffer, size_t compressed_size, char** output_string);

#ifdef __cplusplus
}
#endif

#endif
