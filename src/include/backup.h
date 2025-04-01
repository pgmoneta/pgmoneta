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

#ifndef PGMONETA_BACKUP_H
#define PGMONETA_BACKUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>
#include <json.h>

#include <ev.h>
#include <stdlib.h>

/**
 * Create a backup
 * @param client_fd The client
 * @param server The server
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The payload
 */
void
pgmoneta_backup(int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * List backups for a server
 * @param client_fd The client
 * @param server The server
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The payload
 */
void
pgmoneta_list_backup(int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * Delete a backup for a server
 * @param client_fd The client
 * @param server The server
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The payload
 */
void
pgmoneta_delete_backup(int client_fd, int srv, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * Get the backup max rate for a server
 * @param server The server
 * @return The backup max rate
 */
int
pgmoneta_get_backup_max_rate(int server);

struct backup_context {
    char* base_path;          // Path for backup
    int total_wal_files;      // Total WAL files to process
    int processed_wal_files;  // Current count of processed WAL files
    time_t start_time;        // Start time for ETA calculation
};

bool perform_backup(struct backup_context* ctx);
/**
 * Extract a file from a backup
 * @param server The server
 * @param label The label
 * @param relative_file_path The file path relative to the backup data directory
 * @param target_directory The target root directory
 * @param target_file The target file
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_extract_backup_file(int server, char* label, char* relative_file_path, char* target_directory, char** target_file);

/**
 * Strip the compression/encryption suffix from a file path
 * @param file The file path
 * @param basename [out] The base file path with its compression/encryption suffix stripped,
 * or a copy of the original path if it has none of the suffix
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_file_basename(char* file, char** basename);

#ifdef __cplusplus
}
#endif

#endif
