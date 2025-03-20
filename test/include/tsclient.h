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

#include <json.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 8192

#define PGMONETA_LOG_FILE_TRAIL      "/log/pgmoneta.log"
#define PGMONETA_EXECUTABLE_TRAIL    "/src/pgmoneta-cli"
#define PGMONETA_CONFIGURATION_TRAIL "/pgmoneta-testsuite/conf/pgmoneta.conf"
#define PGMONETA_RESTORE_TRAIL       "/pgmoneta-testsuite/restore/"

extern char project_directory[BUFFER_SIZE];

/**
 * Initialize the tsclient API
 * @param base_dir path to base
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_init(char* base_dir);

/**
 * Destroy the tsclient (must be used after pgmoneta_tsclient_init)
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_destroy();

/**
 * Execute backup command on the server
 * @param socket the value of socket corresponding to the main server
 * @param server the server to perform backup on
 * @param incremental execute backup in incremental mode
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tsclient_execute_backup(char* server, char* incremental);

/**
 * Execute restore command on the server
 * @param socket the value of socket corresponding to the main server
 * @param server the server to perform restore on
 * @param backup_id the backup_id to perform restore on
 * @param position the position parameters
 * @return 0 upon success, otherwise 
 */
int
pgmoneta_tsclient_execute_restore(char* server, char* backup_id, char* position);

/**
 * Execute delete command on the server
 * @param socket the value of socket corresponding to the main server
 * @param server the server to perform delete on
 * @param backup_id the backup_id to delete
 * @return 0 upon success, otherwise 
 */
int
pgmoneta_tsclient_execute_delete(char* server, char* backup_id);

#ifdef __cplusplus
}
#endif

#endif