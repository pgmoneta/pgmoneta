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
#include <brt.h>
#include <deque.h>
#include <extraction.h>
#include <logging.h>
#include <utils.h>
#include <wal.h>
#include <walfile.h>
#include <walfile/wal_reader.h>
#include <walfile/wal_summary.h>

#include <dirent.h>
#include <libgen.h>

static char* summary_file_name(uint64_t s_lsn, uint64_t e_lsn);
static int summarize_walfile(char* path, uint64_t start_lsn, uint64_t end_lsn, block_ref_table* brt);
static int summarize_walfiles(int srv, char* dir_path, uint64_t start_lsn, uint64_t end_lsn, block_ref_table* brt);
static char* get_wal_file_name(char* dir_path, char* file);

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
   if (summarize_walfiles(srv, wal_dir, start_lsn, end_lsn, brt))
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
   snprintf(&hex[0], sizeof(hex), "%08X%08X%08X%08X", (uint32_t)(s_lsn >> 32), (uint32_t)s_lsn, (uint32_t)(e_lsn >> 32), (uint32_t)e_lsn);
   f = pgmoneta_append(f, hex);
   return f;
}

static int
summarize_walfile(char* path, uint64_t start_lsn, uint64_t end_lsn, block_ref_table* brt)
{
   struct walfile* wf = NULL;
   struct deque_iterator* record_iterator = NULL;
   struct decoded_xlog_record* record = NULL;
   char* from = NULL;
   char* to = NULL;

   from = pgmoneta_append(from, path);
   /* Extract the wal file in /tmp/ */
   to = pgmoneta_append(to, "/tmp/");
   to = pgmoneta_append(to, basename(path));

   if (pgmoneta_extract_file(from, &to, 0, true))
   {
      pgmoneta_log_error("Failed to extract WAL file from %s to %s", from, to);
      goto error;
   }

   /* Read and Parse the WAL records of this WAL file */
   if (pgmoneta_read_walfile(-1, to, &wf))
   {
      pgmoneta_log_error("Failed to read WAL file at %s", path);
      goto error;
   }

   if (pgmoneta_deque_iterator_create(wf->records, &record_iterator))
   {
      pgmoneta_log_error("Failed to create deque iterator");
      goto error;
   }

   /* Iterate each record */
   while (pgmoneta_deque_iterator_next(record_iterator))
   {
      record = (struct decoded_xlog_record*)record_iterator->value->data;
      if (pgmoneta_wal_record_summary(record, start_lsn, end_lsn, brt))
      {
         pgmoneta_log_error("Failed to summarize the WAL record at %s", pgmoneta_lsn_to_string(record->lsn));
         goto error;
      }
   }

   free(from);
   pgmoneta_deque_iterator_destroy(record_iterator);
   pgmoneta_destroy_walfile(wf);

   if (to != NULL)
   {
      pgmoneta_delete_file(to, NULL);
      free(to);
   }

   return 0;

error:
   free(from);
   pgmoneta_deque_iterator_destroy(record_iterator);
   pgmoneta_destroy_walfile(wf);

   if (to != NULL)
   {
      pgmoneta_delete_file(to, NULL);
      free(to);
   }

   return 1;
}

static int
summarize_walfiles(int srv, char* dir_path, uint64_t start_lsn, uint64_t end_lsn, block_ref_table* brt)
{
   struct deque* files = NULL;
   struct deque_iterator* file_iterator = NULL;
   char* file_path = malloc(MAX_PATH);
   char* dlog = NULL;
   int retry_count = 0;
   bool active = false;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

retry1:
   if (atomic_compare_exchange_strong(&config->common.servers[srv].wal_repository, &active, true))
   {
      if (pgmoneta_get_wal_files(dir_path, &files))
      {
         atomic_store(&config->common.servers[srv].wal_repository, false);
         goto error;
      }
      atomic_store(&config->common.servers[srv].wal_repository, false);
   }
   else
   {
      pgmoneta_log_debug("WAL: Did not get WAL repository lock for server %s - retrying", config->common.servers[srv].name);
      retry_count++;
      active = false;

      if (retry_count < 10)
      {
         SLEEP_AND_GOTO(500000000L, retry1);
      }
      else
      {
         pgmoneta_log_error("WAL: Did not get WAL repository lock for server %s", config->common.servers[srv].name);
         goto error;
      }
   }

   dlog = pgmoneta_deque_to_string(files, FORMAT_TEXT, NULL, 0);
   pgmoneta_log_debug("WAL files: %s", dlog);

   pgmoneta_deque_iterator_create(files, &file_iterator);
   while (pgmoneta_deque_iterator_next(file_iterator))
   {
      char* file = (char*)file_iterator->value->data;
      char* fn = NULL;
      xlog_seg_no seg_no = 0;
      uint64_t seg_start_lsn = 0;
      int wal_size = config->common.servers[srv].wal_size;

      /* Check if file is beyond end_lsn - skip if so */
      if (pgmoneta_validate_wal_filename(file, NULL, &seg_no, wal_size) == 0)
      {
         XLOG_SEG_NO_OFFEST_TO_REC_PTR(seg_no, 0, wal_size, seg_start_lsn);
         if (seg_start_lsn > end_lsn)
         {
            pgmoneta_log_debug("WAL summary: skipping %s (segment starts at %" PRIX64 ", beyond end_lsn %" PRIX64 ")",
                               file, seg_start_lsn, end_lsn);
            continue;
         }
      }

      fn = get_wal_file_name(dir_path, file);

      /* File needed but not available - error out */
      if (fn == NULL)
      {
         pgmoneta_log_error("WAL summary: %s not available after waiting", file);
         goto error;
      }

      memset(file_path, 0, MAX_PATH);
      if (!pgmoneta_ends_with(dir_path, "/"))
      {
         pgmoneta_snprintf(file_path, MAX_PATH, "%s/%s", dir_path, fn);
      }
      else
      {
         pgmoneta_snprintf(file_path, MAX_PATH, "%s%s", dir_path, fn);
      }

      free(fn);
      fn = NULL;

      pgmoneta_log_debug("WAL file at %s", file_path);

      if (summarize_walfile(file_path, start_lsn, end_lsn, brt))
      {
         pgmoneta_log_error("Summarize WAL error: %s (start: %" PRIX64 ", end: %" PRIX64 ")",
                            file_path, start_lsn, end_lsn);
         goto error;
      }
   }

   free(file_path);
   free(dlog);
   pgmoneta_deque_iterator_destroy(file_iterator);
   pgmoneta_deque_destroy(files);

   return 0;

error:
   free(file_path);
   free(dlog);
   pgmoneta_deque_iterator_destroy(file_iterator);
   pgmoneta_deque_destroy(files);

   return 1;
}

static char*
get_wal_file_name(char* dir_path, char* file)
{
   char* fn = NULL;
   char* base = NULL;
   char* suffix = NULL;
   char compressed_path[MAX_PATH];
   char uncompressed_path[MAX_PATH];
   char partial_path[MAX_PATH];
   int retry_count = 0;
   long sleep_ms = 500;       /* 500ms */
   long max_sleep_ms = 30000; /* 30s cap */
   int max_retries = 10;
   int orig_priority = 0;
   char* sep = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   sep = pgmoneta_ends_with(dir_path, "/") ? "" : "/";

   /* Strip .partial if present to get base name */
   if (pgmoneta_ends_with(file, ".partial"))
   {
      base = pgmoneta_remove_suffix(file, ".partial");
   }
   else
   {
      base = pgmoneta_append(NULL, file);
   }

   /* Build expected compression/encryption suffix */
   if (pgmoneta_get_suffix(config->compression_type, config->encryption, &suffix))
   {
      pgmoneta_log_error("WAL: failed to build suffix for %s", file);
      free(base);
      return NULL;
   }

   /* Build paths for all three file forms */
   memset(compressed_path, 0, MAX_PATH);
   memset(uncompressed_path, 0, MAX_PATH);
   memset(partial_path, 0, MAX_PATH);

   pgmoneta_snprintf(uncompressed_path, MAX_PATH, "%s%s%s", dir_path, sep, base);

   if (suffix != NULL)
   {
      pgmoneta_snprintf(compressed_path, MAX_PATH, "%s%s%s%s", dir_path, sep, base, suffix);
   }
   else
   {
      pgmoneta_snprintf(compressed_path, MAX_PATH, "%s%s%s", dir_path, sep, base);
   }

   pgmoneta_snprintf(partial_path, MAX_PATH, "%s%s%s.partial", dir_path, sep, base);

   /* Lower priority while waiting to give compression more CPU (works without root) */
   orig_priority = pgmoneta_get_priority();
   if (pgmoneta_set_priority(PRIORITY_LOW) == 0)
   {
      pgmoneta_log_debug("WAL: lowered priority to PRIORITY_LOW while waiting for compression");
   }

   /*
    * Three-tier file resolution with exponential backoff:
    * 1. Compressed file (e.g., .zstd) - fully processed
    * 2. Uncompressed file - renamed but not yet compressed
    * 3. .partial file - still active segment (may contain needed records)
    */
   while (retry_count < max_retries)
   {
      /* Tier 1: compressed file */
      if (pgmoneta_exists(compressed_path))
      {
         fn = pgmoneta_append(NULL, base);
         if (suffix != NULL)
         {
            fn = pgmoneta_append(fn, suffix);
         }
         break;
      }

      /* Tier 2: uncompressed file */
      if (suffix != NULL && pgmoneta_exists(uncompressed_path))
      {
         fn = pgmoneta_append(NULL, base);
         break;
      }

      /* Tier 3: .partial file */
      if (pgmoneta_exists(partial_path))
      {
         fn = pgmoneta_append(NULL, base);
         fn = pgmoneta_append(fn, ".partial");
         pgmoneta_log_debug("WAL: using .partial file %s (active segment)", partial_path);
         break;
      }

      struct timespec ts;

      pgmoneta_log_debug("WAL: waiting for %s (retry %d/%d, wait %ldms)",
                         compressed_path, retry_count + 1, max_retries, sleep_ms);

      ts.tv_sec = sleep_ms / 1000;
      ts.tv_nsec = (sleep_ms % 1000) * 1000000L;
      nanosleep(&ts, NULL);

      /* Yield CPU to give compression fork a chance to run */
      pgmoneta_cpu_yield();

      retry_count++;

      /* Exponential backoff: double the sleep time, up to max */
      if (sleep_ms < max_sleep_ms)
      {
         sleep_ms *= 2;
         if (sleep_ms > max_sleep_ms)
         {
            sleep_ms = max_sleep_ms;
         }
      }
   }

   /* Restore original priority */
   pgmoneta_set_priority(orig_priority);

   if (fn == NULL)
   {
      pgmoneta_log_error("WAL: %s not available after retries", compressed_path);
   }

   free(base);
   free(suffix);

   return fn;
}
