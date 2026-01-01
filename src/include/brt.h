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

#ifndef PGMONETA_BLKREFTABLE_H
#define PGMONETA_BLKREFTABLE_H

#include <pgmoneta.h>
#include <art.h>
#include <wal.h>
#include <walfile/wal_reader.h>

/**
 * A block reference table is used to keep track of which blocks have been modified by WAL records within a
 * certain LSN range.
 *
 * For every relation fork, we track all the blocks that have been mentioned in the WAL
 * (Write-Ahead Logging). Along with that, we also record a "limit block," which represents the smallest
 * size (in blocks) that the relation has had during that range of WAL records. This limit block should
 * be set to 0 if the relation fork was either created or deleted, or to the new size after a truncation
 * has occurred.
 *
 * We have to store the blocks that have been modified for each relation file, to make it a bit efficient
 * we have two different representations of each block table entry.
 *
 * Firstly we will divide the relation into chunks of 2^16 blocks and choose between an array representation
 * if the number of modified block in a chunk are less and a bitmap representtaion if nearly all the blocks are modified
 *
 * In array representation, we don't need to store the entire block number instead we store each block number as a 2-byte
 * offset from the start of the chunk.
 *
 * These same basic representational choices are used both when a block reference table is stored in memory
 * and when it is serialized to disk.
 *
 */
#define BITS_PER_BYTE             8
#define BLOCKS_PER_CHUNK          (1 << 16)
#define BLOCKS_PER_ENTRY          (BITS_PER_BYTE * sizeof(uint16_t))
#define MAX_ENTRIES_PER_CHUNK     (BLOCKS_PER_CHUNK / BLOCKS_PER_ENTRY)
#define INITIAL_ENTRIES_PER_CHUNK 16
#define BLOCKS_PER_READ           512
/* Magic number for serialization file format. */
#define BLOCKREFTABLE_MAGIC 0x652b137b

typedef uint16_t* block_ref_table_chunk;

/**
 * A block reference table monitors and records the state of each fork separately.
 * The key is used to search for the block entry in the ART
 */
typedef struct block_ref_table_key
{
   struct rel_file_locator rlocator; /**< The relation file locator for the relation fork */
   enum fork_number forknum;         /**< The fork number of the relation fork */
} block_ref_table_key;

/**
 * State for one relation fork.
 *
 * 'rlocator' and 'forknum' identify the relation fork to which this entry
 * pertains.
 *
 * 'limit_block' represents the smallest known size (in blocks) of a relation during the range of LSNs (log sequence numbers) that a specific block reference table covers.
 * - If the relation fork is either created or dropped, this value should be set to 0.
 * - If the relation is truncated, it should be set to the number of blocks remaining after the truncation.
 *
 * 'nchunks' is the allocated length of each of the three arrays that follow. We can only represent the
 * status of block numbers less than nchunks * BLOCKS_PER_CHUNK.
 *
 * 'chunk_size' is an array storing the allocated size of each chunk.
 *
 * 'chunk_usage' is an array storing the number of elements used in each chunk. If that value is less
 * than MAX_ENTRIES_PER_CHUNK, the corresponding chunk is used as an array; else the corresponding
 * chunk is used as a bitmap. When used as a bitmap, the least significant bit of the first array element
 * is the status of the lowest-numbered block covered by this chunk.
 *
 * 'chunk_data' is the array of chunks, each element is either an array representation or bitmap representation of a chunk, that tracks the block number that have been modified.
 * In the array representation, each element is a 2-byte offset from the start of the chunk.
 * In the bitmap representation, each element is a 16-bit integer where each bit represents a block number within the chunk.
 * The least significant bit corresponds to the first block in the chunk, the next bit corresponds to the second block, and so on.
 */
typedef struct block_ref_table_entry
{
   block_ref_table_key key;           /**< The key used to search for the block entry in the ART */
   block_number limit_block;          /**< The limit block for the relation fork */
   block_number max_block_number;     /**< The maximum block number encoutered */
   uint32_t nchunks;                  /**< The number of chunks for the relation fork */
   uint16_t* chunk_size;              /**< The size of each chunk in the relation fork */
   uint16_t* chunk_usage;             /**< The number of used entries in each chunk, if a chunk has bitmap representation, the value of chunk_usage for that chunk is MAX_ENTRIES_PER_CHUNK */
   block_ref_table_chunk* chunk_data; /**< The array of chunks for the relation fork, each element is either an array representation or bitmap representation of a chunk, that tracks the block number that have been modified. */
} block_ref_table_entry;

/**
 *  Collection of block reference table entries
 */
typedef struct block_ref_table
{
   struct art* table; /**< The ART (Adaptive Radix Tree) used to store the block reference table entries */
} block_ref_table;

/**
 * On-disk serialization format for block reference table entries.
 */
typedef struct block_ref_table_serialized_entry
{
   struct rel_file_locator rlocator; /**< The relation file locator for the relation fork */
   enum fork_number forknum;         /**< The fork number of the relation fork */
   block_number limit_block;         /**< The limit block for the relation fork */
   uint32_t nchunks;                 /**< The number of chunks for the relation fork */
} block_ref_table_serialized_entry;

/**
 * Buffer used for read and write to disk
 */
typedef struct block_ref_table_buffer
{
   char data[65536]; /**< the memory buffer storing the serialized form of block reference table, if the given space is exhausted it call the brt_io callback to write its content to disk */
   int used;         /**< Number of bytes used in the data buffer */
   int cursor;       /**< The current position in the data buffer, used to track where to write next */
} block_ref_table_buffer;

/**
 * State for keeping track of progress while incrementally writing a block
 * reference table file to disk.
 */
struct block_ref_table_writer
{
   block_ref_table_buffer buffer; /**< The buffer used for writing the serialized form of block reference table to disk */
};

/**
 * State for keeping track of progress while incrementally reading a block
 * table reference file from disk.
 */
struct block_ref_table_reader
{
   block_ref_table_buffer buffer;              /**< The buffer used for reading the serialized form of block reference table from disk */
   uint32_t total_chunks;                      /**< The total number of chunks for the RelFileLocator/ForkNumber combination being read */
   uint32_t consumed_chunks;                   /**< The number of chunks that have been read so far */
   uint16_t* chunk_size;                       /**< The array of chunk sizes for the relation fork */
   uint16_t chunk_data[MAX_ENTRIES_PER_CHUNK]; /**< The current chunk being read, it can be either an array or a bitmap */
   uint32_t chunk_position;                    /**< The current position in the chunk_data, used to track how many blocks have been processed */
};

/****  BRT MANIPULATION APIs *****/

/**
 * Create an empty block reference table
 * @returns 0 if success, otherwise 1
 */
int
pgmoneta_brt_create_empty(block_ref_table** brt);

/**
 * Set the 'limit block' for a relation fork
 * Mark any modified block with equal or higher block number as unused
 * @param brt pointer to the block reference table
 * @param rlocator pointer to the relfilelocator for the relation fork
 * @param forknum the fork number of the relation fork
 * @param limit_block the block number to be set as limit block
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_brt_set_limit_block(block_ref_table* brt, const struct rel_file_locator* rlocator,
                             enum fork_number forknum, block_number limit_block);

/**
 * Mark a block in a given relation fork as known to have been modified.
 * @param brt pointer to the block reference table
 * @param rlocator pointer to the relfilelocator for the relation fork
 * @param forknum the fork number of the relation fork
 * @param blknum the block number to be set as modified/used
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_brt_mark_block_modified(block_ref_table* brtab, const struct rel_file_locator* rlocator,
                                 enum fork_number forknum, block_number blknum);

/**
 * Get an entry from the block reference table
 * @param brt pointer to the block reference table
 * @param rlocator pointer to the relfilelocator for the relation fork
 * @param forknum the fork number of the relation fork
 * @param limit_block [out] set the limit_block from the value of entry
 * @return entry [out] The block reference entry if found, otherwise NULL
 */
block_ref_table_entry*
pgmoneta_brt_get_entry(block_ref_table* brtab, const struct rel_file_locator* rlocator,
                       enum fork_number forknum, block_number* limit_block);

/**
 * Get block numbers from a table entry.
 * @param entry pointer to the brt entry
 * @param start_blkno start processing from this block number
 * @param stop_blkno stop processing, beyond this block number
 * @param blocks [out] Block array which contain all the modified blocks in the brt entry between [start_blkno, stop_blkno)
 * @param nblocks The lower bound on the number of blocks that can be stored
 * @param nresult [out] The number of blocks found in the range [start_blkno, stop_blkno)
 * @return 0 if success, otherwise failure.
 */
int
pgmoneta_brt_entry_get_blocks(block_ref_table_entry* entry, block_number start_blkno,
                              block_number stop_blkno, block_number* blocks, int nblocks, int* nresult);

/**
 * Destroy the brt
 * @param brt The table to be destroyed
 */
int
pgmoneta_brt_destroy(block_ref_table* brt);

/**
 * Destroy the block entry (used as a callback)
 * @param entry The entry to be destroyed
 */
void
pgmoneta_brt_entry_destroy(uintptr_t entry);

/****  BRT SERIALIZATION APIs *****/

/**
 * Write the contents of the block reference table to a file stream
 * Format:
 * | magic_number | rlocator0 | forknum0 | limit_block0 | nchunks0 | entry0_chunk_usage | entry0_chunk_data |
 * rlocator1 | forknum1 | limit_block1 | nchunks1 | entry1_chunk_usage | entry1_chunk_data |
 * .....
 * .....
 * rlocatorN | forknumN | limit_blockN | nchunksN | entryN_chunk_usage | entryN_chunk_data | 0 | 0 | 0 | 0 |
 * The last serialized entry is all zeros and denote a termination
 * @param brt The block reference table
 * @param file The file path
 * @return 0 if success, otherwise failure
 */
int
pgmoneta_brt_write(block_ref_table* brt, char* file);

/**
 * Read the contents of the summary file and create a block reference table from it
 * @param file The file path
 * @param [out] brt The block reference table
 * @return 0 if success, otherwise failure
 */
int
pgmoneta_brt_read(char* file, block_ref_table** brt);

#endif