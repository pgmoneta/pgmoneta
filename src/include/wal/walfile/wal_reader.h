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
#ifndef PGMONETA_WAL_READER_H
#define PGMONETA_WAL_READER_H

#ifdef __cplusplus
extern "C" {
#endif

/* pgmoneta-related includes */
#include <pgmoneta.h>
#include <wal/walfile/transaction.h>

/* System includes */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Typedefs */
typedef uint32_t timeline_id;
typedef uint64_t xlog_rec_ptr;
typedef uint32_t pg_crc32c;
typedef uint8_t rmgr_id;
typedef uint64_t xlog_seg_no;
typedef uint16_t rep_origin_id;
typedef int64_t timestamp_tz;

typedef int buffer;
typedef uint32_t block_number;
typedef unsigned int oid;
typedef oid rel_file_number;

/* #define variables */
#define MAXIMUM_ALIGNOF            8  // TODO: double check this value
#define ALIGNOF_SHORT              2  // TODO: double check this value
#define InvalidXLogRecPtr          0
#define InvalidBuffer              0
#define XLOG_PAGE_MAGIC            0xD10D  // WAL version indicator
#define InvalidOid                 ((oid) 0)
#define FLEXIBLE_ARRAY_MEMBER      /* empty */
#define INVALID_REP_ORIGIN_ID      0
#define XLR_MAX_BLOCK_ID           32
#define XLR_BLOCK_ID_DATA_SHORT    255
#define XLR_BLOCK_ID_DATA_LONG     254
#define XLR_BLOCK_ID_ORIGIN        253
#define XLR_BLOCK_ID_TOPLEVEL_XID  252
#define BKPBLOCK_FORK_MASK         0x0F
#define BKPBLOCK_FLAG_MASK         0xF0
#define BKPBLOCK_HAS_IMAGE         0x10    /* Block data is an XLogRecordBlockImage */
#define BKPBLOCK_HAS_DATA          0x20
#define BKPBLOCK_WILL_INIT         0x40    /* Redo will re-init the page */
#define BKPBLOCK_SAME_REL          0x80    /* rel_file_locator omitted, same as previous */
#define BKPIMAGE_HAS_HOLE          0x01    /* Page image has a "hole" */
#define BKPIMAGE_IS_COMPRESSED     0x02    /* Page image is compressed */
#define BKPIMAGE_APPLY             0x04    /* Page image should be restored during replay */
#define BKPIMAGE_COMPRESS_PGLZ     0x04
#define BKPIMAGE_COMPRESS_LZ4      0x08
#define BKPIMAGE_COMPRESS_ZSTD     0x10

#define SIZE_OF_XLOG_LONG_PHD      MAXALIGN(sizeof(struct xlog_long_page_header_data))
#define SIZE_OF_XLOG_SHORT_PHD     MAXALIGN(sizeof(struct xlog_page_header_data))
#define SIZE_OF_XLOG_RECORD        (offsetof(struct xlog_record, xl_crc) + sizeof(pg_crc32c))

/* #define macros */
#define MAXALIGN(x)                (((x) + (sizeof(void*) - 1)) & ~(sizeof(void*) - 1))
#define TYPEALIGN(ALIGNVAL, LEN)   (((uintptr_t)(LEN) + ((ALIGNVAL) -1)) & ~((uintptr_t)((ALIGNVAL) -1)))
#define MAXALIGNTYPE(LEN)          TYPEALIGN(MAXIMUM_ALIGNOF, (LEN))
#define SHORTALIGN(LEN)            TYPEALIGN(ALIGNOF_SHORT, (LEN))

#define XLogRecHasBlockRef(record, block_id) \
        ((record->max_block_id >= (block_id)) && \
         (record->blocks[block_id].in_use))

#define XLogRecHasBlockImage(record, block_id) \
        (record->blocks[block_id].has_image)

#define XLogRecHasBlockData(record, block_id) \
        (record->blocks[block_id].has_data)

#define LSN_FORMAT_ARGS(lsn)    ((uint32_t)((lsn) >> 32)), ((uint32_t)(lsn))
#define XLOG_REC_GET_DATA(record) ((record)->main_data)
#define XLOG_REC_GET_INFO(record) ((record)->header.xl_info)
#define XLOG_REC_GET_BLOCK(record, i) (&record->blocks[(i)])
#define XLOG_REC_BLOCK_IMAGE_APPLY(record, block_id) (record->blocks[block_id].apply_image)
#define XLOG_REC_GET_ORIGIN(record) (record->record_origin)
#define XLOG_REC_GET_DATA_LEN(record) (record->main_data_len)


/* Enums */

/**
 * @enum fork_number
 * @brief Enumeration of different fork numbers.
 *
 * This enum represents various fork types used in PostgreSQL.
 */
enum fork_number
{
    InvalidForkNumber = -1,    /**< Invalid fork number. */
    MAIN_FORKNUM = 0,          /**< Main fork. */
    FSM_FORKNUM,               /**< Free space map fork. */
    VISIBILITYMAP_FORKNUM,     /**< Visibility map fork. */
    INIT_FORKNUM,              /**< Initialization fork. */

    /*
     * NOTE: if you add a new fork, change MAX_FORKNUM and possibly
     * FORKNAMECHARS below, and update the forkNames array in
     * src/common/relpath.c
     */
};

/**
 * @enum wal_level
 * @brief Enumeration of WAL levels.
 *
 * Represents the various levels of WAL (Write-Ahead Logging).
 */
enum wal_level
{
    WAL_LEVEL_MINIMAL = 0,    /**< Minimal WAL logging. */
    WAL_LEVEL_REPLICA,        /**< WAL logging for replication. */
    WAL_LEVEL_LOGICAL         /**< Logical WAL logging. */
};

/* Structs */

/**
 * @struct xlog_page_header_data
 * @brief Represents the header of an XLOG page.
 *
 * Contains metadata for an XLOG page including magic value,
 * timeline ID, and page address.
 *
 * Fields:
 * - xlp_magic: Magic value for correctness checks.
 * - xlp_info: Flag bits for the page.
 * - xlp_tli: Timeline ID of the first record on the page.
 * - xlp_pageaddr: XLOG address of this page.
 * - xlp_rem_len: Remaining length of data for the record.
 */
struct xlog_page_header_data {
    uint16_t xlp_magic;         /**< Magic value for correctness checks. */
    uint16_t xlp_info;          /**< Flag bits for the page. */
    timeline_id xlp_tli;        /**< Timeline ID of the first record on the page. */
    xlog_rec_ptr xlp_pageaddr;  /**< XLOG address of this page. */
    uint32_t xlp_rem_len;       /**< Remaining length of data for the record. */
};

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
struct xlog_long_page_header_data {
    struct xlog_page_header_data std;    /**< Standard header fields. */
    uint64_t xlp_sysid;                  /**< System identifier from pg_control. */
    uint32_t xlp_seg_size;               /**< Segment size for cross-checking. */
    uint32_t xlp_xlog_blcksz;            /**< XLOG block size for cross-checking. */
};

/**
 * @struct xlog_record
 * @brief Represents an XLOG record.
 *
 * Contains metadata for an XLOG record, including transaction ID,
 * previous record pointer, and CRC.
 *
 * Fields:
 * - xl_tot_len: Total length of the entire record.
 * - xl_xid: Transaction ID associated with the record.
 * - xl_prev: Pointer to the previous record in the log.
 * - xl_info: Flag bits for the record.
 * - xl_rmid: Resource manager ID for this record.
 * - xl_crc: CRC for this record.
 */
struct xlog_record {
    uint32_t xl_tot_len;        /**< Total length of the entire record. */
    transaction_id xl_xid;      /**< Transaction ID associated with the record. */
    xlog_rec_ptr xl_prev;       /**< Pointer to the previous record in the log. */
    uint8_t xl_info;            /**< Flag bits for the record. */
    rmgr_id xl_rmid;            /**< Resource manager ID for this record. */
    pg_crc32c xl_crc;           /**< CRC for this record. */
};

/**
 * @struct rel_file_locator
 * @brief Identifies a relation file.
 *
 * Used to locate a relation file by tablespace, database, and relation ID.
 *
 * Fields:
 * - spcOid: Tablespace OID.
 * - dbOid: Database OID.
 * - relNumber: Relation file number.
 */
struct rel_file_locator {
    oid spcOid;                  /**< Tablespace OID. */
    oid dbOid;                   /**< Database OID. */
    rel_file_number relNumber;   /**< Relation file number. */
};

/**
 * @struct decoded_bkp_block
 * @brief Represents a decoded backup block.
 *
 * Contains information about a block reference, including whether
 * it is in use, has an image, and related data.
 *
 * Fields:
 * - in_use: Indicates if this block reference is in use.
 * - rlocator: Locator for the referenced block.
 * - forknum: Fork number of the block.
 * - blkno: Block number.
 * - prefetch_buffer: Prefetching workspace.
 * - flags: Copy of the fork_flags field from the block header.
 * - has_image: Indicates if the block has an image.
 * - apply_image: Indicates if the image should be applied.
 * - bkp_image: Backup image of the block.
 * - hole_offset: Offset of the hole in the image.
 * - hole_length: Length of the hole in the image.
 * - bimg_len: Length of the backup image.
 * - bimg_info: Additional information about the backup image.
 * - has_data: Indicates if the block has associated data.
 * - data: Data associated with the block.
 * - data_len: Length of the data.
 * - data_bufsz: Buffer size for the data.
 */
struct decoded_bkp_block {
    bool in_use;                        /**< Indicates if this block reference is in use. */
    struct rel_file_locator rlocator;   /**< Locator for the referenced block. */
    enum fork_number forknum;           /**< Fork number of the block. */
    block_number blkno;                 /**< Block number. */
    buffer prefetch_buffer;             /**< Prefetching workspace. */
    uint8_t flags;                      /**< Copy of the fork_flags field from the block header. */
    bool has_image;                     /**< Indicates if the block has an image. */
    bool apply_image;                   /**< Indicates if the image should be applied. */
    char* bkp_image;                    /**< Backup image of the block. */
    uint16_t hole_offset;               /**< Offset of the hole in the image. */
    uint16_t hole_length;               /**< Length of the hole in the image. */
    uint16_t bimg_len;                  /**< Length of the backup image. */
    uint8_t bimg_info;                  /**< Additional information about the backup image. */
    bool has_data;                      /**< Indicates if the block has associated data. */
    char* data;                         /**< Data associated with the block. */
    uint16_t data_len;                  /**< Length of the data. */
    uint16_t data_bufsz;                /**< Buffer size for the data. */
};

/**
 * @struct decoded_xlog_record
 * @brief Represents a decoded XLOG record.
 *
 * Contains the decoded contents of an XLOG record, including
 * block references, main data, and transaction information.
 *
 * Fields:
 * - size: Total size of the decoded record.
 * - oversized: Indicates if the record is outside the regular decode buffer.
 * - next: Link to the next decoded record in the queue.
 * - lsn: Location of the record.
 * - next_lsn: Location of the next record.
 * - header: Header of the record.
 * - record_origin: Origin ID of the record.
 * - toplevel_xid: Top-level transaction ID.
 * - main_data: Main data portion of the record.
 * - main_data_len: Length of the main data portion.
 * - max_block_id: Highest block ID in use (-1 if none).
 * - blocks: Array of decoded backup blocks.
 */
struct decoded_xlog_record {
    size_t size;                          /**< Total size of the decoded record. */
    bool oversized;                       /**< Indicates if the record is outside the regular decode buffer. */
    struct decoded_xlog_record* next;     /**< Link to the next decoded record in the queue. */
    xlog_rec_ptr lsn;                     /**< Location of the record. */
    xlog_rec_ptr next_lsn;                /**< Location of the next record. */
    struct xlog_record header;            /**< Header of the record. */
    rep_origin_id record_origin;          /**< Origin ID of the record. */
    transaction_id toplevel_xid;          /**< Top-level transaction ID. */
    char* main_data;                      /**< Main data portion of the record. */
    uint32_t main_data_len;               /**< Length of the main data portion. */
    int max_block_id;                     /**< Highest block ID in use (-1 if none). */
    struct decoded_bkp_block blocks[XLR_MAX_BLOCK_ID + 1];   /**< Array of decoded backup blocks. */
};

/**
 * @struct rel_file_node
 * @brief Identifies a relation file node.
 *
 * Used to locate a relation file by tablespace, database, and relation ID.
 *
 * Fields:
 * - spcNode: Tablespace OID.
 * - dbNode: Database OID.
 * - relNode: Relation OID.
 */
struct rel_file_node {
    oid spcNode;     /**< Tablespace OID. */
    oid dbNode;      /**< Database OID. */
    oid relNode;     /**< Relation OID. */
};

/* External variables */
extern struct server* server_config;

/* Function definitions */

/**
 * Parses a WAL file and populates server information.
 *
 * @param path The file path of the WAL file.
 * @param server_info The server structure to be populated with parsed data.
 */
void
pgmoneta_wal_parse_wal_file(char* path, struct server* server_info);

/**
 * Decodes an XLOG record from a buffer.
 *
 * @param buffer The buffer containing the XLOG record data.
 * @param decoded The structure to hold the decoded XLOG record.
 * @param record The original XLOG record structure.
 * @param block_size The block size used for decoding.
 * @param server_info The server structure for context.
 * @return true if the decoding was successful, false otherwise.
 */
bool
pgmoneta_wal_decode_xlog_record(char* buffer, struct decoded_xlog_record* decoded, struct xlog_record* record, uint32_t block_size, struct server* server_info);

/**
 * Retrieves the length of the XLOG record.
 *
 * @param record The decoded XLOG record.
 * @param rec_len Pointer to hold the record length.
 * @param fpi_len Pointer to hold the full-page image length.
 */
void
pgmoneta_wal_get_record_length(struct decoded_xlog_record* record, uint32_t* rec_len, uint32_t* fpi_len);

/**
 * Displays the decoded XLOG record.
 *
 * @param record The decoded XLOG record to display.
 * @param count The count of records to display.
 * @param server_info The server structure for context.
 */
void
pgmoneta_wal_display_decoded_record(struct decoded_xlog_record* record, int count, struct server* server_info);

/**
 * Retrieves block reference information from the decoded XLOG record.
 *
 * @param buf The buffer to store the block reference information.
 * @param record The decoded XLOG record.
 * @param pretty Format output in a more readable way if true.
 * @param detailed_format Use a detailed format if true.
 * @param fpi_len Pointer to store the full-page image length.
 * @param server_info The server structure for context.
 * @return The buffer containing the block reference information.
 */
char*
pgmoneta_wal_get_record_block_ref_info(char* buf, struct decoded_xlog_record* record, bool pretty, bool detailed_format, uint32_t* fpi_len, struct server* server_info);

/**
 * Retrieves block data from the decoded XLOG record.
 *
 * @param record The decoded XLOG record.
 * @param block_id The block ID to retrieve data from.
 * @param len Pointer to store the length of the retrieved data.
 * @return The block data.
 */
char*
pgmoneta_wal_get_record_block_data(struct decoded_xlog_record* record, uint8_t block_id, size_t* len);

/**
 * Retrieves the block tag extended information from the decoded XLOG record.
 *
 * @param pRecord The decoded XLOG record.
 * @param id The block ID to retrieve the tag for.
 * @param pLocator The relation file locator structure to be filled.
 * @param pNumber The fork number structure to be filled.
 * @param pInt The block number structure to be filled.
 * @param pVoid The buffer structure to be filled.
 * @return true if the retrieval was successful, false otherwise.
 */
bool
pgmoneta_wal_get_record_block_tag_extended(struct decoded_xlog_record* pRecord, int id, struct rel_file_locator* pLocator, enum fork_number* pNumber,
                                           block_number* pInt, buffer* pVoid);

/**
 * Checks if the backup image is compressed.
 *
 * @param server_info The server structure for context.
 * @param bimg_info The backup image information to check.
 * @return true if the image is compressed, false otherwise.
 */
bool
pgmoneta_wal_is_bkp_image_compressed(struct server* server_info, uint8_t bimg_info);

char*
pgmoneta_wal_array_desc(char* buf, void* array, size_t elem_size, int count);

#ifdef __cplusplus
}
#endif

#endif // PGMONETA_WAL_READER_H
