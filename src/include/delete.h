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
 * Delete the backups of a server that are marked as incomplete,
 * locally and on the remote storage engines.
 * Only safe when no backup of the server can be running
 * @param srv The server index
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_delete_incomplete_backups(int srv);

/**
 * Delete a backup that is marked as incomplete, locally and on the remote
 * storage engines. The marker stays until the remote cleanup succeeds
 * @param srv The server index
 * @param label The backup label
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_delete_incomplete_backup(int srv, char* label);

/**
 * Delete the backup directories of a server that have no backup.info.
 * Only safe when no backup or restore can be running
 * @param srv The server index
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_delete_backups_without_metadata(int srv);

/**
 * Delete the incomplete backup of a backup process that died,
 * then release the repository lock
 * @param argv The argv
 * @param srv The server index
 */
void
pgmoneta_delete_failed_backup(char** argv, int srv);

#ifdef __cplusplus
}
#endif

#endif
