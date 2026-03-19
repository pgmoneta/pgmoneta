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

/* pgmoneta */
#include <extraction.h>
#include <logging.h>
#include <pgmoneta.h>
#include <stream.h>
#include <tar.h>
#include <utils.h>
#include <vfile.h>

#include <libgen.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
pgmoneta_extraction_strip_suffix(char* file_path, uint32_t type, char** base_name)
{
   char* current = NULL;
   char* next = NULL;
   uint32_t effective_type = type;

   if (base_name == NULL)
   {
      goto error;
   }

   *base_name = NULL;

   if (file_path == NULL)
   {
      goto error;
   }

   current = pgmoneta_append(current, file_path);
   if (current == NULL)
   {
      goto error;
   }

   if (effective_type == PGMONETA_FILE_TYPE_UNKNOWN)
   {
      effective_type = pgmoneta_get_file_type(current);
   }
   effective_type = pgmoneta_normalize_file_type(effective_type);

   if (effective_type & PGMONETA_FILE_TYPE_ENCRYPTED)
   {
      if (pgmoneta_strip_extension(current, &next))
      {
         goto error;
      }

      free(current);
      current = next;
      next = NULL;
   }

   if (effective_type & PGMONETA_FILE_TYPE_COMPRESSION_MASK)
   {
      if (pgmoneta_strip_extension(current, &next))
      {
         goto error;
      }

      free(current);
      current = next;
      next = NULL;
   }

   if (effective_type & PGMONETA_FILE_TYPE_TAR)
   {
      if (pgmoneta_strip_extension(current, &next))
      {
         goto error;
      }

      free(current);
      current = next;
      next = NULL;
   }

   *base_name = current;
   return 0;

error:
   free(current);
   free(next);
   return 1;
}

int
pgmoneta_extraction_get_suffix(int compression, int encryption, char** suffix)
{
   uint32_t type = PGMONETA_FILE_TYPE_UNKNOWN;

   if (suffix == NULL)
   {
      goto error;
   }

   *suffix = NULL;

   switch (compression)
   {
      case COMPRESSION_CLIENT_GZIP:
      case COMPRESSION_SERVER_GZIP:
         type |= PGMONETA_FILE_TYPE_GZIP;
         break;
      case COMPRESSION_CLIENT_ZSTD:
      case COMPRESSION_SERVER_ZSTD:
         type |= PGMONETA_FILE_TYPE_ZSTD;
         break;
      case COMPRESSION_CLIENT_LZ4:
      case COMPRESSION_SERVER_LZ4:
         type |= PGMONETA_FILE_TYPE_LZ4;
         break;
      case COMPRESSION_CLIENT_BZIP2:
         type |= PGMONETA_FILE_TYPE_BZ2;
         break;
      case COMPRESSION_NONE:
         break;
      default:
         break;
   }

   switch (encryption)
   {
      case ENCRYPTION_AES_256_CBC:
      case ENCRYPTION_AES_192_CBC:
      case ENCRYPTION_AES_128_CBC:
      case ENCRYPTION_AES_256_CTR:
      case ENCRYPTION_AES_192_CTR:
      case ENCRYPTION_AES_128_CTR:
         type |= PGMONETA_FILE_TYPE_ENCRYPTED;
         break;
      case ENCRYPTION_NONE:
         break;
      default:
         break;
   }

   if (type == PGMONETA_FILE_TYPE_UNKNOWN)
   {
      return 0;
   }

   if (pgmoneta_get_type_suffix(type, suffix))
   {
      goto error;
   }

   return 0;

error:
   return 1;
}

/**
 * Map a file type bitmask to the compression enum for the streamer.
 * Derives the compression method from the file suffix, not global config.
 */
static int
bitmask_to_compression(uint32_t file_type)
{
   if (file_type & PGMONETA_FILE_TYPE_GZIP)
   {
      return COMPRESSION_CLIENT_GZIP;
   }
   if (file_type & PGMONETA_FILE_TYPE_ZSTD)
   {
      return COMPRESSION_CLIENT_ZSTD;
   }
   if (file_type & PGMONETA_FILE_TYPE_LZ4)
   {
      return COMPRESSION_CLIENT_LZ4;
   }
   if (file_type & PGMONETA_FILE_TYPE_BZ2)
   {
      return COMPRESSION_CLIENT_BZIP2;
   }
   return COMPRESSION_NONE;
}

/**
 * Determine the encryption type for a file from its bitmask.
 * The .aes suffix doesn't distinguish AES variants, so we fall back to
 * the config's encryption type when the file is encrypted.
 */
static int
bitmask_to_encryption(uint32_t file_type)
{
   struct common_configuration* config = NULL;

   if (file_type & PGMONETA_FILE_TYPE_ENCRYPTED)
   {
      config = (struct common_configuration*)shmem;

      if (config != NULL)
      {
         switch (config->encryption)
         {
            case ENCRYPTION_AES_256_CBC:
            case ENCRYPTION_AES_192_CBC:
            case ENCRYPTION_AES_128_CBC:
            case ENCRYPTION_AES_256_CTR:
            case ENCRYPTION_AES_192_CTR:
            case ENCRYPTION_AES_128_CTR:
               return config->encryption;
            default:
               break;
         }
      }

      return ENCRYPTION_AES_256_CBC;
   }

   return ENCRYPTION_NONE;
}

/**
 * Stream-restore a file: reads src, decrypts+decompresses via streamer(RESTORE),
 * writes the result to dst. All processing happens in memory — no temp files.
 *
 * @param src The source file path (e.g. "file.zstd.aes")
 * @param dst The destination file path (e.g. "file")
 * @param encryption The encryption type (ENCRYPTION_* constant)
 * @param compression The compression type (COMPRESSION_* constant)
 * @return 0 upon success, otherwise 1
 */
static int
stream_restore_file(char* src, char* dst, int encryption, int compression)
{
   struct streamer* strm = NULL;
   struct vfile* reader = NULL;
   struct vfile* writer = NULL;
   bool writer_added = false;
   char buf[BUFFER_SIZE];
   size_t num_read = 0;
   bool last_chunk = false;
   char* dir_path = NULL;

   /* Ensure parent directory exists */
   dir_path = pgmoneta_append(dir_path, dst);
   if (dir_path != NULL)
   {
      char* parent = dirname(dir_path);
      if (pgmoneta_mkdir(parent))
      {
         pgmoneta_log_error("extraction: failed to create parent directory for %s", dst);
         free(dir_path);
         goto error;
      }
      free(dir_path);
      dir_path = NULL;
   }

   if (pgmoneta_vfile_create_local(src, "r", &reader))
   {
      pgmoneta_log_error("extraction: failed to create reader for %s", src);
      goto error;
   }

   if (pgmoneta_streamer_create(STREAMER_MODE_RESTORE, encryption, compression, &strm))
   {
      pgmoneta_log_error("extraction: failed to create restore streamer");
      goto error;
   }

   if (pgmoneta_vfile_create_local(dst, "wb", &writer))
   {
      pgmoneta_log_error("extraction: failed to create writer for %s", dst);
      goto error;
   }

   if (pgmoneta_streamer_add_destination(strm, writer))
   {
      pgmoneta_log_error("extraction: failed to add destination");
      goto error;
   }
   writer_added = true;

   do
   {
      if (reader->read(reader, buf, sizeof(buf), &num_read, &last_chunk))
      {
         pgmoneta_log_error("extraction: failed to read from %s", src);
         goto error;
      }

      if (pgmoneta_streamer_write(strm, buf, num_read, last_chunk))
      {
         pgmoneta_log_error("extraction: failed to stream data");
         goto error;
      }
   }
   while (!last_chunk);

   pgmoneta_vfile_destroy(reader);
   pgmoneta_streamer_destroy(strm);

   return 0;

error:
   pgmoneta_vfile_destroy(reader);
   if (!writer_added)
   {
      pgmoneta_vfile_destroy(writer);
   }
   pgmoneta_streamer_destroy(strm);
   return 1;
}

/**
 * Extract a file to a target path (copy=true path).
 *
 * Uses streamer(RESTORE) for in-memory decryption and decompression.
 * No intermediate files are created.
 *
 * For encrypted and/or compressed files:
 *   Stream-restore (decrypt+decompress) directly to destination.
 *
 * For plain files:
 *   Simple file copy.
 */
static int
extract_file_to_path(char* file_path, uint32_t type, char** destination)
{
   char* dest_name = NULL;
   uint32_t file_type = type;
   int compression = COMPRESSION_NONE;
   int encryption = ENCRYPTION_NONE;

   if (file_path == NULL || destination == NULL || *destination == NULL)
   {
      goto error;
   }

   if (file_type == PGMONETA_FILE_TYPE_UNKNOWN)
   {
      file_type = pgmoneta_get_file_type(file_path);
   }
   file_type = pgmoneta_normalize_file_type(file_type);

   if ((file_type & (PGMONETA_FILE_TYPE_ENCRYPTED | PGMONETA_FILE_TYPE_COMPRESSION_MASK)) == 0)
   {
      /* No encryption or compression — just copy */
      if (pgmoneta_copy_file(file_path, *destination, NULL))
      {
         goto error;
      }
      return 0;
   }

   /* Derive compression and encryption from file bitmask */
   compression = bitmask_to_compression(file_type);
   encryption = bitmask_to_encryption(file_type);

   /* Determine destination name by stripping encryption+compression suffixes */
   if (pgmoneta_extraction_strip_suffix(*destination, file_type & ~PGMONETA_FILE_TYPE_TAR, &dest_name))
   {
      goto error;
   }

   /* Handle .tgz: strip_suffix removes .tgz entirely, re-add .tar */
   if (pgmoneta_ends_with(*destination, ".tgz") || pgmoneta_ends_with(*destination, ".tgz.aes"))
   {
      dest_name = pgmoneta_append(dest_name, ".tar");
   }

   /* Stream-restore: decrypt+decompress in one pass, no temp files */
   if (stream_restore_file(file_path, dest_name, encryption, compression))
   {
      goto error;
   }

   free(*destination);
   *destination = dest_name;

   return 0;

error:
   free(dest_name);
   return 1;
}

/**
 * Extract a tar archive to a directory (copy=false path).
 *
 * Uses streamer(RESTORE) for in-memory decryption and decompression.
 * For encrypted+compressed tar:
 *   1. Stream-restore to a temp tar file (decrypt+decompress in one pass)
 *   2. Untar to destination
 *   3. Clean up temp tar
 */
static int
extract_archive_to_directory(char* file_path, uint32_t type, char* destination)
{
   char* archive_path = NULL;
   bool is_generated_archive = false;
   uint32_t file_type = type;
   uint32_t final_type = 0;
   int compression = COMPRESSION_NONE;
   int encryption = ENCRYPTION_NONE;

   if (file_path == NULL || destination == NULL)
   {
      goto error;
   }

   if (file_type == PGMONETA_FILE_TYPE_UNKNOWN)
   {
      file_type = pgmoneta_get_file_type(file_path);
   }
   file_type = pgmoneta_normalize_file_type(file_type);

   if ((file_type & (PGMONETA_FILE_TYPE_ENCRYPTED | PGMONETA_FILE_TYPE_COMPRESSION_MASK)) != 0)
   {
      /* Derive compression and encryption from file bitmask */
      compression = bitmask_to_compression(file_type);
      encryption = bitmask_to_encryption(file_type);

      /* Determine the archive path by stripping encryption+compression suffixes */
      if (pgmoneta_extraction_strip_suffix(file_path, file_type & ~PGMONETA_FILE_TYPE_TAR, &archive_path))
      {
         goto error;
      }

      /* Handle .tgz: strip_suffix removes .tgz, re-add .tar */
      if (pgmoneta_ends_with(file_path, ".tgz") || pgmoneta_ends_with(file_path, ".tgz.aes"))
      {
         archive_path = pgmoneta_append(archive_path, ".tar");
      }

      is_generated_archive = true;

      /* Stream-restore: decrypt+decompress in one pass to temp tar */
      if (stream_restore_file(file_path, archive_path, encryption, compression))
      {
         goto error;
      }
   }
   else
   {
      archive_path = pgmoneta_append(archive_path, file_path);
      if (archive_path == NULL)
      {
         goto error;
      }
   }

   /* Verify it's a tar and extract */
   final_type = pgmoneta_normalize_file_type(pgmoneta_get_file_type(archive_path));
   if (!(final_type & PGMONETA_FILE_TYPE_TAR))
   {
      pgmoneta_log_error("pgmoneta_extract_file: file is not a TAR archive: %s", file_path);
      goto error;
   }

   if (pgmoneta_untar(archive_path, destination))
   {
      goto error;
   }

   if (is_generated_archive)
   {
      remove(archive_path);
   }
   free(archive_path);

   return 0;

error:
   if (is_generated_archive && archive_path != NULL)
   {
      remove(archive_path);
   }
   free(archive_path);
   return 1;
}

int
pgmoneta_extract_file(char* file_path, uint32_t type, bool copy, char** destination)
{
   if (copy)
   {
      return extract_file_to_path(file_path, type, destination);
   }

   if (destination == NULL || *destination == NULL)
   {
      return 1;
   }

   return extract_archive_to_directory(file_path, type, *destination);
}

int
pgmoneta_extract_backup_file(int server, char* label, char* relative_file_path, char* target_directory, char** target_file)
{
   char* from = NULL;
   char* to = NULL;

   if (target_file == NULL || label == NULL || relative_file_path == NULL)
   {
      goto error;
   }

   *target_file = NULL;

   from = pgmoneta_get_server_backup_identifier_data(server, label);
   if (from == NULL)
   {
      goto error;
   }

   if (!pgmoneta_ends_with(from, "/"))
   {
      from = pgmoneta_append_char(from, '/');
   }
   from = pgmoneta_append(from, relative_file_path);

   if (!pgmoneta_exists(from))
   {
      goto error;
   }

   if (target_directory == NULL || strlen(target_directory) == 0)
   {
      to = pgmoneta_get_server_workspace(server);
      to = pgmoneta_append(to, label);
      to = pgmoneta_append(to, "/");
   }
   else
   {
      to = pgmoneta_append(to, target_directory);
   }

   if (!pgmoneta_ends_with(to, "/"))
   {
      to = pgmoneta_append_char(to, '/');
   }
   to = pgmoneta_append(to, relative_file_path);

   if (pgmoneta_extract_file(from, 0, true, &to))
   {
      goto error;
   }

   *target_file = to;
   free(from);

   return 0;

error:
   free(from);
   free(to);
   return 1;
}
