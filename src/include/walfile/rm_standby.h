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

#ifndef PGMONETA_RM_STANDBY_H
#define PGMONETA_RM_STANDBY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <walfile/rm.h>
#include <walfile/sinval.h>
#include <walfile/wal_reader.h>

#include <stdbool.h>

#define XLOG_STANDBY_LOCK  0x00 /**< XLOG opcode for standby lock. */
#define XLOG_RUNNING_XACTS 0x10 /**< XLOG opcode for running transactions. */
#define XLOG_INVALIDATIONS 0x20 /**< XLOG opcode for invalidations. */

/**
 * @struct xl_standby_lock
 * @brief Represents a standby lock record in XLOG.
 *
 * Contains the transaction ID of the holder of an AccessExclusiveLock,
 * as well as the database and table OIDs.
 */
struct xl_standby_lock
{
   transaction_id xid; /**< Transaction ID of the holder of AccessExclusiveLock. */
   oid db_oid;         /**< Database OID containing the table. */
   oid rel_oid;        /**< OID of the locked table. */
};

/**
 * @struct xl_standby_locks
 * @brief Represents multiple standby lock records in XLOG.
 *
 * Contains an array of standby lock records.
 */
struct xl_standby_locks
{
   int nlocks;                                          /**< Number of entries in the locks array. */
   struct xl_standby_lock locks[FLEXIBLE_ARRAY_MEMBER]; /**< Array of standby lock records. */
};

/**
 * @struct xl_running_xacts
 * @brief Represents running transactions data in XLOG.
 *
 * Contains information about currently running transactions,
 * including the next transaction ID, the oldest running transaction ID,
 * and the latest completed transaction ID.
 */
struct xl_running_xacts
{
   int xcnt;                                   /**< Number of transaction IDs in xids[]. */
   int subxcnt;                                /**< Number of subtransaction IDs in xids[]. */
   bool subxid_overflow;                       /**< Indicates if snapshot overflowed and subxids are missing. */
   transaction_id next_xid;                    /**< Next transaction ID from TransamVariables->next_xid. */
   transaction_id oldest_running_xid;          /**< Oldest running transaction ID (not oldestXmin). */
   transaction_id latest_completed_xid;        /**< Latest completed transaction ID to set xmax. */
   transaction_id xids[FLEXIBLE_ARRAY_MEMBER]; /**< Array of transaction IDs. */
};

/**
 * @struct xl_invalidations
 * @brief Represents invalidation messages in XLOG.
 *
 * Contains information about invalidation messages,
 * including the database and tablespace IDs, and whether
 * to invalidate relcache init files.
 */
struct xl_invalidations
{
   oid dbId;                                                      /**< Database ID. */
   oid tsId;                                                      /**< Tablespace ID. */
   bool relcacheInitFileInval;                                    /**< Indicates if relcache init files should be invalidated. */
   int nmsgs;                                                     /**< Number of shared invalidation messages. */
   union shared_invalidation_message msgs[FLEXIBLE_ARRAY_MEMBER]; /**< Array of invalidation messages. */
};

/**
 * Describes a standby operation from a decoded XLOG record.
 *
 * @param buf The buffer to store the description.
 * @param record The decoded XLOG record containing the standby operation.
 * @return A pointer to the buffer containing the description.
 */
char*
pgmoneta_wal_standby_desc(char* buf, struct decoded_xlog_record* record);

/**
 * Describes invalidation messages from a standby operation.
 *
 * @param buf The buffer to store the description.
 * @param nmsgs The number of invalidation messages.
 * @param msgs The array of invalidation messages.
 * @param dbId The database ID.
 * @param tsId The tablespace ID.
 * @param rel_cache_init_file_inval Indicates if relcache init files should be invalidated.
 * @return A pointer to the buffer containing the description.
 */
char*
pgmoneta_wal_standby_desc_invalidations(char* buf, int nmsgs, union shared_invalidation_message* msgs, oid dbId, oid tsId,
                                        bool rel_cache_init_file_inval);

#ifdef __cplusplus
}
#endif

#endif // PGMONETA_RM_STANDBY_H
