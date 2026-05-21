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
#include <compression.h>
#include <files.h>
#include <pgmoneta.h>
#include <utils.h>

/* system */
#include <libgen.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t normalize_file_type(uint32_t type);

static uint32_t
normalize_file_type(uint32_t type)
{
   uint32_t normalized = type;

   if ((normalized & (PGMONETA_FILE_TYPE_GZIP |
                      PGMONETA_FILE_TYPE_LZ4 |
                      PGMONETA_FILE_TYPE_ZSTD |
                      PGMONETA_FILE_TYPE_BZ2)) != 0)
   {
      normalized |= PGMONETA_FILE_TYPE_COMPRESSED;
   }

   return normalized;
}

uint32_t
pgmoneta_extraction_get_file_type(char* file_path)
{
   uint32_t type = PGMONETA_FILE_TYPE_UNKNOWN;
   char* file_path_copy = NULL;
   char* basename_copy = NULL;
   char* current = NULL;
   char* dot = NULL;

   if (file_path == NULL)
   {
      return type;
   }

   file_path_copy = pgmoneta_append(file_path_copy, file_path);
   if (file_path_copy == NULL)
   {
      return type;
   }

   basename_copy = pgmoneta_append(basename_copy, basename(file_path_copy));
   free(file_path_copy);
   file_path_copy = NULL;
   if (basename_copy == NULL)
   {
      return type;
   }

   current = basename_copy;

   /* Check for encryption suffix first (.aes) */
   if (pgmoneta_ends_with(current, ".aes"))
   {
      type |= PGMONETA_FILE_TYPE_ENCRYPTED;
      current[strlen(current) - 4] = '\0';
   }

   /* Check for compression suffixes - set both generic and specific flags */
   if (pgmoneta_ends_with(current, ".gz"))
   {
      type |= PGMONETA_FILE_TYPE_COMPRESSED | PGMONETA_FILE_TYPE_GZIP;
      dot = strrchr(current, '.');
      if (dot != NULL)
      {
         *dot = '\0';
      }
   }
   else if (pgmoneta_ends_with(current, ".lz4"))
   {
      type |= PGMONETA_FILE_TYPE_COMPRESSED | PGMONETA_FILE_TYPE_LZ4;
      dot = strrchr(current, '.');
      if (dot != NULL)
      {
         *dot = '\0';
      }
   }
   else if (pgmoneta_ends_with(current, ".zstd"))
   {
      type |= PGMONETA_FILE_TYPE_COMPRESSED | PGMONETA_FILE_TYPE_ZSTD;
      dot = strrchr(current, '.');
      if (dot != NULL)
      {
         *dot = '\0';
      }
   }
   else if (pgmoneta_ends_with(current, ".bz2"))
   {
      type |= PGMONETA_FILE_TYPE_COMPRESSED | PGMONETA_FILE_TYPE_BZ2;
      dot = strrchr(current, '.');
      if (dot != NULL)
      {
         *dot = '\0';
      }
   }

   /* Check for TAR archive after stripping compression */
   if (pgmoneta_ends_with(current, ".tar"))
   {
      type |= PGMONETA_FILE_TYPE_TAR;
      current[strlen(current) - 4] = '\0';
   }

   /* Check for .tgz (tar.gz shorthand) */
   if (pgmoneta_ends_with(current, ".tgz"))
   {
      type |= PGMONETA_FILE_TYPE_TAR;
      type |= PGMONETA_FILE_TYPE_COMPRESSED | PGMONETA_FILE_TYPE_GZIP;
      current[strlen(current) - 4] = '\0';
   }

   /* Check for partial suffix */
   if (pgmoneta_ends_with(current, ".partial"))
   {
      type |= PGMONETA_FILE_TYPE_PARTIAL;
      current[strlen(current) - 8] = '\0';
   }

   /* Check for WAL file pattern (24-char hex) */
   if (strlen(current) == 24 && pgmoneta_is_wal_file(current))
   {
      type |= PGMONETA_FILE_TYPE_WAL;
   }

   free(basename_copy);

   return type;
}

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
      effective_type = pgmoneta_extraction_get_file_type(current);
   }
   effective_type = normalize_file_type(effective_type);

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
   const char* compression_suffix = NULL;
   char* result = NULL;

   if (suffix == NULL)
   {
      goto error;
   }

   *suffix = NULL;

   if (pgmoneta_compression_get_suffix(compression, &compression_suffix))
   {
      goto error;
   }

   if (compression_suffix != NULL)
   {
      result = pgmoneta_append(result, compression_suffix);
   }

   switch (encryption)
   {
      case ENCRYPTION_AES_256_GCM:
      case ENCRYPTION_AES_192_GCM:
      case ENCRYPTION_AES_128_GCM:
         result = pgmoneta_append(result, ".aes");
         break;
      case ENCRYPTION_NONE:
         break;
      default:
         break;
   }

   *suffix = result;
   return 0;

error:
   free(result);
   return 1;
}
