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

#include <bzip2_compression.h>
#include <compression.h>
#include <gzip_compression.h>
#include <logging.h>
#include <lz4_compression.h>
#include <utils.h>
#include <zlib.h>
#include <zstandard_compression.h>

static int
create_noop_compressor(struct compressor** compressor);

static int
noop_compress(struct compressor* compressor, void* out_buf, size_t out_capacity, size_t* out_size, bool* finished);

static int
noop_decompress(struct compressor* compressor, void* out_buf, size_t out_capacity, size_t* out_size, bool* finished);

static void
noop_close(struct compressor* compressor);

static int
pgmoneta_decompression_file_callback(char* path, compression_func* decompress_cb)
{
   // Determine the compression method based on file extension
   if (pgmoneta_ends_with(path, ".gz"))
   {
      *decompress_cb = pgmoneta_gunzip_file;
   }
   else if (pgmoneta_ends_with(path, ".zstd"))
   {
      *decompress_cb = pgmoneta_zstandardd_file;
   }
   else if (pgmoneta_ends_with(path, ".lz4"))
   {
      *decompress_cb = pgmoneta_lz4d_file;
   }
   else if (pgmoneta_ends_with(path, ".bz2"))
   {
      *decompress_cb = pgmoneta_bunzip2_file;
   }
   else
   {
      return 1;
   }
   return 0;
}

int
pgmoneta_decompress(char* from, char* to)
{
   compression_func decompress_cb = NULL;
   if (pgmoneta_decompression_file_callback(from, &decompress_cb))
   {
      pgmoneta_log_error("pgmoneta_decompress: no decompression callback found for file %s", from);
      goto error;
   }
   return decompress_cb(from, to);
error:
   return 1;
}

int
pgmoneta_compressor_create(int compression_type, struct compressor** compressor)
{
   *compressor = NULL;
   switch (compression_type)
   {
      case COMPRESSION_CLIENT_ZSTD:
      case COMPRESSION_SERVER_ZSTD:
         return pgmoneta_zstd_compressor_create(compressor);
      case COMPRESSION_CLIENT_LZ4:
      case COMPRESSION_SERVER_LZ4:
         return pgmoneta_lz4_compressor_create(compressor);
      case COMPRESSION_CLIENT_BZIP2:
         return pgmoneta_bzip2_compressor_create(compressor);
      case COMPRESSION_CLIENT_GZIP:
      case COMPRESSION_SERVER_GZIP:
         return pgmoneta_gzip_compressor_create(compressor);
      default:
         return create_noop_compressor(compressor);
   }
}

void
pgmoneta_compressor_prepare(struct compressor* compressor, void* in_buffer, size_t in_size, bool last_chunk)
{
   if (compressor == NULL)
   {
      return;
   }
   compressor->in_buf = in_buffer;
   compressor->in_size = in_size;
   compressor->in_pos = 0;
   compressor->last_chunk = last_chunk;
}

int
pgmoneta_compressor_compress(struct compressor* compressor, void* out_buf, size_t out_capacity, size_t* out_size, bool* finished)
{
   if (compressor == NULL || compressor->in_buf == NULL)
   {
      goto error;
   }

   if (compressor->compress(compressor, out_buf, out_capacity, out_size, finished))
   {
      goto error;
   }

   return 0;

error:
   return 1;
}

int
pgmoneta_compressor_decompress(struct compressor* compressor, void* out_buf, size_t out_capacity, size_t* out_size, bool* finished)
{
   if (compressor == NULL || compressor->in_buf == NULL)
   {
      goto error;
   }

   if (compressor->decompress(compressor, out_buf, out_capacity, out_size, finished))
   {
      goto error;
   }

   return 0;

error:
   return 1;
}

void
pgmoneta_compressor_destroy(struct compressor* compressor)
{
   if (compressor == NULL)
   {
      return;
   }
   compressor->close(compressor);
   free(compressor);
}

static int
noop_compress(struct compressor* compressor, void* out_buf, size_t out_capacity, size_t* out_size, bool* finished)
{
   size_t bytes_to_write = 0;

   if (compressor == NULL || compressor->in_buf == NULL)
   {
      goto error;
   }

   bytes_to_write = MIN(compressor->in_size - compressor->in_pos, out_capacity);
   memcpy(out_buf, compressor->in_buf + compressor->in_pos, bytes_to_write);

   *out_size = bytes_to_write;
   compressor->in_pos += bytes_to_write;
   *finished = compressor->in_pos == compressor->in_size;
   return 0;

error:
   return 1;
}

static int
noop_decompress(struct compressor* compressor, void* out_buf, size_t out_capacity, size_t* out_size, bool* finished)
{
   size_t bytes_to_write = 0;

   if (compressor == NULL || compressor->in_buf == NULL)
   {
      goto error;
   }

   bytes_to_write = MIN(compressor->in_size - compressor->in_pos, out_capacity);
   memcpy(out_buf, compressor->in_buf + compressor->in_pos, bytes_to_write);

   *out_size = bytes_to_write;
   compressor->in_pos += bytes_to_write;
   *finished = compressor->in_pos == compressor->in_size;
   return 0;

error:
   return 1;
}

static void
noop_close(struct compressor* compressor)
{
   compressor->in_buf = NULL;
   compressor->in_pos = 0;
   compressor->in_size = 0;
}

static int
create_noop_compressor(struct compressor** compressor)
{
   struct compressor* c = NULL;
   c = malloc(sizeof(struct compressor));
   memset(c, 0, sizeof(struct compressor));

   c->close = noop_close;
   c->compress = noop_compress;
   c->decompress = noop_decompress;
   *compressor = c;

   return 0;
}