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

#ifndef PGMONETA_PAGECHECKSUM_H
#define PGMONETA_PAGECHECKSUM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** The checksum PostgreSQL stores in a page is never zero, so zero is free to
 * mean "this page carries no checksum". */
#define PAGE_CHECKSUM_NONE 0

/**
 * Compute the checksum PostgreSQL would store in a page
 * @param page The page, of block_size bytes
 * @param block_size The block size of the cluster the page came from
 * @param blockno The block number within the relation, which for a segment
 * other than the first is the offset within the file plus
 * segment number * blocks per segment
 * @return The checksum
 */
uint16_t
pgmoneta_page_checksum(void* page, size_t block_size, uint32_t blockno);

/**
 * Read the checksum stored in a page
 * @param page The page
 * @return The checksum, or PAGE_CHECKSUM_NONE if the page carries none
 */
uint16_t
pgmoneta_page_stored_checksum(void* page);

/**
 * Is a page empty
 *
 * PostgreSQL leaves a page all zero until it is first used, and does not
 * checksum one. Such a page is not corrupt and must not be reported as such.
 * @param page The page
 * @param block_size The block size
 * @return true if the page is empty
 */
bool
pgmoneta_page_is_new(void* page, size_t block_size);

/**
 * Verify the checksum of a page
 * @param page The page
 * @param block_size The block size
 * @param blockno The block number within the relation
 * @param computed [out] The computed checksum, set unless the page is empty
 * @param stored [out] The checksum found in the page, set unless the page is empty
 * @return true if the page is intact, or is empty and therefore has nothing to verify
 */
bool
pgmoneta_page_verify(void* page, size_t block_size, uint32_t blockno,
                     uint16_t* computed, uint16_t* stored);

/**
 * Is a file one whose pages carry checksums
 *
 * Only the relation forks under base, global and pg_tblspc are made of pages,
 * and of those the files PostgreSQL excludes from its own checksumming are
 * excluded here too, so this agrees with pg_checksums.
 * @param path The path of the file, relative or absolute
 * @param segno [out] The segment the file holds, 0 for the first
 * @return true if the pages of this file can be checked
 */
bool
pgmoneta_page_checksummable(char* path, uint32_t* segno);

/**
 * Verify the pages of a file
 *
 * Returning 1 with number_of_bad above zero means the file was read and some of
 * its pages did not verify. Returning 1 with number_of_bad still zero means the
 * file could not be read to the end -- it was missing, unreadable, or did not
 * consist of whole blocks -- and nothing was concluded about its pages.
 * @param path The file
 * @param block_size The block size of the cluster
 * @param relseg_size The number of blocks in a segment
 * @param segno The segment the file holds
 * @param blockno [out] The first block that failed, when one did
 * @param computed [out] The checksum that block should have carried
 * @param stored [out] The checksum it did carry
 * @param number_of_bad [out] How many blocks failed
 * @return 0 if every page verified, otherwise 1
 */
int
pgmoneta_page_verify_file(char* path, size_t block_size, uint32_t relseg_size,
                          uint32_t segno, uint32_t* blockno,
                          uint16_t* computed, uint16_t* stored,
                          int* number_of_bad);

#ifdef __cplusplus
}
#endif

#endif
