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
#include <compression.h>
#include <extraction.h>
#include <logging.h>
#include <pgmoneta.h>
#include <tar.h>
#include <utils.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
extract_layer(char* file_path, uint32_t file_type, char** output_path)
{
   char* extracted_path = NULL;
   uint32_t effective_type = file_type;

   if (output_path == NULL)
   {
      goto error;
   }

   *output_path = NULL;

   if (file_path == NULL)
   {
      goto error;
   }

   if (effective_type == PGMONETA_FILE_TYPE_UNKNOWN)
   {
      effective_type = pgmoneta_get_file_type(file_path);
   }

   if (effective_type & PGMONETA_FILE_TYPE_ENCRYPTED)
   {
      if (pgmoneta_strip_extension(file_path, &extracted_path))
      {
         goto error;
      }

      if (pgmoneta_decrypt_file(file_path, extracted_path))
      {
         goto error;
      }

      *output_path = extracted_path;
      return 0;
   }

   if (effective_type & PGMONETA_FILE_TYPE_COMPRESSED)
   {
      if (pgmoneta_strip_extension(file_path, &extracted_path))
      {
         goto error;
      }

      if (pgmoneta_ends_with(file_path, ".tgz"))
      {
         extracted_path = pgmoneta_append(extracted_path, ".tar");
      }

      if (extracted_path == NULL)
      {
         goto error;
      }

      if (pgmoneta_decompress(file_path, extracted_path))
      {
         goto error;
      }

      *output_path = extracted_path;
      return 0;
   }

error:
   free(extracted_path);
   return 1;
}

static int
extract_layers(char* file_path, uint32_t file_type, char** output_path)
{
   char* current = NULL;
   char* next = NULL;
   uint32_t current_type = file_type;

   if (output_path == NULL)
   {
      goto error;
   }

   *output_path = NULL;

   if (file_path == NULL)
   {
      goto error;
   }

   current = pgmoneta_append(current, file_path);
   if (current == NULL)
   {
      goto error;
   }

   if (current_type == PGMONETA_FILE_TYPE_UNKNOWN)
   {
      current_type = pgmoneta_get_file_type(current);
   }

   while ((current_type & (PGMONETA_FILE_TYPE_ENCRYPTED | PGMONETA_FILE_TYPE_COMPRESSED)) != 0)
   {
      if (extract_layer(current, current_type, &next))
      {
         goto error;
      }

      free(current);
      current = next;
      next = NULL;

      current_type = pgmoneta_get_file_type(current);
   }

   *output_path = current;
   return 0;

error:
   free(current);
   free(next);
   return 1;
}

int
pgmoneta_extract_file(char* file_path, char** destination, uint32_t type, bool copy)
{
   char* archive_path = NULL;
   char* extracted_path = NULL;
   bool is_generated_archive = false;
   uint32_t file_type = type;
   uint32_t final_type = 0;

   if (file_path == NULL || destination == NULL)
   {
      goto error;
   }

   if (file_type == PGMONETA_FILE_TYPE_UNKNOWN)
   {
      file_type = pgmoneta_get_file_type(file_path);
   }

   if (copy)
   {
      if (*destination == NULL)
      {
         goto error;
      }

      if (pgmoneta_copy_file(file_path, *destination, NULL))
      {
         goto error;
      }

      if (extract_layers(*destination, file_type, &extracted_path))
      {
         goto error;
      }

      free(*destination);
      *destination = extracted_path;

      return 0;
   }

   if (extract_layers(file_path, file_type, &archive_path))
   {
      goto error;
   }

   is_generated_archive = strcmp(file_path, archive_path) != 0;

   final_type = pgmoneta_get_file_type(archive_path);
   if (!(final_type & PGMONETA_FILE_TYPE_TAR))
   {
      pgmoneta_log_error("pgmoneta_extract_file: file is not a TAR archive: %s", file_path);
      goto error;
   }

   if (*destination == NULL)
   {
      goto error;
   }

   if (pgmoneta_untar(archive_path, *destination))
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
