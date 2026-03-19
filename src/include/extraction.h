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

/* File type bitmask constants */
#define PGMONETA_FILE_TYPE_UNKNOWN    0x0000 /* Unknown file type */
#define PGMONETA_FILE_TYPE_WAL        0x0001 /* WAL file (24-char hex name) */
#define PGMONETA_FILE_TYPE_COMPRESSED 0x0002 /* Compressed (any type) */
#define PGMONETA_FILE_TYPE_GZIP       0x0004 /* Compressed with gzip (.gz) */
#define PGMONETA_FILE_TYPE_LZ4        0x0008 /* Compressed with lz4 (.lz4) */
#define PGMONETA_FILE_TYPE_ZSTD       0x0010 /* Compressed with zstd (.zstd) */
#define PGMONETA_FILE_TYPE_BZ2        0x0020 /* Compressed with bzip2 (.bz2) */
#define PGMONETA_FILE_TYPE_ENCRYPTED  0x0040 /* Encrypted (.aes) */
#define PGMONETA_FILE_TYPE_TAR        0x0080 /* TAR archive (.tar) */
#define PGMONETA_FILE_TYPE_PARTIAL    0x0100 /* Partial file (.partial) */
#define PGMONETA_FILE_TYPE_ALL        0xFFFF /* Match all file types */
#define PGMONETA_FILE_TYPE_COMPRESSION_MASK \
   (PGMONETA_FILE_TYPE_COMPRESSED | PGMONETA_FILE_TYPE_GZIP | PGMONETA_FILE_TYPE_LZ4 | PGMONETA_FILE_TYPE_ZSTD | PGMONETA_FILE_TYPE_BZ2)
#define PGMONETA_FILE_TYPE_EXTRACTION_MASK \
   (PGMONETA_FILE_TYPE_ENCRYPTED | PGMONETA_FILE_TYPE_TAR | PGMONETA_FILE_TYPE_COMPRESSION_MASK)

/**
 * Get the file type bitmask for a given file path.
 * The bitmask can include combinations of:
 * - PGMONETA_FILE_TYPE_WAL (24-char hex WAL file)
 * - PGMONETA_FILE_TYPE_COMPRESSED (any compression)
 * - PGMONETA_FILE_TYPE_GZIP (.gz)
 * - PGMONETA_FILE_TYPE_LZ4 (.lz4)
 * - PGMONETA_FILE_TYPE_ZSTD (.zstd)
 * - PGMONETA_FILE_TYPE_BZ2 (.bz2)
 * - PGMONETA_FILE_TYPE_ENCRYPTED (.aes)
 * - PGMONETA_FILE_TYPE_TAR (.tar)
 * - PGMONETA_FILE_TYPE_PARTIAL (.partial)
 * @param file_path The file path to check
 * @return Bitmask of file type flags
 */
uint32_t
pgmoneta_get_file_type(char* file_path);

/**
 * Normalize a file type bitmask.
 * This ensures specific compression bits imply PGMONETA_FILE_TYPE_COMPRESSED.
 *
 * @param type The file type bitmask
 * @return Normalized file type bitmask
 */
uint32_t
pgmoneta_normalize_file_type(uint32_t type);

/**
 * Build the compound suffix string for a file type bitmask.
 * For example, TAR | ZSTD | ENCRYPTED produces ".tar.zstd.aes".
 *
 * @param type The file type bitmask (PGMONETA_FILE_TYPE_*)
 * @param suffix The resulting suffix string (caller must free)
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_get_type_suffix(uint32_t type, char** suffix);

/**
 * Build a file-type bitmask from configured compression and encryption values.
 *
 * This helper maps COMPRESSION_* and ENCRYPTION_* settings to the matching
 * PGMONETA_FILE_TYPE_* flags used by extraction helpers.
 *
 * @param compression_type The configured compression type
 * @param encryption_type The configured encryption type
 * @return File-type bitmask for configured suffix layers
 */
uint32_t
pgmoneta_extraction_configured_file_type_mask(int compression_type, int encryption_type);

/**
 * Strip extension suffix from a file name.
 *
 * @param s The source string
 * @param name Resulting string without the last extension (caller must free)
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_strip_extension(char* s, char** name);

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
