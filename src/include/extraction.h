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

#ifndef PGMONETA_EXTRACTION_H
#define PGMONETA_EXTRACTION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Remove extraction-related suffixes from a file path.
 * For example, "001.tar.zstd.aes" becomes "001" when the bitmask includes TAR, ZSTD, and ENCRYPTED.
 *
 * When type is 0, the bitmask is detected automatically from the file path.
 *
 * @param file_path The source file path
 * @param type The file type bitmask (PGMONETA_FILE_TYPE_*), or 0 for auto-detect
 * @param base_name Resulting base path without extraction suffixes
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_extraction_strip_suffix(char* file_path, uint32_t type, char** base_name);

/**
 * Build the compound suffix string for a given compression and encryption configuration.
 * For example, with ZSTD compression and AES encryption, produces ".zstd.aes".
 *
 * @param compression The compression type (COMPRESSION_* constant)
 * @param encryption The encryption type (ENCRYPTION_* constant)
 * @param suffix The resulting suffix string (caller must free)
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_extraction_get_suffix(int compression, int encryption, char** suffix);

/**
 * Extract a file using the streamer for in-memory decryption and decompression.
 * Decryption and decompression are handled in one pass via streamer(RESTORE).
 * Assumes single compression + single encryption.
 *
 * When type is 0, the bitmask is detected automatically from the file path.
 * When copy is true, the source is streamed (decrypted+decompressed) to the destination path,
 * and the destination pointer is updated to the final extracted file path.
 * When copy is false, the source is streamed to a tar, then extracted to the destination directory.
 *
 * @param file_path The source file path
 * @param type The file type bitmask (PGMONETA_FILE_TYPE_*), or 0 for auto-detect
 * @param copy If true, extract to destination path. If false, extract archive to destination directory.
 * @param destination When copy is true, points to the destination path (updated to final extracted path).
 *                    When copy is false, points to the output directory for extraction (not modified).
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_extract_file(char* file_path, uint32_t type, bool copy, char** destination);

/**
 * Extract a file from a backup to a target location.
 * Wrapper around pgmoneta_extract_file for backup-relative paths.
 *
 * Builds the source path from backup data directory + relative_file_path,
 * and the destination from target_directory (or workspace if NULL) + relative_file_path.
 * Then calls pgmoneta_extract_file(from, 0, true, &to).
 *
 * @param server The server index
 * @param label The backup label
 * @param relative_file_path The file path relative to the backup data directory
 * @param target_directory The target directory (NULL to use workspace/label/)
 * @param target_file [out] The final extracted file path (caller must free)
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_extract_backup_file(int server, char* label, char* relative_file_path, char* target_directory, char** target_file);

#ifdef __cplusplus
}
#endif

#endif
