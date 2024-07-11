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

/**
 * Return the extension version number
 * @param server The server
 * @param qr The query result
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_ext_version(int server, struct query_response** qr);

/**
 * Trigger WAL switch operation
 * @param server The server
 * @param qr The query result
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_ext_switch_wal(int server, struct query_response** qr);

/**
 * Force PostgreSQL to carry out an immediate checkpoint
 * @param server The server
 * @param qr The query result
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_ext_checkpoint(int server, struct query_response** qr);

#ifdef __cplusplus
}
#endif

#endif