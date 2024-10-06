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

#ifndef PGMONETA_RM_XLOG_H
#define PGMONETA_RM_XLOG_H

#include <walfile/wal_reader.h>

#define MAXFNAMELEN                  64
#define MAXDATELEN                   128
#define UNIX_EPOCH_JDATE             2440588    /**< == date2j(1970, 1, 1) */
#define POSTGRES_EPOCH_JDATE         2451545    /**< == date2j(2000, 1, 1) */
#define SECS_PER_DAY                 86400

#define INTpgmoneta_wal_64CONST(x)   (x ## L)
#define USECS_PER_SEC                INTpgmoneta_wal_64CONST(1000000)

/**
 * @struct xl_parameter_change
 * @brief Structure representing a change in parameters important for Hot Standby.
 *
 * Fields:
 * - max_connections: Maximum number of connections.
 * - max_worker_processes: Maximum number of worker processes.
 * - max_wal_senders: Maximum number of WAL senders.
 * - max_prepared_xacts: Maximum number of prepared transactions.
 * - max_locks_per_xact: Maximum number of locks per transaction.
 * - wal_level: WAL level.
 * - wal_log_hints: WAL log hints.
 * - track_commit_timestamp: Track commit timestamp.
 */
struct xl_parameter_change
{
   int max_connections;              /**< Maximum number of connections */
   int max_worker_processes;         /**< Maximum number of worker processes */
   int max_wal_senders;              /**< Maximum number of WAL senders */
   int max_prepared_xacts;           /**< Maximum number of prepared transactions */
   int max_locks_per_xact;           /**< Maximum number of locks per transaction */
   int wal_level;                    /**< WAL level */
   bool wal_log_hints;               /**< WAL log hints */
   bool track_commit_timestamp;      /**< Track commit timestamp */
};

/**
 * @struct xl_restore_point
 * @brief Structure representing a restore point log.
 *
 * Fields:
 * - rp_time: Restore point timestamp.
 * - rp_name: Restore point name.
 */
struct xl_restore_point
{
   timestamp_tz rp_time;                /**< Restore point timestamp */
   char rp_name[MAXFNAMELEN];           /**< Restore point name */
};

/**
 * @struct xl_overwrite_contrecord
 * @brief Structure representing an overwrite of a prior contrecord.
 *
 * Fields:
 * - overwritten_lsn: Overwritten Log Sequence Number (LSN).
 * - overwrite_time: Time of overwrite.
 */
struct xl_overwrite_contrecord
{
   xlog_rec_ptr overwritten_lsn;        /**< Overwritten Log Sequence Number (LSN) */
   timestamp_tz overwrite_time;         /**< Time of overwrite */
};

/**
 * @struct xl_end_of_recovery_v17
 * @brief Structure representing the end of recovery log for version 17.
 *
 * Fields:
 * - end_time: End time of recovery.
 * - ThisTimeLineID: The new timeline ID.
 * - PrevTimeLineID: The previous timeline ID.
 * - wal_level: The WAL level.
 */
struct xl_end_of_recovery_v17
{
   timestamp_tz end_time;                /**< End time of recovery */
   timeline_id this_timeline_id;         /**< New timeline ID */
   timeline_id prev_timeline_id;         /**< Previous timeline ID */
   int wal_level;                        /**< WAL level */
};

/**
 * @struct xl_end_of_recovery_v16
 * @brief Structure representing the end of recovery log for version 16.
 *
 * Fields:
 * - end_time: End time of recovery.
 * - ThisTimeLineID: The new timeline ID.
 * - PrevTimeLineID: The previous timeline ID.
 */
struct xl_end_of_recovery_v16
{
   timestamp_tz end_time;                /**< End time of recovery */
   timeline_id this_timeline_id;         /**< New timeline ID */
   timeline_id prev_timeline_id;         /**< Previous timeline ID */
};

/**
 * @struct xl_end_of_recovery
 * @brief Wrapper structure to handle different versions of end of recovery logs.
 *
 * Fields:
 * - data: A union containing version-specific recovery log data.
 * - parse: Function pointer to parse the record.
 * - format: Function pointer to format the record.
 */
struct xl_end_of_recovery
{
   void (*parse)(struct xl_end_of_recovery* wrapper, const void* rec);     /**< Function pointer to parse the record */
   char* (*format)(struct xl_end_of_recovery* wrapper, char* buf);         /**< Function pointer to format the record */
   union
   {
      struct xl_end_of_recovery_v17 v17;                                   /**< End of recovery data for version 17 */
      struct xl_end_of_recovery_v16 v16;                                   /**< End of recovery data for version 16 */
   } data;                                                                 /**< Version-specific recovery log data */
};

/**
 * @struct config_enum_entry
 * @brief Structure representing an enumeration entry in the configuration.
 *
 * Fields:
 * - name: Name of the configuration entry.
 * - val: Value of the configuration entry.
 * - hidden: Indicates if the entry is hidden.
 */
struct config_enum_entry
{
   const char* name;       /**< Name of the configuration entry */
   int val;                /**< Value of the configuration entry */
   bool hidden;            /**< Indicates if the entry is hidden */
};

/**
 * @brief Creates a new xl_end_of_recovery structure.
 *
 * @return A pointer to the newly created xl_end_of_recovery structure.
 */
struct xl_end_of_recovery*
create_xl_end_of_recovery(void);

/**
 * @brief Parses a version 17 end of recovery record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param rec The record to parse.
 */
void
xl_end_of_recovery_parse_v17(struct xl_end_of_recovery* wrapper, const void* rec);

/**
 * @brief Parses a version 16 end of recovery record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param rec The record to parse.
 */
void
xl_end_of_recovery_parse_v16(struct xl_end_of_recovery* wrapper, const void* rec);

/**
 * @brief Formats a version 17 end of recovery record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char*
xl_end_of_recovery_format_v17(struct xl_end_of_recovery* wrapper, char* buf);

/**
 * @brief Formats a version 16 end of recovery record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char*
xl_end_of_recovery_format_v16(struct xl_end_of_recovery* wrapper, char* buf);

/**
 * @brief Convert a timestamp to a string.
 *
 * @param dt The timestamp to convert.
 * @return const char* The string representation of the timestamp.
 */
const char*
pgmoneta_wal_timestamptz_to_str(timestamp_tz dt);

/**
 * @brief Describe an XLOG record.
 *
 * @param buf The buffer to store the description.
 * @param record The decoded XLOG record.
 * @return char* The buffer containing the description of the XLOG record.
 */
char*
pgmoneta_wal_xlog_desc(char* buf, struct decoded_xlog_record* record);

#endif /* PGMONETA_RM_XLOG_H */
