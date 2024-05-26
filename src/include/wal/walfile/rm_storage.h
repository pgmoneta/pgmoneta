#ifndef PGMONETA_RM_STORAGE_H
#define PGMONETA_RM_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <wal/walfile/wal_reader.h>

#define XLOG_SMGR_CREATE   0x10   /**< XLOG opcode for creating a storage manager file. */
#define XLOG_SMGR_TRUNCATE 0x20   /**< XLOG opcode for truncating a storage manager file. */

/**
 * @struct xl_smgr_create
 * @brief Represents a storage manager create operation in XLOG.
 *
 * Contains the relation file node and fork number for the created storage manager file.
 */
struct xl_smgr_create
{
    struct rel_file_node rnode;  /**< Relation file node information. */
    enum fork_number forkNum;    /**< Fork number for the created file. */
};

/**
 * @struct xl_smgr_truncate
 * @brief Represents a storage manager truncate operation in XLOG.
 *
 * Contains the block number, relation file node, and flags indicating which components are truncated.
 */
struct xl_smgr_truncate
{
    block_number blkno;          /**< Block number from where the truncation starts. */
    struct rel_file_node rnode;  /**< Relation file node information. */
    int flags;                   /**< Flags indicating which components are truncated. */
};

/**
 * @brief Describes a storage manager operation from a decoded XLOG record.
 *
 * @param buf The buffer to store the description.
 * @param record The decoded XLOG record containing the storage manager operation.
 * @return A pointer to the buffer containing the description.
 */
char*
pgmoneta_wal_storage_desc(char* buf, struct decoded_xlog_record* record);

#ifdef __cplusplus
}
#endif

#endif // PGMONETA_RM_STORAGE_H
