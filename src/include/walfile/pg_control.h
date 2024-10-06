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

/**
 * @struct check_point_v16
 * @brief Represents a checkpoint record for version 16.
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
struct check_point_v16
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
   void (*parse)(struct check_point* wrapper, const void* rec);     /**< Parse function pointer */
   char* (*format)(struct check_point* wrapper, char* buf);          /**< Format function pointer */
   union
   {
      struct check_point_v16 v16;       /**< Version 16 data */
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
check_point_parse_v16(struct check_point* wrapper, const void* rec);

/**
 * @brief Parses a version 17 checkpoint record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param rec The record to parse.
 */
void
check_point_parse_v17(struct check_point* wrapper, const void* rec);

/**
 * @brief Formats a version 16 checkpoint record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char*
check_point_format_v16(struct check_point* wrapper, char* buf);

/**
 * @brief Formats a version 17 checkpoint record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char*
check_point_format_v17(struct check_point* wrapper, char* buf);

#endif // PGMONETA_PG_CONTROL_H
