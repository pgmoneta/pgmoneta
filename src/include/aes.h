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

#ifndef PGMONETA_AES_H
#define PGMONETA_AES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>
#include <json.h>
#include <workers.h>

#include <openssl/ssl.h>

/**
 * Encrypt a string
 * @param plaintext The string
 * @param password The master password
 * @param ciphertext The ciphertext output
 * @param ciphertext_length The length of the ciphertext
 * @param mode The encryption mode
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_encrypt(char* plaintext, char* password, char** ciphertext, int* ciphertext_length, int mode);

/**
 * Decrypt a string
 * @param ciphertext The string
 * @param ciphertext_length The length of the ciphertext
 * @param password The master password
 * @param plaintext The plaintext output
 * @param mode The decryption mode
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_decrypt(char* ciphertext, int ciphertext_length, char* password, char** plaintext, int mode);

/**
 * Encrypt the files under the directory in place recursively, also remove unencrypted files.
 * @param d The data directory
 * @param workers The optional workers
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_encrypt_data(char* d, struct workers* workers);

/**
 * Encrypt the files under the tablespace directories in place recursively, also remove unencrypted files.
 * @param root The root directory
 * @param workers The optional workers
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_encrypt_tablespaces(char* root, struct workers* workers);

/**
 * Encrypt the files under the directory in place, also remove unencrypted files.
 * @param d The wal directory
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_encrypt_wal(char* d);

/**
 * Encrypt a single file, also remove the original file
 * @param ssl The SSL
 * @param client_fd The client descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The payload of the request
 */
void
pgmoneta_encrypt_request(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * Encrypt a single file, also remove the original file
 * @param from The from file
 * @param to The to file
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_encrypt_file(char* from, char* to);

/**
 * Decrypt a single file, also remove the original file
 * @param from The from file
 * @param to The to file
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_decrypt_file(char* from, char* to);

/**
 * Decrypt the files under the directory in place, also remove encrypted files.
 * @param d wal directory
 * @param workers The optional workers
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_decrypt_directory(char* d, struct workers* workers);

/**
 * Decrypt a single file, also remove encrypted file
 * @param ssl The SSL
 * @param client_fd The client descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The payload of the request
 */
void
pgmoneta_decrypt_request(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 *
 * Encrypt a buffer
 * @param origin_buffer The original buffer
 * @param origin_size The size of the buffer
 * @param enc_buffer The result buffer
 * @param enc_size The result buffer size
 * @param mode The aes mode
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_encrypt_buffer(unsigned char* origin_buffer, size_t origin_size, unsigned char** enc_buffer, size_t* enc_size, int mode);

/**
 *
 * Decrypt a buffer
 * @param origin_buffer The original buffer
 * @param origin_size The size of the buffer
 * @param dec_buffer The result buffer
 * @param dec_size The result buffer size
 * @param mode The aes mode
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_decrypt_buffer(unsigned char* origin_buffer, size_t origin_size, unsigned char** dec_buffer, size_t* dec_size, int mode);

#ifdef __cplusplus
}
#endif

#endif
