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

#ifndef PGMONETA_VERIFY_H
#define PGMONETA_VERIFY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>
#include <deque.h>
#include <json.h>
#include <storage.h>

#include <stdlib.h>

/**
 * Create a verify
 * @param ssl The SSL connection
 * @param client_fd The client
 * @param server The server
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The payload
 */
void
pgmoneta_verify(SSL* ssl, int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * Run sha512 verification job
 * @param argv The argv
 */
void
pgmoneta_sha512_verification(char** argv);

/**
 * Check the files of a backup, as stored, against its backup.sha512
 * @param server The server
 * @param label The label of the backup
 * @param missing_ok Whether a missing file is skipped (a backup staged from a remote storage engine)
 * @param failed A deque that gets an entry for each failed file, or NULL
 * @return 0 if all files match, 1 if a file is missing, unreadable or different, 2 if there is no backup.sha512
 */
int
pgmoneta_verify_sha512(int server, char* label, bool missing_ok, struct deque* failed);

/**
 * Check a backup and the backups it is built on against their backup.sha512
 * @param server The server
 * @param label The label of the backup
 * @param self Whether to check the backup itself, or only the backups it is built on
 * @param missing_ok Whether a missing file is skipped (a backup staged from a remote storage engine)
 * @param failed A deque that gets an entry for each failed file, or NULL
 * @return 0 if all files match, otherwise 1
 */
int
pgmoneta_verify_sha512_chain(int server, char* label, bool self, bool missing_ok, struct deque* failed);

#ifdef __cplusplus
}
#endif

#endif
