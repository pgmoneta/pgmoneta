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

#ifndef PGMONETA_SERVER_H
#define PGMONETA_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>

#include <stdlib.h>

/**
 * Get the information for a server
 * @param srv The server index
 * @param ssl The SSL connection
 * @param socket The socket
 */
void
pgmoneta_server_info(int srv, SSL* ssl, int socket);

/**
 * Is the base settings for the server set
 * @param srv The server index
 * @return True if valid, otherwise false
 */
bool
pgmoneta_server_valid(int srv);

/**
 * Verify that a connection can be established to a server
 * @param srv The server index
 * @return True if yes, otherwise false
 */
bool
pgmoneta_server_verify_connection(int srv);

/**
 * Read a relation file from the server cluster
 * @param srv The server index
 * @param ssl The SSL connection
 * @param relative_file_path The relative path of the relation file inside the data cluter
 * @param offset The offset of the file from where data retrieval should start
 * @param length The number of bytes that should be retrieved
 * @param socket The socket
 * @param [out] out The binary output
 * @param [out] len The binary output length
 * @return return 0 if success, otherwise failure
 */
int
pgmoneta_server_read_binary_file(int srv, SSL* ssl, char* relative_file_path, int offset,
     int length, int socket, uint8_t** out, int* len);

#ifdef __cplusplus
}
#endif

#endif
