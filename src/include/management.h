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
#define MANAGEMENT_UNKNOWN         0
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

#define MANAGEMENT_MASTER_KEY     24
#define MANAGEMENT_ADD_USER       25
#define MANAGEMENT_UPDATE_USER    26
#define MANAGEMENT_REMOVE_USER    27
#define MANAGEMENT_LIST_USERS     28

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
#define MANAGEMENT_ARGUMENT_BIGGEST_FILE_SIZE     "BiggestFileSize"
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
#define MANAGEMENT_ARGUMENT_INCREMENTAL           "Incremental"
#define MANAGEMENT_ARGUMENT_INCREMENTAL_PARENT    "IncrementalParent"
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
#define MANAGEMENT_ARGUMENT_WORKFLOW              "Workflow"
#define MANAGEMENT_ARGUMENT_WORKSPACE_FREE_SPACE  "WorkspaceFreeSpace"

/**
 * Management error
 */
#define MANAGEMENT_ERROR_BAD_PAYLOAD     1
#define MANAGEMENT_ERROR_UNKNOWN_COMMAND 2
#define MANAGEMENT_ERROR_ALLOCATION      3

#define MANAGEMENT_ERROR_BACKUP_INVALID      100
#define MANAGEMENT_ERROR_BACKUP_WAL          101
#define MANAGEMENT_ERROR_BACKUP_ACTIVE       102
#define MANAGEMENT_ERROR_BACKUP_NOBACKUPS    103
#define MANAGEMENT_ERROR_BACKUP_NOCHILD      104
#define MANAGEMENT_ERROR_BACKUP_ALREADYCHILD 105
#define MANAGEMENT_ERROR_BACKUP_SETUP        106
#define MANAGEMENT_ERROR_BACKUP_EXECUTE      107
#define MANAGEMENT_ERROR_BACKUP_TEARDOWN     108
#define MANAGEMENT_ERROR_BACKUP_NETWORK      109
#define MANAGEMENT_ERROR_BACKUP_OFFLINE      110
#define MANAGEMENT_ERROR_BACKUP_NOSERVER     111
#define MANAGEMENT_ERROR_BACKUP_NOFORK       111
#define MANAGEMENT_ERROR_BACKUP_ERROR        112

#define MANAGEMENT_ERROR_INCREMENTAL_BACKUP_SETUP    200
#define MANAGEMENT_ERROR_INCREMENTAL_BACKUP_EXECUTE  201
#define MANAGEMENT_ERROR_INCREMENTAL_BACKUP_TEARDOWN 202

#define MANAGEMENT_ERROR_LIST_BACKUP_DEQUE_CREATE 300
#define MANAGEMENT_ERROR_LIST_BACKUP_BACKUPS      301
#define MANAGEMENT_ERROR_LIST_BACKUP_JSON_VALUE   302
#define MANAGEMENT_ERROR_LIST_BACKUP_NETWORK      303
#define MANAGEMENT_ERROR_LIST_BACKUP_NOSERVER     304
#define MANAGEMENT_ERROR_LIST_BACKUP_NOFORK       305

#define MANAGEMENT_ERROR_DELETE_SETUP    400
#define MANAGEMENT_ERROR_DELETE_EXECUTE  401
#define MANAGEMENT_ERROR_DELETE_TEARDOWN 402
#define MANAGEMENT_ERROR_DELETE_NOSERVER 403
#define MANAGEMENT_ERROR_DELETE_NOFORK   404
#define MANAGEMENT_ERROR_DELETE_NETWORK  405
#define MANAGEMENT_ERROR_DELETE_ERROR    406

#define MANAGEMENT_ERROR_DELETE_BACKUP_SETUP    500
#define MANAGEMENT_ERROR_DELETE_BACKUP_EXECUTE  501
#define MANAGEMENT_ERROR_DELETE_BACKUP_TEARDOWN 502

#define MANAGEMENT_ERROR_RESTORE_NOBACKUP 600
#define MANAGEMENT_ERROR_RESTORE_NODISK   601
#define MANAGEMENT_ERROR_RESTORE_NOSERVER 602
#define MANAGEMENT_ERROR_RESTORE_SETUP    603
#define MANAGEMENT_ERROR_RESTORE_EXECUTE  604
#define MANAGEMENT_ERROR_RESTORE_TEARDOWN 605
#define MANAGEMENT_ERROR_RESTORE_NOFORK   606
#define MANAGEMENT_ERROR_RESTORE_NETWORK  607
#define MANAGEMENT_ERROR_RESTORE_ERROR    608

#define MANAGEMENT_ERROR_COMBINE_SETUP    700
#define MANAGEMENT_ERROR_COMBINE_EXECUTE  701
#define MANAGEMENT_ERROR_COMBINE_TEARDOWN 702

#define MANAGEMENT_ERROR_VERIFY_NOSERVER 800
#define MANAGEMENT_ERROR_VERIFY_SETUP    801
#define MANAGEMENT_ERROR_VERIFY_EXECUTE  802
#define MANAGEMENT_ERROR_VERIFY_TEARDOWN 803
#define MANAGEMENT_ERROR_VERIFY_NOFORK   804
#define MANAGEMENT_ERROR_VERIFY_NETWORK  805
#define MANAGEMENT_ERROR_VERIFY_ERROR    806

#define MANAGEMENT_ERROR_ARCHIVE_NOBACKUP 900
#define MANAGEMENT_ERROR_ARCHIVE_NOSERVER 901
#define MANAGEMENT_ERROR_ARCHIVE_SETUP    902
#define MANAGEMENT_ERROR_ARCHIVE_EXECUTE  903
#define MANAGEMENT_ERROR_ARCHIVE_TEARDOWN 904
#define MANAGEMENT_ERROR_ARCHIVE_NOFORK   905
#define MANAGEMENT_ERROR_ARCHIVE_NETWORK  906
#define MANAGEMENT_ERROR_ARCHIVE_ERROR    907

#define MANAGEMENT_ERROR_STATUS_NOFORK   1000
#define MANAGEMENT_ERROR_STATUS_NETWORK  1001

#define MANAGEMENT_ERROR_STATUS_DETAILS_NOFORK  1100
#define MANAGEMENT_ERROR_STATUS_DETAILS_NETWORK 1101

#define MANAGEMENT_ERROR_RETAIN_NOBACKUP 1200
#define MANAGEMENT_ERROR_RETAIN_NOSERVER 1201
#define MANAGEMENT_ERROR_RETAIN_NOFORK   1202
#define MANAGEMENT_ERROR_RETAIN_NETWORK  1203
#define MANAGEMENT_ERROR_RETAIN_ERROR    1204

#define MANAGEMENT_ERROR_EXPUNGE_NOBACKUP 1300
#define MANAGEMENT_ERROR_EXPUNGE_NOSERVER 1301
#define MANAGEMENT_ERROR_EXPUNGE_NOFORK   1302
#define MANAGEMENT_ERROR_EXPUNGE_NETWORK  1303
#define MANAGEMENT_ERROR_EXPUNGE_ERROR    1304

#define MANAGEMENT_ERROR_DECRYPT_NOFILE  1400
#define MANAGEMENT_ERROR_DECRYPT_NOFORK  1401
#define MANAGEMENT_ERROR_DECRYPT_NETWORK 1402
#define MANAGEMENT_ERROR_DECRYPT_ERROR   1403

#define MANAGEMENT_ERROR_ENCRYPT_NOFILE  1500
#define MANAGEMENT_ERROR_ENCRYPT_NOFORK  1501
#define MANAGEMENT_ERROR_ENCRYPT_NETWORK 1502
#define MANAGEMENT_ERROR_ENCRYPT_ERROR   1503

#define MANAGEMENT_ERROR_GZIP_NOFILE  1600
#define MANAGEMENT_ERROR_GZIP_NOFORK  1601
#define MANAGEMENT_ERROR_GZIP_NETWORK 1602
#define MANAGEMENT_ERROR_GZIP_ERROR   1603

#define MANAGEMENT_ERROR_ZSTD_NOFILE  1700
#define MANAGEMENT_ERROR_ZSTD_NOFORK  1701
#define MANAGEMENT_ERROR_ZSTD_NETWORK 1702
#define MANAGEMENT_ERROR_ZSTD_ERROR   1703

#define MANAGEMENT_ERROR_LZ4_NOFILE  1800
#define MANAGEMENT_ERROR_LZ4_NOFORK  1801
#define MANAGEMENT_ERROR_LZ4_NETWORK 1802
#define MANAGEMENT_ERROR_LZ4_ERROR   1803

#define MANAGEMENT_ERROR_BZIP2_NOFILE  1900
#define MANAGEMENT_ERROR_BZIP2_NOFORK  1901
#define MANAGEMENT_ERROR_BZIP2_NETWORK 1902
#define MANAGEMENT_ERROR_BZIP2_ERROR   1903

#define MANAGEMENT_ERROR_DECOMPRESS_NOFORK  2000
#define MANAGEMENT_ERROR_DECOMPRESS_UNKNOWN 2001

#define MANAGEMENT_ERROR_COMPRESS_NOFORK  2100
#define MANAGEMENT_ERROR_COMPRESS_UNKNOWN 2101

#define MANAGEMENT_ERROR_INFO_NOBACKUP 2200
#define MANAGEMENT_ERROR_INFO_NOSERVER 2201
#define MANAGEMENT_ERROR_INFO_NOFORK   2202
#define MANAGEMENT_ERROR_INFO_NETWORK  2203
#define MANAGEMENT_ERROR_INFO_ERROR    2204

#define MANAGEMENT_ERROR_RETENTION_SETUP    2302
#define MANAGEMENT_ERROR_RETENTION_EXECUTE  2303
#define MANAGEMENT_ERROR_RETENTION_TEARDOWN 2304

#define MANAGEMENT_ERROR_WAL_SHIPPING_SETUP    2402
#define MANAGEMENT_ERROR_WAL_SHIPPING_EXECUTE  2403
#define MANAGEMENT_ERROR_WAL_SHIPPING_TEARDOWN 2404

#define MANAGEMENT_ERROR_ANNOTATE_NOBACKUP 2500
#define MANAGEMENT_ERROR_ANNOTATE_NOSERVER 2501
#define MANAGEMENT_ERROR_ANNOTATE_NOFORK   2502
#define MANAGEMENT_ERROR_ANNOTATE_FAILED   2503
#define MANAGEMENT_ERROR_ANNOTATE_NETWORK  2504
#define MANAGEMENT_ERROR_ANNOTATE_ERROR    2505

#define MANAGEMENT_ERROR_CONF_GET_NOFORK  2600
#define MANAGEMENT_ERROR_CONF_GET_NETWORK 2602
#define MANAGEMENT_ERROR_CONF_GET_ERROR   2603

#define MANAGEMENT_ERROR_CONF_SET_NOFORK                    2700
#define MANAGEMENT_ERROR_CONF_SET_NOREQUEST                 2701
#define MANAGEMENT_ERROR_CONF_SET_NOCONFIG_KEY_OR_VALUE     2702
#define MANAGEMENT_ERROR_CONF_SET_NORESPONSE                2703
#define MANAGEMENT_ERROR_CONF_SET_UNKNOWN_CONFIGURATION_KEY 2704
#define MANAGEMENT_ERROR_CONF_SET_UNKNOWN_SERVER            2705
#define MANAGEMENT_ERROR_CONF_SET_NETWORK                   2706
#define MANAGEMENT_ERROR_CONF_SET_ERROR                     2707

/**
 * Output formats
 */
#define MANAGEMENT_OUTPUT_FORMAT_TEXT 0
#define MANAGEMENT_OUTPUT_FORMAT_JSON 1
#define MANAGEMENT_OUTPUT_FORMAT_RAW  2

/**
 * Create header for management command
 * @param command The command
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @param json The target json
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_create_header(int32_t command, uint8_t compression, uint8_t encryption, int32_t output_format, struct json** json);

/**
 * Create request for management command
 * @param json The target json
 * @param json The request json
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_create_request(struct json* json, struct json** request);

/**
 * Create success outcome for management command
 * @param json The target json
 * @param start_t The start time of the command
 * @param end_t The end time of the command
 * @param outcome The outcome json
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_create_outcome_success(struct json* json, struct timespec start_t, struct timespec end_t, struct json** outcome);

/**
 * Create success outcome for management command
 * @param json The target json
 * @param error The error code
 * @param workflow The workflow
 * @param outcome The outcome json
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_create_outcome_failure(struct json* json, int32_t error, char* workflow, struct json** outcome);

/**
 * Create a backup request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param incremental The base of incremental backup
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_request_backup(SSL* ssl, int socket, char* server, uint8_t compression, uint8_t encryption, char* incremental, int32_t output_format);

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
pgmoneta_management_response_ok(SSL* ssl, int socket, struct timespec start_time, struct timespec end_time, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * Create an error response
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server
 * @param error The error code
 * @param workflow The workflow
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The full payload
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_management_response_error(SSL* ssl, int socket, char* server, int32_t error, char* workflow,
                                   uint8_t compression, uint8_t encryption, struct json* payload);

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
