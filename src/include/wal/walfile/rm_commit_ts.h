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

#ifndef PGMONETA_RM_COMMIT_TS_H
#define PGMONETA_RM_COMMIT_TS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <wal/walfile/rm.h>
#include <wal/walfile/wal_reader.h>

/* XLOG stuff */
#define COMMIT_TS_ZEROPAGE      0x00
#define COMMIT_TS_TRUNCATE      0x10

/**
 * @struct xl_commit_ts_set
 * @brief Represents a commit timestamp set record.
 *
 * Fields:
 * - timestamp: The commit timestamp.
 * - nodeid: The replication origin node ID.
 * - mainxid: The main transaction ID.
 * - subxact Xids follow: Subsequent subtransaction IDs.
 */
struct xl_commit_ts_set {
    timestamp_tz timestamp;       /**< Commit timestamp */
    rep_origin_id nodeid;         /**< Replication origin node ID */
    transaction_id mainxid;       /**< Main transaction ID */
    /* subxact Xids follow */
};

/**
 * @struct xl_commit_ts_truncate_17
 * @brief Represents a commit timestamp truncate record for version 17.
 *
 * Fields:
 * - pageno: The page number to truncate.
 * - oldestXid: The oldest transaction ID.
 */
struct xl_commit_ts_truncate_17 {
    int64_t pageno;               /**< Page number to truncate */
    transaction_id oldestXid;     /**< Oldest transaction ID */
};

/**
 * @struct xl_commit_ts_truncate_16
 * @brief Represents a commit timestamp truncate record for version 16.
 *
 * Fields:
 * - pageno: The page number to truncate.
 * - oldestXid: The oldest transaction ID.
 */
struct xl_commit_ts_truncate_16 {
    int pageno;                   /**< Page number to truncate */
    transaction_id oldestXid;     /**< Oldest transaction ID */
};

/**
 * @struct xl_commit_ts_truncate
 * @brief Wrapper structure for commit timestamp truncate records.
 *
 * Fields:
 * - data: A union containing version-specific truncate record data.
 * - parse: Function pointer to parse the record.
 * - format: Function pointer to format the record.
 */
struct xl_commit_ts_truncate {
    void (*parse)(struct xl_commit_ts_truncate* wrapper, char* rec);    /**< Function pointer to parse the record */
    char* (*format)(struct xl_commit_ts_truncate* wrapper, char* buf);  /**< Function pointer to format the record */
    union {
        struct xl_commit_ts_truncate_16 v16;                            /**< Truncate record for version 16 */
        struct xl_commit_ts_truncate_17 v17;                            /**< Truncate record for version 17 */
    } data;                                                             /**< Version-specific truncate record data */
};

/**
 * @brief Creates a new xl_commit_ts_truncate structure.
 *
 * @return A pointer to the newly created xl_commit_ts_truncate structure.
 */
struct xl_commit_ts_truncate*
create_xl_commit_ts_truncate(void);

/**
 * @brief Parses a version 16 commit timestamp truncate record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param rec The record to parse.
 */
void
xl_commit_ts_truncate_parse_v16(struct xl_commit_ts_truncate* wrapper, char* rec);

/**
 * @brief Parses a version 17 commit timestamp truncate record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param rec The record to parse.
 */
void
xl_commit_ts_truncate_parse_v17(struct xl_commit_ts_truncate* wrapper, char* rec);

/**
 * @brief Formats a version 16 commit timestamp truncate record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char*
xl_commit_ts_truncate_format_v16(struct xl_commit_ts_truncate* wrapper, char* buf);

/**
 * @brief Formats a version 17 commit timestamp truncate record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char*
xl_commit_ts_truncate_format_v17(struct xl_commit_ts_truncate* wrapper, char* buf);

/**
 * @brief Describes a commit timestamp record.
 *
 * @param buf The buffer to store the description.
 * @param record The decoded XLOG record to describe.
 * @return A pointer to the description string.
 */
char*
pgmoneta_wal_commit_ts_desc(char* buf, struct decoded_xlog_record* record);

#ifdef __cplusplus
}
#endif

#endif // PGMONETA_RM_COMMIT_TS_H
