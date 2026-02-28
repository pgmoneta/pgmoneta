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

#ifndef PGMONETA_BUFFER_H
#define PGMONETA_BUFFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <aes.h>
#include <compression.h>
#include <deque.h>
#include <vfile.h>

#define BUFFER_SIZE           1024 * 1024
#define STREAMER_MODE_NONE    0
#define STREAMER_MODE_BACKUP  1
#define STREAMER_MODE_RESTORE 2

struct streamer
{
   struct compressor* compressor; /* The compressor */
   struct encryptor* encryptor;   /* The encryptor */
   struct deque* destinations;    /* The streaming destinations */
   char buffer[BUFFER_SIZE];      /* The internal buffer */
   size_t size;                   /* The buffer data size */
   size_t capacity;               /* The buffer capacity */
   size_t written;                /* Total data streamed */
   int compression;               /* The compression mode */
   int encryption;                /* The encryption mode */
   /**
    * The stream callback, this processes the input and streams to destination
    * @param streamer The streamer
    * @param last_chunk If current chunk is the last
    * @return 0 upon success, 1 if otherwise
    */
   int (*stream_cb)(struct streamer* streamer, bool last_chunk);
   /**
    * The callback to help generate resulting filename by adding or stripping suffixes
    * @param streamer The streamer
    * @param file_name The file name
    * @param dest_file_name [out] The destination file name
    * @return 0 upon success, 1 if otherwise
    */
   int (*get_dest_file_name)(struct streamer* streamer, char* file_name, char** dest_file_name);
};

/**
 * Create the streamer
 * @param mode The streamer mode, mode BACKUP compress and encrypt the data, mode RESTORE decrypt and decompress the data
 * @param encryption The encryption mode
 * @param compression The compression mode
 * @param streamer [out] The streamer
 * @return 0 upon success, 1 if otherwise
 */
int
pgmoneta_streamer_create(int mode, int encryption, int compression, struct streamer** streamer);

/**
 * Destroy the streamer, this also destroy all the added destinations
 * @param streamer The streamer
 */
void
pgmoneta_streamer_destroy(struct streamer* streamer);

/**
 * Use streamer to write data to destinations
 * @param streamer The streamer
 * @param buffer The input buffer
 * @param size The input data size
 * @param last_chunk If current chunk is the last
 * @return 0 upon success, 1 if otherwise
 */
int
pgmoneta_streamer_write(struct streamer* streamer, void* buffer, size_t size, bool last_chunk);

/**
 * Add a destination to streamer
 * @param streamer The streamer
 * @param file The destination
 * @return 0 upon success, 1 if otherwise
 */
int
pgmoneta_streamer_add_destination(struct streamer* streamer, struct vfile* file);

/**
 * Reset the streamer, remove all the destinations
 * @param streamer The streamer
 */
void
pgmoneta_streamer_reset(struct streamer* streamer);

#ifdef __cplusplus
}
#endif

#endif