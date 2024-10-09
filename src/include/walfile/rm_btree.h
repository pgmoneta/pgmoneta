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

#ifndef PGMONETA_RM_BTREE_H
#define PGMONETA_RM_BTREE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <walfile/rm.h>
#include <walfile/transaction.h>
#include <walfile/wal_reader.h>

#include <stdint.h>

#define INVALID_OFFSET_NUMBER          ((offset_number) 0)
#define FIRST_OFFSET_NUMBER            ((offset_number) 1)
#define MAX_OFFSET_NUMBER              ((offset_number) (8192 / sizeof(struct item_id_data))) // TODO: Replace 8192 with block size from pg_control

#define XLOG_BTREE_INSERT_LEAF         0x00  /**< Add index tuple without split */
#define XLOG_BTREE_INSERT_UPPER        0x10  /**< Same, on a non-leaf page */
#define XLOG_BTREE_INSERT_META         0x20  /**< Same, plus update metapage */
#define XLOG_BTREE_SPLIT_L             0x30  /**< Add index tuple with split */
#define XLOG_BTREE_SPLIT_R             0x40  /**< As above, new item on right */
#define XLOG_BTREE_INSERT_POST         0x50  /**< Add index tuple with posting split */
#define XLOG_BTREE_DEDUP               0x60  /**< Deduplicate tuples for a page */
#define XLOG_BTREE_DELETE              0x70  /**< Delete leaf index tuples for a page */
#define XLOG_BTREE_UNLINK_PAGE         0x80  /**< Delete a half-dead page */
#define XLOG_BTREE_UNLINK_PAGE_META    0x90  /**< Same, and update metapage */
#define XLOG_BTREE_NEWROOT             0xA0  /**< New root page */
#define XLOG_BTREE_MARK_PAGE_HALFDEAD  0xB0  /**< Mark a leaf as half-dead */
#define XLOG_BTREE_VACUUM              0xC0  /**< Delete entries on a page during vacuum */
#define XLOG_BTREE_REUSE_PAGE          0xD0  /**< Old page is about to be reused from FSM */
#define XLOG_BTREE_META_CLEANUP        0xE0  /**< Update cleanup-related data in the metapage */

#define SIZE_OF_BTREE_UPDATE           (offsetof(struct xl_btree_update, ndeletedtids) + sizeof(uint16_t))

#define OFFSET_NUMBER_IS_VALID(offsetNumber) \
        ((bool) ((offsetNumber != INVALID_OFFSET_NUMBER) && \
                 (offsetNumber <= MAX_OFFSET_NUMBER)))

/**
 * @struct item_id_data
 * @brief Describes a line pointer on a page in a B-tree.
 *
 * Fields:
 * - lp_off: Offset to tuple (from start of page).
 * - lp_flags: State of line pointer.
 * - lp_len: Byte length of tuple.
 */
struct item_id_data
{
   unsigned lp_off : 15;         /**< Offset to tuple (from start of page) */
   unsigned lp_flags : 2;        /**< State of line pointer */
   unsigned lp_len : 15;         /**< Byte length of tuple */
};

/**
 * @struct xl_btree_metadata_v13
 * @brief Represents the btree metadata for version 13.
 *
 * This data structure is used to store metadata about a btree for version 13.
 */
struct xl_btree_metadata_v13
{
   uint32_t version;                           /**< Version number. */
   block_number root;                          /**< Block number of the root. */
   uint32_t level;                             /**< Level of the btree. */
   block_number fastroot;                      /**< Fast root block number. */
   uint32_t fastlevel;                         /**< Fast root level. */
   transaction_id oldest_btpo_xact;            /**< Oldest B-tree page transaction. */
   double last_cleanup_num_heap_tuples;        /**< Number of heap tuples after last cleanup. */
   bool allequalimage;                         /**< All equal image flag. */
};

/**
 * @struct xl_btree_metadata_v14
 * @brief Represents the btree metadata for version 14.
 *
 * This data structure is used to store metadata about a btree for version 14.
 */
struct xl_btree_metadata_v14
{
   uint32_t version;                        /**< Version number. */
   block_number root;                       /**< Block number of the root. */
   uint32_t level;                          /**< Level of the btree. */
   block_number fastroot;                   /**< Fast root block number. */
   uint32_t fastlevel;                      /**< Fast root level. */
   uint32_t last_cleanup_num_delpages;      /**< Number of deleted pages after last cleanup. */
   bool allequalimage;                      /**< All equal image flag. */
};

/**
 * @struct xl_btree_metadata
 * @brief A wrapper struct that holds either version 13 or 14 of btree metadata.
 *
 * This structure provides methods to parse and format the metadata.
 */
struct xl_btree_metadata
{
   void (*parse)(struct xl_btree_metadata* wrapper, const char* rec);  /**< Pointer to parse function.*/
   char* (*format)(struct xl_btree_metadata* wrapper, char* buf);      /**< Pointer to format function.*/
   union
   {
      struct xl_btree_metadata_v13 v13;                                /**< Version 13 of btree metadata.*/
      struct xl_btree_metadata_v14 v14;                                /**< Version 14 of btree metadata.*/
   } data;                                                             /**< Union of version-specific metadata.*/
};

/**
 * @struct xl_btree_insert
 * @brief Describes a B-tree insert operation without a page split.
 *
 * This data structure is used for INSERT_LEAF, INSERT_UPPER, INSERT_META,
 * and INSERT_POST operations.
 *
 * Fields:
 * - offnum: Offset number where the new tuple is inserted.
 */
struct xl_btree_insert
{
   offset_number offnum;   /**< Offset number for the new tuple */
};

#define SizeOfBtreeInsert   (offsetof(xl_btree_insert, offnum) + sizeof(OffsetNumber))

/**
 * @struct xl_btree_split
 * @brief Describes a B-tree page split operation.
 *
 * This structure handles both left and right splits and includes the necessary
 * information to fully restore the new right sibling page.
 *
 * Fields:
 * - level: Tree level of the page being split.
 * - firstrightoff: Offset number of the first item on the right page.
 * - newitemoff: Offset number of the new item.
 * - postingoff: Offset inside the original posting tuple.
 */
struct xl_btree_split
{
   uint32_t level;                      /**< Tree level of the page being split */
   offset_number firstrightoff;         /**< First original page item on the right page */
   offset_number newitemoff;            /**< Offset number of the new item */
   uint16_t postingoff;                 /**< Offset inside the original posting tuple */
};

/**
 * @struct xl_btree_dedup
 * @brief Describes a B-tree page deduplication operation.
 *
 * This structure represents a deduplication pass on a leaf page.
 *
 * Fields:
 * - nintervals: Number of deduplication intervals.
 */
struct xl_btree_dedup
{
   uint16_t nintervals;   /**< Number of deduplication intervals */
};

/**
 * @struct xl_btree_reuse_page_v13
 * @brief Represents a reused page in a B-tree index for version 13.
 *
 * This structure contains information about a reused B-tree page,
 * specifically for PostgreSQL version 13.
 */
struct xl_btree_reuse_page_v13
{
   struct rel_file_node node;                              /**< Identifier for a specific relation. */
   block_number block;                                     /**< Block number being reused. */
   transaction_id latest_removed_xid;                      /**< Transaction ID of the latest removed full transaction. */
};

/**
 * @struct xl_btree_reuse_page_v15
 * @brief Represents a reused page in a B-tree index for version 15.
 *
 * This structure contains information about a reused B-tree page,
 * specifically for PostgreSQL version 15.
 */
struct xl_btree_reuse_page_v15
{
   struct rel_file_node node;                              /**< Identifier for a specific relation. */
   block_number block;                                     /**< Block number being reused. */
   struct full_transaction_id latest_removed_full_xid;     /**< Transaction ID of the latest removed full transaction. */
};

/**
 * @struct xl_btree_reuse_page_v16
 * @brief Represents a reused page in a B-tree index for version 16.
 *
 * This structure contains information about a reused B-tree page,
 * specifically for PostgreSQL version 16.
 */
struct xl_btree_reuse_page_v16
{
   struct rel_file_locator locator;                        /**< Locator for a specific relation. */
   block_number block;                                     /**< Block number being reused. */
   struct full_transaction_id snapshot_conflict_horizon_id;  /**< Transaction ID for snapshot conflict horizon. */
   bool is_catalog_rel;                                    /**< Flag indicating if it's a catalog relation. */
};

/**
 * @struct xl_btree_reuse_page
 * @brief Wrapper structure to handle different versions of xl_btree_reuse_page.
 *
 * This structure contains a union of the v15 and v16 versions of xl_btree_reuse_page.
 */
struct xl_btree_reuse_page
{
   void (*parse)(struct xl_btree_reuse_page* wrapper, const void* rec);  /**< Function pointer to parse the structure.*/
   char* (*format)(struct xl_btree_reuse_page* wrapper, char* buf);      /**< Function pointer to format the structure.*/
   union
   {
      struct xl_btree_reuse_page_v13 v13;                                /**< Version 13 of xl_btree_reuse_page.*/
      struct xl_btree_reuse_page_v15 v15;                                /**< Version 15 of xl_btree_reuse_page.*/
      struct xl_btree_reuse_page_v16 v16;                                /**< Version 16 of xl_btree_reuse_page.*/
   } data;                                                               /**< Union of version-specific xl_btree_reuse_page structures.*/
};

/**
 * @struct xl_btree_vacuum
 * @brief Describes a B-tree page vacuum operation.
 *
 * This structure is used by VACUUM to represent the deletion of index tuples
 * on a leaf page.
 *
 * Fields:
 * - ndeleted: Number of deleted tuples.
 * - nupdated: Number of updated tuples.
 */
struct xl_btree_vacuum
{
   uint16_t ndeleted;    /**< Number of deleted tuples */
   uint16_t nupdated;    /**< Number of updated tuples */
};

/**
 * @struct xl_btree_delete_v13
 * @brief Represents the deletion of btree entries in version 15.
 *
 * This data structure is used for storing the deletion information in btree index logs.
 */
struct xl_btree_delete_v13
{
   transaction_id latest_removed_xid;   /**< The latest transaction ID removed by this operation. */
   uint32_t ndeleted;                  /**< Number of deleted tuples. */
};

/**
 * @struct xl_btree_delete_v15
 * @brief Represents the deletion of btree entries in version 15.
 *
 * This data structure is used for storing the deletion information in btree index logs.
 */
struct xl_btree_delete_v15
{
   transaction_id latestRemovedXid;    /**< The latest transaction ID removed by this operation. */
   uint16_t ndeleted;                  /**< Number of deleted tuples. */
   uint16_t nupdated;                  /**< Number of updated tuples. */
   /* DELETED TARGET OFFSET NUMBERS FOLLOW */
   /* UPDATED TARGET OFFSET NUMBERS FOLLOW */
   /* UPDATED TUPLES METADATA (xl_btree_update) ARRAY FOLLOWS */
};

/**
 * @struct xl_btree_delete_v16
 * @brief Represents the deletion of btree entries in version 16.
 *
 * This data structure is used for storing the deletion information in btree index logs.
 */
struct xl_btree_delete_v16
{
   transaction_id snapshot_conflict_horizon;   /**< Transaction ID snapshot conflict horizon. */
   uint16_t ndeleted;                          /**< Number of deleted tuples. */
   uint16_t nupdated;                          /**< Number of updated tuples. */
   bool is_catalog_rel;                        /**< Indicates if the relation is a catalog relation. */
   /*----
    * In payload of blk 0 :
    * - DELETED TARGET OFFSET NUMBERS
    * - UPDATED TARGET OFFSET NUMBERS
    * - UPDATED TUPLES METADATA (xl_btree_update) ARRAY
    *----
    */
};

/**
 * @struct xl_btree_delete
 * @brief A wrapper structure for different versions of btree deletion.
 */
struct xl_btree_delete
{
   void (*parse)(struct xl_btree_delete* wrapper, const void* rec);    /**< Function pointer to parse the structure. */
   char* (*format)(struct xl_btree_delete* wrapper, char* buf);        /**< Function pointer to format the structure. */
   union
   {
      struct xl_btree_delete_v13 v13;     /**< Version 13 of the structure. */
      struct xl_btree_delete_v15 v15;     /**< Version 15 of the structure. */
      struct xl_btree_delete_v16 v16;     /**< Version 16 of the structure. */
   } data;                               /**< Union of version-specific structures. */
};

/**
 * @struct xl_btree_update
 * @brief Describes the update of a B-tree posting list tuple.
 *
 * The offsets in this structure are offsets into the original posting list
 * tuple, not page offset numbers.
 *
 * Fields:
 * - ndeletedtids: Number of deleted TIDs in the posting list.
 */
struct xl_btree_update
{
   uint16_t ndeletedtids;   /**< Number of deleted TIDs */
};

/**
 * @struct xl_btree_mark_page_halfdead
 * @brief Describes marking a B-tree page as half-dead.
 *
 * This structure is used to mark an empty subtree for deletion.
 *
 * Fields:
 * - poffset: Offset of the deleted tuple in the parent page.
 * - leafblk: Block number of the leaf page being deleted.
 * - leftblk: Block number of the left sibling of the leaf page.
 * - rightblk: Block number of the right sibling of the leaf page.
 * - topparent: Block number of the topmost internal page in the subtree.
 */
struct xl_btree_mark_page_halfdead
{
   offset_number poffset;         /**< Offset of the deleted tuple in the parent page */
   block_number leafblk;          /**< Leaf block being deleted */
   block_number leftblk;          /**< Left sibling block of the leaf */
   block_number rightblk;         /**< Right sibling block of the leaf */
   block_number topparent;        /**< Topmost internal page in the subtree */
};

/**
 * @struct xl_btree_unlink_page_v13
 * @brief Represents the structure for version 13 of xl_btree_unlink_page.
 *
 * This structure holds information needed to handle a page unlink operation
 * in B-Tree for version 13.
 */
struct xl_btree_unlink_page_v13
{
   block_number leftsib;             /**< Target block's left sibling, if any. */
   block_number rightsib;            /**< Target block's right sibling. */
   block_number leafleftsib;         /**< Left sibling of the leaf page. */
   block_number leafrightsib;        /**< Right sibling of the leaf page. */
   block_number topparent;           /**< Next child down in the branch. */
   transaction_id btpo_xact;         /**< Value of btpo.xact for use in recovery. */
};

/**
 * @struct xl_btree_unlink_page_v14
 * @brief Represents the structure for version 14 of xl_btree_unlink_page.
 *
 * This structure holds information needed to handle a page unlink operation
 * in B-Tree for version 14.
 */
struct xl_btree_unlink_page_v14
{
   block_number leftsib;                     /**< Target block's left sibling, if any. */
   block_number rightsib;                    /**< Target block's right sibling. */
   uint32_t level;                           /**< Target block's level. */
   struct full_transaction_id safexid;       /**< XID of BTPageSetDeleted operation. */
   block_number leafleftsib;                 /**< Left sibling of the leaf page. */
   block_number leafrightsib;                /**< Right sibling of the leaf page. */
   block_number leaftopparent;               /**< Next child down in the subtree. */
};

/**
 * @struct xl_btree_unlink_page
 * @brief A wrapper struct containing versioned xl_btree_unlink_page data.
 *
 * This union allows handling different versions of the xl_btree_unlink_page
 * structure.
 */
struct xl_btree_unlink_page
{
   void (*parse)(struct xl_btree_unlink_page* wrapper, const void* rec);    /**< Function to parse the record.*/
   char* (*format)(struct xl_btree_unlink_page* wrapper, char* buf);        /**< Function to format the record as a string.*/
   union
   {
      struct xl_btree_unlink_page_v13 v13;                                  /**< Version 13 of the structure.*/
      struct xl_btree_unlink_page_v14 v14;                                  /**< Version 14 of the structure.*/
   } data;                                                                  /**< Union of version-specific structures.*/
};

/**
 * @struct xl_btree_newroot
 * @brief Describes the creation of a new B-tree root page.
 *
 * This structure is used when a new root page is established,
 * typically after a root page split.
 *
 * Fields:
 * - rootblk: Block number of the new root page.
 * - level: Tree level of the new root.
 */
struct xl_btree_newroot
{
   block_number rootblk;   /**< Block number of the new root */
   uint32_t level;         /**< Tree level of the new root */
};

/**
 * Creates an xl_btree_reuse_page wrapper structure.
 *
 * @return A pointer to the newly created xl_btree_reuse_page structure.
 */
struct xl_btree_reuse_page* pgmoneta_wal_create_xl_btree_reuse_page(void);

/**
 * Parses a version 13 xl_btree_reuse_page record.
 *
 * @param wrapper Pointer to the wrapper structure.
 * @param rec The raw record data to be parsed.
 */
void pgmoneta_wal_parse_xl_btree_reuse_page_v13(struct xl_btree_reuse_page* wrapper, const void* rec);

/**
 * Parses a version 15 xl_btree_reuse_page record.
 *
 * @param wrapper Pointer to the wrapper structure.
 * @param rec The raw record data to be parsed.
 */
void pgmoneta_wal_parse_xl_btree_reuse_page_v15(struct xl_btree_reuse_page* wrapper, const void* rec);

/**
 * Parses a version 16 xl_btree_reuse_page record.
 *
 * @param wrapper Pointer to the wrapper structure.
 * @param rec The raw record data to be parsed.
 */
void pgmoneta_wal_parse_xl_btree_reuse_page_v16(struct xl_btree_reuse_page* wrapper, const void* rec);

/**
 * Formats a version 13 xl_btree_reuse_page record.
 *
 * @param wrapper Pointer to the wrapper structure.
 * @param buf The buffer where the formatted string will be stored.
 * @return A pointer to the formatted string.
 */
char* pgmoneta_wal_format_xl_btree_reuse_page_v13(struct xl_btree_reuse_page* wrapper, char* buf);

/**
 * Formats a version 15 xl_btree_reuse_page record.
 *
 * @param wrapper Pointer to the wrapper structure.
 * @param buf The buffer where the formatted string will be stored.
 * @return A pointer to the formatted string.
 */
char* pgmoneta_wal_format_xl_btree_reuse_page_v15(struct xl_btree_reuse_page* wrapper, char* buf);

/**
 * Formats a version 16 xl_btree_reuse_page record.
 *
 * @param wrapper Pointer to the wrapper structure.
 * @param buf The buffer where the formatted string will be stored.
 * @return A pointer to the formatted string.
 */
char* pgmoneta_wal_format_xl_btree_reuse_page_v16(struct xl_btree_reuse_page* wrapper, char* buf);

/**
 * Parses the v13 version of xl_btree_delete.
 *
 * @param wrapper The wrapper structure.
 * @param rec The raw record to parse.
 */
void pgmoneta_wal_parse_xl_btree_delete_v13(struct xl_btree_delete* wrapper, const void* rec);

/**
 * Parses the v15 version of xl_btree_delete.
 *
 * @param wrapper The wrapper structure.
 * @param rec The raw record to parse.
 */
void pgmoneta_wal_parse_xl_btree_delete_v15(struct xl_btree_delete* wrapper, const void* rec);

/**
 * Parses the v16 version of xl_btree_delete.
 *
 * @param wrapper The wrapper structure.
 * @param rec The raw record to parse.
 */
void pgmoneta_wal_parse_xl_btree_delete_v16(struct xl_btree_delete* wrapper, const void* rec);

/**
 * Formats the v13 version of xl_btree_delete into a string.
 *
 * @param wrapper The wrapper structure.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char* pgmoneta_wal_format_xl_btree_delete_v13(struct xl_btree_delete* wrapper, char* buf);

/**
 * Formats the v15 version of xl_btree_delete into a string.
 *
 * @param wrapper The wrapper structure.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char* pgmoneta_wal_format_xl_btree_delete_v15(struct xl_btree_delete* wrapper, char* buf);

/**
 * Formats the v16 version of xl_btree_delete into a string.
 *
 * @param wrapper The wrapper structure.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char* pgmoneta_wal_format_xl_btree_delete_v16(struct xl_btree_delete* wrapper, char* buf);

/**
 * Allocates and initializes an xl_btree_metadata structure based on the server version.
 *
 * @return A pointer to the initialized xl_btree_metadata structure.
 */
struct xl_btree_metadata* pgmoneta_wal_create_xl_btree_metadata(void);

/**
 * Parses a version 13 xl_btree_metadata record.
 *
 * @param wrapper The wrapper containing the metadata.
 * @param rec The raw record to parse.
 */
void pgmoneta_wal_parse_xl_btree_metadata_v13(struct xl_btree_metadata* wrapper, const char* rec);

/**
 * Parses a version 14 xl_btree_metadata record.
 *
 * @param wrapper The wrapper containing the metadata.
 * @param rec The raw record to parse.
 */
void pgmoneta_wal_parse_xl_btree_metadata_v14(struct xl_btree_metadata* wrapper, const char* rec);

/**
 * Formats a version 13 xl_btree_metadata record as a string.
 *
 * @param wrapper The wrapper containing the metadata.
 * @param buf The buffer to write the formatted string.
 * @return A pointer to the buffer with the formatted string.
 */
char* pgmoneta_wal_format_xl_btree_metadata_v13(struct xl_btree_metadata* wrapper, char* buf);

/**
 * Formats a version 14 xl_btree_metadata record as a string.
 *
 * @param wrapper The wrapper containing the metadata.
 * @param buf The buffer to write the formatted string.
 * @return A pointer to the buffer with the formatted string.
 */
char* pgmoneta_wal_format_xl_btree_metadata_v14(struct xl_btree_metadata* wrapper, char* buf);

/**
 * Parses a version 13 xl_btree_unlink_page record.
 *
 * @param wrapper The wrapper struct.
 * @param rec The record to parse.
 */
void pgmoneta_wal_parse_xl_btree_unlink_page_v13(struct xl_btree_unlink_page* wrapper, const void* rec);

/**
 * Parses a version 14 xl_btree_unlink_page record.
 *
 * @param wrapper The wrapper struct.
 * @param rec The record to parse.
 */
void pgmoneta_wal_parse_xl_btree_unlink_page_v14(struct xl_btree_unlink_page* wrapper, const void* rec);

/**
 * Formats a version 13 xl_btree_unlink_page record into a string.
 *
 * @param wrapper The wrapper struct.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char* pgmoneta_wal_format_xl_btree_unlink_page_v13(struct xl_btree_unlink_page* wrapper, char* buf);

/**
 * Formats a version 14 xl_btree_unlink_page record into a string.
 *
 * @param wrapper The wrapper struct.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char* pgmoneta_wal_format_xl_btree_unlink_page_v14(struct xl_btree_unlink_page* wrapper, char* buf);

/**
 * Creates an xl_btree_unlink_page wrapper.
 *
 * @return A pointer to the created wrapper.
 */
struct xl_btree_unlink_page* pgmoneta_wal_create_xl_btree_unlink_page(void);

/**
 * @brief Describe a B-tree operation from a decoded XLog record.
 *
 * @param buf The buffer to store the description.
 * @param record The decoded XLog record.
 * @return A pointer to the description in buf.
 */
char*
pgmoneta_wal_btree_desc(char* buf, struct decoded_xlog_record* record);

#ifdef __cplusplus
}
#endif

#endif //PGMONETA_RM_BTREE_H
