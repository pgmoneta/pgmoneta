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
#define INFO_BIGGEST_FILE              "BIGGEST_FILE"
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
#define INFO_TYPE                      "TYPE"
#define INFO_PARENT                    "PARENT"

#define TYPE_FULL        0
#define TYPE_INCREMENTAL 1

#define VALID_UNKNOWN -1
#define VALID_FALSE    0
#define VALID_TRUE     1

#define INCREMENTAL_MAGIC 0xd3ae1f0d
#define INCREMENTAL_PREFIX_LENGTH (sizeof(INCREMENTAL_PREFIX) - 1)
#define MANIFEST_FILES "Files"

#define INFO_BUFFER_SIZE 8192

/**
 * @struct rfile
 * An rfile stores the metadata we need to use a file on disk for reconstruction.
 * For full backup file in the chain, only file name and file pointer are initialized.
 *
 * extracted flag indicates if the file is a copy extracted from the original file
 * num_blocks is the number of blocks present inside an incremental file.
 * These are the blocks that have changed since the last checkpoint.
 * truncation_block_length is basically the shortest length this file has been between this and last checkpoint.
 * Note that truncation_block_length could be even greater than the number of blocks the original file has.
 * Because the tables are not locked during the backup, so blocks could be truncated during the process,
 * while truncation_block_length only reflects length until the checkpoint before backup starts.
 * relative_block_numbers are the relative BlockNumber of each block in the file. Relative here means relative to
 * the starting BlockNumber of this file.
 */
struct rfile
{
   char* filepath;                     /**< The path of the backup file  */
   FILE* fp;                           /**< The file descriptor corresponding to the backup file */
   size_t header_length;               /**< The header length */
   uint32_t num_blocks;                /**< The number of blocks present inside an incremental file */
   uint32_t* relative_block_numbers;   /**< relative_block_numbers are the relative BlockNumber of each block in the file */
   uint32_t truncation_block_length;   /**< truncation_block_length only reflects length until the checkpoint before backup starts. */
};

/** @struct backup
 * Defines a backup
 */
struct backup
{
   char label[MISC_LENGTH];                                       /**< The label of the backup */
   char wal[MISC_LENGTH];                                         /**< The name of the WAL file */
   uint64_t backup_size;                                          /**< The backup size */
   uint64_t restore_size;                                         /**< The restore size */
   uint64_t biggest_file_size;                                    /**< The biggest file */
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
   int compression;                                               /**< The compression type */
   int encryption;                                                /**< The encryption type */
   char comments[MAX_COMMENT];                                    /**< The comments */
   char extra[MAX_EXTRA_PATH];                                    /**< The extra directory */
   int type;                                                      /**< The backup type */
   char parent_label[MISC_LENGTH];                                /**< The label of backup's parent, only used when backup is incremental */
} __attribute__ ((aligned (64)));

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
 * Get the backups
 * @param directory The directory
 * @param number_of_backups The number of backups
 * @param backups The backups
 * @return The result
 */
int
pgmoneta_load_infos(char* directory, int* number_of_backups, struct backup*** backups);

/**
 * Get a backup
 * @param directory The directory
 * @param identifier The identifier
 * @param backup The backup
 * @return The result
 */
int
pgmoneta_load_info(char* directory, char* identifier, struct backup** backup);

/**
 * Get the number of valid backups
 * @param i The server
 * @return The result
 */
int
pgmoneta_get_number_of_valid_backups(int server);

/**
 * Get the parent for a backup
 * @param server The server
 * @param backup The backup
 * @param parent The parent
 * @return The result
 */
int
pgmoneta_get_backup_parent(int server, struct backup* backup, struct backup** parent);

/**
 * Get the root for a backup
 * @param server The server
 * @param backup The backup
 * @param root The root
 * @return The result
 */
int
pgmoneta_get_backup_root(int server, struct backup* backup, struct backup** root);

/**
 * Get the child for a backup
 * @param server The server
 * @param backup The backup
 * @param child The child
 * @return The result
 */
int
pgmoneta_get_backup_child(int server, struct backup* backup, struct backup** child);

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

/**
 * Save backup information
 * @param directory The backup directory
 * @param backup The backup
 */
int
pgmoneta_save_info(char* directory, struct backup* backup);

/**
 * Create an rfile structure of a backup file
 * @param server The server
 * @param label The label of the backup
 * @param relative_dir The relative path inside the data directory (excluding the filename)
 * @param base_file_name The file name
 * @param encryption The encryption method
 * @param compression The compression method
 * @param rfile [out] The rfile
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_rfile_create(int server, char* label, char* relative_dir, char* base_file_name, int encryption, int compression, struct rfile** rfile);

/**
 * Destroy the rfile structure
 * @param rfile The rfile to be destroyed
 */
void
pgmoneta_rfile_destroy(struct rfile* rf);

/**
 * Initialize an rfile structure of an incremental file by reading the incremental file headers
 * @param server The server
 * @param label The label of the backup
 * @param relative_dir The relative path inside the data directory (excluding the filename)
 * @param base_file_name The file name
 * @param encryption The encryption method
 * @param compression The compression method
 * @param rfile [out] The rfile
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_incremental_rfile_initialize(int server, char* label, char* relative_dir, char* base_file_name, int encryption, int compression, struct rfile** rfile);

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
 * Get an approximate size of a backup repository
 * The goal is to iterate over all file entries in the manifest. If an entry represents an incremental
 * file, retrieve its block_length using the file's truncated_block_length (which indicates the total
 * number of blocks in the fully restored file). For non-incremental files, simply use the size value
 * directly from the file entry in the manifest.
 * @param server The server
 * @param label The label
 * @param size [out] The size of the incremental backup
 * @param biggest_file_size [out] The size of the biggest file in the incremental backup
 * @return 0 on success, 1 if otherwise
 */
int
pgmoneta_backup_size(int server, char* label, unsigned long* size, uint64_t* biggest_file_size);

/**
 * Sort the backup array by its label
 * @param backups The backups
 * @param number_of_backups The number of backups
 * @param desc Sort in descending order
 * @return 0 on success, 1 if otherwise
 */
int
pgmoneta_sort_backups(struct backup** backups, int number_of_backups, bool desc);

#ifdef __cplusplus
}
#endif

#endif
