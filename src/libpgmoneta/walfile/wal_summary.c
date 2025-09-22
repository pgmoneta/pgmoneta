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

#include <pgmoneta.h>
#include <brt.h>
#include <logging.h>
#include <utils.h>
#include <wal.h>
#include <walfile.h>
#include <walfile/wal_summary.h>

#include <dirent.h>
#include <libgen.h>

static char* summary_file_name(uint64_t s_lsn, uint64_t e_lsn);

/**
 * Handles summarization of arbitary range in a timeline only
 * (It is the job of upper layers to keep the timeline intact)
 *
 * Assumptions this function makes:
 * - The WAL records from start_lsn to end_lsn is present in WAL archive
 * - wal_level is greater than WAL_LEVEL_MINIMAL
 * - start_lsn and end_lsn are always the starting of a WAL record and not the middle of WAL record
 */
int
pgmoneta_summarize_wal(int srv, char* dir, uint64_t start_lsn, uint64_t end_lsn, block_ref_table** b)
{
   char* wal_dir = NULL;
   block_ref_table* brt = NULL;

   /**
    * Iterate through the wal directory and check against start_lsn and end_lsn
    */
   if (dir == NULL)
   {
      wal_dir = pgmoneta_get_server_wal(srv);
   }
   else
   {
      wal_dir = pgmoneta_append(wal_dir, dir);
   }
   pgmoneta_log_debug("pgmoneta_summarize_wal: %s is the corresponding wal directory", wal_dir);
   if (!pgmoneta_is_directory(wal_dir))
   {
      pgmoneta_log_error("pgmoneta_summarize_wal: %s is not a directory", wal_dir);
      goto error;
   }

   if (pgmoneta_brt_create_empty(&brt))
   {
      pgmoneta_log_error("pgmoneta_summarize_wal: failed to create an empty block reference table");
      goto error;
   }

   partial_record = malloc(sizeof(struct partial_xlog_record));
   partial_record->data_buffer_bytes_read = 0;
   partial_record->xlog_record_bytes_read = 0;
   partial_record->xlog_record = NULL;
   partial_record->data_buffer = NULL;
   /* Look upon the WAL archive directory and summarize the WAL records in the range [start_lsn, end_lsn) */
   if (pgmoneta_summarize_walfiles(wal_dir, start_lsn, end_lsn, brt))
   {
      pgmoneta_log_error("Error while reading/describing WAL directory");
      goto error;
   }
   if (partial_record->xlog_record != NULL)
   {
      free(partial_record->xlog_record);
   }
   if (partial_record->data_buffer != NULL)
   {
      free(partial_record->data_buffer);
   }
   free(partial_record);
   partial_record = NULL;

   *b = brt;

   free(wal_dir);
   return 0;

error:
   free(wal_dir);
   pgmoneta_brt_destroy(brt);
   return 1;
}

int
pgmoneta_wal_summary_save(int srv, uint64_t s_lsn, uint64_t e_lsn, block_ref_table* brt)
{
   char* summary_dir = NULL;
   char* summary_filename = NULL;

   char tmp_file[MAX_PATH] = {0};
   char file[MAX_PATH] = {0};

   summary_dir = pgmoneta_get_server_summary(srv);
   if (!pgmoneta_ends_with(summary_dir, "/"))
   {
      summary_dir = pgmoneta_append_char(summary_dir, '/');
   }
   pgmoneta_log_debug("summary dir: %s", summary_dir);
   /* Assuming the directory is created beforehand, just check */
   if (!pgmoneta_is_directory(summary_dir))
   {
      pgmoneta_log_error("pgmoneta_summarize_wal: %s is not a directory", summary_dir);
      goto error;
   }

   summary_filename = summary_file_name(s_lsn, e_lsn);
   
   if (pgmoneta_ends_with(summary_dir, "/"))
   {
      snprintf(tmp_file, sizeof(tmp_file), "%s%s.partial", summary_dir, summary_filename);
      snprintf(file, sizeof(file), "%s%s", summary_dir, summary_filename);
   }
   else
   {
      snprintf(tmp_file, sizeof(tmp_file), "%s/%s.partial", summary_dir, summary_filename);
      snprintf(file, sizeof(file), "%s/%s", summary_dir, summary_filename);
   }

   if (pgmoneta_brt_write(brt, tmp_file))
   {
      pgmoneta_log_error("pgmoneta_summarize_wal: unable to generate summary for [%d, %d)", s_lsn, e_lsn);
      goto error;
   }

   if (rename(tmp_file, file) != 0)
   {
      pgmoneta_log_error("pgmoneta_summarize_wal: could not rename file %s to %s", tmp_file, file);
      goto error;
   }

   free(summary_dir);
   free(summary_filename);
   return 0;

error:
   free(summary_dir);
   free(summary_filename);
   return 1;
}

static char*
summary_file_name(uint64_t s_lsn, uint64_t e_lsn)
{
   char hex[128];
   char* f = NULL;
   memset(&hex[0], 0, sizeof(hex));
   snprintf(&hex[0], sizeof(hex), "%08X%08X%08X%08X", (uint32_t)(s_lsn >> 32), (uint32_t) s_lsn, (uint32_t)(e_lsn >> 32), (uint32_t) e_lsn);
   f = pgmoneta_append(f, hex);
   return f;
}
