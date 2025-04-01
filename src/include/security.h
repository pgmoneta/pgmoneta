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

#ifndef PGMONETA_SECURITY_H
#define PGMONETA_SECURITY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>

#include <stdlib.h>

#include <openssl/ssl.h>

#define HASH_ALGORITHM_DEFAULT 0
#define HASH_ALGORITHM_CRC32C  1
#define HASH_ALGORITHM_SHA224  2
#define HASH_ALGORITHM_SHA256  3
#define HASH_ALGORITHM_SHA384  4
#define HASH_ALGORITHM_SHA512  5

/**
 * Authenticate a user
 * @param server The server
 * @param database The database
 * @param username The username
 * @param password The password
 * @param replication Is replication enabled
 * @param ssl The resulting SSL structure
 * @param fd The resulting socket
 * @return AUTH_SUCCESS, AUTH_BAD_PASSWORD or AUTH_ERROR
 */
int
pgmoneta_server_authenticate(int server, char* database, char* username, char* password, bool replication, SSL** ssl, int* fd);

/**
 * Authenticate a remote management user
 * @param client_fd The descriptor
 * @param address The client address
 * @param client_ssl The client SSL context
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_remote_management_auth(int client_fd, char* address, SSL** client_ssl);

/**
 * Connect using SCRAM-SHA256
 * @param username The user name
 * @param password The password
 * @param server_fd The descriptor
 * @param s_ssl The SSL context
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_remote_management_scram_sha256(char* username, char* password, int server_fd, SSL** s_ssl);

/**
 * Get the master key
 * @param masterkey The master key
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_get_master_key(char** masterkey);

/**
 * Is the TLS configuration valid
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tls_valid(void);

/**
 * Generate SHA224 for a file
 * @param filename The file path
 * @param sha224 The hash value
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_sha224_file(char* filename, char** sha224);

/**
 * Generate SHA256 for a file
 * @param filename The file path
 * @param sha256 The hash value
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_sha256_file(char* filename, char** sha256);

/**
 * Generate SHA384 for a file
 * @param filename The file path
 * @param sha384 The hash value
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_sha384_file(char* filename, char** sha384);

/**
 * Generate SHA512 for a file
 * @param filename The file path
 * @param sha512 The hash value
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_sha512_file(char* filename, char** sha512);

/**
 * Generate SHA256 for a string.
 * @param filename The string.
 * @param sha256 The hash value.
 * @return 0 upon success, otherwise 1.
 */
int
pgmoneta_generate_string_sha256_hash(char* string, char** sha256);

/**
 * Generate HMAC by using the SHA256 algorithm for a string.
 * @param key The key.
 * @param key_length The length of the key.
 * @param value The value.
 * @param value_length The length of the value.
 * @param hmac The digest.
 * @param hmac_length The length of the digest.
 * @return 0 upon success, otherwise 1.
 */
int
pgmoneta_generate_string_hmac_sha256_hash(char* key, int key_length, char* value,
                                          int value_length, unsigned char** hmac,
                                          int* hmac_length);

/**
 * Generate CRC32C for a buffer
 * @param buffer The buffer
 * @param size The size of the buffer
 * @param crc_buff The hash value
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_crc32c_buffer(void* buffer, size_t size, uint32_t* crc_buf);

/**
 * @param path The file path.
 * @param crc The hash value.
 * @return 0 upon success, otherwise 1.
 */
int
pgmoneta_create_crc32c_file(char* path, char** crc);

/**
 * Create file hash with given algorithm
 * @param algorithm The algorithm represented by index
 * @param file_path The file path
 * @param hash [out] The hash value
 * @return 0 upon success, otherwise 1.
 */
int
pgmoneta_create_file_hash(int algorithm, char* file_path, char** hash);

/**
 * Close a SSL structure
 * @param ssl The SSL structure
 */
void
pgmoneta_close_ssl(SSL* ssl);

/**
 * Convert an algorithm string into algorithm enum index
 * @param algorithm The algorithm, case insensitive
 * @return The algorithm index
 */
int
pgmoneta_get_hash_algorithm(char* algorithm);

#ifdef __cplusplus
}
#endif

#endif
