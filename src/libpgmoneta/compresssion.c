/*
 * Copyright (C) 2025 The pgmoneta community
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
#include <zstandard_compression.h>

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
