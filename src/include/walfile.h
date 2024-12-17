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

#ifndef PGMONETA_WALFILE_H
#define PGMONETA_WALFILE_H

#include <deque.h>
#include <walfile/wal_reader.h>

/**
 * @struct xlog_long_page_header_data
 * @brief Extended XLOG page header.
 *
 * Extends `xlog_page_header_data` with additional fields such as
 * system identifier, segment size, and block size.
 *
 * Fields:
 * - std: Standard header fields.
 * - xlp_sysid: System identifier from pg_control.
 * - xlp_seg_size: Segment size for cross-checking.
 * - xlp_xlog_blcksz: XLOG block size for cross-checking.
 */
struct xlog_long_page_header_data
{
   struct xlog_page_header_data std;       /**< Standard header fields. */
   uint64_t xlp_sysid;                     /**< System identifier from pg_control. */
   uint32_t xlp_seg_size;                  /**< Segment size for cross-checking. */
   uint32_t xlp_xlog_blcksz;               /**< XLOG block size for cross-checking. */
};

/**
 * @struct walfile
 * @brief Represents a WAL (Write-Ahead Log) file in a Postgres.
 *
 * WAL files consist of a series of pages, each with a size of 8192 bytes by default.
 * The first page in a WAL file contains a long header (`struct xlog_long_page_header_data`),
 * while all subsequent pages have a standard header (`struct xlog_page_header_data`).
 *
 * Each WAL page holds a collection of records that represent the changes made to the database. These records
 * include both metadata and the actual data that must be recovered in case of a system failure.
 *
 * Fields:
 *   - magic_number: This field indicates the PostgreSQL version that created the WAL file. It is used to validate
 *                   compatibility between the WAL file and the PostgreSQL version handling recovery. This ensures
 *                   that the WAL file can be interpreted correctly by the corresponding PostgreSQL version.
 *   - long_phd: A pointer to the first page header. The first page header is an extended page that contains
 *               the standard page header plus a few more fields.
 *   - page_headers: A deque that holds the headers of each page (excluding the first long page header)
 *                   within the WAL file.
 *                   Each element has a `struct xlog_page_header_data` data type.
 *                   Each page contains metadata about the organization of that page.
 *   - records: A deque that holds the WAL records stored in the WAL file.
 *              Each element has a `struct decoded_xlog_record` data type.
 */
struct walfile
{
   uint32_t magic_number;                         /**< Magic number for the WAL file. */
   struct xlog_long_page_header_data* long_phd;   /**< Extended XLOG page header. */
   struct deque* page_headers;                    /**< Deque of page headers in the WAL file. */
   struct deque* records;                         /**< Deque of records in the WAL file. */
};

/**
 * Read a WAL file
 * @param server The server index
 * @param path The path to the WAL file
 * @param wf The WAL file structure to populate
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_read_walfile(int server, char* path, struct walfile** wf);

/**
 * Write a WAL file
 * @param wf The WAL file structure
 * @param server The server index
 * @param path The path to the WAL file
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_write_walfile(struct walfile* wf, int server, char* path);

/**
 * Destroy a WAL file
 * @param wf The WAL file structure
 */
void
pgmoneta_destroy_walfile(struct walfile* wf);

/**
 * Describe a WAL file
 * @param path The path to the WAL file
 * @param type The type of output description
 * @param output The output descriptor
 * @param quiet Is the WAL file printed
 * @param color Are colors used
 * @param rms The resource managers
 * @param start_lsn The start LSN
 * @param end_lsn The end LSN
 * @param xids The XIDs
 * @param limit The limit
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_describe_walfile(char* path, enum value_type type, char* output, bool quiet, bool color,
                          struct deque* rms, uint64_t start_lsn, uint64_t end_lsn, struct deque* xids,
                          uint32_t limit);

#endif //PGMONETA_WALFILE_H
