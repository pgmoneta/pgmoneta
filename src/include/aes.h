/*
 * Copyright (C) 2022 Red Hat
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

#include <openssl/ssl.h>

/**
 * Encrypt a string
 * @param plaintext The string
 * @param password The master password
 * @param ciphertext The ciphertext output
 * @param ciphertext_length The length of the ciphertext
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
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_decrypt(char* ciphertext, int ciphertext_length, char* password, char** plaintext, int mode);

/**
 * Encrypt the files under the directory in place recursively, also remvoe unencrypted files.
 * @param d The wal directory
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_encrypt_data(char* d);

/**
 * Encrypt the files under the directory in place, also remvoe unencrypted files.
 * @param d The wal directory
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_encrypt_wal(char* d);

/**
 * Encrypt a single file, also remvoe unencrypted file.
 * @param from the path of file
 * @param to the path that encrypted file will be
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_encrypt_file(char* from, char* to);

/**
 * Decrypt the files under the directory in place, also remvoe encrypted files.
 * @param d The wal directory
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_decrypt_data(char* d);