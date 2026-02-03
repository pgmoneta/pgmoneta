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

#ifndef PGMONETA_SERVER_H
#define PGMONETA_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>

#include <stdlib.h>
#include <time.h>

/**
 * Stores the stats of a file
 */
struct file_stats
{
   size_t size;            /**< The size of the file */
   bool is_dir;            /**< Is the file a directory */
   struct tm timetamps[4]; /**< array of timestamps in the order: access time, modification time, change time and creation time */
};

/**
 * Store the contents of label file returned from backup stop
 *
 * Format of label file column (returned from pg_backup_stop)
 * - START WAL LOCATION: start_wal_lsn (file wal_segment_name)
 * - CHECKPOINT LOCATION: checkpoint_lsn
 * - BACKUP METHOD: method
 * - BACKUP FROM: server_name
 * - START TIME: year-month-day hour:minutes:seconds time_zone
 * - LABEL: label
 * - START TIMELINE: timeline
 *
 * Since we have already captured start wal location during pg_bacup_start(), we won't trouble
 * ourselves parsing it here.
 *
 */
struct label_file_contents
{
   char checkpoint_lsn[MISC_LENGTH]; /**< The checkpoint location */
   char backup_method[MISC_LENGTH];  /**< The backup method */
   char backup_from[MISC_LENGTH];    /**< The backup server */
   struct tm start_time;             /**< The start time */
   char label[MISC_LENGTH];          /**< The label */
   uint32_t start_tli;               /**< The start timeline */
};

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
 * Is the  server online
 * @param srv The server index
 * @return True if online, otherwise false
 */
bool
pgmoneta_server_is_online(int srv);

/**
 * Set the server online state
 * @param srv The server index
 * @param v The state
 */
void
pgmoneta_server_set_online(int srv, bool v);

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

/**
 * Force a checkpoint
 * @param srv The server index
 * @param ssl The SSL connection
 * @param socket The socket
 * @param [out] chkpt_lsn The corresponding checkpoint LSN
 * @param [out] tli The timeline ID
 * @return return 0 if success, otherwise failure
 */
int
pgmoneta_server_checkpoint(int srv, SSL* ssl, int socket, uint64_t* chkpt_lsn, uint32_t* tli);

/**
 * Fetch metadata of a file
 * @param srv The server index
 * @param ssl The SSL connectiom
 * @param socket The socket
 * @param relative_file_path The relative path of the relation file inside the data cluster
 * @param file_stat [out] The file stat
 * @return return 0 if success, otherwise failure
 */
int
pgmoneta_server_file_stat(int srv, SSL* ssl, int socket, char* relative_file_path, struct file_stats* stat);

/**
 * Start the backup
 *
 * @param srv The server index
 * @param ssl The SSL connection
 * @param socket The socket
 * @param label The backup label
 * @param lsn [out] The start backup lsn
 * @return return 0 if success, otherwise failure
 */
int
pgmoneta_server_start_backup(int srv, SSL* ssl, int socket, char* label, char** lsn);

/**
 * Stop the backup
 *
 * @param srv The server index
 * @param ssl The SSL connection
 * @param socket The socket
 * @param bl_dir The directory where the file must be generated
 * @param lsn [out] The stop backup lsn
 * @param lf [out] The label file contents
 * @return return 0 if success, otherwise failure
 */
int
pgmoneta_server_stop_backup(int srv, SSL* ssl, int socket, char* bl_dir, char** lsn, struct label_file_contents* lf);

/**
 * Get the size of a database
 * @param srv The server index
 * @param ssl The SSL connection
 * @param socket The socket
 * @param database The database name
 * @param size [out] The size
 * @return return 0 if success, otherwise failure
 */
int
pgmoneta_server_database_size(int srv, SSL* ssl, int socket, char* database, uint64_t* size);

#ifdef __cplusplus
}
#endif

#endif
