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
#include <files.h>
#include <info.h>
#include <logging.h>
#include <pgmoneta.h>
#include <rfile.h>
#include <utils.h>

/* system */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int file_final_name(char* file, int encryption, int compression, char** finalname);

static int
file_final_name(char* file, int encryption, int compression, char** finalname)
{
   char* final = NULL;

   *finalname = NULL;
   if (file == NULL)
   {
      goto error;
   }

   final = pgmoneta_append(final, file);
   {
      char* suffix = NULL;

      if (pgmoneta_extraction_get_suffix(compression, encryption, &suffix))
      {
         goto error;
      }
      if (suffix != NULL)
      {
         final = pgmoneta_append(final, suffix);
      }
      free(suffix);
   }

   *finalname = final;
   return 0;

error:
   free(final);
   return 1;
}

int
pgmoneta_rfile_create(int server, char* label, char* relative_dir, char* base_file_name, int encryption, int compression, struct rfile** rfile)
{
   struct rfile* rf = NULL;
   char* extracted_file_path = NULL;
   char* final_relative_path = NULL;
   char base_relative_path[MAX_PATH];
   FILE* fp = NULL;

   memset(base_relative_path, 0, MAX_PATH);
   if (pgmoneta_ends_with(relative_dir, "/"))
   {
      pgmoneta_snprintf(base_relative_path, MAX_PATH, "%s%s", relative_dir, base_file_name);
   }
   else
   {
      pgmoneta_snprintf(base_relative_path, MAX_PATH, "%s/%s", relative_dir, base_file_name);
   }

   /* try both base and final relative path */
   if (pgmoneta_extract_backup_file(server, label, base_relative_path, NULL, &extracted_file_path))
   {
      free(extracted_file_path);
      extracted_file_path = NULL;
      file_final_name(base_relative_path, encryption, compression, &final_relative_path);
      if (pgmoneta_extract_backup_file(server, label, final_relative_path, NULL, &extracted_file_path))
      {
         goto error;
      }
   }
   fp = fopen(extracted_file_path, "r");

   if (fp == NULL)
   {
      goto error;
   }
   rf = (struct rfile*)malloc(sizeof(struct rfile));
   memset(rf, 0, sizeof(struct rfile));

   rf->fp = fp;
   rf->filepath = extracted_file_path;
   *rfile = rf;

   free(final_relative_path);
   return 0;

error:
   free(extracted_file_path);
   free(final_relative_path);
   pgmoneta_rfile_destroy(rf);
   return 1;
}

void
pgmoneta_rfile_destroy(struct rfile* rf)
{
   if (rf == NULL)
   {
      return;
   }
   if (rf->fp != NULL)
   {
      fclose(rf->fp);
   }
   if (rf->filepath != NULL)
   {
      pgmoneta_delete_file(rf->filepath, NULL);
   }

   free(rf->filepath);
   free(rf->relative_block_numbers);
   free(rf);
}

int
pgmoneta_incremental_rfile_initialize(int server, char* label, char* relative_dir, char* base_file_name, int encryption, int compression, struct rfile** rfile)
{
   uint32_t magic = 0;
   uint32_t nread = 0;
   struct rfile* rf = NULL;
   struct main_configuration* config;
   size_t relsegsz = 0;
   size_t blocksz = 0;

   config = (struct main_configuration*)shmem;

   relsegsz = config->common.servers[server].relseg_size;
   blocksz = config->common.servers[server].block_size;

   if (pgmoneta_rfile_create(server, label, relative_dir, base_file_name, encryption, compression, &rf))
   {
      pgmoneta_log_error("rfile initialize: failed to open incremental backup (label %s) file at %s/%s", label, relative_dir, base_file_name);
      goto error;
   }

   nread = fread(&magic, 1, sizeof(uint32_t), rf->fp);
   if (nread != sizeof(uint32_t))
   {
      pgmoneta_log_error("rfile initialize: incomplete file header at %s, cannot read magic number", rf->filepath);
      goto error;
   }

   if (magic != INCREMENTAL_MAGIC)
   {
      pgmoneta_log_error("rfile initialize: incorrect magic number, getting %X, expecting %X", magic, INCREMENTAL_MAGIC);
      goto error;
   }

   nread = fread(&rf->num_blocks, 1, sizeof(uint32_t), rf->fp);
   if (nread != sizeof(uint32_t))
   {
      pgmoneta_log_error("rfile initialize: incomplete file header at %s%s, cannot read block count", relative_dir, base_file_name);
      goto error;
   }
   if (rf->num_blocks > relsegsz)
   {
      pgmoneta_log_error("rfile initialize: file has %d blocks which is more than server's segment size", rf->num_blocks);
      goto error;
   }

   nread = fread(&rf->truncation_block_length, 1, sizeof(uint32_t), rf->fp);
   if (nread != sizeof(uint32_t))
   {
      pgmoneta_log_error("rfile initialize: incomplete file header at %s%s, cannot read truncation block length", relative_dir, base_file_name);
      goto error;
   }
   if (rf->truncation_block_length > relsegsz)
   {
      pgmoneta_log_error("rfile initialize: file has truncation block length of %d which is more than server's segment size", rf->truncation_block_length);
      goto error;
   }

   if (rf->num_blocks > 0)
   {
      rf->relative_block_numbers = malloc(sizeof(uint32_t) * rf->num_blocks);
      nread = fread(rf->relative_block_numbers, sizeof(uint32_t), rf->num_blocks, rf->fp);
      if (nread != rf->num_blocks)
      {
         pgmoneta_log_error("rfile initialize: incomplete file header at %s, cannot read relative block numbers", rf->filepath);
         goto error;
      }
   }

   rf->header_length = sizeof(uint32_t) * (1 + 1 + 1 + rf->num_blocks);
   if (rf->num_blocks > 0 && rf->header_length % blocksz != 0)
   {
      rf->header_length += (blocksz - (rf->header_length % blocksz));
   }

   *rfile = rf;
   return 0;
error:
   pgmoneta_rfile_destroy(rf);
   return 1;
}
