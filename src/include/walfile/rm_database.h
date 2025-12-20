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

#ifndef PGMONETA_RM_DATABASE_H
#define PGMONETA_RM_DATABASE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <walfile/wal_reader.h>

// #define variables
#define XLOG_DBASE_CREATE 0x00 /**< Create database log type */
#define XLOG_DBASE_DROP   0x10 /**< Drop database log type */

// v17 and later
#define XLOG_DBASE_CREATE_FILE_COPY 0x00 /**< Create database log type */
#define XLOG_DBASE_CREATE_WAL_LOG   0x10 /**< Create database log type */
#define XLOG_DBASE_DROP_V17         0x20 /**< Drop database log type */

// #define macros
#define MIN_SIZE_OF_DBASE_DROP_REC offsetof(xl_dbase_drop_rec, tablespace_ids) /**< Minimum size of xl_dbase_drop_rec */

// Structs
/**
 * @struct xl_dbase_create_rec
 * @brief Represents the creation of a database, including the copying of a single subdirectory and its contents.
 *
 * Fields:
 * - db_id: Identifier for the database being created.
 * - tablespace_id: Identifier for the tablespace of the database.
 * - src_db_id: Identifier for the source database.
 * - src_tablespace_id: Identifier for the source tablespace.
 */
struct xl_dbase_create_rec
{
   oid db_id;             /**< Database ID */
   oid tablespace_id;     /**< Tablespace ID */
   oid src_db_id;         /**< Source Database ID */
   oid src_tablespace_id; /**< Source Tablespace ID */
};

/**
 * @struct xl_dbase_drop_rec
 * @brief Represents the dropping of a database, including the removal of associated tablespaces.
 *
 * Fields:
 * - db_id: Identifier for the database being dropped.
 * - ntablespaces: Number of tablespace IDs associated with the database.
 * - tablespace_ids: Array of tablespace IDs.
 */
struct xl_dbase_drop_rec
{
   oid db_id;                                 /**< Database ID */
   int ntablespaces;                          /**< Number of tablespace IDs */
   oid tablespace_ids[FLEXIBLE_ARRAY_MEMBER]; /**< Array of tablespace IDs */
};

/**
 * @struct xl_dbase_create_file_copy_rec
 * @brief Represents the record for copying a file during database creation, copying both the database and tablespace.
 *
 * Fields:
 * - db_id: Identifier for the database being created.
 * - tablespace_id: Identifier for the tablespace associated with the database being created.
 * - src_db_id: Identifier for the source database being copied.
 * - src_tablespace_id: Identifier for the source tablespace associated with the source database.
 */
struct xl_dbase_create_file_copy_rec
{
   oid db_id;             /**< Database ID */
   oid tablespace_id;     /**< Tablespace ID */
   oid src_db_id;         /**< Source Database ID */
   oid src_tablespace_id; /**< Source Tablespace ID */
};

/**
 * @struct xl_dbase_create_wal_log_rec
 * @brief Represents a write-ahead log (WAL) record for creating a database and logging the new tablespace.
 *
 * Fields:
 * - db_id: Identifier for the database being created.
 * - tablespace_id: Identifier for the tablespace associated with the new database.
 */
struct xl_dbase_create_wal_log_rec
{
   oid db_id;         /**< Database ID */
   oid tablespace_id; /**< Tablespace ID */
};

// Functions
/**
 * @brief Describes a database record in a human-readable format.
 *
 * @param buf The buffer to hold the description string.
 * @param record The decoded XLOG record to be described.
 * @return A pointer to the buffer containing the description.
 */
char*
pgmoneta_wal_database_desc(char* buf, struct decoded_xlog_record* record);

#ifdef __cplusplus
}
#endif

#endif // PGMONETA_RM_DATABASE_H
