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

#ifndef PGMONETA_RM_BRIN_H
#define PGMONETA_RM_BRIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <walfile/rm.h>
#include <walfile/wal_reader.h>

// WAL record definitions for BRIN's WAL operations
#define XLOG_BRIN_CREATE_INDEX      0x00
#define XLOG_BRIN_INSERT            0x10
#define XLOG_BRIN_UPDATE            0x20
#define XLOG_BRIN_SAMEPAGE_UPDATE   0x30
#define XLOG_BRIN_REVMAP_EXTEND     0x40
#define XLOG_BRIN_DESUMMARIZE       0x50

#define XLOG_BRIN_OPMASK            0x70

#define XLOG_BRIN_INIT_PAGE         0x80

/**
 * @struct xl_brin_createidx
 * @brief Contains information required to create a BRIN index.
 *
 * Fields:
 * - pagesPerRange: Number of pages per range.
 * - version: Version of the BRIN index.
 */
struct xl_brin_createidx
{
    block_number pagesPerRange;  /**< Number of pages per range */
    uint16_t version;            /**< Version of the BRIN index */
};

/**
 * @struct xl_brin_insert
 * @brief Holds the necessary data for a BRIN tuple insert operation.
 *
 * Fields:
 * - heapBlk: Block number of the heap.
 * - pagesPerRange: Extra information for revmap update.
 * - offnum: Offset number for inserting the tuple on the main page.
 */
struct xl_brin_insert
{
    block_number heapBlk;         /**< Block number of the heap */
    block_number pagesPerRange;   /**< Pages per range information */
    offset_number offnum;         /**< Offset number for tuple insertion */
};

/**
 * @struct xl_brin_update
 * @brief Stores information for a BRIN cross-page update, similar to an insert but also includes details about
 * the old tuple.
 *
 * Fields:
 * - oldOffnum: Offset number of the old tuple on the old page.
 * - insert: Data for the insert operation.
 */
struct xl_brin_update
{
    offset_number oldOffnum;  /**< Offset number of old tuple on old page */
    struct xl_brin_insert insert;  /**< Insert operation data */
};

/**
 * @struct xl_brin_samepage_update
 * @brief Holds data for a BRIN tuple samepage update.
 *
 * Fields:
 * - offnum: Offset number of the updated tuple on the same page.
 */
struct xl_brin_samepage_update
{
    offset_number offnum;  /**< Offset number of the updated tuple */
};

/**
 * @struct xl_brin_revmap_extend
 * @brief Contains information for a revmap extension.
 *
 * Fields:
 * - targetBlk: Target block number, which is redundant as it is part of backup block 1.
 */
struct xl_brin_revmap_extend
{
    block_number targetBlk;  /**< Target block number */
};


/**
 * @struct xl_brin_desummarize
 * @brief Holds data required for range de-summarization in a BRIN index.
 *
 * Fields:
 * - pagesPerRange: Number of pages per range.
 * - heapBlk: Page number location to set to invalid.
 * - regOffset: Offset of the item to delete in the regular index page.
 */
struct xl_brin_desummarize
{
    block_number pagesPerRange;  /**< Number of pages per range */
    block_number heapBlk;        /**< Page number location to set to invalid */
    offset_number regOffset;     /**< Offset of item to delete in the regular index page */
};

/**
 * @brief Provides a description of a BRIN operation for logging purposes.
 *
 * @param buf A buffer to hold the resulting description.
 * @param record A pointer to the decoded WAL record structure.
 * @return A pointer to the buffer containing the description.
 */
char*
pgmoneta_wal_brin_desc(char* buf, struct decoded_xlog_record* record);

#ifdef __cplusplus
}
#endif

#endif // PGMONETA_RM_BRIN_H
