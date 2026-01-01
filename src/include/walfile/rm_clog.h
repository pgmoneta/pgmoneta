/*
 * Copyright (C) 2026 The pgmoneta community
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

#ifndef PGMONETA_RM_CLOG_H
#define PGMONETA_RM_CLOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <walfile/wal_reader.h>

#define TRANSACTION_STATUS_IN_PROGRESS   0x00 /**< Transaction is in progress */
#define TRANSACTION_STATUS_COMMITTED     0x01 /**< Transaction has been committed */
#define TRANSACTION_STATUS_ABORTED       0x02 /**< Transaction has been aborted */
#define TRANSACTION_STATUS_SUB_COMMITTED 0x03 /**< Transaction is sub-committed */

#define CLOG_ZEROPAGE                    0x00 /**< CLOG zero page */
#define CLOG_TRUNCATE                    0x10 /**< CLOG truncate */

/**
 * @struct xl_clog_truncate_17
 * @brief Represents a CLOG truncate record for PostgreSQL version 17.
 *
 * This structure holds information related to the truncation of the
 * commit log (CLOG) in PostgreSQL version 17.
 *
 * Fields:
 * - pageno: The page number of the CLOG to truncate.
 * - oldestXact: The oldest transaction ID to retain.
 * - oldestXactDb: The database ID of the oldest transaction to retain.
 */
struct xl_clog_truncate_17
{
   int pageno;                /**< The page number of the CLOG to truncate */
   transaction_id oldestXact; /**< The oldest transaction ID to retain */
   oid oldestXactDb;          /**< The database ID of the oldest transaction */
};

/**
 * @struct xl_clog_truncate_16
 * @brief Represents a CLOG truncate record for PostgreSQL version 16.
 *
 * This structure holds information related to the truncation of the
 * commit log (CLOG) in PostgreSQL version 16.
 *
 * Fields:
 * - pageno: The page number of the CLOG to truncate.
 * - oldestXact: The oldest transaction ID to retain.
 * - oldestXactDb: The database ID of the oldest transaction to retain.
 */
struct xl_clog_truncate_16
{
   int64_t pageno;            /**< The page number of the CLOG to truncate */
   transaction_id oldestXact; /**< The oldest transaction ID to retain */
   oid oldestXactDb;          /**< The database ID of the oldest transaction */
};

/**
 * @struct xl_clog_truncate
 * @brief Represents a CLOG truncate record wrapper.
 *
 * This structure is a version-agnostic wrapper for the CLOG truncate
 * records in different PostgreSQL versions. It contains a union of
 * version-specific structures and function pointers for parsing and
 * formatting the records.
 *
 * Fields:
 * - pg_version: The PostgreSQL version of the CLOG truncate record.
 * - data: A union containing version-specific truncate record structures.
 * - parse: A function pointer to parse the truncate record.
 * - format: A function pointer to format the truncate record.
 */
struct xl_clog_truncate
{
   void (*parse)(struct xl_clog_truncate* wrapper, char* rec);   /**< Function pointer to parse the record */
   char* (*format)(struct xl_clog_truncate* wrapper, char* buf); /**< Function pointer to format the record */
   union
   {
      struct xl_clog_truncate_16 v16; /**< Truncate record for version 16 */
      struct xl_clog_truncate_17 v17; /**< Truncate record for version 17 */
   } data;                            /**< Version-specific truncate record data */
};

/**
 * @brief Creates a new xl_clog_truncate structure.
 *
 * @return A pointer to the newly created xl_clog_truncate structure.
 */
struct xl_clog_truncate* create_xl_clog_truncate(void);

/**
 * @brief Parses a version 16 CLOG truncate record.
 *
 * @param wrapper A pointer to the xl_clog_truncate structure.
 * @param rec A pointer to the raw record data to parse.
 */
void xl_clog_truncate_parse_v16(struct xl_clog_truncate* wrapper, char* rec);

/**
 * @brief Parses a version 17 CLOG truncate record.
 *
 * @param wrapper A pointer to the xl_clog_truncate structure.
 * @param rec A pointer to the raw record data to parse.
 */
void xl_clog_truncate_parse_v17(struct xl_clog_truncate* wrapper, char* rec);

/**
 * @brief Formats a version 16 CLOG truncate record.
 *
 * @param wrapper A pointer to the xl_clog_truncate structure.
 * @param buf A buffer to store the formatted output.
 * @return A pointer to the buffer containing the formatted output.
 */
char* xl_clog_truncate_format_v16(struct xl_clog_truncate* wrapper, char* buf);

/**
 * @brief Formats a version 17 CLOG truncate record.
 *
 * @param wrapper A pointer to the xl_clog_truncate structure.
 * @param buf A buffer to store the formatted output.
 * @return A pointer to the buffer containing the formatted output.
 */
char* xl_clog_truncate_format_v17(struct xl_clog_truncate* wrapper, char* buf);

/**
 * @brief Describes a CLOG WAL record.
 *
 * This function generates a human-readable description of a CLOG
 * Write-Ahead Logging (WAL) record.
 *
 * @param buf A buffer to store the description.
 * @param record The decoded XLOG record to describe.
 * @return A pointer to the buffer containing the description.
 */
char* pgmoneta_wal_clog_desc(char* buf, struct decoded_xlog_record* record);

#ifdef __cplusplus
}
#endif

#endif // PGMONETA_RM_CLOG_H
