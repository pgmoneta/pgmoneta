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

#include <aes.h>
#include <bzip2_compression.h>
#include <compression.h>
#include <extraction.h>
#include <gzip_compression.h>
#include <logging.h>
#include <lz4_compression.h>
#include <utils.h>
#include <workers.h>
#include <zlib.h>
#include <zstandard_compression.h>

#include <dirent.h>
#include <string.h>
#include <sys/types.h>

static int
create_noop_compressor(struct compressor** compressor);

struct compression_operation_task
{
   struct worker_common common;
   int type;
   bool decompress;
   char from[MAX_PATH];
   char to[MAX_PATH];
};

static bool
is_directory_entry(struct dirent* entry, char* path);

static bool
is_regular_entry(struct dirent* entry, char* path);

static int
pgmoneta_decompression_file_callback(char* path, compression_func* decompress_cb);

static int
pgmoneta_decompression_type_callback(int type, compression_func* decompress_cb);

static int
pgmoneta_compression_file_callback(int type, compression_func* compress_cb);

static int
create_compression_operation_task(char* from, char* to, int type, bool decompress,
                                  struct workers* workers, struct compression_operation_task** task);

static void
do_compression_operation(struct worker_common* wc);

static int
dispatch_compression_operation(char* from, char* to, int type, bool decompress, struct workers* workers);

static int
process_directory_operation(char* directory, int type, struct workers* workers, bool decompress);

static int
noop_compress(struct compressor* compressor, void* out_buf, size_t out_capacity, size_t* out_size, bool* finished);

static int
noop_decompress(struct compressor* compressor, void* out_buf, size_t out_capacity, size_t* out_size, bool* finished);

static void
noop_close(struct compressor* compressor);

static int
pgmoneta_decompression_file_callback(char* path, compression_func* decompress_cb)
{
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

static int
pgmoneta_decompression_type_callback(int type, compression_func* decompress_cb)
{
   switch (COMPRESSION_ALGORITHM(type))
   {
      case COMPRESSION_ALG_GZIP:
         *decompress_cb = pgmoneta_gunzip_file;
         break;
      case COMPRESSION_ALG_ZSTD:
         *decompress_cb = pgmoneta_zstandardd_file;
         break;
      case COMPRESSION_ALG_LZ4:
         *decompress_cb = pgmoneta_lz4d_file;
         break;
      case COMPRESSION_ALG_BZIP2:
         *decompress_cb = pgmoneta_bunzip2_file;
         break;
      case COMPRESSION_ALG_NONE:
      default:
         return 1;
   }

   return 0;
}

static int
pgmoneta_compression_file_callback(int type, compression_func* compress_cb)
{
   switch (COMPRESSION_ALGORITHM(type))
   {
      case COMPRESSION_ALG_GZIP:
         *compress_cb = pgmoneta_gzip_file;
         break;
      case COMPRESSION_ALG_ZSTD:
         *compress_cb = pgmoneta_zstandardc_file;
         break;
      case COMPRESSION_ALG_LZ4:
         *compress_cb = pgmoneta_lz4c_file;
         break;
      case COMPRESSION_ALG_BZIP2:
         *compress_cb = pgmoneta_bzip2_file;
         break;
      case COMPRESSION_ALG_NONE:
      default:
         return 1;
   }

   return 0;
}

int
pgmoneta_compress_file(char* from, char* to, int type, struct workers* workers)
{
   compression_func compress_cb = NULL;

   (void)workers;

   if (pgmoneta_compression_file_callback(type, &compress_cb))
   {
      pgmoneta_log_error("pgmoneta_compress: no compression callback found for type %d", COMPRESSION_ALGORITHM(type));
      goto error;
   }

   return compress_cb(from, to);

error:
   return 1;
}

int
pgmoneta_compress_directory(char* directory, int type, struct workers* workers)
{
   return process_directory_operation(directory, type, workers, false);
}

int
pgmoneta_decompress_file(char* from, char* to, int type, struct workers* workers)
{
   compression_func decompress_cb = NULL;

   (void)workers;

   if (COMPRESSION_ALGORITHM(type) == COMPRESSION_ALG_NONE)
   {
      if (pgmoneta_decompression_file_callback(from, &decompress_cb))
      {
         pgmoneta_log_error("pgmoneta_decompress: no decompression callback found for file %s", from);
         goto error;
      }
   }
   else if (pgmoneta_decompression_type_callback(type, &decompress_cb))
   {
      pgmoneta_log_error("pgmoneta_decompress: no decompression callback found for type %d", COMPRESSION_ALGORITHM(type));
      goto error;
   }

   return decompress_cb(from, to);

error:
   return 1;
}

int
pgmoneta_decompress_directory(char* directory, int type, struct workers* workers)
{
   return process_directory_operation(directory, type, workers, true);
}

static bool
is_directory_entry(struct dirent* entry, char* path)
{
   if (entry->d_type == DT_DIR)
   {
      return true;
   }

   if (entry->d_type == DT_LNK || entry->d_type == DT_UNKNOWN)
   {
      return pgmoneta_is_directory(path);
   }

   return false;
}

static bool
is_regular_entry(struct dirent* entry, char* path)
{
   if (entry->d_type == DT_REG)
   {
      return true;
   }

   if (entry->d_type == DT_LNK || entry->d_type == DT_UNKNOWN)
   {
      return pgmoneta_is_file(path);
   }

   return false;
}

static int
create_compression_operation_task(char* from, char* to, int type, bool decompress,
                                  struct workers* workers, struct compression_operation_task** task)
{
   struct compression_operation_task* t = NULL;

   *task = NULL;

   if (from == NULL || to == NULL)
   {
      goto error;
   }

   if (strlen(from) >= MAX_PATH || strlen(to) >= MAX_PATH)
   {
      pgmoneta_log_error("Compression path too long: %s -> %s", from, to);
      goto error;
   }

   t = (struct compression_operation_task*)malloc(sizeof(struct compression_operation_task));
   if (t == NULL)
   {
      goto error;
   }

   memset(t, 0, sizeof(struct compression_operation_task));

   memcpy(t->from, from, strlen(from));
   memcpy(t->to, to, strlen(to));
   t->type = type;
   t->decompress = decompress;
   t->common.workers = workers;

   *task = t;

   return 0;

error:
   free(t);
   return 1;
}

static void
do_compression_operation(struct worker_common* wc)
{
   struct compression_operation_task* task = (struct compression_operation_task*)wc;
   int result;

   if (task->decompress)
   {
      result = pgmoneta_decompress_file(task->from, task->to, task->type, NULL);
   }
   else
   {
      result = pgmoneta_compress_file(task->from, task->to, task->type, NULL);
   }

   if (result != 0 && task->common.workers != NULL)
   {
      task->common.workers->outcome = false;
   }

   free(task);
}

static int
dispatch_compression_operation(char* from, char* to, int type, bool decompress, struct workers* workers)
{
   struct compression_operation_task* task = NULL;

   if (create_compression_operation_task(from, to, type, decompress, workers, &task))
   {
      goto error;
   }

   if (workers != NULL)
   {
      if (workers->outcome)
      {
         if (pgmoneta_workers_add(workers, do_compression_operation, (struct worker_common*)task))
         {
            goto error;
         }
      }
      else
      {
         do_compression_operation((struct worker_common*)task);
      }
   }
   else
   {
      do_compression_operation((struct worker_common*)task);
   }

   return 0;

error:
   free(task);
   return 1;
}

static int
process_directory_operation(char* directory, int type, struct workers* workers, bool decompress)
{
   DIR* dir = NULL;
   struct dirent* entry = NULL;
   char full_path[MAX_PATH];
   const char* suffix = NULL;
   int algorithm = COMPRESSION_ALGORITHM(type);

   if (directory == NULL)
   {
      goto error;
   }

   if (!decompress)
   {
      if (pgmoneta_compression_get_suffix(type, &suffix) || suffix == NULL)
      {
         pgmoneta_log_error("pgmoneta_compress: no suffix found for type %d", algorithm);
         goto error;
      }
   }
   else if (algorithm != COMPRESSION_ALG_NONE)
   {
      if (pgmoneta_compression_get_suffix(type, &suffix) || suffix == NULL)
      {
         pgmoneta_log_error("pgmoneta_decompress: no suffix found for type %d", algorithm);
         goto error;
      }
   }

   if (!(dir = opendir(directory)))
   {
      goto error;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      char* to = NULL;

      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      {
         continue;
      }

      pgmoneta_snprintf(full_path, sizeof(full_path), "%s/%s", directory, entry->d_name);

      if (is_directory_entry(entry, full_path))
      {
         if (process_directory_operation(full_path, type, workers, decompress))
         {
            goto error;
         }

         continue;
      }

      if (!is_regular_entry(entry, full_path))
      {
         continue;
      }

      if (decompress)
      {
         if (algorithm == COMPRESSION_ALG_NONE)
         {
            if (!pgmoneta_is_compressed(full_path))
            {
               continue;
            }
         }
         else if (!pgmoneta_ends_with(full_path, (char*)suffix))
         {
            continue;
         }

         if (pgmoneta_extraction_strip_suffix(full_path, PGMONETA_FILE_TYPE_UNKNOWN, &to))
         {
            goto error;
         }
      }
      else
      {
         if (pgmoneta_ends_with(entry->d_name, "backup_manifest") ||
             pgmoneta_ends_with(entry->d_name, "backup_label"))
         {
            continue;
         }

         if (pgmoneta_is_compressed(full_path) || pgmoneta_is_encrypted(full_path))
         {
            continue;
         }

         to = pgmoneta_append(to, full_path);
         to = pgmoneta_append(to, suffix);
      }

      if (dispatch_compression_operation(full_path, to, type, decompress, workers))
      {
         free(to);
         goto error;
      }

      free(to);
   }

   closedir(dir);

   return 0;

error:
   if (dir != NULL)
   {
      closedir(dir);
   }

   return 1;
}

int
pgmoneta_compressor_create(int compression_type, struct compressor** compressor)
{
   *compressor = NULL;
   switch (COMPRESSION_ALGORITHM(compression_type))
   {
      case COMPRESSION_ALG_ZSTD:
         return pgmoneta_zstd_compressor_create(compressor);
      case COMPRESSION_ALG_LZ4:
         return pgmoneta_lz4_compressor_create(compressor);
      case COMPRESSION_ALG_BZIP2:
         return pgmoneta_bzip2_compressor_create(compressor);
      case COMPRESSION_ALG_GZIP:
         return pgmoneta_gzip_compressor_create(compressor);
      case COMPRESSION_ALG_NONE:
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

bool
pgmoneta_is_compressed(char* file_path)
{
   if (pgmoneta_ends_with(file_path, ".zstd") ||
       pgmoneta_ends_with(file_path, ".lz4") ||
       pgmoneta_ends_with(file_path, ".bz2") ||
       pgmoneta_ends_with(file_path, ".gz"))
   {
      return true;
   }

   return false;
}

int
pgmoneta_compression_get_suffix(int type, const char** suffix)
{
   if (suffix == NULL)
   {
      return 1;
   }

   *suffix = NULL;

   switch (COMPRESSION_ALGORITHM(type))
   {
      case COMPRESSION_ALG_GZIP:
         *suffix = ".gz";
         break;
      case COMPRESSION_ALG_ZSTD:
         *suffix = ".zstd";
         break;
      case COMPRESSION_ALG_LZ4:
         *suffix = ".lz4";
         break;
      case COMPRESSION_ALG_BZIP2:
         *suffix = ".bz2";
         break;
      case COMPRESSION_ALG_NONE:
      default:
         break;
   }

   return 0;
}

int
pgmoneta_compression_get_level(int type, int* level)
{
   if (level == NULL)
   {
      return 1;
   }

   switch (COMPRESSION_ALGORITHM(type))
   {
      case COMPRESSION_ALG_GZIP:
         if (*level < 1)
         {
            *level = 1;
         }
         else if (*level > 9)
         {
            *level = 9;
         }
         break;
      case COMPRESSION_ALG_ZSTD:
         if (*level < -131072)
         {
            *level = -131072;
         }
         else if (*level > 22)
         {
            *level = 22;
         }
         break;
      case COMPRESSION_ALG_LZ4:
         if (*level < 1)
         {
            *level = 1;
         }
         else if (*level > 12)
         {
            *level = 12;
         }
         break;
      case COMPRESSION_ALG_BZIP2:
         if (*level < 1)
         {
            *level = 1;
         }
         else if (*level > 9)
         {
            *level = 9;
         }
         break;
      case COMPRESSION_ALG_NONE:
      default:
         break;
   }

   return 0;
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
