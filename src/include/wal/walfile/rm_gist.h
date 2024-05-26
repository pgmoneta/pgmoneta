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

#ifndef PGMONETA_RM_GIST_H
#define PGMONETA_RM_GIST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <wal/walfile/rm.h>
#include <wal/walfile/wal_reader.h>

typedef xlog_rec_ptr gist_nsn;

#define XLOG_GIST_PAGE_UPDATE  0x00 /**< Update a GIST index page. */
#define XLOG_GIST_DELETE       0x10 /**< Delete leaf index tuples for a page. */
#define XLOG_GIST_PAGE_REUSE   0x20 /**< Old page is about to be reused from FSM. */
#define XLOG_GIST_PAGE_SPLIT   0x30 /**< Split a GIST index page. */
#define XLOG_GIST_PAGE_DELETE  0x60 /**< Delete a GIST index page. */
#define XLOG_GIST_ASSIGN_LSN   0x70 /**< Assign a new LSN, no operation. */

/**
 * @struct gist_xlog_page_update
 * @brief Represents a page update in a GIST index.
 *
 * Contains the number of deleted offsets and the number of inserted tuples.
 */
struct gist_xlog_page_update
{
    uint16_t ntodelete;   /**< Number of deleted offsets. */
    uint16_t ntoinsert;   /**< Number of tuples to insert. */

    /* In payload of blk 0: 1. todelete OffsetNumbers, 2. tuples to insert */
};

/**
 * @struct gist_xlog_delete_v15
 * @brief Represents a delete operation in a GIST index (version 15).
 *
 * This structure contains information necessary for deleting tuples from a GIST index page.
 * It includes the ID of the latest removed transaction and the number of offsets to be deleted.
 *
 * Fields:
 * - latestRemovedXid: The transaction ID of the latest transaction that has been removed from the page.
 *                     This ID is used for conflict resolution during recovery.
 * - ntodelete: The number of offsets that are marked for deletion within the GIST index page.
 */
struct gist_xlog_delete_v15
{
    transaction_id latestRemovedXid;     /**< ID of the latest removed transaction */
    uint16_t ntodelete;                  /**< Number of offsets to delete */

    /*
     * The offsets to delete will be included in the payload of block 0
     */
};

/**
 * @struct gist_xlog_delete_v16
 * @brief Represents a delete operation in a GIST index (version 16).
 *
 * This structure is used to describe a delete operation in a GIST index page.
 * It includes fields for handling snapshot conflict horizons, the number of offsets to delete,
 * and a flag to indicate whether the operation is on a catalog relation. This structure is used
 * during logical decoding and recovery operations to manage conflicts and ensure data integrity.
 *
 * Fields:
 * - snapshot_conflict_horizon_id: The transaction ID horizon up to which snapshot conflicts need to be handled.
 *                            This is important for ensuring that the recovery process can detect conflicts
 *                            between transactions during logical decoding on standby servers.
 * - ntodelete: The number of offsets that are to be deleted within the GIST index page.
 * - is_catalog_rel: A flag indicating whether the delete operation involves a catalog relation.
 *                 This flag is used to handle conflicts during logical decoding on standby servers.
 * - offsets: An array to store the offset numbers of the tuples that are to be deleted from the GIST index page.
 *            This field is marked with FLEXIBLE_ARRAY_MEMBER to allow flexibility in the number of offsets.
 */
struct gist_xlog_delete_v16
{
    transaction_id snapshotConflictHorizon;        /**< Horizon for conflict handling in snapshot */
    uint16_t ntodelete;                            /**< Number of offsets to delete */
    uint8_t is_catalog_rel;                        /**< Boolean to handle recovery conflict during logical decoding on standby */

    offset_number offsets[FLEXIBLE_ARRAY_MEMBER];  /**< Array of offset numbers to delete */
};

/**
 * @struct gist_xlog_delete
 * @brief Wrapper structure for GIST index delete records.
 *
 * Contains version-specific delete record data and function pointers for parsing and formatting.
 */
struct gist_xlog_delete
{
    void (*parse)(struct gist_xlog_delete* wrapper, const void* rec);    /**< Parsing function pointer */
    char* (*format)(struct gist_xlog_delete* wrapper, char* buf);        /**< Formatting function pointer */
    union {
        struct gist_xlog_delete_v15 v15;                                 /**< Version 15 structure */
        struct gist_xlog_delete_v16 v16;                                 /**< Version 16 structure */
    } data;                                                              /**< Version-specific delete record data */
};


/**
 * @struct gist_xlog_page_split
 * @brief Represents a page split operation in a GIST index.
 *
 * Contains information about the original right link, original NSN, and the number of pages in the split.
 *
 * Fields:
 * - origrlink: The original right link of the page before the split.
 * - orignsn: The original NSN (Next Sequential Number) of the page before the split.
 * - origleaf: A flag indicating whether the original page was a leaf page.
 * - npage: The number of pages involved in the split operation.
 * - markfollowright: A flag to set the F_FOLLOW_RIGHT flag, indicating that the split
 *                    page should be followed by future inserts or splits.
 */
struct gist_xlog_page_split
{
    block_number origrlink;          /**< Right link of the page before split. */
    gist_nsn orignsn;                /**< NSN of the page before split. */
    bool origleaf;                   /**< Was the split page a leaf page? */
    uint16_t npage;                  /**< Number of pages in the split. */
    bool markfollowright;            /**< Set F_FOLLOW_RIGHT flags. */

    /* Follow: 1. gistxlogPage and array of IndexTupleData per page */
};

/**
 * @struct gist_xlog_page_delete
 * @brief Represents a page delete operation in a GIST index.
 *
 * Contains the full transaction ID and the offset of the downlink referencing this page.
 *
 * Fields:
 * - deleteXid: The full transaction ID of the last transaction that could see the page during a scan.
 * - downlinkOffset: The offset of the downlink in the parent page that references this page.
 */
struct gist_xlog_page_delete
{
    struct full_transaction_id deleteXid;        /**< Last Xid which could see page in scan. */
    offset_number downlinkOffset;                /**< Offset of downlink referencing this page. */
};

#define SIZE_OF_GISTXLOG_PAGE_DELETE (offsetof(struct gist_xlog_page_delete, downlinkOffset) + sizeof(OffsetNumber))

/**
 * @struct gist_xlog_page_reuse_v15
 * @brief Represents a page reuse operation in a GIST index (version 15).
 *
 * Contains information necessary to reuse a page in a GIST index during hot standby.
 *
 * Fields:
 * - node: The RelFileNode identifying the relation.
 * - block: The block number being reused.
 * - latestRemovedFullXid: The full transaction ID of the latest removed transaction.
 */
struct gist_xlog_page_reuse_v15
{
    struct rel_file_node node;                       /**< RelFileNode for the page. */
    block_number block;                              /**< Block number being reused. */
    struct full_transaction_id latestRemovedFullXid; /**< Latest removed full transaction ID. */
};

/**
 * @struct gist_xlog_page_reuse_v16
 * @brief Represents a page reuse operation in a GIST index (version 16).
 *
 * Contains information necessary to reuse a page in a GIST index during hot standby,
 * including handling recovery conflicts during logical decoding on standby.
 *
 * Fields:
 * - locator: The RelFileLocator identifying the relation.
 * - block: The block number being reused.
 * - snapshot_conflict_horizon_id: The transaction ID horizon for conflict handling in snapshot.
 * - is_catalog_rel: A flag indicating whether the operation involves a catalog relation,
 *                 to handle recovery conflict during logical decoding on standby.
 */
struct gist_xlog_page_reuse_v16 {
    struct rel_file_locator locator;                         /**< RelFileLocator for the page. */
    block_number block;                                      /**< Block number being reused. */
    struct full_transaction_id snapshot_conflict_horizon;    /**< Horizon for conflict handling in snapshot */
    bool is_catalog_rel;                                     /**< Boolean to handle recovery conflict during logical decoding on standby */
};

/**
 * @struct gist_xlog_page_reuse
 * @brief Wrapper structure for GIST index page reuse records.
 *
 * Contains version-specific page reuse record data and function pointers for parsing and formatting.
 */
struct gist_xlog_page_reuse {
    void (*parse)(struct gist_xlog_page_reuse* wrapper, const void* rec); /**< Parsing function pointer */
    char* (*format)(struct gist_xlog_page_reuse* wrapper, char* buf);     /**< Formatting function pointer */
    union {
        struct gist_xlog_page_reuse_v15 v15;                              /**< Version 15 structure */
        struct gist_xlog_page_reuse_v16 v16;                              /**< Version 16 structure */
    } data;                                                               /**< Version-specific page reuse record data */
};


/**
 * @brief Creates a new gist_xlog_delete structure.
 *
 * @return A pointer to the newly created gist_xlog_delete structure.
 */
struct gist_xlog_delete*
create_gist_xlog_delete(void);

/**
 * @brief Parses a version 15 GIST index delete record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param rec The record to parse.
 */
void
pgmoneta_wal_parse_gist_xlog_delete_v15(struct gist_xlog_delete* wrapper, const void* rec);

/**
 * @brief Parses a version 16 GIST index delete record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param rec The record to parse.
 */
void
pgmoneta_wal_parse_gist_xlog_delete_v16(struct gist_xlog_delete* wrapper, const void* rec);

/**
 * @brief Formats a version 15 GIST index delete record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char*
pgmoneta_wal_format_gist_xlog_delete_v15(struct gist_xlog_delete* wrapper, char* buf);

/**
 * @brief Formats a version 16 GIST index delete record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char*
pgmoneta_wal_format_gist_xlog_delete_v16(struct gist_xlog_delete* wrapper, char* buf);

/**
 * @brief Describes a GIST index operation from a decoded XLOG record.
 *
 * @param buf The buffer to store the description.
 * @param record The decoded XLOG record containing the GIST operation.
 * @return A pointer to the buffer containing the description.
 */
char*
pgmoneta_wal_gist_desc(char* buf, struct decoded_xlog_record* record);

/**
 * @brief Creates a new gist_xlog_page_reuse structure.
 *
 * @return A pointer to the newly created gist_xlog_page_reuse structure.
 */
struct gist_xlog_page_reuse*
create_gist_xlog_page_reuse(void);

/**
 * @brief Parses a version 15 GIST index page reuse record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param rec The record to parse.
 */
void
pgmoneta_wal_parse_gist_xlog_page_reuse_v15(struct gist_xlog_page_reuse* wrapper, const void* rec);

/**
 * @brief Parses a version 16 GIST index page reuse record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param rec The record to parse.
 */
void
pgmoneta_wal_parse_gist_xlog_page_reuse_v16(struct gist_xlog_page_reuse* wrapper, const void* rec);

/**
 * @brief Formats a version 15 GIST index page reuse record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char*
pgmoneta_wal_format_gist_xlog_page_reuse_v15(struct gist_xlog_page_reuse* wrapper, char* buf);

/**
 * @brief Formats a version 16 GIST index page reuse record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char*
pgmoneta_wal_format_gist_xlog_page_reuse_v16(struct gist_xlog_page_reuse* wrapper, char* buf);

#ifdef __cplusplus
}
#endif

#endif // PGMONETA_RM_GIST_H
