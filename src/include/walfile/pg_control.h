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

#ifndef PGMONETA_PG_CONTROL_H
#define PGMONETA_PG_CONTROL_H

#include <walfile/transaction.h>
#include <walfile/wal_reader.h>

#include <stdint.h>
#include <stdbool.h>

typedef int64_t pg_time_t;

/* XLOG info values for XLOG rmgr */
#define XLOG_CHECKPOINT_SHUTDOWN      0x00   /**< XLOG record type for a shutdown checkpoint */
#define XLOG_CHECKPOINT_ONLINE        0x10   /**< XLOG record type for an online checkpoint */
#define XLOG_NOOP                     0x20   /**< XLOG record type for a no-op */
#define XLOG_NEXTOID                  0x30   /**< XLOG record type for the next OID */
#define XLOG_SWITCH                   0x40   /**< XLOG record type for a switch */
#define XLOG_BACKUP_END               0x50   /**< XLOG record type for the end of a backup */
#define XLOG_PARAMETER_CHANGE         0x60   /**< XLOG record type for a parameter change */
#define XLOG_RESTORE_POINT            0x70   /**< XLOG record type for a restore point */
#define XLOG_FPW_CHANGE               0x80   /**< XLOG record type for a full-page writes change */
#define XLOG_END_OF_RECOVERY          0x90   /**< XLOG record type for the end of recovery */
#define XLOG_FPI_FOR_HINT             0xA0   /**< XLOG record type for a full-page image for hint bits */
#define XLOG_FPI                      0xB0   /**< XLOG record type for a full-page image */
#define XLOG_OVERWRITE_CONTRECORD     0xD0   /**< XLOG record type for overwriting a continuation record */

#define MOCK_AUTH_NONCE_LEN      32
#define PG_CONTROL_MAX_SAFE_SIZE 512

/**
 * @struct check_point_v13
 * @brief Represents a checkpoint record for version 13.
 *
 * Fields:
 * - redo: The next available RecPtr when the checkpoint was created (i.e., REDO start point).
 * - this_timeline_id: Current timeline ID.
 * - prev_timeline_id: Previous timeline ID, if the record begins a new timeline (equals this_timeline_id otherwise).
 * - full_page_writes: Indicates the current full_page_writes setting.
 * - next_xid: The next free transaction ID.
 * - next_oid: The next free OID.
 * - next_multi: The next free multi_xact_id.
 * - next_multi_offset: The next free MultiXact offset.
 * - oldest_xid: Cluster-wide minimum datfrozenxid.
 * - oldest_xid_db: Database with minimum datfrozenxid.
 * - oldest_multi: Cluster-wide minimum datminmxid.
 * - oldest_multi_db: Database with minimum datminmxid.
 * - time: The timestamp of the checkpoint.
 * - oldest_commit_ts_xid: The oldest XID with a valid commit timestamp.
 * - newest_commit_ts_xid: The newest XID with a valid commit timestamp.
 * - oldest_active_xid: The oldest XID still running, calculated only for online checkpoints and when wal_level is replica.
 */
struct check_point_v13
{
   xlog_rec_ptr redo;                      /**< Next RecPtr available when the checkpoint was created (i.e., REDO start point). */
   timeline_id this_timeline_id;           /**< Current timeline ID. */
   timeline_id prev_timeline_id;           /**< Previous timeline ID, if the record begins a new timeline (equals this_timeline_id otherwise). */
   bool full_page_writes;                  /**< Indicates the current full_page_writes setting. */
   struct full_transaction_id next_xid;    /**< Next free transaction ID. */
   oid next_oid;                           /**< Next free OID. */
   multi_xact_id next_multi;               /**< Next free multi_xact_id. */
   multi_xact_offset next_multi_offset;    /**< Next free MultiXact offset. */
   transaction_id oldest_xid;              /**< Cluster-wide minimum datfrozenxid. */
   oid oldest_xid_db;                      /**< Database with minimum datfrozenxid. */
   multi_xact_id oldest_multi;             /**< Cluster-wide minimum datminmxid. */
   oid oldest_multi_db;                    /**< Database with minimum datminmxid. */
   pg_time_t time;                         /**< Timestamp of the checkpoint. */
   transaction_id oldest_commit_ts_xid;    /**< Oldest XID with a valid commit timestamp. */
   transaction_id newest_commit_ts_xid;    /**< Newest XID with a valid commit timestamp. */
   transaction_id oldest_active_xid;       /**< Oldest XID still running, calculated only for online checkpoints and when wal_level is replica. */
};

/**
 * @struct check_point_v17
 * @brief Represents a checkpoint record for version 17.
 *
 * Fields:
 * - redo: The next available RecPtr when the checkpoint was created (i.e., REDO start point).
 * - this_timeline_id: Current timeline ID.
 * - prev_timeline_id: Previous timeline ID, if the record begins a new timeline (equals this_timeline_id otherwise).
 * - full_page_writes: Indicates the current full_page_writes setting.
 * - wal_level: Current wal_level.
 * - next_xid: The next free transaction ID.
 * - next_oid: The next free OID.
 * - next_multi: The next free multi_xact_id.
 * - next_multi_offset: The next free MultiXact offset.
 * - oldest_xid: Cluster-wide minimum datfrozenxid.
 * - oldest_xid_db: Database with minimum datfrozenxid.
 * - oldest_multi: Cluster-wide minimum datminmxid.
 * - oldest_multi_db: Database with minimum datminmxid.
 * - time: The timestamp of the checkpoint.
 * - oldest_commit_ts_xid: The oldest XID with a valid commit timestamp.
 * - newest_commit_ts_xid: The newest XID with a valid commit timestamp.
 * - oldest_active_xid: The oldest XID still running, calculated only for online checkpoints and when wal_level is replica.
 */
struct check_point_v17
{
   xlog_rec_ptr redo;                      /**< Next RecPtr available when the checkpoint was created (i.e., REDO start point). */
   timeline_id this_timeline_id;           /**< Current timeline ID. */
   timeline_id prev_timeline_id;           /**< Previous timeline ID, if the record begins a new timeline (equals this_timeline_id otherwise). */
   bool full_page_writes;                  /**< Indicates the current full_page_writes setting. */
   int wal_level;                          /**< Current wal_level. */
   struct full_transaction_id next_xid;    /**< Next free transaction ID. */
   oid next_oid;                           /**< Next free OID. */
   multi_xact_id next_multi;               /**< Next free multi_xact_id. */
   multi_xact_offset next_multi_offset;    /**< Next free MultiXact offset. */
   transaction_id oldest_xid;              /**< Cluster-wide minimum datfrozenxid. */
   oid oldest_xid_db;                      /**< Database with minimum datfrozenxid. */
   multi_xact_id oldest_multi;             /**< Cluster-wide minimum datminmxid. */
   oid oldest_multi_db;                    /**< Database with minimum datminmxid. */
   pg_time_t time;                         /**< Timestamp of the checkpoint. */
   transaction_id oldest_commit_ts_xid;    /**< Oldest XID with a valid commit timestamp. */
   transaction_id newest_commit_ts_xid;    /**< Newest XID with a valid commit timestamp. */
   transaction_id oldest_active_xid;       /**< Oldest XID still running, calculated only for online checkpoints and when wal_level is replica. */
};

/**
 * @struct check_point
 * @brief Wrapper structure to handle different versions of checkpoint records.
 *
 * Fields:
 * - data: A union containing version-specific checkpoint record data.
 * - parse: Function pointer to parse the checkpoint record.
 * - format: Function pointer to format the checkpoint record.
 */
struct check_point
{
   void (*parse)(struct check_point* wrapper, void* rec);     /**< Parse function pointer */
   char* (*format)(struct check_point* wrapper, char* buf);          /**< Format function pointer */
   union
   {
      struct check_point_v13 v13;       /**< Version 13 data */
      struct check_point_v17 v17;       /**< Version 17 data */
   } data;   /**< Union holding version-specific checkpoint data */
};

/**
 * @brief Creates a new check_point structure.
 *
 * @return A pointer to the newly created check_point structure.
 */
struct check_point*
create_check_point(void);

/**
 * @brief Parses a version 16 checkpoint record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param rec The record to parse.
 */
void
check_point_parse_v13(struct check_point* wrapper, void* rec);

/**
 * @brief Parses a version 17 checkpoint record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param rec The record to parse.
 */
void
check_point_parse_v17(struct check_point* wrapper, void* rec);

/**
 * @brief Formats a version 16 checkpoint record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char*
check_point_format_v13(struct check_point* wrapper, char* buf);

/**
 * @brief Formats a version 17 checkpoint record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char*
check_point_format_v17(struct check_point* wrapper, char* buf);

/*
 * System status indicator.  Note this is stored in pg_control.
 */
typedef enum db_state
{
   DB_STARTUP = 0,
   DB_SHUTDOWNED,
   DB_SHUTDOWNED_IN_RECOVERY,
   DB_SHUTDOWNING,
   DB_IN_CRASH_RECOVERY,
   DB_IN_ARCHIVE_RECOVERY,
   DB_IN_PRODUCTION,
} db_state;

/**
 * @struct control_file_data_v13
 * @brief Structure representing the control file data of a PostgreSQL database.
 *
 * Fields:
 * - system_identifier: Unique system identifier for matching WAL files with their database.
 * - pg_control_version: Version identifier for pg_control file format.
 * - catalog_version_no: Version identifier for system catalog format.
 * - state: Current state of the database system.
 * - time: Timestamp of the last pg_control update.
 * - checkpoint: Last checkpoint record pointer.
 * - checkpoint_copy: Copy of the last checkpoint record.
 * - unlogged_lsn: Fake LSN value for unlogged relations.
 * - min_recovery_point: Minimum recovery LSN before database startup is allowed.
 * - min_recovery_point_tli: Timeline ID for min_recovery_point.
 * - backup_start_point: Redo pointer of the backup start checkpoint.
 * - backup_end_point: LSN marking the backup end location.
 * - backup_end_required: Boolean indicating if an end-of-backup record is required before startup.
 * - wal_level: WAL logging level.
 * - wal_log_hints: Boolean indicating if full-page writes are logged for hints.
 * - max_connections: Maximum number of allowed concurrent connections.
 * - max_worker_processes: Maximum number of worker processes.
 * - max_wal_senders: Maximum number of WAL sender processes.
 * - max_prepared_xacts: Maximum number of prepared transactions.
 * - max_locks_per_xact: Maximum number of locks per transaction.
 * - track_commit_timestamp: Boolean indicating if commit timestamps are tracked.
 * - max_align: Alignment requirement for tuples.
 * - float_format: Floating point format validation constant.
 * - blcksz: Block size for database storage.
 * - relseg_size: Blocks per segment for large relations.
 * - xlog_blcksz: Block size for WAL files.
 * - xlog_seg_size: Size of each WAL segment.
 * - name_data_len: Maximum length of catalog names.
 * - index_max_keys: Maximum number of columns in an index.
 * - toast_max_chunk_size: Maximum chunk size in TOAST tables.
 * - loblksize: Chunk size in pg_largeobject.
 * - float8_by_val: Boolean indicating if float8/int8 values are passed by value.
 * - data_checksum_version: Version of data checksum, zero if checksums are disabled.
 * - mock_authentication_nonce: Random nonce for authentication processes.
 * - crc: CRC checksum for control file integrity.
 */
struct control_file_data_v13
{
   uint64_t system_identifier;                          /**< Unique system identifier for WAL file matching. */
   uint32_t pg_control_version;                         /**< Version of the pg_control file format. */
   uint32_t catalog_version_no;                         /**< Version of the system catalog format. */
   db_state state;                                      /**< Current state of the database system. */
   pg_time_t time;                                      /**< Timestamp of the last pg_control update. */
   xlog_rec_ptr checkpoint;                             /**< Last checkpoint record pointer. */

   struct check_point_v13 checkpoint_copy;              /**< Copy of the last checkpoint record. */

   xlog_rec_ptr unlogged_lsn;                           /**< Fake LSN value for unlogged relations. */
   xlog_rec_ptr min_recovery_point;                     /**< Minimum recovery LSN before startup. */
   timeline_id min_recovery_point_tli;                  /**< Timeline ID for min_recovery_point. */
   xlog_rec_ptr backup_start_point;                     /**< Redo pointer of backup start checkpoint. */
   xlog_rec_ptr backup_end_point;                       /**< LSN marking the backup end location. */
   bool backup_end_required;                            /**< Indicates if an end-of-backup record is required before startup. */

   int wal_level;                                       /**< WAL logging level. */
   bool wal_log_hints;                                  /**< Indicates if full-page writes are logged for hints. */
   int max_connections;                                 /**< Maximum number of allowed concurrent connections. */
   int max_worker_processes;                            /**< Maximum number of worker processes. */
   int max_wal_senders;                                 /**< Maximum number of WAL sender processes. */
   int max_prepared_xacts;                              /**< Maximum number of prepared transactions. */
   int max_locks_per_xact;                              /**< Maximum number of locks per transaction. */
   bool track_commit_timestamp;                         /**< Indicates if commit timestamps are tracked. */

   uint32_t max_align;                                  /**< Alignment requirement for tuples. */
   double float_format;                                 /**< Floating point format validation constant. */

   uint32_t blcksz;                                     /**< Block size for database storage. */
   uint32_t relseg_size;                                /**< Blocks per segment for large relations. */

   uint32_t xlog_blcksz;                                /**< Block size for WAL files. */
   uint32_t xlog_seg_size;                              /**< Size of each WAL segment. */

   uint32_t name_data_len;                              /**< Maximum length of catalog names. */
   uint32_t index_max_keys;                             /**< Maximum number of columns in an index. */

   uint32_t toast_max_chunk_size;                       /**< Maximum chunk size in TOAST tables. */
   uint32_t loblksize;                                  /**< Chunk size in pg_largeobject. */

   bool float8_by_val;                                  /**< Indicates if float8/int8 values are passed by value. */

   uint32_t data_checksum_version;                      /**< Version of data checksum, zero if disabled. */

   char mock_authentication_nonce[MOCK_AUTH_NONCE_LEN]; /**< Random nonce for authentication. */

   pg_crc32c crc;                                       /**< CRC checksum for control file integrity. */
};

/**
 * @struct control_file_data_v17
 * @brief Structure representing the control file data of a PostgreSQL database.
 *
 * Fields:
 * - system_identifier: Unique system identifier for matching WAL files with their database.
 * - pg_control_version: Version identifier for pg_control file format.
 * - catalog_version_no: Version identifier for system catalog format.
 * - state: Current state of the database system.
 * - time: Timestamp of the last pg_control update.
 * - checkpoint: Last checkpoint record pointer.
 * - checkpoint_copy: Copy of the last checkpoint record.
 * - unlogged_lsn: Fake LSN value for unlogged relations.
 * - min_recovery_point: Minimum recovery LSN before database startup is allowed.
 * - min_recovery_point_tli: Timeline ID for min_recovery_point.
 * - backup_start_point: Redo pointer of the backup start checkpoint.
 * - backup_end_point: LSN marking the backup end location.
 * - backup_end_required: Boolean indicating if an end-of-backup record is required before startup.
 * - wal_level: WAL logging level.
 * - wal_log_hints: Boolean indicating if full-page writes are logged for hints.
 * - max_connections: Maximum number of allowed concurrent connections.
 * - max_worker_processes: Maximum number of worker processes.
 * - max_wal_senders: Maximum number of WAL sender processes.
 * - max_prepared_xacts: Maximum number of prepared transactions.
 * - max_locks_per_xact: Maximum number of locks per transaction.
 * - track_commit_timestamp: Boolean indicating if commit timestamps are tracked.
 * - max_align: Alignment requirement for tuples.
 * - float_format: Floating point format validation constant.
 * - blcksz: Block size for database storage.
 * - relseg_size: Blocks per segment for large relations.
 * - xlog_blcksz: Block size for WAL files.
 * - xlog_seg_size: Size of each WAL segment.
 * - name_data_len: Maximum length of catalog names.
 * - index_max_keys: Maximum number of columns in an index.
 * - toast_max_chunk_size: Maximum chunk size in TOAST tables.
 * - loblksize: Chunk size in pg_largeobject.
 * - float8_by_val: Boolean indicating if float8/int8 values are passed by value.
 * - data_checksum_version: Version of data checksum, zero if checksums are disabled.
 * - mock_authentication_nonce: Random nonce for authentication processes.
 * - crc: CRC checksum for control file integrity.
 */
struct control_file_data_v17
{
   uint64_t system_identifier;                          /**< Unique system identifier for WAL file matching. */
   uint32_t pg_control_version;                         /**< Version of the pg_control file format. */
   uint32_t catalog_version_no;                         /**< Version of the system catalog format. */
   db_state state;                                      /**< Current state of the database system. */
   pg_time_t time;                                      /**< Timestamp of the last pg_control update. */
   xlog_rec_ptr checkpoint;                             /**< Last checkpoint record pointer. */

   struct check_point_v17 checkpoint_copy;              /**< Copy of the last checkpoint record. */

   xlog_rec_ptr unlogged_lsn;                           /**< Fake LSN value for unlogged relations. */
   xlog_rec_ptr min_recovery_point;                     /**< Minimum recovery LSN before startup. */
   timeline_id min_recovery_point_tli;                  /**< Timeline ID for min_recovery_point. */
   xlog_rec_ptr backup_start_point;                     /**< Redo pointer of backup start checkpoint. */
   xlog_rec_ptr backup_end_point;                       /**< LSN marking the backup end location. */
   bool backup_end_required;                            /**< Indicates if an end-of-backup record is required before startup. */

   int wal_level;                                       /**< WAL logging level. */
   bool wal_log_hints;                                  /**< Indicates if full-page writes are logged for hints. */
   int max_connections;                                 /**< Maximum number of allowed concurrent connections. */
   int max_worker_processes;                            /**< Maximum number of worker processes. */
   int max_wal_senders;                                 /**< Maximum number of WAL sender processes. */
   int max_prepared_xacts;                              /**< Maximum number of prepared transactions. */
   int max_locks_per_xact;                              /**< Maximum number of locks per transaction. */
   bool track_commit_timestamp;                         /**< Indicates if commit timestamps are tracked. */

   uint32_t max_align;                                  /**< Alignment requirement for tuples. */
   double float_format;                                 /**< Floating point format validation constant. */

   uint32_t blcksz;                                     /**< Block size for database storage. */
   uint32_t relseg_size;                                /**< Blocks per segment for large relations. */

   uint32_t xlog_blcksz;                                /**< Block size for WAL files. */
   uint32_t xlog_seg_size;                              /**< Size of each WAL segment. */

   uint32_t name_data_len;                              /**< Maximum length of catalog names. */
   uint32_t index_max_keys;                             /**< Maximum number of columns in an index. */

   uint32_t toast_max_chunk_size;                       /**< Maximum chunk size in TOAST tables. */
   uint32_t loblksize;                                  /**< Chunk size in pg_largeobject. */

   bool float8_by_val;                                  /**< Indicates if float8/int8 values are passed by value. */

   uint32_t data_checksum_version;                      /**< Version of data checksum, zero if disabled. */

   char mock_authentication_nonce[MOCK_AUTH_NONCE_LEN]; /**< Random nonce for authentication. */

   pg_crc32c crc;                                       /**< CRC checksum for control file integrity. */
};

/**
 * @struct control_file_data_v18
 * @brief Structure representing the control file data of a PostgreSQL database.
 *
 * Fields:
 * - system_identifier: Unique system identifier for matching WAL files with their database.
 * - pg_control_version: Version identifier for pg_control file format.
 * - catalog_version_no: Version identifier for system catalog format.
 * - state: Current state of the database system.
 * - time: Timestamp of the last pg_control update.
 * - checkpoint: Last checkpoint record pointer.
 * - checkpoint_copy: Copy of the last checkpoint record.
 * - unlogged_lsn: Fake LSN value for unlogged relations.
 * - min_recovery_point: Minimum recovery LSN before database startup is allowed.
 * - min_recovery_point_tli: Timeline ID for min_recovery_point.
 * - backup_start_point: Redo pointer of the backup start checkpoint.
 * - backup_end_point: LSN marking the backup end location.
 * - backup_end_required: Boolean indicating if an end-of-backup record is required before startup.
 * - wal_level: WAL logging level.
 * - wal_log_hints: Boolean indicating if full-page writes are logged for hints.
 * - max_connections: Maximum number of allowed concurrent connections.
 * - max_worker_processes: Maximum number of worker processes.
 * - max_wal_senders: Maximum number of WAL sender processes.
 * - max_prepared_xacts: Maximum number of prepared transactions.
 * - max_locks_per_xact: Maximum number of locks per transaction.
 * - track_commit_timestamp: Boolean indicating if commit timestamps are tracked.
 * - max_align: Alignment requirement for tuples.
 * - float_format: Floating point format validation constant.
 * - blcksz: Block size for database storage.
 * - relseg_size: Blocks per segment for large relations.
 * - xlog_blcksz: Block size for WAL files.
 * - xlog_seg_size: Size of each WAL segment.
 * - name_data_len: Maximum length of catalog names.
 * - index_max_keys: Maximum number of columns in an index.
 * - toast_max_chunk_size: Maximum chunk size in TOAST tables.
 * - loblksize: Chunk size in pg_largeobject.
 * - float8_by_val: Boolean indicating if float8/int8 values are passed by value.
 * - data_checksum_version: Version of data checksum, zero if checksums are disabled.
 * - param default_char_signedness Boolean indicating if default char is signed.
 * - mock_authentication_nonce: Random nonce for authentication processes.
 * - crc: CRC checksum for control file integrity.
 */
struct control_file_data_v18
{
   uint64_t system_identifier;                          /**< Unique system identifier for WAL file matching. */
   uint32_t pg_control_version;                         /**< Version of the pg_control file format. */
   uint32_t catalog_version_no;                         /**< Version of the system catalog format. */
   db_state state;                                      /**< Current state of the database system. */
   pg_time_t time;                                      /**< Timestamp of the last pg_control update. */
   xlog_rec_ptr checkpoint;                             /**< Last checkpoint record pointer. */

   struct check_point_v17 checkpoint_copy;              /**< Copy of the last checkpoint record. */

   xlog_rec_ptr unlogged_lsn;                           /**< Fake LSN value for unlogged relations. */
   xlog_rec_ptr min_recovery_point;                     /**< Minimum recovery LSN before startup. */
   timeline_id min_recovery_point_tli;                  /**< Timeline ID for min_recovery_point. */
   xlog_rec_ptr backup_start_point;                     /**< Redo pointer of backup start checkpoint. */
   xlog_rec_ptr backup_end_point;                       /**< LSN marking the backup end location. */
   bool backup_end_required;                            /**< Indicates if an end-of-backup record is required before startup. */

   int wal_level;                                       /**< WAL logging level. */
   bool wal_log_hints;                                  /**< Indicates if full-page writes are logged for hints. */
   int max_connections;                                 /**< Maximum number of allowed concurrent connections. */
   int max_worker_processes;                            /**< Maximum number of worker processes. */
   int max_wal_senders;                                 /**< Maximum number of WAL sender processes. */
   int max_prepared_xacts;                              /**< Maximum number of prepared transactions. */
   int max_locks_per_xact;                              /**< Maximum number of locks per transaction. */
   bool track_commit_timestamp;                         /**< Indicates if commit timestamps are tracked. */

   uint32_t max_align;                                  /**< Alignment requirement for tuples. */
   double float_format;                                 /**< Floating point format validation constant. */

   uint32_t blcksz;                                     /**< Block size for database storage. */
   uint32_t relseg_size;                                /**< Blocks per segment for large relations. */

   uint32_t xlog_blcksz;                                /**< Block size for WAL files. */
   uint32_t xlog_seg_size;                              /**< Size of each WAL segment. */

   uint32_t name_data_len;                              /**< Maximum length of catalog names. */
   uint32_t index_max_keys;                             /**< Maximum number of columns in an index. */

   uint32_t toast_max_chunk_size;                       /**< Maximum chunk size in TOAST tables. */
   uint32_t loblksize;                                  /**< Chunk size in pg_largeobject. */

   bool float8_by_val;                                  /**< Indicates if float8/int8 values are passed by value. */

   uint32_t data_checksum_version;                      /**< Version of data checksum, zero if disabled. */

   bool default_char_signedness;                        /**< Indicates if default char is signed. */

   char mock_authentication_nonce[MOCK_AUTH_NONCE_LEN]; /**< Random nonce for authentication. */

   pg_crc32c crc;                                       /**< CRC checksum for control file integrity. */
};

enum control_file_version
{
   CONTROL_FILE_V13,
   CONTROL_FILE_V14,
   CONTROL_FILE_V15,
   CONTROL_FILE_V16,
   CONTROL_FILE_V17,
   CONTROL_FILE_V18,
};

/**
 * @struct control_file_data
 * @brief Wrapper structure to handle different versions of control_file_data.
 *
 * Fields:
 * - version: An enum field containing struct version.
 * - data: A union containing version-specific control file data.
 */
struct control_file_data
{
   enum control_file_version version;           /**< Holds data version */
   union
   {
      struct control_file_data_v13 v13;         /**< Version 13 data */
      struct control_file_data_v13 v14;         /**< Version 14 data */
      struct control_file_data_v13 v15;         /**< Version 15 data */
      struct control_file_data_v13 v16;         /**< Version 16 data */
      struct control_file_data_v17 v17;         /**< Version 17 data */
      struct control_file_data_v18 v18;         /**< Version 18 data */
   } data;                                      /**< Version-specific control file data */
};

/**
 * Read the control data from pg_control file
 * @param server The server
 * @param directory The base directory
 * @param controldata [out] The control data
 * @return 0 on success, 1 if otherwise
 */
int
pgmoneta_read_control_data(int server, char* directory, struct control_file_data** controldata);

#endif // PGMONETA_PG_CONTROL_H
