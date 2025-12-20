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

#ifndef PGMONETA_RM_HEAP_H
#define PGMONETA_RM_HEAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <walfile/rm.h>
#include <walfile/wal_reader.h>

#include <stdint.h>

// Typedefs
/**
 * @typedef command_id
 * @brief A type definition for a command ID.
 */
typedef uint32_t command_id;

// Define variables
#define XLOG_HEAP_INSERT                0x00
#define XLOG_HEAP_DELETE                0x10
#define XLOG_HEAP_UPDATE                0x20
#define XLOG_HEAP_TRUNCATE              0x30
#define XLOG_HEAP_HOT_UPDATE            0x40
#define XLOG_HEAP_CONFIRM               0x50
#define XLOG_HEAP_LOCK                  0x60
#define XLOG_HEAP_INPLACE               0x70

#define XLOG_HEAP_OPMASK                0x70
#define XLOG_HEAP_INIT_PAGE             0x80

#define XLOG_HEAP2_REWRITE              0x00
#define XLOG_HEAP2_PRUNE                0x10
#define XLOG_HEAP2_VACUUM               0x20
#define XLOG_HEAP2_FREEZE_PAGE          0x30
#define XLOG_HEAP2_VISIBLE              0x40
#define XLOG_HEAP2_MULTI_INSERT         0x50
#define XLOG_HEAP2_LOCK_UPDATED         0x60
#define XLOG_HEAP2_NEW_CID              0x70

#define XLOG_HEAP2_PRUNE_ON_ACCESS      0x10
#define XLOG_HEAP2_PRUNE_VACUUM_SCAN    0x20
#define XLOG_HEAP2_PRUNE_VACUUM_CLEANUP 0x30
#define XLOG_HEAP2_VISIBLE              0x40
#define XLOG_HEAP2_MULTI_INSERT         0x50
#define XLOG_HEAP2_LOCK_UPDATED         0x60
#define XLOG_HEAP2_NEW_CID              0x70

#define XLHL_XMAX_IS_MULTI              0x01
#define XLHL_XMAX_LOCK_ONLY             0x02
#define XLHL_XMAX_EXCL_LOCK             0x04
#define XLHL_XMAX_KEYSHR_LOCK           0x08
#define XLHL_KEYS_UPDATED               0x10

// Define variables
#define XLOG_HEAP_INSERT        0x00
#define XLOG_HEAP_DELETE        0x10
#define XLOG_HEAP_UPDATE        0x20
#define XLOG_HEAP_TRUNCATE      0x30
#define XLOG_HEAP_HOT_UPDATE    0x40
#define XLOG_HEAP_CONFIRM       0x50
#define XLOG_HEAP_LOCK          0x60
#define XLOG_HEAP_INPLACE       0x70

#define XLOG_HEAP_OPMASK        0x70
#define XLOG_HEAP_INIT_PAGE     0x80

#define XLOG_HEAP2_REWRITE      0x00
#define XLOG_HEAP2_PRUNE        0x10
#define XLOG_HEAP2_VACUUM       0x20
#define XLOG_HEAP2_FREEZE_PAGE  0x30
#define XLOG_HEAP2_VISIBLE      0x40
#define XLOG_HEAP2_MULTI_INSERT 0x50
#define XLOG_HEAP2_LOCK_UPDATED 0x60
#define XLOG_HEAP2_NEW_CID      0x70

// V17 and later
#define XLOG_HEAP2_PRUNE_ON_ACCESS      0x10
#define XLOG_HEAP2_PRUNE_VACUUM_SCAN    0x20
#define XLOG_HEAP2_PRUNE_VACUUM_CLEANUP 0x30
#define XLOG_HEAP2_VISIBLE              0x40
#define XLOG_HEAP2_MULTI_INSERT         0x50
#define XLOG_HEAP2_LOCK_UPDATED         0x60
#define XLOG_HEAP2_NEW_CID              0x70

#define XLHL_XMAX_IS_MULTI              0x01
#define XLHL_XMAX_LOCK_ONLY             0x02
#define XLHL_XMAX_EXCL_LOCK             0x04
#define XLHL_XMAX_KEYSHR_LOCK           0x08
#define XLHL_KEYS_UPDATED               0x10

// To handle recovery conflict during logical decoding on standby
#define XLHP_IS_CATALOG_REL (1 << 1)

// Does replaying the record require a cleanup-lock?
#define XLHP_CLEANUP_LOCK (1 << 2)

// If we remove or freeze any entries that contain xids, we need to include a snapshot conflict horizon.
#define XLHP_HAS_CONFLICT_HORIZON (1 << 3)

// Indicates that an xlhp_freeze_plans sub-record and one or more xlhp_freeze_plan sub-records are present.
#define XLHP_HAS_FREEZE_PLANS (1 << 4)

// XLHP_HAS_REDIRECTIONS, XLHP_HAS_DEAD_ITEMS, and XLHP_HAS_NOW_UNUSED_ITEMS indicate that xlhp_prune_items sub-records with redirected, dead, and unused item offsets are present.
#define XLHP_HAS_REDIRECTIONS     (1 << 5)
#define XLHP_HAS_DEAD_ITEMS       (1 << 6)
#define XLHP_HAS_NOW_UNUSED_ITEMS (1 << 7)

// xlhp_freeze_plan describes how to freeze a group of one or more heap tuples (appears in xl_heap_prune's xlhp_freeze_plans sub-record)
/* 0x01 was XLH_FREEZE_XMIN */
#define XLH_FREEZE_XVAC           0x02
#define XLH_INVALID_XVAC          0x04

#define XLH_TRUNCATE_CASCADE      (1 << 0)
#define XLH_TRUNCATE_RESTART_SEQS (1 << 1)

#define SizeOfHeapPruneV17        (offsetof(struct xl_heap_prune_v17, flags) + sizeof(uint8_t))

// Struct definitions
/**
 * @struct xl_heap_insert
 * @brief Represents an insert operation in the heap.
 *
 * Contains the offset number for the inserted tuple and associated flags.
 */
struct xl_heap_insert
{
   offset_number offnum; /**< Inserted tuple's offset. */
   uint8_t flags;        /**< Flags associated with the insert operation. */
};

/**
 * @struct xl_heap_delete
 * @brief Represents a delete operation in the heap.
 *
 * Contains the transaction ID of the deleted tuple, offset number, and associated flags.
 */
struct xl_heap_delete
{
   transaction_id xmax;  /**< Transaction ID of the deleted tuple. */
   offset_number offnum; /**< Deleted tuple's offset.              */
   uint8_t infobits_set; /**< Infomask bits.                       */
   uint8_t flags;        /**< Flags associated with the delete operation. */
};

/**
 * @struct xl_heap_update
 * @brief Represents an update operation in the heap.
 *
 * Contains the transaction IDs and offsets for both the old and new tuples, along with associated flags.
 */
struct xl_heap_update
{
   transaction_id old_xmax;  /**< Transaction ID of the old tuple. */
   offset_number old_offnum; /**< Old tuple's offset.              */
   uint8_t old_infobits_set; /**< Infomask bits to set on old tuple. */
   uint8_t flags;            /**< Flags associated with the update operation. */
   transaction_id new_xmax;  /**< Transaction ID of the new tuple. */
   offset_number new_offnum; /**< New tuple's offset.              */
};

/**
 * @struct xl_heap_truncate
 * @brief Represents a truncate operation in the heap.
 *
 * Contains the database ID, number of relation IDs, associated flags, and the array of relation IDs.
 */
struct xl_heap_truncate
{
   oid dbId;                          /**< Database ID. */
   uint32_t nrelids;                  /**< Number of relation IDs. */
   uint8_t flags;                     /**< Flags associated with the truncate operation. */
   oid relids[FLEXIBLE_ARRAY_MEMBER]; /**< Array of relation IDs. */
};

/**
 * @struct xl_heap_confirm
 * @brief Represents a confirmation of speculative insertion in the heap.
 *
 * Contains the offset number for the confirmed tuple.
 */
struct xl_heap_confirm
{
   offset_number offnum; /**< Confirmed tuple's offset on page. */
};

/**
 * @struct xl_heap_lock
 * @brief Represents a lock operation in the heap.
 *
 * Contains the locking transaction ID, offset number, infomask bits, and associated flags.
 */
struct xl_heap_lock
{
   transaction_id locking_xid; /**< Transaction ID of the locking operation. */
   offset_number offnum;       /**< Locked tuple's offset on page.           */
   int8_t infobits_set;        /**< Infomask and infomask2 bits to set.     */
   uint8_t flags;              /**< Flags associated with the lock operation. */
};

/**
 * @struct xl_heap_inplace
 * @brief Represents an in-place update operation in the heap.
 *
 * Contains the offset number for the updated tuple.
 */
struct xl_heap_inplace
{
   offset_number offnum; /**< Updated tuple's offset on page. */
   /* TUPLE DATA FOLLOWS AT END OF STRUCT */
};

/**
 * @struct xl_heap_prune_v17
 * @brief Represents a prune operation in the heap (version 17).
 *
 * Contains reason and flags for the prune operation.
 */
struct xl_heap_prune_v17
{
   uint8_t reason; /**< Reason for pruning. */
   uint8_t flags;  /**< Flags for pruning operation. */
   /* If XLHP_HAS_CONFLICT_HORIZON is set, the conflict horizon XID follows, unaligned */
};

/**
 * @struct xl_heap_prune_v16
 * @brief Represents a prune operation in the heap (version 16).
 *
 * Contains transaction ID for snapshot conflict horizon, number of redirected and dead tuples, and a flag indicating if it's a catalog relation.
 */
struct xl_heap_prune_v16
{
   transaction_id snapshotConflictHorizon; /**< Conflict horizon XID. */
   uint16_t nredirected;                   /**< Number of redirected tuples. */
   uint16_t ndead;                         /**< Number of dead tuples. */
   bool is_catalog_rel;                    /**< Is this a catalog relation. */
   /* OFFSET NUMBERS are in the block reference 0 */
};

/**
 * @struct xl_heap_prune_v15
 * @brief Represents a prune operation in the heap (version 15).
 *
 * Contains transaction ID for the latest removed tuple, number of redirected and dead tuples.
 */
struct xl_heap_prune_v15
{
   transaction_id latestRemovedXid; /**< Latest removed XID. */
   uint16_t nredirected;            /**< Number of redirected tuples. */
   uint16_t ndead;                  /**< Number of dead tuples. */
   /* OFFSET NUMBERS are in the block reference 0 */
};

/**
 * @struct xl_heap_prune_v14
 * @brief Represents a prune operation in the heap (version 14).
 *
 * Contains transaction ID for the latest removed tuple, number of redirected and dead tuples.
 */
struct xl_heap_prune_v14
{
   transaction_id latestRemovedXid; /**< Latest removed XID. */
   uint16_t nredirected;            /**< Number of redirected tuples. */
   uint16_t ndead;                  /**< Number of dead tuples. */
   /* OFFSET NUMBERS are in the block reference 0 */
};

/**
 * @struct xl_heap_clean_v13
 * @brief Represents a cleaning operation in the heap (version 13).
 *
 * Similar to prune but named differently.
 * Contains transaction ID for the latest removed tuple, number of redirected and dead tuples.
 */
struct xl_heap_clean_v13
{
   transaction_id latestRemovedXid; /**< Latest removed XID. */
   uint16_t nredirected;            /**< Number of redirected tuples. */
   uint16_t ndead;                  /**< Number of dead tuples. */
   /* OFFSET NUMBERS are in the block reference 0 */
};

/**
 * @struct xl_heap_prune
 * @brief Wrapper structure to handle different versions of prune operations in the heap.
 *
 * Contains a union for version-specific data and function pointers for parsing and formatting records.
 */
struct xl_heap_prune
{
   void (*parse)(struct xl_heap_prune* wrapper, void* rec);   /**< Function pointer to parse the record */
   char* (*format)(struct xl_heap_prune* wrapper, char* buf); /**< Function pointer to format the record */
   union
   {
      struct xl_heap_prune_v17 v17; /**< Prune operation for version 17 */
      struct xl_heap_prune_v16 v16; /**< Prune operation for version 16 */
      struct xl_heap_prune_v15 v15; /**< Prune operation for version 15 */
      struct xl_heap_prune_v14 v14; /**< Prune operation for version 14 */
      struct xl_heap_clean_v13 v13; /**< Prune operation for version 13 (named clean) */
   } data;                          /**< Version-specific prune data */
};

/**
 * @struct xl_heap_vacuum
 * @brief Represents a vacuum operation in the heap.
 *
 * Contains the number of unused items.
 */
struct xl_heap_vacuum
{
   uint16_t nunused; /**< Number of unused items. */
};

/**
 * @struct xl_heap_visible
 * @brief Represents the setting of a visibility map bit in the heap.
 *
 * Contains the cutoff transaction ID and associated flags.
 */
struct xl_heap_visible
{
   transaction_id cutoff_xid; /**< Cutoff transaction ID. */
   uint8_t flags;             /**< Flags associated with the visibility operation. */
};

/**
 * @struct xl_heap_freeze_page_v15
 * @brief Represents a heap freeze operation for version 15.
 *
 * This structure is used in WAL records to describe freezing of tuples in heap pages for version 15.
 */
struct xl_heap_freeze_page_v15
{
   transaction_id cutoff_xid; /**< Transaction ID cutoff for freezing tuples. */
   uint16_t ntuples;          /**< Number of tuples to freeze. */
};

/**
 * @struct xl_heap_freeze_page_v16
 * @brief Represents a heap freeze operation for version 16.
 *
 * This structure is used in WAL records to describe freezing of tuples in heap pages for version 16.
 */
struct xl_heap_freeze_page_v16
{
   transaction_id snapshot_conflict_horizon; /**< Transaction ID snapshot conflict horizon. */
   uint16_t nplans;                          /**< Number of freeze plans. */
   bool is_catalog_rel;                      /**< Indicates if the relation is a catalog relation. */
};

/**
 * @struct xl_heap_freeze_page
 * @brief Wrapper structure to handle different versions of heap freeze operation.
 *
 * This structure allows for handling different versions of heap freeze operations using a union.
 */
struct xl_heap_freeze_page
{
   void (*parse)(struct xl_heap_freeze_page* wrapper, void* rec);   /**< Parse function pointer.    */
   char* (*format)(struct xl_heap_freeze_page* wrapper, char* buf); /**< Format function pointer.  */
   union
   {
      struct xl_heap_freeze_page_v15 v15; /**< Version 15 heap freeze structure. */
      struct xl_heap_freeze_page_v16 v16; /**< Version 16 heap freeze structure. */
   } data;                                /**< Version-specific data.    */
};

/**
 * @struct xl_heap_new_cid
 * @brief Represents a new command ID operation in the heap.
 *
 * Contains the top-level transaction ID, command IDs, and the target relfilenode and ctid.
 */
struct xl_heap_new_cid
{
   transaction_id top_xid;              /**< Top-level transaction ID. */
   command_id cmin;                     /**< Minimum command ID. */
   command_id cmax;                     /**< Maximum command ID. */
   command_id combocid;                 /**< Combined command ID (for debugging).*/
   struct rel_file_node target_node;    /**< Target relfilenode. */
   struct item_pointer_data target_tid; /**< Target ctid. */
};

/**
 * @struct xl_heap_multi_insert
 * @brief Represents a multi-insert operation in the heap.
 *
 * Contains flags, number of tuples, and an array of offsets for the tuples.
 */
struct xl_heap_multi_insert
{
   uint8_t flags;                                /**< Flags associated with the multi-insert operation. */
   uint16_t ntuples;                             /**< Number of tuples to insert. */
   offset_number offsets[FLEXIBLE_ARRAY_MEMBER]; /**< Array of tuple offsets. */
};

/**
 * @struct xl_heap_lock_updated
 * @brief Represents a lock operation on an updated version of a row in the heap.
 *
 * Contains the transaction ID, offset number, infomask bits, and associated flags.
 */
struct xl_heap_lock_updated
{
   transaction_id xmax;  /**< Transaction ID of the locking operation. */
   offset_number offnum; /**< Offset of the locked tuple on page.      */
   uint8_t infobits_set; /**< Infomask bits to set.                    */
   uint8_t flags;        /**< Flags associated with the lock operation. */
};

/**
 * @struct xlhp_freeze_plan
 * @brief Represents a freeze plan for heap tuples.
 *
 * Contains transaction ID, infomask, and associated flags for the freeze operation.
 */
struct xlhp_freeze_plan
{
   transaction_id xmax;  /**< Transaction ID for freezing. */
   uint16_t t_infomask2; /**< Second infomask value. */
   uint16_t t_infomask;  /**< First infomask value. */
   uint8_t frzflags;     /**< Flags for freeze operation. */
   uint16_t ntuples;     /**< Number of tuples affected. */
};

/**
 * @struct xlhp_freeze_plans
 * @brief Represents a collection of freeze plans for heap tuples.
 *
 * Contains the number of freeze plans and an array of freeze plans.
 */
struct xlhp_freeze_plans
{
   uint16_t nplans;                                      /**< Number of freeze plans. */
   struct xlhp_freeze_plan plans[FLEXIBLE_ARRAY_MEMBER]; /**< Array of freeze plans. */
};

/**
 * @struct xlhp_prune_items
 * @brief Represents a collection of prune items for heap tuples.
 *
 * Contains the number of prune items and an array of offsets for the items.
 */
struct xlhp_prune_items
{
   uint16_t ntargets;                         /**< Number of prune items. */
   offset_number data[FLEXIBLE_ARRAY_MEMBER]; /**< Array of prune item offsets. */
};

/**
 * @struct xl_heap_cleanup_info
 * @brief Represents cleanup information for the heap.
 *
 * Contains the latest removed transaction ID.
 */
struct xl_heap_cleanup_info
{
   struct rel_file_node node;       /**< RelFileNode of the relation */
   transaction_id latestRemovedXid; /**< Latest removed transaction ID */
};

// Function declarations
/**
 * @brief Creates a new xl_heap_prune structure.
 *
 * @return A pointer to the newly created xl_heap_prune structure.
 */
struct xl_heap_prune* create_xl_heap_prune(void);

/**
 * @brief Parses a version 17 prune record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param rec The record to parse.
 */
void xl_heap_prune_parse_v17(struct xl_heap_prune* wrapper, void* rec);

/**
 * @brief Parses a version 16 prune record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param rec The record to parse.
 */
void xl_heap_prune_parse_v16(struct xl_heap_prune* wrapper, void* rec);

/**
 * @brief Parses a version 15 prune record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param rec The record to parse.
 */
void xl_heap_prune_parse_v15(struct xl_heap_prune* wrapper, void* rec);

/**
 * @brief Parses a version 14 prune record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param rec The record to parse.
 */
void xl_heap_prune_parse_v14(struct xl_heap_prune* wrapper, void* rec);

/**
 * @brief Parses a version 13 clean record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param rec The record to parse.
 */
void xl_heap_prune_parse_v13(struct xl_heap_prune* wrapper, void* rec);

/**
 * @brief Formats a version 17 prune record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char* xl_heap_prune_format_v17(struct xl_heap_prune* wrapper, char* buf);

/**
 * @brief Formats a version 16 prune record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char* xl_heap_prune_format_v16(struct xl_heap_prune* wrapper, char* buf);

/**
 * @brief Formats a version 15 prune record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char* xl_heap_prune_format_v15(struct xl_heap_prune* wrapper, char* buf);

/**
 * @brief Formats a version 14 prune record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char* xl_heap_prune_format_v14(struct xl_heap_prune* wrapper, char* buf);

/**
 * @brief Formats a version 13 clean record.
 *
 * @param wrapper The wrapper structure containing the record data.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the formatted string.
 */
char* xl_heap_prune_format_v13(struct xl_heap_prune* wrapper, char* buf);

/**
 * @brief Describes a heap operation from a decoded XLOG record.
 *
 * @param buf The buffer to store the description.
 * @param record The decoded XLOG record containing the heap operation.
 * @return A pointer to the buffer containing the description.
 */
char* pgmoneta_wal_heap_desc(char* buf, struct decoded_xlog_record* record);

/**
 * @brief Describes a heap2 operation from a decoded XLOG record.
 *
 * @param buf The buffer to store the description.
 * @param record The decoded XLOG record containing the heap2 operation.
 * @return A pointer to the buffer containing the description.
 */
char* pgmoneta_wal_heap2_desc(char* buf, struct decoded_xlog_record* record);

/**
 * @brief Deserializes prune and freeze operations for the heap.
 *
 * @param cursor The cursor position within the XLOG record.
 * @param flags Flags indicating the presence of certain operations.
 * @param nplans Pointer to store the number of freeze plans.
 * @param plans Pointer to store the freeze plans array.
 * @param frz_offsets Pointer to store the freeze offsets array.
 * @param nredirected Pointer to store the number of redirected items.
 * @param redirected Pointer to store the redirected items array.
 * @param ndead Pointer to store the number of dead items.
 * @param nowdead Pointer to store the dead items array.
 * @param nunused Pointer to store the number of unused items.
 * @param nowunused Pointer to store the unused items array.
 */
void heap_xlog_deserialize_prune_and_freeze(char* cursor, uint8_t flags,
                                            int* nplans, struct xlhp_freeze_plan** plans,
                                            offset_number** frz_offsets,
                                            int* nredirected, offset_number** redirected,
                                            int* ndead, offset_number** nowdead,
                                            int* nunused, offset_number** nowunused);

/**
 * Creates a new xl_heap_freeze_page structure.
 *
 * @return A pointer to the newly created xl_heap_freeze_page structure.
 */
struct xl_heap_freeze_page* pgmoneta_wal_create_xl_heap_freeze_page(void);

/**
 * Parses a version 15 xl_heap_freeze_page structure.
 *
 * @param wrapper The wrapper structure containing the union of different versions.
 * @param rec The raw data to parse.
 */
void pgmoneta_wal_parse_xl_heap_freeze_page_v15(struct xl_heap_freeze_page* wrapper, void* rec);

/**
 * Parses a version 16 xl_heap_freeze_page structure.
 *
 * @param wrapper The wrapper structure containing the union of different versions.
 * @param rec The raw data to parse.
 */
void pgmoneta_wal_parse_xl_heap_freeze_page_v16(struct xl_heap_freeze_page* wrapper, void* rec);

/**
 * Formats a version 15 xl_heap_freeze_page structure.
 *
 * @param wrapper The wrapper structure containing the union of different versions.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the buffer containing the formatted string.
 */
char* pgmoneta_wal_format_xl_heap_freeze_page_v15(struct xl_heap_freeze_page* wrapper, char* buf);

/**
 * Formats a version 16 xl_heap_freeze_page structure.
 *
 * @param wrapper The wrapper structure containing the union of different versions.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the buffer containing the formatted string.
 */
char* pgmoneta_wal_format_xl_heap_freeze_page_v16(struct xl_heap_freeze_page* wrapper, char* buf);

#ifdef __cplusplus
}
#endif

#endif // PGMONETA_RM_HEAP_H
