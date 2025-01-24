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

#ifndef PGMONETA_INFO_H
#define PGMONETA_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

/* pgmoneta */
#include <pgmoneta.h>
#include <json.h>

/* system */
#include <stdlib.h>

#define INFO_PGMONETA_VERSION          "PGMONETA_VERSION"
#define INFO_BACKUP                    "BACKUP"
#define INFO_CHKPT_WALPOS              "CHKPT_WALPOS"
#define INFO_COMMENTS                  "COMMENTS"
#define INFO_COMPRESSION               "COMPRESSION"
#define INFO_ELAPSED                   "ELAPSED"
#define INFO_BASEBACKUP_ELAPSED        "BASEBACKUP_ELAPSED"
#define INFO_MANIFEST_ELAPSED          "MANIFEST_ELAPSED"
#define INFO_COMPRESSION_ZSTD_ELAPSED  "COMPRESSION_ZSTD_ELAPSED"
#define INFO_COMPRESSION_GZIP_ELAPSED  "COMPRESSION_GZIP_ELAPSED"
#define INFO_COMPRESSION_BZIP2_ELAPSED "COMPRESSION_BZIP2_ELAPSED"
#define INFO_COMPRESSION_LZ4_ELAPSED   "COMPRESSION_LZ4_ELAPSED"
#define INFO_ENCRYPTION_ELAPSED        "ENCRYPTION_ELAPSED"
#define INFO_LINKING_ELAPSED           "LINKING_ELAPSED"
#define INFO_REMOTE_SSH_ELAPSED        "REMOTE_SSH_ELAPSED"
#define INFO_REMOTE_S3_ELAPSED         "REMOTE_S3_ELAPSED"
#define INFO_REMOTE_AZURE_ELAPSED      "REMOTE_AZURE_ELAPSED"
#define INFO_ENCRYPTION                "ENCRYPTION"
#define INFO_END_TIMELINE              "END_TIMELINE"
#define INFO_END_WALPOS                "END_WALPOS"
#define INFO_EXTRA                     "EXTRA"
#define INFO_HASH_ALGORITHM            "HASH_ALGORITHM"
#define INFO_KEEP                      "KEEP"
#define INFO_LABEL                     "LABEL"
#define INFO_MAJOR_VERSION             "MAJOR_VERSION"
#define INFO_MINOR_VERSION             "MINOR_VERSION"
#define INFO_RESTORE                   "RESTORE"
#define INFO_START_TIMELINE            "START_TIMELINE"
#define INFO_START_WALPOS              "START_WALPOS"
#define INFO_STATUS                    "STATUS"
#define INFO_TABLESPACES               "TABLESPACES"
#define INFO_WAL                       "WAL"

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
   double total_elapsed_time;                                     /**< The total elapsed time in seconds */
   double basebackup_elapsed_time;                                /**< The basebackup elapsed time in seconds */
   double manifest_elapsed_time;                                  /**< The manifest elapsed time in seconds */
   double compression_gzip_elapsed_time;                          /**< The compression elapsed time in seconds */
   double compression_zstd_elapsed_time;                          /**< The compression elapsed time in seconds */
   double compression_lz4_elapsed_time;                           /**< The compression elapsed time in seconds */
   double compression_bzip2_elapsed_time;                         /**< The compression elapsed time in seconds */
   double encryption_elapsed_time;                                /**< The encryption elapsed time in seconds */
   double linking_elapsed_time;                                   /**< The linking elapsed time in seconds */
   double remote_ssh_elapsed_time;                                /**< The remote ssh elapsed time in seconds */
   double remote_s3_elapsed_time;                                 /**< The remote s3 elapsed time in seconds */
   double remote_azure_elapsed_time;                              /**< The remote azure elapsed time in seconds */
   int32_t major_version;                                         /**< The major version */
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
   int hash_algorithm;                                            /**< The hash algorithm for the manifest */
   int compression;                                               /**< The compression type */
   int encryption;                                                /**< The encryption type */
   char comments[MAX_COMMENT];                                    /**< The comments */
   char extra[MAX_EXTRA_PATH];                                    /**< The extra directory */
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
 * Update backup information: float
 * @param directory The backup directory
 * @param key The key
 * @param value The value
 */
void
pgmoneta_update_info_double(char* directory, char* key, double value);

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
 * @param server The server
 * @param backup The backup
 * @param action The action (add / remove)
 * @param key The key
 * @param comment The comment
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_update_info_annotate(int server, struct backup* backup, char* action, char* key, char* comment);

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
 * @param server The server
 * @param identifier The identifier
 * @param backup The backup
 * @return The result
 */
int
pgmoneta_get_backup_server(int server, char* identifier, struct backup** backup);

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

/**
 * Create an info request
 * @param ssl The SSL connection
 * @param client_fd The client
 * @param server The server
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The payload
 */
void
pgmoneta_info_request(SSL* ssl, int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * Create an annotate request
 * @param ssl The SSL connection
 * @param client_fd The client
 * @param server The server
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The payload
 */
void
pgmoneta_annotate_request(SSL* ssl, int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* payload);

#ifdef __cplusplus
}
#endif

#endif
