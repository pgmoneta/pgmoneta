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

#ifndef PGMONETA_RM_HASH_H
#define PGMONETA_RM_HASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <walfile/rm.h>
#include <walfile/wal_reader.h>

typedef oid reg_procedure;

/* XLOG records for hash operations */
#define XLOG_HASH_INIT_META_PAGE         0x00 /**< Initialize the meta page. */
#define XLOG_HASH_INIT_BITMAP_PAGE       0x10 /**< Initialize the bitmap page. */
#define XLOG_HASH_INSERT                 0x20 /**< Add index tuple without split. */
#define XLOG_HASH_ADD_OVFL_PAGE          0x30 /**< Add overflow page. */
#define XLOG_HASH_SPLIT_ALLOCATE_PAGE    0x40 /**< Allocate new page for split. */
#define XLOG_HASH_SPLIT_PAGE             0x50 /**< Split page. */
#define XLOG_HASH_SPLIT_COMPLETE         0x60 /**< Completion of split operation. */
#define XLOG_HASH_MOVE_PAGE_CONTENTS     0x70 /**< Remove tuples from one page and add to another page. */
#define XLOG_HASH_SQUEEZE_PAGE           0x80 /**< Add tuples to one of the previous pages in chain and free the overflow page. */
#define XLOG_HASH_DELETE                 0x90 /**< Delete index tuples from a page. */
#define XLOG_HASH_SPLIT_CLEANUP          0xA0 /**< Clear split-cleanup flag in primary bucket page. */
#define XLOG_HASH_UPDATE_META_PAGE       0xB0 /**< Update meta page after vacuum. */
#define XLOG_HASH_VACUUM_ONE_PAGE        0xC0 /**< Remove dead tuples from index page. */

#define XLH_SPLIT_META_UPDATE_MASKS      (1 << 0)
#define XLH_SPLIT_META_UPDATE_SPLITPOINT (1 << 1)

/**
 * @struct xl_hash_insert
 * @brief Represents a simple insert operation (without split) in a hash index.
 *
 * This data record is used for the XLOG_HASH_INSERT operation.
 */
struct xl_hash_insert
{
   offset_number offnum; /**< Offset where the tuple is inserted. */
};

/**
 * @struct xl_hash_add_ovfl_page
 * @brief Represents the addition of an overflow page in a hash index.
 *
 * This data record is used for the XLOG_HASH_ADD_OVFL_PAGE operation.
 */
struct xl_hash_add_ovfl_page
{
   uint16_t bmsize;   /**< Size of the bitmap. */
   bool bmpage_found; /**< Indicates if a bitmap page was found. */
};

/**
 * @struct xl_hash_split_allocate_page
 * @brief Represents the allocation of a page for a split operation in a hash index.
 *
 * This data record is used for the XLOG_HASH_SPLIT_ALLOCATE_PAGE operation.
 */
struct xl_hash_split_allocate_page
{
   uint32_t new_bucket;      /**< New bucket number. */
   uint16_t old_bucket_flag; /**< Flag for the old bucket. */
   uint16_t new_bucket_flag; /**< Flag for the new bucket. */
   uint8_t flags;            /**< Additional flags for the split operation. */
};

/**
 * @struct xl_hash_split_complete
 * @brief Represents the completion of a split operation in a hash index.
 *
 * This data record is used for the XLOG_HASH_SPLIT_COMPLETE operation.
 */
struct xl_hash_split_complete
{
   uint16_t old_bucket_flag; /**< Flag for the old bucket. */
   uint16_t new_bucket_flag; /**< Flag for the new bucket. */
};

/**
 * @struct xl_hash_move_page_contents
 * @brief Represents the movement of page contents during a squeeze operation in a hash index.
 *
 * This data record is used for the XLOG_HASH_MOVE_PAGE_CONTENTS operation.
 */
struct xl_hash_move_page_contents
{
   uint16_t ntups;               /**< Number of tuples moved. */
   bool is_prim_bucket_same_wrt; /**< Indicates if the primary bucket page is the same as the page to which tuples are moved. */
};

/**
 * @struct xl_hash_squeeze_page
 * @brief Represents a squeeze page operation in a hash index.
 *
 * This data record is used for the XLOG_HASH_SQUEEZE_PAGE operation.
 */
struct xl_hash_squeeze_page
{
   block_number prevblkno;       /**< Block number of the previous page. */
   block_number nextblkno;       /**< Block number of the next page.     */
   uint16_t ntups;               /**< Number of tuples moved.            */
   bool is_prim_bucket_same_wrt; /**< Indicates if the primary bucket page is the same as the page to which tuples are moved. */
   bool is_prev_bucket_same_wrt; /**< Indicates if the previous page is the same as the page to which tuples are moved. */
};

/**
 * @struct xl_hash_delete
 * @brief Represents the deletion of index tuples from a page in a hash index.
 *
 * This data record is used for the XLOG_HASH_DELETE operation.
 */
struct xl_hash_delete
{
   bool clear_dead_marking;     /**< Indicates if the LH_PAGE_HAS_DEAD_TUPLES flag is cleared. */
   bool is_primary_bucket_page; /**< Indicates if the operation is for the primary bucket page. */
};

/**
 * @struct xl_hash_update_meta_page
 * @brief Represents an update to the meta page of a hash index.
 *
 * This data record is used for the XLOG_HASH_UPDATE_META_PAGE operation.
 */
struct xl_hash_update_meta_page
{
   double ntuples; /**< Number of tuples in the meta page. */
};

/**
 * @struct xl_hash_init_meta_page
 * @brief Represents the initialization of the meta page of a hash index.
 *
 * This data record is used for the XLOG_HASH_INIT_META_PAGE operation.
 */
struct xl_hash_init_meta_page
{
   double num_tuples;    /**< Initial number of tuples. */
   reg_procedure procid; /**< Procedure ID. */
   uint16_t ffactor;     /**< Fill factor. */
};

/**
 * @struct xl_hash_init_bitmap_page
 * @brief Represents the initialization of a bitmap page in a hash index.
 *
 * This data record is used for the XLOG_HASH_INIT_BITMAP_PAGE operation.
 */
struct xl_hash_init_bitmap_page
{
   uint16_t bmsize; /**< Size of the bitmap. */
};

/**
 * @struct xl_hash_vacuum_one_page_v15
 * @brief Represents a vacuum operation on a single page in a hash index for version 15.
 *
 * This data record is used for the XLOG_HASH_VACUUM_ONE_PAGE operation in version 15.
 */
struct xl_hash_vacuum_one_page_v15
{
   transaction_id latestRemovedXid; /**< Latest removed transaction ID. */
   int ntuples;                     /**< Number of tuples to vacuum. */
   /* TARGET OFFSET NUMBERS FOLLOW AT THE END */
};

/**
 * @struct xl_hash_vacuum_one_page_v16
 * @brief Represents a vacuum operation on a single page in a hash index for version 16.
 *
 * This data record is used for the XLOG_HASH_VACUUM_ONE_PAGE operation in version 16.
 */
struct xl_hash_vacuum_one_page_v16
{
   transaction_id snaphost_conflict_horizon; /**< Snapshot conflict horizon. */
   uint16_t ntuples;                         /**< Number of tuples to vacuum. */
   bool is_catalog_rel;                      /**< Indicates if the relation is a catalog relation. */
   /* TARGET OFFSET NUMBERS */
   offset_number offsets[FLEXIBLE_ARRAY_MEMBER]; /**< Array of target offset numbers. */
};

/**
 * @struct xl_hash_vacuum_one_page
 * @brief Wrapper struct to handle different versions of xl_hash_vacuum_one_page.
 */
struct xl_hash_vacuum_one_page
{
   void (*parse)(struct xl_hash_vacuum_one_page* wrapper, void* rec);   /**< Function pointer to parse the record */
   char* (*format)(struct xl_hash_vacuum_one_page* wrapper, char* buf); /**< Function pointer to format the record */
   union
   {
      struct xl_hash_vacuum_one_page_v15 v15; /**< Version 15 */
      struct xl_hash_vacuum_one_page_v16 v16; /**< Version 16 */
   } data;                                    /**< Version-specific data. */
};

/**
 * Describes a hash index operation from a decoded XLOG record.
 *
 * @param buf The buffer to store the description.
 * @param record The decoded XLOG record containing the hash operation.
 * @return A pointer to the buffer containing the description.
 */
char*
pgmoneta_wal_hash_desc(char* buf, struct decoded_xlog_record* record);

/**
 * Create a new xl_hash_vacuum_one_page structure.
 *
 * This function allocates and initializes a new instance of the
 * xl_hash_vacuum_one_page structure.
 *
 * @return A pointer to the newly created xl_hash_vacuum_one_page structure.
 */
struct xl_hash_vacuum_one_page*
pgmoneta_wal_create_xl_hash_vacuum_one_page(void);

/**
 * Parse a WAL record of type xl_hash_vacuum_one_page for PostgreSQL version 15.
 *
 * This function parses the WAL record data and populates the provided
 * xl_hash_vacuum_one_page structure with the extracted information.
 *
 * @param wrapper A pointer to the xl_hash_vacuum_one_page structure to populate.
 * @param rec A pointer to the raw WAL record data to be parsed.
 */
void
pgmoneta_wal_parse_xl_hash_vacuum_one_page_v15(struct xl_hash_vacuum_one_page* wrapper, void* rec);

/**
 * Parse a WAL record of type xl_hash_vacuum_one_page for PostgreSQL version 16.
 *
 * This function parses the WAL record data and populates the provided
 * xl_hash_vacuum_one_page structure with the extracted information.
 *
 * @param wrapper A pointer to the xl_hash_vacuum_one_page structure to populate.
 * @param rec A pointer to the raw WAL record data to be parsed.
 */
void
pgmoneta_wal_parse_xl_hash_vacuum_one_page_v16(struct xl_hash_vacuum_one_page* wrapper, void* rec);

/**
 * Format the xl_hash_vacuum_one_page structure for PostgreSQL version 15 into a string.
 *
 * This function formats the contents of the provided xl_hash_vacuum_one_page
 * structure into a human-readable string and stores it in the provided buffer.
 *
 * @param wrapper A pointer to the xl_hash_vacuum_one_page structure to format.
 * @param buf A pointer to the buffer where the formatted string will be stored.
 * @return A pointer to the buffer containing the formatted string.
 */
char*
pgmoneta_wal_format_xl_hash_vacuum_one_page_v15(struct xl_hash_vacuum_one_page* wrapper, char* buf);

/**
 * Format the xl_hash_vacuum_one_page structure for PostgreSQL version 16 into a string.
 *
 * This function formats the contents of the provided xl_hash_vacuum_one_page
 * structure into a human-readable string and stores it in the provided buffer.
 *
 * @param wrapper A pointer to the xl_hash_vacuum_one_page structure to format.
 * @param buf A pointer to the buffer where the formatted string will be stored.
 * @return A pointer to the buffer containing the formatted string.
 */
char*
pgmoneta_wal_format_xl_hash_vacuum_one_page_v16(struct xl_hash_vacuum_one_page* wrapper, char* buf);

#ifdef __cplusplus
}
#endif

#endif // PGMONETA_RM_HASH_H
