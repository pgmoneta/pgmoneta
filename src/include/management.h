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

#ifndef PGMONETA_MANAGEMENT_H
#define PGMONETA_MANAGEMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>
#include <deque.h>
#include <json.h>

#include <stdbool.h>
#include <stdlib.h>

#include <openssl/ssl.h>

/**
 * Management header
 */
#define MANAGEMENT_COMPRESSION_NONE     0
#define MANAGEMENT_COMPRESSION_GZIP     1
#define MANAGEMENT_COMPRESSION_ZSTD     2
#define MANAGEMENT_COMPRESSION_LZ4      3
#define MANAGEMENT_COMPRESSION_BZIP2    4

#define MANAGEMENT_ENCRYPTION_NONE      0
#define MANAGEMENT_ENCRYPTION_AES256    1
#define MANAGEMENT_ENCRYPTION_AES192    2
#define MANAGEMENT_ENCRYPTION_AES128    3

/**
 * Management commands
 */
#define MANAGEMENT_BACKUP          1
#define MANAGEMENT_LIST_BACKUP     2
#define MANAGEMENT_RESTORE         3
#define MANAGEMENT_ARCHIVE         4
#define MANAGEMENT_DELETE          5
#define MANAGEMENT_SHUTDOWN        6
#define MANAGEMENT_STATUS          7
#define MANAGEMENT_STATUS_DETAILS  8
#define MANAGEMENT_PING            9
#define MANAGEMENT_RESET          10
#define MANAGEMENT_RELOAD         11
#define MANAGEMENT_RETAIN         12
#define MANAGEMENT_EXPUNGE        13
#define MANAGEMENT_DECRYPT        14
#define MANAGEMENT_ENCRYPT        15
#define MANAGEMENT_DECOMPRESS     16
#define MANAGEMENT_COMPRESS       17
#define MANAGEMENT_INFO           18
#define MANAGEMENT_VERIFY         19
#define MANAGEMENT_ANNOTATE       20
#define MANAGEMENT_CONF_LS        21
#define MANAGEMENT_CONF_GET       22
#define MANAGEMENT_CONF_SET       23

/**
 * Management categories
 */
#define MANAGEMENT_CATEGORY_HEADER   "Header"
#define MANAGEMENT_CATEGORY_REQUEST  "Request"
#define MANAGEMENT_CATEGORY_RESPONSE "Response"
#define MANAGEMENT_CATEGORY_OUTCOME  "Outcome"

/**
 * Management arguments
 */
#define MANAGEMENT_ARGUMENT_ACTION                "Action"
#define MANAGEMENT_ARGUMENT_ALL                   "All"
#define MANAGEMENT_ARGUMENT_BACKUP                "Backup"
#define MANAGEMENT_ARGUMENT_BACKUPS               "Backups"
#define MANAGEMENT_ARGUMENT_BACKUP_SIZE           "BackupSize"
#define MANAGEMENT_ARGUMENT_CALCULATED            "Calculated"
#define MANAGEMENT_ARGUMENT_CHECKPOINT_HILSN      "CheckpointHiLSN"
#define MANAGEMENT_ARGUMENT_CHECKPOINT_LOLSN      "CheckpointLoLSN"
#define MANAGEMENT_ARGUMENT_CHECKSUMS             "Checksums"
#define MANAGEMENT_ARGUMENT_CLIENT_VERSION        "ClientVersion"
#define MANAGEMENT_ARGUMENT_COMMAND               "Command"
#define MANAGEMENT_ARGUMENT_COMMENT               "Comment"
#define MANAGEMENT_ARGUMENT_COMMENTS              "Comments"
#define MANAGEMENT_ARGUMENT_COMPRESSION           "Compression"
#define MANAGEMENT_ARGUMENT_COMPRESSION           "Compression"
#define MANAGEMENT_ARGUMENT_CONFIG_KEY            "ConfigKey"
#define MANAGEMENT_ARGUMENT_CONFIG_VALUE          "ConfigValue"
#define MANAGEMENT_ARGUMENT_DELTA                 "Delta"
#define MANAGEMENT_ARGUMENT_DESTINATION_FILE      "DestinationFile"
#define MANAGEMENT_ARGUMENT_DIRECTORY             "Directory"
#define MANAGEMENT_ARGUMENT_ELAPSED               "Elapsed"
#define MANAGEMENT_ARGUMENT_ENCRYPTION            "Encryption"
#define MANAGEMENT_ARGUMENT_ENCRYPTION            "Encryption"
#define MANAGEMENT_ARGUMENT_END_HILSN             "EndHiLSN"
#define MANAGEMENT_ARGUMENT_END_LOLSN             "EndLoLSN"
#define MANAGEMENT_ARGUMENT_END_TIMELINE          "EndTimeline"
#define MANAGEMENT_ARGUMENT_ERROR                 "Error"
#define MANAGEMENT_ARGUMENT_FAILED                "Failed"
#define MANAGEMENT_ARGUMENT_FILENAME              "FileName"
#define MANAGEMENT_ARGUMENT_FILES                 "Files"
#define MANAGEMENT_ARGUMENT_FREE_SPACE            "FreeSpace"
#define MANAGEMENT_ARGUMENT_HASH_ALGORITHM        "HashAlgorithm"
#define MANAGEMENT_ARGUMENT_HOT_STANDBY_SIZE      "HotStandbySize"
#define MANAGEMENT_ARGUMENT_KEEP                  "Keep"
#define MANAGEMENT_ARGUMENT_KEY                   "Key"
#define MANAGEMENT_ARGUMENT_MAJOR_VERSION         "MajorVersion"
#define MANAGEMENT_ARGUMENT_MINOR_VERSION         "MinorVersion"
#define MANAGEMENT_ARGUMENT_NUMBER_OF_BACKUPS     "NumberOfBackups"
#define MANAGEMENT_ARGUMENT_NUMBER_OF_SERVERS     "NumberOfServers"
#define MANAGEMENT_ARGUMENT_NUMBER_OF_TABLESPACES "NumberOfTablespaces"
#define MANAGEMENT_ARGUMENT_OFFLINE               "Offline"
#define MANAGEMENT_ARGUMENT_ORIGINAL              "Original"
#define MANAGEMENT_ARGUMENT_OUTPUT                "Output"
#define MANAGEMENT_ARGUMENT_POSITION              "Position"
#define MANAGEMENT_ARGUMENT_RESTART               "Restart"
#define MANAGEMENT_ARGUMENT_RESTORE_SIZE          "RestoreSize"
#define MANAGEMENT_ARGUMENT_RETENTION_DAYS        "RetentionDays"
#define MANAGEMENT_ARGUMENT_RETENTION_MONTHS      "RetentionMonths"
#define MANAGEMENT_ARGUMENT_RETENTION_WEEKS       "RetentionWeeks"
#define MANAGEMENT_ARGUMENT_RETENTION_YEARS       "RetentionYears"
#define MANAGEMENT_ARGUMENT_SERVER                "Server"
#define MANAGEMENT_ARGUMENT_SERVERS               "Servers"
#define MANAGEMENT_ARGUMENT_SERVER_SIZE           "ServerSize"
#define MANAGEMENT_ARGUMENT_SERVER_VERSION        "ServerVersion"
#define MANAGEMENT_ARGUMENT_SOURCE_FILE           "SourceFile"
#define MANAGEMENT_ARGUMENT_START_HILSN           "StartHiLSN"
#define MANAGEMENT_ARGUMENT_START_LOLSN           "StartLoLSN"
#define MANAGEMENT_ARGUMENT_START_TIMELINE        "StartTimeline"
#define MANAGEMENT_ARGUMENT_STATUS                "Status"
#define MANAGEMENT_ARGUMENT_TABLESPACE            "Tablespace"
#define MANAGEMENT_ARGUMENT_TABLESPACES           "Tablespaces"
#define MANAGEMENT_ARGUMENT_TABLESPACE_NAME       "TablespaceName"
#define MANAGEMENT_ARGUMENT_TIME                  "Time"
#define MANAGEMENT_ARGUMENT_TIMESTAMP             "Timestamp"
#define MANAGEMENT_ARGUMENT_TOTAL_SPACE           "TotalSpace"
#define MANAGEMENT_ARGUMENT_USED_SPACE            "UsedSpace"
#define MANAGEMENT_ARGUMENT_VALID                 "Valid"
#define MANAGEMENT_ARGUMENT_WAL                   "WAL"
#define MANAGEMENT_ARGUMENT_WORKERS               "Workers"

/**
 * Management error
 */
#define MANAGEMENT_ERROR_BAD_PAYLOAD     1
#define MANAGEMENT_ERROR_UNKNOWN_COMMAND 2
#define MANAGEMENT_ERROR_ALLOCATION      3

#define MANAGEMENT_ERROR_BACKUP_INVALID  100
#define MANAGEMENT_ERROR_BACKUP_WAL      101
#define MANAGEMENT_ERROR_BACKUP_ACTIVE   102
#define MANAGEMENT_ERROR_BACKUP_SETUP    103
#define MANAGEMENT_ERROR_BACKUP_EXECUTE  104
#define MANAGEMENT_ERROR_BACKUP_TEARDOWN 105
#define MANAGEMENT_ERROR_BACKUP_NETWORK  106
#define MANAGEMENT_ERROR_BACKUP_OFFLINE  107
#define MANAGEMENT_ERROR_BACKUP_NOSERVER 108
#define MANAGEMENT_ERROR_BACKUP_NOFORK   109
#define MANAGEMENT_ERROR_BACKUP_ERROR    110

#define MANAGEMENT_ERROR_LIST_BACKUP_DEQUE_CREATE 200
#define MANAGEMENT_ERROR_LIST_BACKUP_BACKUPS      201
#define MANAGEMENT_ERROR_LIST_BACKUP_JSON_VALUE   202
#define MANAGEMENT_ERROR_LIST_BACKUP_NETWORK      203
#define MANAGEMENT_ERROR_LIST_BACKUP_NOSERVER     204
#define MANAGEMENT_ERROR_LIST_BACKUP_NOFORK       205

#define MANAGEMENT_ERROR_DELETE_SETUP    300
#define MANAGEMENT_ERROR_DELETE_EXECUTE  301
#define MANAGEMENT_ERROR_DELETE_TEARDOWN 302
#define MANAGEMENT_ERROR_DELETE_NOSERVER 303
#define MANAGEMENT_ERROR_DELETE_NOFORK   304
#define MANAGEMENT_ERROR_DELETE_NETWORK  305
#define MANAGEMENT_ERROR_DELETE_ERROR    306

#define MANAGEMENT_ERROR_RESTORE_NOBACKUP 400
#define MANAGEMENT_ERROR_RESTORE_NOSERVER 401
#define MANAGEMENT_ERROR_RESTORE_NOFORK   402
#define MANAGEMENT_ERROR_RESTORE_NETWORK  403
#define MANAGEMENT_ERROR_RESTORE_ERROR    404

#define MANAGEMENT_ERROR_VERIFY_NOSERVER 500
#define MANAGEMENT_ERROR_VERIFY_NOFORK   501
#define MANAGEMENT_ERROR_VERIFY_NETWORK  502
#define MANAGEMENT_ERROR_VERIFY_ERROR    503

#define MANAGEMENT_ERROR_ARCHIVE_NOBACKUP 600
#define MANAGEMENT_ERROR_ARCHIVE_NOSERVER 601
#define MANAGEMENT_ERROR_ARCHIVE_NOFORK   602
#define MANAGEMENT_ERROR_ARCHIVE_NETWORK  603
#define MANAGEMENT_ERROR_ARCHIVE_ERROR    604

#define MANAGEMENT_ERROR_STATUS_NOFORK   700
#define MANAGEMENT_ERROR_STATUS_NETWORK  701

#define MANAGEMENT_ERROR_STATUS_DETAILS_NOFORK  800
#define MANAGEMENT_ERROR_STATUS_DETAILS_NETWORK 801

#define MANAGEMENT_ERROR_RETAIN_NOBACKUP 900
#define MANAGEMENT_ERROR_RETAIN_NOSERVER 901
#define MANAGEMENT_ERROR_RETAIN_NOFORK   902
#define MANAGEMENT_ERROR_RETAIN_NETWORK  903
#define MANAGEMENT_ERROR_RETAIN_ERROR    904

#define MANAGEMENT_ERROR_EXPUNGE_NOBACKUP 1000
#define MANAGEMENT_ERROR_EXPUNGE_NOSERVER 1001
#define MANAGEMENT_ERROR_EXPUNGE_NOFORK   1002
#define MANAGEMENT_ERROR_EXPUNGE_NETWORK  1003
#define MANAGEMENT_ERROR_EXPUNGE_ERROR    1004

#define MANAGEMENT_ERROR_DECRYPT_NOFILE  1100
#define MANAGEMENT_ERROR_DECRYPT_NOFORK  1101
#define MANAGEMENT_ERROR_DECRYPT_NETWORK 1102
#define MANAGEMENT_ERROR_DECRYPT_ERROR   1103

#define MANAGEMENT_ERROR_ENCRYPT_NOFILE  1200
#define MANAGEMENT_ERROR_ENCRYPT_NOFORK  1201
#define MANAGEMENT_ERROR_ENCRYPT_NETWORK 1202
#define MANAGEMENT_ERROR_ENCRYPT_ERROR   1203

#define MANAGEMENT_ERROR_GZIP_NOFILE  1300
#define MANAGEMENT_ERROR_GZIP_NOFORK  1301
#define MANAGEMENT_ERROR_GZIP_NETWORK 1302
#define MANAGEMENT_ERROR_GZIP_ERROR   1303

#define MANAGEMENT_ERROR_ZSTD_NOFILE  1400
#define MANAGEMENT_ERROR_ZSTD_NOFORK  1401
#define MANAGEMENT_ERROR_ZSTD_NETWORK 1402
#define MANAGEMENT_ERROR_ZSTD_ERROR   1403

#define MANAGEMENT_ERROR_LZ4_NOFILE  1500
#define MANAGEMENT_ERROR_LZ4_NOFORK  1501
#define MANAGEMENT_ERROR_LZ4_NETWORK 1502
#define MANAGEMENT_ERROR_LZ4_ERROR   1503

#define MANAGEMENT_ERROR_BZIP2_NOFILE  1600
#define MANAGEMENT_ERROR_BZIP2_NOFORK  1601
#define MANAGEMENT_ERROR_BZIP2_NETWORK 1602
#define MANAGEMENT_ERROR_BZIP2_ERROR   1603

#define MANAGEMENT_ERROR_DECOMPRESS_NOFORK  1700
#define MANAGEMENT_ERROR_DECOMPRESS_UNKNOWN 1701

#define MANAGEMENT_ERROR_COMPRESS_NOFORK  1800
#define MANAGEMENT_ERROR_COMPRESS_UNKNOWN 1801

#define MANAGEMENT_ERROR_INFO_NOBACKUP 1900
#define MANAGEMENT_ERROR_INFO_NOSERVER 1901
#define MANAGEMENT_ERROR_INFO_NOFORK   1902
#define MANAGEMENT_ERROR_INFO_NETWORK  1903
#define MANAGEMENT_ERROR_INFO_ERROR    1904

#define MANAGEMENT_ERROR_ANNOTATE_NOBACKUP 2000
#define MANAGEMENT_ERROR_ANNOTATE_NOSERVER 2001
#define MANAGEMENT_ERROR_ANNOTATE_NOFORK   2002
#define MANAGEMENT_ERROR_ANNOTATE_FAILED   2003
#define MANAGEMENT_ERROR_ANNOTATE_NETWORK  2004
#define MANAGEMENT_ERROR_ANNOTATE_ERROR    2005

#define MANAGEMENT_ERROR_CONF_GET_NOFORK  2100
#define MANAGEMENT_ERROR_CONF_GET_NETWORK 2102
#define MANAGEMENT_ERROR_CONF_GET_ERROR   2103

#define MANAGEMENT_ERROR_CONF_SET_NOFORK                    2200
#define MANAGEMENT_ERROR_CONF_SET_NOREQUEST                 2201
#define MANAGEMENT_ERROR_CONF_SET_NOCONFIG_KEY_OR_VALUE     2202
#define MANAGEMENT_ERROR_CONF_SET_NORESPONSE                2203
#define MANAGEMENT_ERROR_CONF_SET_UNKNOWN_CONFIGURATION_KEY 2204
#define MANAGEMENT_ERROR_CONF_SET_UNKNOWN_SERVER            2205
#define MANAGEMENT_ERROR_CONF_SET_NETWORK                   2206
#define MANAGEMENT_ERROR_CONF_SET_ERROR                     2207

/**
 * Output formats
 */
#define MANAGEMENT_OUTPUT_FORMAT_TEXT 0
#define MANAGEMENT_OUTPUT_FORMAT_JSON 1
#define MANAGEMENT_OUTPUT_FORMAT_RAW  2

/**
 * Create a backup request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_backup(SSL* ssl, int socket, char* server, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create a list backup request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_list_backup(SSL* ssl, int socket, char* server, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create a restore request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server
 * @param backup_id The backup
 * @param position The position parameters
 * @param directory The directory
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_restore(SSL* ssl, int socket, char* server, char* backup_id, char* position, char* directory, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create a verify request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server
 * @param backup_id The backup
 * @param directory The directory
 * @param files The files filter
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_verify(SSL* ssl, int socket, char* server, char* backup_id, char* directory, char* files, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create an archive request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server
 * @param backup_id The backup
 * @param position The position parameters
 * @param directory The directory
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_archive(SSL* ssl, int socket, char* server, char* backup_id, char* position, char* directory, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create a delete request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server
 * @param backup_id The backup
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_delete(SSL* ssl, int socket, char* server, char* backup_id, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create a shutdown request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_shutdown(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create a status payload
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_status(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create a status details request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_status_details(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create a ping request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_ping(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create a reset request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_reset(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create a reload request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_reload(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create a conf ls request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_conf_ls(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create a conf get request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_conf_get(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create a conf get request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param config_key The configuration key
 * @param config_value The configuration value
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_conf_set(SSL* ssl, int socket, char* config_key, char* config_value, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create a retain request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server
 * @param backup_id The backup
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_retain(SSL* ssl, int socket, char* server, char* backup_id, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create an expunge request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server
 * @param backup_id The backup
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_expunge(SSL* ssl, int socket, char* server, char* backup_id, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create a decrypt request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param path The file path
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_decrypt(SSL* ssl, int socket, char* path, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create an encrypt request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param path The file path
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_encrypt(SSL* ssl, int socket, char* path, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create a decompress request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param path The file path
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_decompress(SSL* ssl, int socket, char* path, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create a compress request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param path The file path
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_compress(SSL* ssl, int socket, char* path, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create an info request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server
 * @param backup_id The backup
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_info(SSL* ssl, int socket, char* server, char* backup_id, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create an annotate request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server
 * @param backup_id The backup
 * @param action The action
 * @param key The key
 * @param comment The comment
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_annotate(SSL* ssl, int socket, char* server, char* backup_id, char* action, char* key, char* comment, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create an ok response
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param start_time The start time
 * @param end_time The end time
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The full payload
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_response_ok(SSL* ssl, int socket, time_t start_time, time_t end_time, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * Create an error response
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server
 * @param error The error code
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The full payload
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_response_error(SSL* ssl, int socket, char* server, int32_t error, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * Create a response
 * @param json The JSON structure
 * @param server The server
 * @param response The response
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_create_response(struct json* json, int server, struct json** response);

/**
 * Read the management JSON
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The pointer to an integer that will store the compress method
 * @param json The JSON structure
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_read_json(SSL* ssl, int socket, uint8_t* compression, uint8_t* encryption, struct json** json);

/**
 * Write the management JSON
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param json The JSON structure
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_write_json(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, struct json* json);

#ifdef __cplusplus
}
#endif

#endif
