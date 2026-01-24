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
 *
 */

#ifndef PGMONETA_TSCLIENT_H
#define PGMONETA_TSCLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>
#include <brt.h>
#include <json.h>
#include <walfile/wal_reader.h>

/**
 * Execute backup command on the server
 * @param server the server to perform backup on
 * @param incremental execute backup in incremental mode
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_backup(char* server, char* incremental, int expected_error);

/**
 * Execute list-backup command on the server
 * @param server the server
 * @param sort_order the sort order
 * @param response optional output for JSON response (NULL to ignore)
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_list_backup(char* server, char* sort_order, struct json** response, int expected_error);

/**
 * Execute restore command on the server
 * @param server the server to perform restore on
 * @param backup_id the backup_id to perform restore on
 * @param position the position parameters
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_restore(char* server, char* backup_id, char* position, int expected_error);

/**
 * Execute verify command on the server
 * @param server the server
 * @param backup_id the backup
 * @param directory the directory
 * @param files the files
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_verify(char* server, char* backup_id, char* directory, char* files, int expected_error);

/**
 * Execute archive command on the server
 * @param server the server
 * @param backup_id the backup
 * @param position the position
 * @param directory the directory
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_archive(char* server, char* backup_id, char* position, char* directory, int expected_error);

/**
 * Execute delete command on the server
 * @param server the server to perform delete on
 * @param backup_id the backup_id to delete
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_delete(char* server, char* backup_id, int expected_error);

/**
 * Execute force delete command on the server
 * @param server the server to perform delete on
 * @param backup_id the backup_id to delete
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_force_delete(char* server, char* backup_id, int expected_error);

/**
 * Execute retain command on the server
 * @param server the server
 * @param backup_id the backup
 * @param cascade cascade
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_retain(char* server, char* backup_id, bool cascade, int expected_error);

/**
 * Execute expunge command on the server
 * @param server the server
 * @param backup_id the backup
 * @param cascade cascade
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_expunge(char* server, char* backup_id, bool cascade, int expected_error);

/**
 * Execute decrypt command on the server
 * @param path the path
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_decrypt(char* path, int expected_error);

/**
 * Execute encrypt command on the server
 * @param path the path
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_encrypt(char* path, int expected_error);

/**
 * Execute decompress command on the server
 * @param path the path
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_decompress(char* path, int expected_error);

/**
 * Execute compress command on the server
 * @param path the path
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_compress(char* path, int expected_error);

/**
 * Execute ping command on the server
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_ping(int expected_error);

/**
 * Execute shutdown command on the server
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_shutdown(int expected_error);

/**
 * Execute status command on the server
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_status(int expected_error);

/**
 * Execute status details command on the server
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_status_details(int expected_error);

/**
 * Execute reload command on the server
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_reload(int expected_error);

/**
 * Execute reset command on the server
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_reset(int expected_error);

/**
 * Execute conf ls command on the server
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_conf_ls(int expected_error);

/**
 * Execute conf get command on the server
 * @param config_key the key
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_conf_get(char* config_key, int expected_error);

/**
 * Execute conf set command on the server
 * @param config_key the key
 * @param config_value the value
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_conf_set(char* config_key, char* config_value, int expected_error);

/**
 * Execute info command on the server
 * @param server the server
 * @param backup_id the backup
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_info(char* server, char* backup_id, int expected_error);

/**
 * Execute annotate command on the server
 * @param server the server
 * @param backup_id the backup
 * @param action the action
 * @param key the key
 * @param comment the comment
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_annotate(char* server, char* backup_id, char* action, char* key, char* comment, int expected_error);

/**
 * Execute mode command on the server
 * @param server the server
 * @param action the action
 * @param expected_error expected error code
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_mode(char* server, char* action, int expected_error);

#ifdef __cplusplus
}
#endif

#endif
