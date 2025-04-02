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

#ifndef PGMONETA_RESTORE_H
#define PGMONETA_RESTORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <art.h>
#include <deque.h>
#include <info.h>
#include <json.h>

#include <stdbool.h>
#include <stdlib.h>

/**
 * Fill the passed arugment with the last files names to restore
 * @param output The string array that will be filled with the last files names to restore
 * @return integer showing the status of the operation
 */
int
pgmoneta_get_restore_last_files_names(char*** output);

/**
 * Is the file part of the restore last chain
 * @param file_name The file name
 * @return True if part of the chain, otherwise false
 */
bool
pgmoneta_is_restore_last_name(char* file_name);

/**
 * Create a restore
 * @param ssl The SSL connection
 * @param client_fd The client
 * @param server The server
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param request The request
 */
void
pgmoneta_restore(SSL* ssl, int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* request);

/**
 * Restore to a directory
 * @param nodes The nodes
 * @return The result
 */
int
pgmoneta_restore_backup(struct art* nodes);

/**
 * Combine the provided backups
 * @param server The server
 * @param label The label of the current backup to combine
 * @param base The base directory that contains data and tablespaces
 * @param input_dir The base directory of the current input incremental backup
 * @param output_dir The base directory of the output incremental backup
 * (the last level of directory should not be followed by back slash)
 * @param prior_labels The labels of prior incremental/full backups, from newest to oldest
 * @param bck The backup to be restored
 * @param manifest The manifest of the incremental backup to be combined
 * @param incremental Whether to combine the backups into an incremental backup
 * @param combine_as_is Whether to alter the resulting backup
 * @return 0 on success, 1 if otherwise
 */
int
pgmoneta_combine_backups(int server, char* label, char* base, char* input_dir, char* output_dir, struct deque* prior_labels,
                         struct backup* bck, struct json* manifest, bool incremental, bool combine_as_is);

/**
 * Rollup backups into a new backup
 * @param server The server
 * @param newest_label The newest backup label
 * @param oldest_label The oldest backup label
 * @return
 */
int
pgmoneta_roll_up(int server, char* newest_label, char* oldest_label);

#ifdef __cplusplus
}
#endif

#endif
