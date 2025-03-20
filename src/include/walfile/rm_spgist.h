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

#ifndef PGMONETA_RM_SPGIST_H
#define PGMONETA_RM_SPGIST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Include pgmoneta-related headers */
#include <walfile/rm.h>
#include <walfile/wal_reader.h>

/* Define macros */
#define XLOG_SPGIST_ADD_LEAF         0x10 /**< XLOG opcode for adding a leaf node in SPGiST. */
#define XLOG_SPGIST_MOVE_LEAFS       0x20 /**< XLOG opcode for moving leaf nodes in SPGiST. */
#define XLOG_SPGIST_ADD_NODE         0x30 /**< XLOG opcode for adding a node in SPGiST. */
#define XLOG_SPGIST_SPLIT_TUPLE      0x40 /**< XLOG opcode for splitting a tuple in SPGiST. */
#define XLOG_SPGIST_PICKSPLIT        0x50 /**< XLOG opcode for picking and splitting tuples in SPGiST. */
#define XLOG_SPGIST_VACUUM_LEAF      0x60 /**< XLOG opcode for vacuuming leaf nodes in SPGiST. */
#define XLOG_SPGIST_VACUUM_ROOT      0x70 /**< XLOG opcode for vacuuming root nodes in SPGiST. */
#define XLOG_SPGIST_VACUUM_REDIRECT  0x80 /**< XLOG opcode for vacuuming redirect pointers in SPGiST. */

/**
 * @struct spg_xlog_state
 * @brief Represents the state needed by some SPGiST redo functions.
 *
 * Contains the transaction ID and a flag indicating if the operation is during index build.
 */
struct spg_xlog_state
{
   transaction_id my_xid;   /**< Transaction ID of the current operation. */
   bool is_build;           /**< Indicates if the operation is during index build. */
};

/**
 * @struct spg_xlog_add_leaf
 * @brief Represents an operation to add a leaf node in SPGiST.
 *
 * Contains information about the destination page, parent page, and
 * the offsets for the leaf tuple and head tuple in the chain.
 */
struct spg_xlog_add_leaf
{
   bool new_page;                    /**< Indicates if the destination page is new. */
   bool stores_nulls;                /**< Indicates if the page is in the nulls tree. */
   offset_number offnum_leaf;        /**< Offset where the leaf tuple is placed. */
   offset_number offnum_head_leaf;   /**< Offset of the head tuple in the chain, if any. */
   offset_number offnum_parent;      /**< Offset where the parent downlink is, if any. */
   uint16_t node_i;                  /**< Index of the node in the parent. */
   /* New leaf tuple follows (unaligned!). */
};

/**
 * @struct spg_xlog_move_leafs
 * @brief Represents an operation to move leaf nodes in SPGiST.
 *
 * Contains information about the source and destination pages, parent page,
 * and the state of the source page.
 */
struct spg_xlog_move_leafs
{
   uint16_t n_moves;                               /**< Number of tuples moved from the source page. */
   bool new_page;                                  /**< Indicates if the destination page is new. */
   bool replace_dead;                              /**< Indicates if a dead source tuple is being replaced. */
   bool stores_nulls;                              /**< Indicates if the pages are in the nulls tree. */
   offset_number offnum_parent;                    /**< Offset where the parent downlink is. */
   uint16_t node_i;                                /**< Index of the node in the parent. */
   struct spg_xlog_state state_src;                /**< State of the source page. */
   offset_number offsets[FLEXIBLE_ARRAY_MEMBER];   /**< Array of tuple offsets. */
};

/**
 * @struct spg_xlog_add_node
 * @brief Represents an operation to add a node in SPGiST.
 *
 * Contains information about the original page, new page, and parent page.
 */
struct spg_xlog_add_node
{
   offset_number offnum;               /**< Offset of the original inner tuple on the original page. */
   offset_number offnum_new;           /**< Offset of the new tuple on the new page. */
   bool new_page;                      /**< Indicates if the new page is initialized. */
   int8_t parent_blk;                  /**< Indicates which page the parent downlink is on. */
   offset_number offnum_parent;        /**< Offset within the parent page. */
   uint16_t node_i;                    /**< Index of the node in the parent. */
   struct spg_xlog_state state_src;    /**< State of the source page. */
   /* Updated inner tuple follows (unaligned!). */
};

/**
 * @struct spg_xlog_split_tuple
 * @brief Represents an operation to split a tuple in SPGiST.
 *
 * Contains information about the prefix and postfix tuples and the pages they are stored on.
 */
struct spg_xlog_split_tuple
{
   offset_number offnum_prefix;    /**< Offset where the prefix tuple goes. */
   offset_number offnum_postfix;   /**< Offset where the postfix tuple goes. */
   bool new_page;                  /**< Indicates if the page is initialized. */
   bool postfix_blk_same;          /**< Indicates if the postfix tuple is on the same page as the prefix. */
   /* New prefix and postfix inner tuples follow (unaligned!). */
};

/**
 * @struct spg_xlog_pick_split
 * @brief Represents an operation to pick and split tuples in SPGiST.
 *
 * Contains information about the source, destination, inner, and parent pages.
 */
struct spg_xlog_pick_split
{
   bool is_root_split;                             /**< Indicates if this is a root split. */
   uint16_t n_delete;                              /**< Number of tuples to delete from the source. */
   uint16_t n_insert;                              /**< Number of tuples to insert on the source and/or destination. */
   bool init_src;                                  /**< Indicates if the source page is re-initialized. */
   bool init_dest;                                 /**< Indicates if the destination page is re-initialized. */
   offset_number offnum_inner;                     /**< Offset where the new inner tuple goes. */
   bool init_inner;                                /**< Indicates if the inner page is re-initialized. */
   bool stores_nulls;                              /**< Indicates if the pages are in the nulls tree. */
   bool inner_is_parent;                           /**< Indicates if the inner page is the parent page. */
   offset_number offnum_parent;                    /**< Offset within the parent page. */
   uint16_t node_i;                                /**< Index of the node in the parent. */
   struct spg_xlog_state state_src;                /**< State of the source page. */
   offset_number offsets[FLEXIBLE_ARRAY_MEMBER];   /**< Array of tuple offsets. */
};

/**
 * @struct spg_xlog_vacuum_leaf
 * @brief Represents an operation to vacuum leaf nodes in SPGiST.
 *
 * Contains information about the tuples to be marked as DEAD, placeholders, moved, or re-chained.
 */
struct spg_xlog_vacuum_leaf
{
   uint16_t n_dead;                                /**< Number of tuples to be marked as DEAD. */
   uint16_t n_placeholder;                         /**< Number of tuples to be marked as PLACEHOLDER. */
   uint16_t n_move;                                /**< Number of tuples to be moved. */
   uint16_t n_chain;                               /**< Number of tuples to be re-chained. */
   struct spg_xlog_state state_src;                /**< State of the source page. */
   offset_number offsets[FLEXIBLE_ARRAY_MEMBER];   /**< Array of tuple offsets. */
};

/**
 * @struct spg_xlog_vacuum_root
 * @brief Represents an operation to vacuum a root page when it is also a leaf in SPGiST.
 *
 * Contains information about the tuples to be deleted.
 */
struct spg_xlog_vacuum_root
{
   uint16_t n_delete;                              /**< Number of tuples to be deleted. */
   struct spg_xlog_state state_src;                /**< State of the source page. */
   offset_number offsets[FLEXIBLE_ARRAY_MEMBER];   /**< Array of tuple offsets. */
};

/**
 * @struct spg_xlog_vacuum_redirect_v15
 * @brief Represents the SP-GiST vacuum redirect operation in version 15.
 *
 * This structure is used in XLOG records for vacuuming redirects in a
 * space-partitioned GiST (SP-GiST) index in version 15.
 */
struct spg_xlog_vacuum_redirect_v15
{
   uint16_t nToPlaceholder;                        /**< Number of redirects to make placeholders. */
   offset_number firstPlaceholder;                 /**< First placeholder tuple to remove. */
   transaction_id newestRedirectXid;               /**< Newest XID of removed redirects. */
   offset_number offsets[FLEXIBLE_ARRAY_MEMBER];   /**< Offsets of redirect tuples to make placeholders. */
};

/**
 * @struct spg_xlog_vacuum_redirect_v16
 * @brief Represents the SP-GiST vacuum redirect operation in version 16.
 *
 * This structure is used in XLOG records for vacuuming redirects in a
 * space-partitioned GiST (SP-GiST) index in version 16.
 */
struct spg_xlog_vacuum_redirect_v16
{
   uint16_t n_to_placeholder;                      /**< Number of redirects to make placeholders. */
   offset_number first_placeholder;                /**< First placeholder tuple to remove. */
   transaction_id snapshot_conflict_horizon;       /**< Newest XID of removed redirects. */
   bool is_catalog_rel;                            /**< Handle recovery conflict during logical decoding on standby. */
   offset_number offsets[FLEXIBLE_ARRAY_MEMBER];   /**< Offsets of redirect tuples to make placeholders. */
};

/**
 * @struct spg_xlog_vacuum_redirect
 * @brief Wrapper structure for handling different versions of SP-GiST vacuum redirect.
 *
 * This structure wraps version-specific structures for easier handling of different
 * SP-GiST vacuum redirect versions.
 */
struct spg_xlog_vacuum_redirect
{
   void (*parse)(struct spg_xlog_vacuum_redirect* wrapper, void* rec);  /**< Function pointer to parse the record. */
   char* (*format)(struct spg_xlog_vacuum_redirect* wrapper, char* buf);      /**< Function pointer to format the record. */

   union
   {
      struct spg_xlog_vacuum_redirect_v15 v15;                                /**< Version 15 of the structure. */
      struct spg_xlog_vacuum_redirect_v16 v16;                                /**< Version 16 of the structure. */
   } data;                                                                    /**< Version-specific data. */
};

/* Functions */

/**
 * Describes an SPGiST operation from a decoded XLOG record.
 *
 * @param buf The buffer to store the description.
 * @param record The decoded XLOG record containing the SPGiST operation.
 * @return A pointer to the buffer containing the description.
 */
char*
pgmoneta_wal_spg_desc(char* buf, struct decoded_xlog_record* record);

/**
 * Parse SP-GiST vacuum redirect XLOG record for version 15.
 *
 * @param wrapper The wrapper structure to populate.
 * @param rec The raw XLOG record to parse.
 */
void
pgmoneta_wal_parse_spg_xlog_vacuum_redirect_v15(struct spg_xlog_vacuum_redirect* wrapper, void* rec);

/**
 * Parse SP-GiST vacuum redirect XLOG record for version 16.
 *
 * @param wrapper The wrapper structure to populate.
 * @param rec The raw XLOG record to parse.
 */
void
pgmoneta_wal_parse_spg_xlog_vacuum_redirect_v16(struct spg_xlog_vacuum_redirect* wrapper, void* rec);

/**
 * Format the SP-GiST vacuum redirect XLOG record for version 15.
 *
 * @param wrapper The wrapper structure to format.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the buffer containing the formatted string.
 */
char*
pgmoneta_wal_format_spg_xlog_vacuum_redirect_v15(struct spg_xlog_vacuum_redirect* wrapper, char* buf);

/**
 * Format the SP-GiST vacuum redirect XLOG record for version 16.
 *
 * @param wrapper The wrapper structure to format.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the buffer containing the formatted string.
 */
char*
pgmoneta_wal_format_spg_xlog_vacuum_redirect_v16(struct spg_xlog_vacuum_redirect* wrapper, char* buf);

#ifdef __cplusplus
}
#endif

#endif // PGMONETA_RM_SPGIST_H
