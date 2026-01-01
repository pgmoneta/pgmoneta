/*
 * Copyright (C) 2026 The pgmoneta community
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

#ifndef PGMONETA_RM_H
#define PGMONETA_RM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef uint16_t offset_number;
typedef struct block_id_data* block_id;

// #define Variables
#define XLR_INFO_MASK      0x0F
#define XLR_RMGR_INFO_MASK 0xF0

// #define Macros
/**
 * @def ITEM_POINTER_GET_OFFSET_NUMBER_NO_CHECK(pointer)
 * @brief Retrieves the offset number from an item pointer without validation.
 * @param pointer Pointer to the item.
 * @return The offset number.
 */
#define ITEM_POINTER_GET_OFFSET_NUMBER_NO_CHECK(pointer) \
   (                                                     \
      (pointer)->ip_posid)

/**
 * @def ITEM_POINTER_GET_OFFSET_NUMBER(pointer)
 * @brief Retrieves the offset number from an item pointer with validation.
 * @param pointer Pointer to the item.
 * @return The offset number.
 */
#define ITEM_POINTER_GET_OFFSET_NUMBER(pointer) \
   (                                            \
      ITEM_POINTER_GET_OFFSET_NUMBER_NO_CHECK(pointer))

/**
 * @def ITEM_POINTER_GET_BLOCK_NUMBER_NO_CHECK(pointer)
 * @brief Retrieves the block number from an item pointer without validation.
 * @param pointer Pointer to the item.
 * @return The block number.
 */
#define ITEM_POINTER_GET_BLOCK_NUMBER_NO_CHECK(pointer) \
   (                                                    \
      BLOCK_ID_GET_BLOCK_NUMBER(&(pointer)->ip_blkid))

/**
 * @def ITEM_POINTER_GET_BLOCK_NUMBER(pointer)
 * @brief Retrieves the block number from an item pointer with validation.
 * @param pointer Pointer to the item.
 * @return The block number.
 */
#define ITEM_POINTER_GET_BLOCK_NUMBER(pointer) \
   (                                           \
      ITEM_POINTER_GET_BLOCK_NUMBER_NO_CHECK(pointer))

/**
 * @def BLOCK_ID_GET_BLOCK_NUMBER(blockId)
 * @brief Retrieves the block number from a block identifier.
 * @param blockId Pointer to the block identifier.
 * @return The block number.
 */
#define BLOCK_ID_GET_BLOCK_NUMBER(blockId) \
   (                                       \
      ((((block_number)(blockId)->bi_hi) << 16) | ((block_number)(blockId)->bi_lo)))

/**
 * @def POSTING_ITEM_GET_BLOCK_NUMBER(pointer)
 * @brief Retrieves the block number from a posting item pointer.
 * @param pointer Pointer to the item.
 * @return The block number.
 */
#define POSTING_ITEM_GET_BLOCK_NUMBER(pointer) \
   BLOCK_ID_GET_BLOCK_NUMBER(&(pointer)->child_blkno)

/**
 * @def PostingItemSetBlockNumber(pointer, blockNumber)
 * @brief Sets the block number in a posting item pointer.
 * @param pointer Pointer to the item.
 * @param blockNumber Block number to set.
 */
#define PostingItemSetBlockNumber(pointer, blockNumber) \
   BlockIdSet(&((pointer)->child_blkno), (blockNumber))

// Structs
/**
 * @struct block_id_data
 * @brief Represents a block identifier with high and low parts.
 * Fields:
 * - bi_hi: High part of the block identifier.
 * - bi_lo: Low part of the block identifier.
 */
struct block_id_data
{
   uint16_t bi_hi; /**< High part of the block identifier. */
   uint16_t bi_lo; /**< Low part of the block identifier. */
};

/**
 * @struct item_pointer_data
 * @brief Represents a pointer to a specific item on a disk.
 * Fields:
 * - ip_blkid: Block identifier.
 * - ip_posid: Offset within the block.
 */
struct item_pointer_data
{
   struct block_id_data ip_blkid; /**< Block identifier. */
   offset_number ip_posid;        /**< Offset within the block. */
};

#ifdef __cplusplus
}
#endif

#endif // PGMONETA_RM_H
