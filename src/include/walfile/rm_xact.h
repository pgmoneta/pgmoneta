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

#ifndef PGMONETA_RM_XACT_H
#define PGMONETA_RM_XACT_H

#include <walfile/rm.h>
#include <walfile/rm_xlog.h>
#include <walfile/sinval.h>
#include <walfile/wal_reader.h>

#define GIDSIZE                                     200  /**< Maximum size of Global Transaction ID (including '\0'). */
#define XLOG_XACT_OPMASK                            0x70 /**< Mask for filtering opcodes out of xl_info. */
#define XLOG_XACT_HAS_INFO                          0x80 /**< Indicates if this record has a 'xinfo' field. */
#define XLOG_XACT_COMMIT                            0x00
#define XLOG_XACT_PREPARE                           0x10
#define XLOG_XACT_ABORT                             0x20
#define XLOG_XACT_COMMIT_PREPARED                   0x30
#define XLOG_XACT_ABORT_PREPARED                    0x40
#define XLOG_XACT_ASSIGNMENT                        0x50
#define XLOG_XACT_INVALIDATIONS                     0x60

#define XACT_XINFO_HAS_DBINFO                       (1U << 0)
#define XACT_XINFO_HAS_SUBXACTS                     (1U << 1)
#define XACT_XINFO_HAS_RELFILENODES                 (1U << 2)
#define XACT_XINFO_HAS_INVALS                       (1U << 3)
#define XACT_XINFO_HAS_TWOPHASE                     (1U << 4)
#define XACT_XINFO_HAS_ORIGIN                       (1U << 5)
#define XACT_XINFO_HAS_AE_LOCKS                     (1U << 6)
#define XACT_XINFO_HAS_GID                          (1U << 7)
#define XACT_XINFO_HAS_DROPPED_STATS                (1U << 8)

#define XACT_COMPLETION_APPLY_FEEDBACK_FLAG         (1U << 29)
#define XACT_COMPLETION_UPDATE_RELCACHE_FILE_FLAG   (1U << 30)
#define XACT_COMPLETION_FORCE_SYNC_COMMIT_FLAG      (1U << 31)

#define XACT_COMPLETION_APPLY_FEEDBACK(xinfo) \
        ((xinfo & XACT_COMPLETION_APPLY_FEEDBACK_FLAG) != 0)
#define XACT_COMPLETION_RELCACHE_INIT_FILE_INVAL(xinfo) \
        ((xinfo & XACT_COMPLETION_UPDATE_RELCACHE_FILE_FLAG) != 0)
#define XACT_COMPLETION_FORCE_SYNC_COMMIT(xinfo) \
        ((xinfo & XACT_COMPLETION_FORCE_SYNC_COMMIT_FLAG) != 0)

#define MIN_SIZE_OF_XACT_STATS_ITEMS    offsetof(struct xl_xact_stats_items, items)
#define MIN_SIZE_OF_XACT_SUBXACTS       offsetof(struct xl_xact_subxacts, subxacts)
#define MIN_SIZE_OF_XACT_RELFILENODES   offsetof(struct xl_xact_relfilenodes, xnodes)
#define MIN_SIZE_OF_XACT_INVALS         offsetof(struct xl_xact_invals, msgs)
#define MIN_SIZE_OF_XACT_COMMIT         (offsetof(struct xl_xact_commit, xact_time) + sizeof(timestamp_tz))
#define MIN_SIZE_OF_XACT_ABORT          sizeof(struct xl_xact_abort)

/**
 * @struct xl_xact_stats_item
 * @brief Represents a transaction statistic item.
 *
 * This structure holds information about a particular object within a transaction.
 * Each object is represented by a kind, a database OID, and the object's OID.
 *
 * Fields:
 * - kind: An integer that represents the type of transaction statistic being recorded.
 *         This could refer to various types of transaction events, such as an insert, delete, or update.
 * - dboid: The OID (Object Identifier) of the database in which the transaction is taking place.
 *          The OID is a unique identifier used internally by the database to distinguish between different objects.
 * - objoid: The OID of the specific object (table, index, etc.) that the transaction statistic pertains to.
 *           This represents the target object of the operation within the transaction.
 */
struct xl_xact_stats_item
{
   int kind;          /**< Type of transaction statistic */
   oid dboid;         /**< Database OID */
   oid objoid;        /**< Object OID */
};

/**
 * @struct xl_xact_stats_items
 * @brief Represents a collection of transaction statistic items.
 *
 * This structure aggregates multiple transaction statistic items, which provide information
 * about various objects affected by a transaction. It contains a count of the items and an
 * array of individual transaction statistic items.
 *
 * Fields:
 * - nitems: The number of transaction statistic items included in this structure.
 * - items: A flexible array of `xl_xact_stats_item` structures, where each item represents
 *          a particular object affected by the transaction. The flexible array member allows
 *          for variable-length arrays of transaction statistic items.
 */
struct xl_xact_stats_items
{
   int nitems;                                               /**< Number of transaction statistic items */
   struct xl_xact_stats_item items[FLEXIBLE_ARRAY_MEMBER];   /**< Array of transaction statistic items */
};

/**
 * @struct xl_xact_assignment
 * @brief Represents the assignment of a transaction ID in XLOG.
 *
 * Contains the top-level transaction ID and any associated subtransaction IDs.
 */
struct xl_xact_assignment
{
   transaction_id xtop;                         /**< Assigned XID's top-level XID. */
   int nsubxacts;                               /**< Number of subtransaction XIDs. */
   transaction_id xsub[FLEXIBLE_ARRAY_MEMBER];  /**< Array of assigned subxids. */
};

/**
 * @struct xl_xact_xinfo
 * @brief Represents extended transaction information in commit/abort records.
 *
 * Holds additional flags that indicate the presence of various pieces of
 * information within a commit or abort record.
 */
struct xl_xact_xinfo
{
   uint32_t xinfo;   /**< Flags indicating additional information in the record. */
};

/**
 * @struct xl_xact_dbinfo
 * @brief Represents database information in a commit/abort record.
 *
 * Contains the database ID and tablespace ID associated with the transaction.
 */
struct xl_xact_dbinfo
{
   oid db_id;     /**< Database ID. */
   oid ts_id;     /**< Tablespace ID. */
};

/**
 * @struct xl_xact_subxacts
 * @brief Represents subtransactions in a commit/abort record.
 *
 * Contains an array of subtransaction IDs.
 */
struct xl_xact_subxacts
{
   int nsubxacts;                                   /**< Number of subtransaction XIDs. */
   transaction_id subxacts[FLEXIBLE_ARRAY_MEMBER];  /**< Array of subtransaction IDs. */
};

/**
 * @struct xl_xact_relfilenodes
 * @brief Represents relfilenodes in a commit/abort record.
 *
 * Contains an array of relfilenodes for the transaction.
 */
struct xl_xact_relfilenodes
{
   int nrels;                                           /**< Number of relations. */
   struct rel_file_node xnodes[FLEXIBLE_ARRAY_MEMBER];  /**< Array of relation file nodes. */
};

/**
 * @struct xl_xact_invals
 * @brief Represents invalidation messages in a commit/abort record.
 *
 * Contains an array of shared invalidation messages.
 */
struct xl_xact_invals
{
   int nmsgs;                                                      /**< Number of shared invalidation messages. */
   union shared_invalidation_message msgs[FLEXIBLE_ARRAY_MEMBER];  /**< Array of invalidation messages. */
};

/**
 * @struct xl_xact_twophase
 * @brief Represents two-phase commit information in a commit/abort record.
 *
 * Contains the transaction ID for a two-phase commit.
 */
struct xl_xact_twophase
{
   transaction_id xid;   /**< Transaction ID for the two-phase commit. */
};

/**
 * @struct xl_xact_origin
 * @brief Represents the origin of a transaction in a commit/abort record.
 *
 * Contains the LSN and timestamp of the transaction at the origin node.
 */
struct xl_xact_origin
{
   xlog_rec_ptr origin_lsn;          /**< LSN of this record at the origin node. */
   timestamp_tz origin_timestamp;    /**< Timestamp of the transaction at the origin node. */
};

/**
 * @struct xl_xact_commit
 * @brief Represents a commit record in XLOG.
 *
 * Contains the timestamp of the commit and may include additional
 * information depending on the flags in `xinfo`.
 */
struct xl_xact_commit
{
   timestamp_tz xact_time;   /**< Time of commit. */

   /* Additional structures follow based on flags in `xinfo`:
    * - xl_xact_xinfo
    * - xl_xact_dbinfo
    * - xl_xact_subxacts
    * - xl_xact_relfilenodes
    * - xl_xact_invals
    * - xl_xact_twophase
    * - twophase_gid
    * - xl_xact_origin
    */
};

/**
 * @struct xl_xact_abort
 * @brief Represents an abort record in XLOG.
 *
 * Contains the timestamp of the abort and may include additional
 * information depending on the flags in `xinfo`.
 */
struct xl_xact_abort
{
   timestamp_tz xact_time;   /**< Time of abort. */

   /* Additional structures follow based on flags in `xinfo`:
    * - xl_xact_xinfo
    * - xl_xact_dbinfo
    * - xl_xact_subxacts
    * - xl_xact_relfilenodes
    * - xl_xact_twophase
    * - twophase_gid
    * - xl_xact_origin
    */
};

/**
 * @struct xl_xact_prepare_v14
 * @brief Represents the prepared transaction record (version 14).
 *
 * This data record is used for the XLOG_XACT_PREPARE operation in version 14.
 */
struct xl_xact_prepare_v14
{
   uint32_t magic;                    /**< Format identifier. */
   uint32_t total_len;                /**< Actual file length. */
   transaction_id xid;                /**< Original transaction XID. */
   oid database;                      /**< OID of the database it was in. */
   timestamp_tz prepared_at;          /**< Time of preparation. */
   oid owner;                         /**< User running the transaction. */
   int32_t nsubxacts;                 /**< Number of following subxact XIDs. */
   int32_t ncommitrels;               /**< Number of delete-on-commit rels. */
   int32_t nabortrels;                /**< Number of delete-on-abort rels. */
   int32_t ninvalmsgs;                /**< Number of cache invalidation messages. */
   bool initfileinval;                /**< Does relcache init file need invalidation? */
   uint16_t gidlen;                   /**< Length of the GID - GID follows the header. */
   xlog_rec_ptr origin_lsn;           /**< LSN of this record at origin node. */
   timestamp_tz origin_timestamp;     /**< Time of prepare at origin node. */
};

/**
 * @struct xl_xact_prepare_v15
 * @brief Represents the prepared transaction record (version 15).
 *
 * This data record is used for the XLOG_XACT_PREPARE operation in version 15.
 */
struct xl_xact_prepare_v15
{
   uint32_t magic;                     /**< Format identifier. */
   uint32_t total_len;                 /**< Actual file length. */
   transaction_id xid;                 /**< Original transaction XID. */
   oid database;                       /**< OID of the database it was in. */
   timestamp_tz prepared_at;           /**< Time of preparation. */
   oid owner;                          /**< User running the transaction. */
   int32_t nsubxacts;                  /**< Number of following subxact XIDs. */
   int32_t ncommitrels;                /**< Number of delete-on-commit rels. */
   int32_t nabortrels;                 /**< Number of delete-on-abort rels. */
   int32_t ncommitstats;               /**< Number of stats to drop on commit. */
   int32_t nabortstats;                /**< Number of stats to drop on abort. */
   int32_t ninvalmsgs;                 /**< Number of cache invalidation messages. */
   bool initfileinval;                 /**< Does relcache init file need invalidation? */
   uint16_t gidlen;                    /**< Length of the GID - GID follows the header. */
   xlog_rec_ptr origin_lsn;            /**< LSN of this record at origin node. */
   timestamp_tz origin_timestamp;      /**< Time of prepare at origin node. */
};

/**
 * @struct xl_xact_prepare
 * @brief A wrapper for versioned structures of the prepared transaction record.
 *
 * This is used to handle multiple versions of the xl_xact_prepare structure.
 */
struct xl_xact_prepare
{
   void (*parse)(struct xl_xact_prepare* wrapper, void* rec);         /**< Parsing function. */
   char* (*format)(struct xl_xact_prepare* wrapper, char* rec, char* buf);  /**< Formatting function. */
   union
   {
      struct xl_xact_prepare_v14 v14;                                       /**< Version 14 structure. */
      struct xl_xact_prepare_v15 v15;                                       /**< Version 15 structure. */
   } data;                                                                  /**< Union holding different structure versions. */
};

/**
 * @struct xl_xact_parsed_commit_v14
 * @brief Represents a parsed commit record from version 14.
 *
 * This structure holds details of a parsed commit operation in the transaction log.
 */
struct xl_xact_parsed_commit_v14
{
   timestamp_tz xact_time;                   /**< Transaction commit timestamp. */
   uint32_t xinfo;                           /**< Additional commit info flags. */

   oid db_id;                                /**< Database ID for the transaction. */
   oid ts_id;                                /**< Tablespace ID for the transaction. */

   int nsubxacts;                            /**< Number of subtransactions. */
   transaction_id* subxacts;                 /**< Array of subtransaction IDs. */

   int nrels;                                /**< Number of relations involved. */
   struct rel_file_node* xnodes;             /**< Array of relation file nodes. */

   int nmsgs;                                /**< Number of invalidation messages. */
   union shared_invalidation_message* msgs;  /**< Array of invalidation messages. */

   transaction_id twophase_xid;              /**< 2PC transaction ID. */
   char twophase_gid[GIDSIZE];               /**< Global ID for 2PC. */
   int nabortrels;                           /**< Number of relations aborted in 2PC. */
   struct rel_file_node* abortnodes;         /**< Array of aborted relation file nodes. */

   xlog_rec_ptr origin_lsn;                  /**< Log sequence number of the origin. */
   timestamp_tz origin_timestamp;            /**< Timestamp of the origin. */
};

typedef struct xl_xact_parsed_commit_v14 xl_xact_parsed_prepare_v14;

/**
 * @struct xl_xact_parsed_commit_v15
 * @brief Represents a parsed commit record from version 15.
 *
 * This structure holds details of a parsed commit operation in the transaction log.
 */
struct xl_xact_parsed_commit_v15
{
   timestamp_tz xact_time;                    /**< Transaction commit timestamp. */
   uint32_t xinfo;                            /**< Additional commit info flags. */
   oid db_id;                                 /**< Database ID for the transaction. */
   oid ts_id;                                 /**< Tablespace ID for the transaction. */
   int nsubxacts;                             /**< Number of subtransactions. */
   transaction_id* subxacts;                  /**< Array of subtransaction IDs. */
   int nrels;                                 /**< Number of relations involved. */
   struct rel_file_node* xnodes;              /**< Array of relation file nodes. */
   int nstats;                                /**< Number of statistical records. */
   struct xl_xact_stats_item* stats;          /**< Array of statistical records. */
   int nmsgs;                                 /**< Number of invalidation messages. */
   union shared_invalidation_message* msgs;   /**< Array of invalidation messages. */
   transaction_id twophase_xid;               /**< 2PC transaction ID. */
   char twophase_gid[GIDSIZE];                /**< Global ID for 2PC. */
   int nabortrels;                            /**< Number of relations aborted in 2PC. */
   struct rel_file_node* abortnodes;          /**< Array of aborted relation file nodes. */
   int nabortstats;                           /**< Number of aborted statistical records in 2PC. */
   struct xl_xact_stats_item* abortstats;     /**< Array of aborted statistical records in 2PC. */
   xlog_rec_ptr origin_lsn;                   /**< Log sequence number of the origin. */
   timestamp_tz origin_timestamp;             /**< Timestamp of the origin. */
};

typedef struct xl_xact_parsed_commit_v15 xl_xact_parsed_prepare_v15;

/**
 * @struct xl_xact_parsed_commit
 * @brief Wrapper structure for different versions of parsed commit records.
 *
 * This structure encapsulates version 14 and version 15 commit records.
 */
struct xl_xact_parsed_commit
{
   void (*parse)(struct xl_xact_parsed_commit* wrapper, void* rec);           /**< Parsing function pointer. */
   char* (*format)(struct xl_xact_parsed_commit* wrapper, char* rec, char* buf);    /**< Formatting function pointer. */
   union
   {
      struct xl_xact_parsed_commit_v14 v14;                                         /**< Parsed commit record for version 14. */
      struct xl_xact_parsed_commit_v15 v15;                                         /**< Parsed commit record for version 15. */
   } data;                                                                          /**< Union of parsed commit records. */
};

/**
 * @struct xl_xact_parsed_abort_v14
 * @brief Represents a parsed abort transaction in PostgreSQL (v14).
 *
 * This structure is used to store parsed transaction abort data for version 14.
 */
struct xl_xact_parsed_abort_v14
{
   timestamp_tz xact_time;             /**< Transaction commit timestamp. */
   uint32_t xinfo;                     /**< Additional info flags. */
   oid dbId;                           /**< Database OID. */
   oid tsId;                           /**< Tablespace OID. */
   int nsubxacts;                      /**< Number of subtransactions. */
   transaction_id* subxacts;           /**< Array of subtransaction IDs. */
   int nrels;                          /**< Number of relations involved. */
   struct rel_file_node* xnodes;       /**< Array of relation file nodes. */
   transaction_id twophase_xid;        /**< Two-phase transaction ID. */
   char twophase_gid[GIDSIZE];         /**< Two-phase transaction GID. */
   xlog_rec_ptr origin_lsn;            /**< Replication origin LSN. */
   timestamp_tz origin_timestamp;      /**< Replication origin timestamp. */
};

/**
 * @struct xl_xact_parsed_abort_v15
 * @brief Represents a parsed abort transaction in PostgreSQL (v15).
 *
 * This structure is used to store parsed transaction abort data for version 15.
 */
struct xl_xact_parsed_abort_v15
{
   timestamp_tz xact_time;             /**< Transaction commit timestamp. */
   uint32_t xinfo;                     /**< Additional info flags. */
   oid db_id;                          /**< Database OID. */
   oid ts_id;                          /**< Tablespace OID. */
   int nsubxacts;                      /**< Number of subtransactions. */
   transaction_id* subxacts;           /**< Array of subtransaction IDs. */
   int nrels;                          /**< Number of relations involved. */
   struct rel_file_node* xnodes;       /**< Array of relation file nodes. */
   int nstats;                         /**< Number of statistical changes. */
   struct xl_xact_stats_item* stats;   /**< Array of statistical changes. */
   transaction_id twophase_xid;        /**< Two-phase transaction ID. */
   char twophase_gid[GIDSIZE];         /**< Two-phase transaction GID. */
   xlog_rec_ptr origin_lsn;            /**< Replication origin LSN. */
   timestamp_tz origin_timestamp;      /**< Replication origin timestamp. */
};

/**
 * @struct xl_xact_parsed_abort
 * @brief Wrapper for handling different versions of parsed abort transactions.
 *
 * This structure provides a union for handling different versions of
 * the parsed abort transaction data (v14 and v15).
 */
struct xl_xact_parsed_abort
{
   void (*parse)(struct xl_xact_parsed_abort* wrapper, void* rec);         /**< Parse function pointer. */
   char* (*format)(struct xl_xact_parsed_abort* wrapper, char* rec, char* buf);  /**< Format function pointer. */
   union
   {
      struct xl_xact_parsed_abort_v14 v14;                                       /**< Parsed abort transaction data for version 14. */
      struct xl_xact_parsed_abort_v15 v15;                                       /**< Parsed abort transaction data for version 15. */
   } data;                                                                       /**< Union of parsed abort transaction data. */
};

/**
 * Describes a transaction operation from a decoded XLOG record.
 *
 * @param buf The buffer to store the description.
 * @param record The decoded XLOG record containing the transaction operation.
 * @return A pointer to the buffer containing the description.
 */
char*
pgmoneta_wal_xact_desc(char* buf, struct decoded_xlog_record* record);

/**
 * Parses a version 14 xl_xact_prepare record.
 *
 * @param wrapper The xl_xact_prepare wrapper.
 * @param rec The record data to parse.
 */
void
pgmoneta_wal_parse_xl_xact_prepare_v14(struct xl_xact_prepare* wrapper, void* rec);

/**
 * Parses a version 15 xl_xact_prepare record.
 *
 * @param wrapper The xl_xact_prepare wrapper.
 * @param rec The record data to parse.
 */
void
pgmoneta_wal_parse_xl_xact_prepare_v15(struct xl_xact_prepare* wrapper, void* rec);

/**
 * Formats a version 14 xl_xact_prepare record into a buffer.
 *
 * @param wrapper The xl_xact_prepare wrapper.
 * @param buf The buffer to store the formatted result.
 * @return The formatted buffer.
 */
char*
pgmoneta_wal_format_xl_xact_prepare_v14(struct xl_xact_prepare* wrapper, char* rec, char* buf);

/**
 * Formats a version 15 xl_xact_prepare record into a buffer.
 *
 * @param wrapper The xl_xact_prepare wrapper.
 * @param buf The buffer to store the formatted result.
 * @return The formatted buffer.
 */
char*
pgmoneta_wal_format_xl_xact_prepare_v15(struct xl_xact_prepare* wrapper, char* rec, char* buf);

/**
 * Initializes a parsed commit wrapper structure based on the server version.
 *
 * @return A pointer to the initialized xl_xact_parsed_commit structure.
 */
struct xl_xact_parsed_commit*
pgmoneta_wal_create_xact_parsed_commit(void);

/**
 * Parses a commit record for version 14.
 *
 * @param wrapper The commit wrapper structure to populate.
 * @param rec The raw commit record to parse.
 */
void
pgmoneta_wal_parse_xact_commit_v14(struct xl_xact_parsed_commit* wrapper, void* rec);

/**
 * Parses a commit record for version 15.
 *
 * @param wrapper The commit wrapper structure to populate.
 * @param rec The raw commit record to parse.
 */
void
pgmoneta_wal_parse_xact_commit_v15(struct xl_xact_parsed_commit* wrapper, void* rec);

/**
 * Parses a commit record for versions less than 15
 *
 * @param info The additional information inside the record
 * @param xlrec The main data of the record
 * @param parsed [out] The parsed contents
 */
void
pgmoneta_wal_parse_commit_record_l15(uint8_t info, struct xl_xact_commit* xlrec, struct xl_xact_parsed_commit_v14* parsed);

/**
 * Parses a commit record for versions greater than or equal to 15
 *
 * @param info The additional information inside the record
 * @param xlrec The main data of the record
 * @param parsed [out] The parsed contents
 */
void
pgmoneta_wal_parse_commit_record_ge15(uint8_t info, struct xl_xact_commit* xlrec, struct xl_xact_parsed_commit_v15* parsed);

/**
 * Formats a commit record for version 14 into a human-readable string.
 *
 * @param wrapper The commit wrapper structure to format.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the buffer containing the formatted string.
 */
char*
pgmoneta_wal_format_xact_commit_v14(struct xl_xact_parsed_commit* wrapper, char* rec, char* buf);

/**
 * Formats a commit record for version 15 into a human-readable string.
 *
 * @param wrapper The commit wrapper structure to format.
 * @param buf The buffer to store the formatted string.
 * @return A pointer to the buffer containing the formatted string.
 */
char*
pgmoneta_wal_format_xact_commit_v15(struct xl_xact_parsed_commit* wrapper, char* rec, char* buf);

/**
 * Create a new xl_xact_parsed_abort structure.
 *
 * This function allocates and initializes a new xl_xact_parsed_abort structure,
 * which is used to store parsed information about a transaction abort record
 * in the Write-Ahead Logging (WAL).
 *
 * @return Pointer to the newly created xl_xact_parsed_abort structure.
 */
struct xl_xact_parsed_abort*
pgmoneta_wal_create_xl_xact_parsed_abort(void);

/**
 * Parse a transaction abort record (v14).
 *
 * This function parses the transaction abort record for version 14 of the
 * WAL and stores the parsed information in the provided xl_xact_parsed_abort
 * structure.
 *
 * @param wrapper Pointer to the xl_xact_parsed_abort structure where the parsed data will be stored.
 * @param rec Pointer to the raw WAL record to be parsed.
 */
void
pgmoneta_wal_parse_xl_xact_parsed_abort_v14(struct xl_xact_parsed_abort* wrapper, void* rec);

/**
 * Parse a transaction abort record (v15).
 *
 * This function parses the transaction abort record for version 15 of the
 * WAL and stores the parsed information in the provided xl_xact_parsed_abort
 * structure.
 *
 * @param wrapper Pointer to the xl_xact_parsed_abort structure where the parsed data will be stored.
 * @param rec Pointer to the raw WAL record to be parsed.
 */
void
pgmoneta_wal_parse_xl_xact_parsed_abort_v15(struct xl_xact_parsed_abort* wrapper, void* rec);

/**
 * Parses a abort record for versions less than 15
 *
 * @param info The additional information inside the record
 * @param xlrec The main data of the record
 * @param parsed [out] The parsed contents
 */
void
pgmoneta_wal_parse_abort_record_l15(uint8_t info, struct xl_xact_abort* xlrec, struct xl_xact_parsed_abort_v14* parsed);

/**
 * Parses a abort record for versions greater than equal to 15
 *
 * @param info The additional information inside the record
 * @param xlrec The main data of the record
 * @param parsed [out] The parsed contents
 */
void
pgmoneta_wal_parse_abort_record_ge15(uint8_t info, struct xl_xact_abort* xlrec, struct xl_xact_parsed_abort_v15* parsed);

/**
 * Format a transaction abort record (v14) as a string.
 *
 * This function formats the parsed transaction abort record for version 14
 * of the WAL into a human-readable string and stores it in the provided buffer.
 *
 * @param wrapper Pointer to the xl_xact_parsed_abort structure containing the parsed data.
 * @param rec Pointer to the raw WAL record.
 * @param buf Buffer where the formatted string will be stored.
 * @return Pointer to the formatted string.
 */
char*
pgmoneta_wal_format_xl_xact_parsed_abort_v14(struct xl_xact_parsed_abort* wrapper, char* rec, char* buf);

/**
 * Format a transaction abort record (v15) as a string.
 *
 * This function formats the parsed transaction abort record for version 15
 * of the WAL into a human-readable string and stores it in the provided buffer.
 *
 * @param wrapper Pointer to the xl_xact_parsed_abort structure containing the parsed data.
 * @param rec Pointer to the raw WAL record.
 * @param buf Buffer where the formatted string will be stored.
 * @return Pointer to the formatted string.
 */
char*
pgmoneta_wal_format_xl_xact_parsed_abort_v15(struct xl_xact_parsed_abort* wrapper, char* rec, char* buf);

#endif // PGMONETA_RM_XACT_H
