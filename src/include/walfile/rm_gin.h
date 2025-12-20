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

#ifndef PGMONETA_RM_GIN_H
#define PGMONETA_RM_GIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <walfile/rm.h>
#include <walfile/wal_reader.h>

#include <stdint.h>

#define GIN_INSERT_ISDATA              0x01 /**< for both insert and split records */
#define GIN_INSERT_ISLEAF              0x02 /**< ditto */
#define GIN_SPLIT_ROOT                 0x04 /**< only for split records */

#define GIN_CURRENT_VERSION            2

#define GIN_NDELETE_AT_ONCE            Min(16, XLR_MAX_BLOCK_ID - 1)

#define XLOG_GIN_CREATE_PTREE          0x10 /**< Create a new posting tree */
#define XLOG_GIN_INSERT                0x20 /**< Insert record */
#define XLOG_GIN_SPLIT                 0x30 /**< Split page */
#define XLOG_GIN_VACUUM_PAGE           0x40 /**< Vacuum page */
#define XLOG_GIN_VACUUM_DATA_LEAF_PAGE 0x90 /**< Vacuum data leaf page */
#define XLOG_GIN_DELETE_PAGE           0x50 /**< Delete page */
#define XLOG_GIN_UPDATE_META_PAGE      0x60 /**< Update metadata page */
#define XLOG_GIN_INSERT_LISTPAGE       0x70 /**< Insert into list page */
#define XLOG_GIN_DELETE_LISTPAGE       0x80 /**< Delete from list page */

#define GIN_SEGMENT_UNMODIFIED         0 /**< No action (not used in WAL records) */
#define GIN_SEGMENT_DELETE             1 /**< A whole segment is removed */
#define GIN_SEGMENT_INSERT             2 /**< A whole segment is added */
#define GIN_SEGMENT_REPLACE            3 /**< A segment is replaced */
#define GIN_SEGMENT_ADDITEMS           4 /**< Items are added to existing segment */

#define SIZE_OF_GIN_POSTING_LIST(plist) \
   (offsetof(struct gin_posting_list, bytes) + SHORTALIGN((plist)->nbytes))

/**
 * @struct index_tuple_data
 * @brief Structure representing the header of an index tuple.
 *
 * Fields:
 * - t_tid: Reference TID to heap tuple.
 * - t_info: Various information about the tuple, including size and flags.
 */
struct index_tuple_data
{
   struct item_pointer_data t_tid; /**< Reference TID to heap tuple */
   unsigned short t_info;          /**< Various info about tuple */
}; /* MORE DATA FOLLOWS AT END OF STRUCT */

/**
 * @struct posting_item
 * @brief Structure representing a posting item in a non-leaf posting-tree page.
 *
 * Fields:
 * - child_blkno: Block ID for the child node.
 * - key: Key associated with this posting item.
 */
struct posting_item
{
   struct block_id_data child_blkno; /**< Block ID for the child node */
   struct item_pointer_data key;     /**< Key associated with this posting item */
};

/**
 * @struct gin_xlog_create_posting_tree
 * @brief Structure representing the creation of a posting tree in a GIN index.
 *
 * Fields:
 * - size: Size of the posting list that follows.
 */
struct gin_xlog_create_posting_tree
{
   uint32_t size; /**< Size of the posting list that follows */
};

/**
 * @struct gin_xlog_insert
 * @brief Common structure for insertion records in a GIN index.
 *
 * Fields:
 * - flags: Flags indicating whether the page is a leaf or contains data.
 */
struct gin_xlog_insert
{
   uint16_t flags; /**< GIN_INSERT_ISLEAF and/or GIN_INSERT_ISDATA flags */
};

/**
 * @struct gin_xlog_insert_entry
 * @brief Structure representing an insertion entry in a GIN index.
 *
 * Fields:
 * - offset: Offset number for the entry.
 * - isDelete: Flag indicating if the entry is a deletion.
 * - tuple: The index tuple data associated with this entry.
 */
struct gin_xlog_insert_entry
{
   offset_number offset;          /**< Offset number for the entry */
   bool isDelete;                 /**< Flag indicating if the entry is a deletion */
   struct index_tuple_data tuple; /**< Index tuple data (variable length) */
};

/**
 * @struct gin_xlog_recompress_data_leaf
 * @brief Structure representing recompression of a data leaf in a GIN index.
 *
 * Fields:
 * - nactions: Number of actions in the recompression.
 */
struct gin_xlog_recompress_data_leaf
{
   uint16_t nactions; /**< Number of actions in the recompression */
};

/**
 * @struct gin_xlog_insert_data_internal
 * @brief Structure representing an internal insertion of data in a GIN index.
 *
 * Fields:
 * - offset: Offset number for the new item.
 * - newitem: The new posting item to be inserted.
 */
struct gin_xlog_insert_data_internal
{
   offset_number offset;        /**< Offset number for the new item */
   struct posting_item newitem; /**< New posting item to be inserted */
};

/**
 * @struct gin_xlog_split
 * @brief Structure representing a split in a GIN index.
 *
 * Fields:
 * - node: The rel file node of the GIN index.
 * - rrlink: Right link or root's block number if root split.
 * - leftChildBlkno: Block number of the left child on a non-leaf split.
 * - rightChildBlkno: Block number of the right child on a non-leaf split.
 * - flags: Flags associated with the split.
 */
struct gin_xlog_split
{
   struct rel_file_node node;    /**< The rel file node of the GIN index */
   block_number rrlink;          /**< Right link or root's block number if root split */
   block_number leftChildBlkno;  /**< Block number of the left child (non-leaf split) */
   block_number rightChildBlkno; /**< Block number of the right child (non-leaf split) */
   uint16_t flags;               /**< Flags associated with the split */
};

/**
 * @struct gin_xlog_vacuum_data_leaf_page
 * @brief Structure representing the vacuuming of a data leaf page in a GIN index.
 *
 * Fields:
 * - data: The recompressed data leaf information.
 */
struct gin_xlog_vacuum_data_leaf_page
{
   struct gin_xlog_recompress_data_leaf data; /**< Recompressed data leaf information */
};

/**
 * @struct gin_xlog_delete_page
 * @brief Structure representing the deletion of a page in a GIN index.
 *
 * Fields:
 * - parentOffset: Offset of the parent page.
 * - rightLink: Right link to the next page.
 * - deleteXid: Transaction ID of the last Xid that could see this page in a scan.
 */
struct gin_xlog_delete_page
{
   offset_number parentOffset; /**< Offset of the parent page */
   block_number rightLink;     /**< Right link to the next page */
   transaction_id deleteXid;   /**< Last Xid which could see this page in scan */
};

/**
 * @struct gin_meta_page_data
 * @brief Structure representing metadata for a GIN index.
 *
 * Fields:
 * - head: Pointer to the head of the pending list.
 * - tail: Pointer to the tail of the pending list.
 * - tailFreeSize: Free space in bytes in the pending list's tail page.
 * - nPendingPages: Number of pages in the pending list.
 * - nPendingHeapTuples: Number of heap tuples in the pending list.
 * - nTotalPages: Total number of pages in the index.
 * - nEntryPages: Number of entry pages in the index.
 * - nDataPages: Number of data pages in the index.
 * - nEntries: Number of entries in the index.
 * - ginVersion: Version number of the GIN index.
 */
struct gin_meta_page_data
{
   block_number head;          /**< Head of the pending list */
   block_number tail;          /**< Tail of the pending list */
   uint32_t tailFreeSize;      /**< Free space in bytes in the tail page */
   block_number nPendingPages; /**< Number of pages in the pending list */
   int64_t nPendingHeapTuples; /**< Number of heap tuples in the pending list */
   block_number nTotalPages;   /**< Total number of pages in the index */
   block_number nEntryPages;   /**< Number of entry pages in the index */
   block_number nDataPages;    /**< Number of data pages in the index */
   int64_t nEntries;           /**< Number of entries in the index */
   int32_t ginVersion;         /**< Version number of the GIN index */
};

/**
 * @struct gin_xlog_update_meta
 * @brief Structure representing an update to the metadata of a GIN index.
 *
 * Fields:
 * - node: The rel file node of the GIN index.
 * - metadata: The updated metadata for the GIN index.
 * - prevTail: Previous tail of the pending list.
 * - newRightlink: New right link for the list page.
 * - ntuples: Number of tuples inserted or updated.
 */
struct gin_xlog_update_meta
{
   struct rel_file_node node;          /**< The rel file node of the GIN index */
   struct gin_meta_page_data metadata; /**< Updated metadata for the GIN index */
   block_number prevTail;              /**< Previous tail of the pending list */
   block_number newRightlink;          /**< New right link for the list page */
   int32_t ntuples;                    /**< Number of tuples inserted or updated */
};

/**
 * @struct gin_xlog_insert_list_page
 * @brief Structure representing an insertion into a list page in a GIN index.
 *
 * Fields:
 * - rightlink: Right link to the next page.
 * - ntuples: Number of tuples inserted.
 */
struct gin_xlog_insert_list_page
{
   block_number rightlink; /**< Right link to the next page */
   int32_t ntuples;        /**< Number of tuples inserted */
};

/**
 * @struct gin_xlog_delete_list_pages
 * @brief Structure representing the deletion of list pages in a GIN index.
 *
 * Fields:
 * - metadata: Metadata after the deletion of the pages.
 * - ndeleted: Number of pages deleted.
 */
struct gin_xlog_delete_list_pages
{
   struct gin_meta_page_data metadata; /**< Metadata after deletion */
   int32_t ndeleted;                   /**< Number of pages deleted */
};

/**
 * @struct gin_posting_list
 * @brief Structure representing a posting list in a GIN index.
 *
 * Fields:
 * - first: First item in the posting list (unpacked).
 * - nbytes: Number of bytes in the posting list.
 * - bytes: Varbyte encoded items in the posting list.
 */
struct gin_posting_list
{
   struct item_pointer_data first;             /**< First item in the posting list (unpacked) */
   uint16_t nbytes;                            /**< Number of bytes in the posting list */
   unsigned char bytes[FLEXIBLE_ARRAY_MEMBER]; /**< Varbyte encoded items */
};

/**
 * @brief Describes a GIN WAL record.
 *
 * @param buf Buffer to hold the description.
 * @param record The decoded WAL record to describe.
 * @return Pointer to the buffer containing the description.
 */
char*
pgmoneta_wal_gin_desc(char* buf, struct decoded_xlog_record* record);

#ifdef __cplusplus
}
#endif

#endif // PGMONETA_RM_GIN_H
