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

#ifndef PGMONETA_DELETE_H
#define PGMONETA_DELETE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>

#include <stdlib.h>

/**
 * Delete a backup from a server
 * @param srv The server index
 * @param backup_id The backup_id
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_delete(int srv, char* backup_id);

/**
 * Delete WAL from a server
 * @param srv The server index
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_delete_wal(int srv);

/**
 * Delete the backups of a server that did not finish, locally and on the
 * remote storage engines. A marker stays until the remote cleanup of its
 * backup succeeds, so the next call tries again.
 * The caller holds the repository lock of the server, or nothing runs yet
 * @param srv The server index
 * @param startup Also delete the backup directories without backup.info,
 *                which is only safe before anything runs, since a restore
 *                from S3, Azure or GCS downloads one without the lock
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_delete_incomplete_backups(int srv, bool startup);

/**
 * Delete the incomplete backup of a backup process that died,
 * then release the repository lock
 * @param argv The argv
 * @param srv The server index
 * @param pid The backup process that died
 */
void
pgmoneta_delete_failed_backup(char** argv, int srv, pid_t pid);

#ifdef __cplusplus
}
#endif

#endif
