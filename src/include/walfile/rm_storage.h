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

#ifndef PGMONETA_RM_STORAGE_H
#define PGMONETA_RM_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <walfile/wal_reader.h>

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
   struct rel_file_node rnode;   /**< Relation file node information. */
   enum fork_number forkNum;     /**< Fork number for the created file. */
};

/**
 * @struct xl_smgr_truncate
 * @brief Represents a storage manager truncate operation in XLOG.
 *
 * Contains the block number, relation file node, and flags indicating which components are truncated.
 */
struct xl_smgr_truncate
{
   block_number blkno;           /**< Block number from where the truncation starts. */
   struct rel_file_node rnode;   /**< Relation file node information. */
   int flags;                    /**< Flags indicating which components are truncated. */
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
