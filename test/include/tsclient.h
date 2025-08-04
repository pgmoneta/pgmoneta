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
 *
 */

#ifndef PGMONETA_TSCLIENT_H
#define PGMONETA_TSCLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>
#include <brt.h>
#include <walfile/wal_reader.h>

/**
 * Execute backup command on the server
 * @param server the server to perform backup on
 * @param incremental execute backup in incremental mode
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_backup(char* server, char* incremental);

/**
 * Execute restore command on the server
 * @param server the server to perform restore on
 * @param backup_id the backup_id to perform restore on
 * @param position the position parameters
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_restore(char* server, char* backup_id, char* position);

/**
 * Execute delete command on the server
 * @param server the server to perform delete on
 * @param backup_id the backup_id to delete
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_delete(char* server, char* backup_id);

/**
 * Execute reload command on the server
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_reload();

#ifdef __cplusplus
}
#endif

#endif