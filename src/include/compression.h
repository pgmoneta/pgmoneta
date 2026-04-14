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

#ifndef PGMONETA_COMPRESSION_H
#define PGMONETA_COMPRESSION_H

#include <pgmoneta.h>
#include <workers.h>

struct workers;

typedef int (*compression_func)(char*, char*);

/** @struct compressor
 * Defines a compressor
 */
struct compressor
{
   /**
    * The compress callback
    * @param compressor The compressor
    * @param out_buf The output buffer
    * @param out_capacity The output buffer capacity
    * @param out_size The output data size
    * @param finished If compressor has finished flushing output for current chunk
    * @return 0 upon success, 1 if otherwise
    */
   int (*compress)(struct compressor* compressor, void* out_buf, size_t out_capacity, size_t* out_size, bool* finished);

   /**
    * The decompress callback
    * @param compressor The compressor
    * @param out_buf The output buffer
    * @param out_capacity The output buffer capacity
    * @param out_size The output data size
    * @param finished If compressor has finished flushing output for current chunk
    * @return 0 upon success, 1 if otherwise
    */
   int (*decompress)(struct compressor* compressor, void* out_buf, size_t out_capacity, size_t* out_size, bool* finished);

   /**
    * Close the compressor
    * @param compressor The compressor
    */
   void (*close)(struct compressor* compressor);
   void* in_buf;    /**< The input buffer */
   size_t in_size;  /**< The input data size */
   size_t in_pos;   /**< Current postition the compressor has processed */
   bool last_chunk; /**< If current chunk is the last chunk */
};

/**
 * Create a compressor according to compression type
 * @param compression_type The compression type
 * @param compressor [out] The compressor
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_compressor_create(int compression_type, struct compressor** compressor);

/**
 * Prepare the compressor with the input buffer to compress or decompress
 * @param compressor The compressor
 * @param in_buffer The input buffer
 * @param in_size The input size
 * @param last_chunk True if the current chunk is the last, false if otherwise
 */
void
pgmoneta_compressor_prepare(struct compressor* compressor, void* in_buffer, size_t in_size, bool last_chunk);

/**
 * Destroy the compressor
 * @param compressor The compressor
 */
void
pgmoneta_compressor_destroy(struct compressor* compressor);

/**
 * Get the algorithm suffix (e.g. ".gz", ".zstd") for a compression type.
 *
 * The returned suffix is owned by the library and must not be freed.
 * On success, *suffix is set to either a non-NULL string or NULL if
 * there is no compression suffix for the given type.
 *
 * @param type The compression type (including side bits)
 * @param suffix [out] Pointer to receive the suffix
 * @return 0 on success, 1 on error
 */
int
pgmoneta_compression_get_suffix(int type, const char** suffix);

/**
 * Clamp a compression level according to the compression algorithm.
 *
 * Updates *level in-place to fall within the supported range for the
 * given compression type. If the algorithm doesn't use a level, the
 * value is left unchanged.
 *
 * @param type The compression type (including side bits)
 * @param level [in/out] The compression level to clamp
 * @return 0 on success, 1 on error
 */
int
pgmoneta_compression_get_level(int type, int* level);

/**
 * Compress a file using the selected compression method.
 * @param from The source file path
 * @param to The destination file path
 * @param type The compression type
 * @param workers Optional worker pool. If NULL, runs synchronously.
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_compress_file(char* from, char* to, int type, struct workers* workers);

/**
 * Compress a directory recursively using the selected compression method.
 * @param directory The directory path
 * @param type The compression type
 * @param workers Optional worker pool. If NULL, runs synchronously.
 * @param excludes Excluded file patterns
 * @param server The server index for progress tracking, or -1 to disable
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_compress_directory(int server, char* directory, int type, struct workers* workers, struct deque* excludes);

/**
 * Decompress a file using the appropriate decompression method.
 * @param from The source file path, expected to be a compressed file
 * @param to The destination file path where the decompressed output will be saved
 * @param type The compression type. If algorithm is
 *             COMPRESSION_ALG_NONE, decompression is auto-detected from file suffix.
 * @param workers Optional worker pool. If NULL, runs synchronously.
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_decompress_file(char* from, char* to, int type, struct workers* workers);

/**
 * Decompress a directory using the appropriate decompression method.
 * @param directory The directory
 * @param type The compression type. If algorithm is
 *             COMPRESSION_ALG_NONE, decompression is auto-detected per file suffix.
 * @param workers Optional worker pool. If NULL, runs synchronously.
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_decompress_directory(char* directory, int type, struct workers* workers, struct deque* excludes);

/**
 * Is the file compressed
 * @param file_path The file path
 * @return True if compressed, otherwise false
 */
bool
pgmoneta_is_compressed(char* file_path);

#endif //PGMONETA_COMPRESSION_H
