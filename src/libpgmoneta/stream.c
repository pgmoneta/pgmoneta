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

#include <pgmoneta.h>
#include <aes.h>
#include <compression.h>
#include <deque.h>
#include <logging.h>
#include <stream.h>
#include <utils.h>
#include <value.h>

#include <stdlib.h>

static int noop_stream_cb(struct streamer* this, bool last_chunk);
static int backup_stream_cb(struct streamer* this, bool last_chunk);
static int restore_stream_cb(struct streamer* this, bool last_chunk);
static int get_backup_file_name_cb(struct streamer* this, char* file_name, char** dest_file_name);
static int get_restore_file_name_cb(struct streamer* this, char* file_name, char** dest_file_name);
static void vfile_destroy_cb(uintptr_t val);

int
pgmoneta_streamer_create(int mode, int encryption, int compression, struct streamer** streamer)
{
   struct streamer* s = NULL;
   s = malloc(sizeof(struct streamer));
   memset(s, 0, sizeof(struct streamer));
   s->capacity = sizeof(s->buffer);
   s->compression = compression;
   s->encryption = encryption;

   if (encryption == ENCRYPTION_NONE && compression == COMPRESSION_NONE)
   {
      // fall back to NONE to avoid unnecessary overhead
      mode = STREAMER_MODE_NONE;
   }

   switch (mode)
   {
      case STREAMER_MODE_NONE:
         s->stream_cb = noop_stream_cb;
         s->get_dest_file_name = get_backup_file_name_cb;
         break;
      case STREAMER_MODE_BACKUP:
         s->stream_cb = backup_stream_cb;
         s->get_dest_file_name = get_backup_file_name_cb;
         break;
      case STREAMER_MODE_RESTORE:
         s->stream_cb = restore_stream_cb;
         s->get_dest_file_name = get_restore_file_name_cb;
         break;
      default:
         goto error;
   }

   if (mode != STREAMER_MODE_NONE)
   {
      if (pgmoneta_encryptor_create(encryption, &s->encryptor))
      {
         goto error;
      }

      if (pgmoneta_compressor_create(compression, &s->compressor))
      {
         goto error;
      }
   }

   *streamer = s;
   return 0;

error:
   pgmoneta_streamer_destroy(s);
   return 1;
}

void
pgmoneta_streamer_destroy(struct streamer* streamer)
{
   if (streamer == NULL)
   {
      return;
   }
   pgmoneta_compressor_destroy(streamer->compressor);
   pgmoneta_encryptor_destroy(streamer->encryptor);
   pgmoneta_deque_destroy(streamer->destinations);
   free(streamer);
}

int
pgmoneta_streamer_write(struct streamer* streamer, void* buffer, size_t size, bool last_chunk)
{
   size_t bytes_to_write = 0;
   size_t offset = 0;
   bool last_chk = false;
   if (streamer == NULL || streamer->destinations == NULL)
   {
      goto error;
   }

   do
   {
      bytes_to_write = MIN(size, streamer->capacity - streamer->size);
      memcpy(streamer->buffer + streamer->size, buffer + offset, bytes_to_write);
      streamer->size += bytes_to_write;
      size -= bytes_to_write;
      offset += bytes_to_write;
      last_chk = last_chunk && size == 0;
      if (streamer->size == streamer->capacity || last_chk)
      {
         // process and send data when the buffer is full or we have no more data to send
         if (streamer->stream_cb(streamer, last_chk))
         {
            //TODO: need to log what has failed at least
            goto error;
         }
         // update bytes written on success
         streamer->written += streamer->size;
         streamer->size = 0;
      }
   }
   while (size > 0);

   return 0;
error:
   return 1;
}

int
pgmoneta_streamer_add_destination(struct streamer* streamer, struct vfile* file)
{
   if (file == NULL)
   {
      goto error;
   }
   if (streamer->destinations == NULL)
   {
      pgmoneta_deque_create(false, &streamer->destinations);
   }

   struct value_config config = {.destroy_data = vfile_destroy_cb, .to_string = NULL};

   pgmoneta_deque_add_with_config(streamer->destinations, NULL, (uintptr_t)file, &config);

   return 0;

error:
   return 1;
}

void
pgmoneta_streamer_reset(struct streamer* streamer)
{
   if (streamer == NULL)
   {
      return;
   }
   pgmoneta_compressor_destroy(streamer->compressor);
   pgmoneta_encryptor_destroy(streamer->encryptor);

   pgmoneta_compressor_create(streamer->compression, &streamer->compressor);
   pgmoneta_encryptor_create(streamer->encryption, &streamer->encryptor);

   pgmoneta_deque_clear(streamer->destinations);
   streamer->size = 0;
   streamer->written = 0;
}

static int
noop_stream_cb(struct streamer* this, bool last_chunk)
{
   struct deque_iterator* vfile_iter = NULL;
   struct vfile* f = NULL;
   (void)last_chunk;
   if (this == NULL || this->destinations == NULL)
   {
      pgmoneta_log_error("This streamer is not initialized");
      goto error;
   }
   pgmoneta_deque_iterator_create(this->destinations, &vfile_iter);
   while (pgmoneta_deque_iterator_next(vfile_iter))
   {
      f = (struct vfile*)pgmoneta_value_data(vfile_iter->value);
      if (f->write(f, this->buffer, this->size, last_chunk))
      {
         pgmoneta_log_error("Failed to write buffer");
      }
   }
   pgmoneta_deque_iterator_destroy(vfile_iter);
   return 0;
error:
   pgmoneta_deque_iterator_destroy(vfile_iter);
   return 1;
}

static int
backup_stream_cb(struct streamer* this, bool last_chunk)
{
   char cbuf[BUFFER_SIZE];
   void* ebuf = NULL;
   size_t ebuf_size;
   size_t cbuf_size = 0;
   bool finished = false;
   struct deque_iterator* vfile_iter = NULL;
   struct vfile* f = NULL;

   if (this == NULL || this->compressor == NULL || this->encryptor == NULL || this->destinations == NULL)
   {
      pgmoneta_log_error("This streamer is not initialized");
      goto error;
   }

   pgmoneta_compressor_prepare(this->compressor, this->buffer, this->size, last_chunk);
   while (!finished)
   {
      if (this->compressor->compress(this->compressor, cbuf, sizeof(cbuf), &cbuf_size, &finished))
      {
         goto error;
      }
      // Chances are we may receive no output at all this round before we provide more input
      // in that case skip the following steps
      if (cbuf_size == 0)
      {
         continue;
      }
      if (this->encryptor->encrypt(this->encryptor, cbuf, cbuf_size, finished && last_chunk, &ebuf, &ebuf_size))
      {
         goto error;
      }
      //write ebuf to file
      pgmoneta_deque_iterator_create(this->destinations, &vfile_iter);
      while (pgmoneta_deque_iterator_next(vfile_iter))
      {
         f = (struct vfile*)pgmoneta_value_data(vfile_iter->value);
         if (f->write(f, ebuf, ebuf_size, last_chunk))
         {
            pgmoneta_log_error("Failed to write buffer");
         }
      }
      pgmoneta_deque_iterator_destroy(vfile_iter);
      vfile_iter = NULL;

      free(ebuf);
      ebuf = NULL;
      ebuf_size = 0;
   }

   pgmoneta_deque_iterator_destroy(vfile_iter);
   free(ebuf);
   return 0;

error:
   pgmoneta_deque_iterator_destroy(vfile_iter);
   free(ebuf);
   return 1;
}

static int
restore_stream_cb(struct streamer* this, bool last_chunk)
{
   char cbuf[BUFFER_SIZE];
   void* ebuf = NULL;
   size_t ebuf_size;
   size_t cbuf_size = 0;
   bool finished = false;
   struct deque_iterator* vfile_iter = NULL;
   struct vfile* f = NULL;

   if (this == NULL || this->compressor == NULL || this->encryptor == NULL || this->destinations == NULL)
   {
      pgmoneta_log_error("This streamer is not initialized");
      goto error;
   }

   if (this->encryptor->decrypt(this->encryptor, this->buffer, this->size, last_chunk, &ebuf, &ebuf_size))
   {
      goto error;
   }

   pgmoneta_compressor_prepare(this->compressor, ebuf, ebuf_size, last_chunk);
   while (!finished)
   {
      if (this->compressor->decompress(this->compressor, cbuf, sizeof(cbuf), &cbuf_size, &finished))
      {
         goto error;
      }

      // Chances are we may receive no output at all this round before we provide more input
      // in that case skip the following steps
      if (cbuf_size == 0)
      {
         continue;
      }

      pgmoneta_deque_iterator_create(this->destinations, &vfile_iter);
      while (pgmoneta_deque_iterator_next(vfile_iter))
      {
         f = (struct vfile*)pgmoneta_value_data(vfile_iter->value);
         if (f->write(f, cbuf, cbuf_size, last_chunk))
         {
            pgmoneta_log_error("Failed to write buffer");
         }
      }
      pgmoneta_deque_iterator_destroy(vfile_iter);
      vfile_iter = NULL;
   }

   pgmoneta_deque_iterator_destroy(vfile_iter);
   free(ebuf);
   return 0;

error:
   pgmoneta_deque_iterator_destroy(vfile_iter);
   free(ebuf);
   return 1;
}

static void
vfile_destroy_cb(uintptr_t val)
{
   struct vfile* file = (struct vfile*)val;
   pgmoneta_vfile_destroy(file);
}

static int
get_backup_file_name_cb(struct streamer* this, char* file_name, char** dest_file_name)
{
   char* dest = NULL;
   if (this == NULL || file_name == NULL)
   {
      goto error;
   }
   dest = pgmoneta_append(dest, file_name);
   switch (this->compression)
   {
      case COMPRESSION_CLIENT_ZSTD:
      case COMPRESSION_SERVER_ZSTD:
         dest = pgmoneta_append(dest, ".zstd");
         break;
      case COMPRESSION_CLIENT_GZIP:
      case COMPRESSION_SERVER_GZIP:
         dest = pgmoneta_append(dest, ".gz");
         break;
      case COMPRESSION_SERVER_LZ4:
      case COMPRESSION_CLIENT_LZ4:
         dest = pgmoneta_append(dest, ".lz4");
         break;
      case COMPRESSION_CLIENT_BZIP2:
         dest = pgmoneta_append(dest, ".bz2");
         break;
      default:
         break;
   }
   if (this->encryption != ENCRYPTION_NONE)
   {
      dest = pgmoneta_append(dest, ".aes");
   }
   *dest_file_name = dest;
   return 0;
error:
   free(dest);
   return 1;
}

static int
get_restore_file_name_cb(struct streamer* this, char* file_name, char** dest_file_name)
{
   char* dest = NULL;
   char* current = NULL;
   int file_type = PGMONETA_FILE_TYPE_UNKNOWN;
   if (this == NULL || file_name == NULL)
   {
      goto error;
   }
   dest = pgmoneta_append(dest, file_name);
   file_type = pgmoneta_get_file_type(file_name);
   if (file_type & PGMONETA_FILE_TYPE_ENCRYPTED)
   {
      if (pgmoneta_strip_extension(dest, &current))
      {
         goto error;
      }
      free(dest);
      dest = current;
      current = NULL;
   }

   if (file_type & PGMONETA_FILE_TYPE_COMPRESSED)
   {
      if (pgmoneta_strip_extension(dest, &current))
      {
         goto error;
      }
      free(dest);
      dest = current;
      current = NULL;
   }
   *dest_file_name = dest;
   return 0;
error:
   free(dest);
   free(current);
   return 1;
}