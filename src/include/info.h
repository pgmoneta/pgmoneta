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

#ifndef PGMONETA_INFO_H
#define PGMONETA_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

/* pgmoneta */
#include <pgmoneta.h>

/* system */
#include <stdlib.h>

#define INFO_PGMONETA_VERSION "PGMONETA_VERSION"
#define INFO_STATUS           "STATUS"
#define INFO_LABEL            "LABEL"
#define INFO_WAL              "WAL"
#define INFO_ELAPSED          "ELAPSED"
#define INFO_VERSION          "VERSION"
#define INFO_MINOR_VERSION    "MINOR_VERSION"
#define INFO_KEEP             "KEEP"
#define INFO_BACKUP           "BACKUP"
#define INFO_RESTORE          "RESTORE"
#define INFO_TABLESPACES      "TABLESPACES"
#define INFO_START_WALPOS     "START_WALPOS"
#define INFO_END_WALPOS       "END_WALPOS"
#define INFO_CHKPT_WALPOS     "CHKPT_WALPOS"
#define INFO_START_TIMELINE   "START_TIMELINE"
#define INFO_END_TIMELINE     "END_TIMELINE"
#define INFO_HASH_ALGORITHM   "HASH_ALGORITM"
#define INFO_COMMENTS         "COMMENTS"

#define VALID_UNKNOWN -1
#define VALID_FALSE    0
#define VALID_TRUE     1

/** @struct backup
 * Defines a backup
 */
struct backup
{
   char label[MISC_LENGTH];                                       /**< The label of the backup */
   char wal[MISC_LENGTH];                                         /**< The name of the WAL file */
   uint64_t backup_size;                                          /**< The backup size */
   uint64_t restore_size;                                         /**< The restore size */
   int32_t elapsed_time;                                          /**< The elapsed time in seconds */
   int32_t version;                                               /**< The version */
   int32_t minor_version;                                         /**< The minor version */
   bool keep;                                                     /**< Keep the backup */
   char valid;                                                    /**< Is the backup valid */
   uint64_t number_of_tablespaces;                                /**< The number of tablespaces */
   char tablespaces[MAX_NUMBER_OF_TABLESPACES][MISC_LENGTH];      /**< The names of the tablespaces */
   char tablespaces_oids[MAX_NUMBER_OF_TABLESPACES][MISC_LENGTH]; /**< The OIDs of the tablespaces */
   char tablespaces_paths[MAX_NUMBER_OF_TABLESPACES][MAX_PATH];   /**< The paths of the tablespaces */
   uint32_t start_lsn_hi32;                                       /**< The high 32 bits of WAL starting position of the backup */
   uint32_t start_lsn_lo32;                                       /**< The low 32 bits of WAL starting position of the backup */
   uint32_t end_lsn_hi32;                                         /**< The high 32 bits of WAL ending position of the backup */
   uint32_t end_lsn_lo32;                                         /**< The low 32 bits of WAL ending position of the backup */
   uint32_t checkpoint_lsn_hi32;                                  /**< The high 32 bits of WAL checkpoint position of the backup */
   uint32_t checkpoint_lsn_lo32;                                  /**< The low 32 bits of WAL checkpoint position of the backup */
   uint32_t start_timeline;                                       /**< The starting timeline of the backup */
   uint32_t end_timeline;                                         /**< The ending timeline of the backup */
   int hash_algoritm;                                             /**< The hash algoritm for the manifest */
   char comments[MAX_COMMENT];                                    /**< The comments */
} __attribute__ ((aligned (64)));

/**
 * Create a backup information file
 * @param directory The backup directory
 * @param label The label
 * @param status The status
 */
void
pgmoneta_create_info(char* directory, char* label, int status);

/**
 * Update backup information: unsigned long
 * @param directory The backup directory
 * @param key The key
 * @param value The value
 */
void
pgmoneta_update_info_unsigned_long(char* directory, char* key, unsigned long value);

/**
 * Update backup information: string
 * @param directory The backup directory
 * @param key The key
 * @param value The value
 */
void
pgmoneta_update_info_string(char* directory, char* key, char* value);

/**
 * Update backup information: bool
 * @param directory The backup directory
 * @param key The key
 * @param value The value
 */
void
pgmoneta_update_info_bool(char* directory, char* key, bool value);

/**
 * Update backup information: annotate
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server
 * @param backup The backup label
 * @param command The command (add / remove)
 * @param key The key
 * @param comment The comment
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_update_info_annotate(SSL* ssl, int socket, char* server, char* backup, char* command, char* key, char* comment);

/**
 * Get a backup string value
 * @param backup The backup
 * @param key The key
 * @param value The value
 * @return The result
 */
int
pgmoneta_get_info_string(struct backup* backup, char* key, char** value);

/**
 * Get the backups
 * @param directory The directory
 * @param number_of_backups The number of backups
 * @param backups The backups
 * @return The result
 */
int
pgmoneta_get_backups(char* directory, int* number_of_backups, struct backup*** backups);

/**
 * Get a backup
 * @param directory The directory
 * @param label The label
 * @param backup The backup
 * @return The result
 */
int
pgmoneta_get_backup(char* directory, char* label, struct backup** backup);

/**
 * Get a backup
 * @param fn The file name
 * @param backup The backup
 * @return The result
 */
int
pgmoneta_get_backup_file(char* fn, struct backup** backup);

/**
 * Get the number of valid backups
 * @param i The server
 * @return The result
 */
int
pgmoneta_get_number_of_valid_backups(int server);

#ifdef __cplusplus
}
#endif

#endif
