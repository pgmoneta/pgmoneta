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

typedef int (*compression_func)(char*, char*);

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
   void* in_buf;    /* The input buffer */
   size_t in_size;  /* The input data size */
   size_t in_pos;   /* Current postition the compressor has processed */
   bool last_chunk; /* If current chunk is the last chunk */
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
 * Decompress a file using the appropriate decompression method.
 *
 * This function determines the compression type of the input file by calling
 * `pgmoneta_decompression_file_callback`, and then uses the resulting callback to
 * decompress the file from the `from` path to the `to` path.
 * If no appropriate decompression callback is found, an error is logged.
 *
 * @param from   The source file path, expected to be a compressed file.
 * @param to     The destination file path where the decompressed output will be saved.
 *
 * @return 0 if decompression succeeds, 1 if no matching decompression callback is found or decompression fails.
 */
int
pgmoneta_decompress(char* from, char* to);

#endif //PGMONETA_COMPRESSION_H
