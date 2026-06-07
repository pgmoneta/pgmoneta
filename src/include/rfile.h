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

#ifndef PGMONETA_RFILE_H
#define PGMONETA_RFILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <deque.h>

#include <stdint.h>
#include <stdio.h>

/**
 * @struct rfile
 * An rfile stores the metadata we need to use a file on disk for reconstruction.
 * For full backup file in the chain, only filepath and file pointer are initialized.
 *
 * num_blocks is the number of blocks present inside an incremental file.
 * These are the blocks that have changed since the last checkpoint.
 * truncation_block_length is basically the shortest length this file has been between this and last checkpoint.
 * Note that truncation_block_length could be even greater than the number of blocks the original file has.
 * Because the tables are not locked during the backup, so blocks could be truncated during the process,
 * while truncation_block_length only reflects length until the checkpoint before backup starts.
 * relative_block_numbers are the relative BlockNumber of each block in the file. Relative here means relative to
 * the starting BlockNumber of this file.
 */
struct rfile
{
   char* filepath;                   /**< The path of the backup file  */
   FILE* fp;                         /**< The file descriptor corresponding to the backup file */
   size_t header_length;             /**< The header length */
   uint32_t num_blocks;              /**< The number of blocks present inside an incremental file */
   uint32_t* relative_block_numbers; /**< relative_block_numbers are the relative BlockNumber of each block in the file */
   uint32_t truncation_block_length; /**< truncation_block_length only reflects length until the checkpoint before backup starts. */
};

/**
 * Create an rfile structure of a backup file
 * @param server The server
 * @param label The label of the backup
 * @param relative_dir The relative path inside the data directory (excluding the filename)
 * @param base_file_name The file name
 * @param encryption The encryption method
 * @param compression The compression method
 * @param failures The failure deque
 * @param rfile [out] The rfile
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_rfile_create(int server, char* label, char* relative_dir, char* base_file_name, int encryption, int compression, struct deque* failures, struct rfile** rfile);

/**
 * Destroy the rfile structure
 * @param rfile The rfile to be destroyed
 */
void
pgmoneta_rfile_destroy(struct rfile* rf);

/**
 * Initialize an rfile structure of an incremental file by reading the incremental file headers
 * @param server The server
 * @param label The label of the backup
 * @param relative_dir The relative path inside the data directory (excluding the filename)
 * @param base_file_name The file name
 * @param encryption The encryption method
 * @param compression The compression method
 * @param failures The failure deque
 * @param rfile [out] The rfile
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_incremental_rfile_initialize(int server, char* label, char* relative_dir, char* base_file_name, int encryption, int compression, struct deque* failures, struct rfile** rfile);

#ifdef __cplusplus
}
#endif

#endif
