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
 *
 */

#include <pgmoneta.h>
#include <mctf_logslice.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MCTF_LOG_QUIET_STABLE_MS 1500
#define MCTF_LOG_QUIET_TIMEOUT_MS 15000

/**
 * Build path to the main pgmoneta log file used during tests.
 *
 * The test harness exports PGMONETA_TEST_BASE_DIR which points to
 * /tmp/pgmoneta-test/base. The pgmoneta log lives in ../log/pgmoneta.log
 * relative to that base directory.
 */
static int
mctf_build_pgmoneta_log_path(char* path, size_t size)
{
   char base[MAX_PATH];
   char* slash = NULL;
   char* base_dir = NULL;
   int n = 0;

   if (path == NULL || size == 0)
   {
      return 1;
   }

   memset(base, 0, sizeof(base));

   base_dir = getenv("PGMONETA_TEST_BASE_DIR");
   if (base_dir == NULL || base_dir[0] == '\0')
   {
      return 1;
   }

   strncpy(base, base_dir, sizeof(base) - 1);
   base[sizeof(base) - 1] = '\0';

   slash = strrchr(base, '/');
   if (slash == NULL)
   {
      return 1;
   }

   *slash = '\0';

   n = snprintf(path, size, "%s/log/pgmoneta.log", base);
   if (n <= 0 || (size_t)n >= size)
   {
      return 1;
   }

   return 0;
}

/**
 * Get current size of the pgmoneta log file.
 *
 * Returns 0 on success with size in *out_size, non-zero on failure.
 */
static int
mctf_get_pgmoneta_log_size(off_t* out_size)
{
   char log_path[MAX_PATH];
   struct stat st;

   if (out_size == NULL)
   {
      return 1;
   }

   if (mctf_build_pgmoneta_log_path(log_path, sizeof(log_path)) != 0)
   {
      return 1;
   }

   if (stat(log_path, &st) != 0)
   {
      return 1;
   }

   *out_size = st.st_size;
   return 0;
}

/* Current monotonic time in milliseconds. */
static long
mctf_monotonic_ms(void)
{
   struct timespec ts;

   if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
   {
      return 0;
   }

   return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

/*
 * Wait for pgmoneta.log size to remain unchanged for a stability window.
 * Returns 0 on success and stores the stable size in out_size.
 */
static int
mctf_wait_for_pgmoneta_log_quiet(long stable_ms, long timeout_ms, off_t* out_size)
{
   const long poll_ms = 25;
   long started_at = 0;
   long stable_since = 0;
   off_t last_size = 0;
   off_t current_size = 0;

   if (out_size == NULL)
   {
      return 1;
   }

   if (stable_ms < 0)
   {
      stable_ms = 0;
   }

   if (timeout_ms <= 0)
   {
      timeout_ms = stable_ms + 1000;
   }

   if (mctf_get_pgmoneta_log_size(&last_size) != 0)
   {
      return 1;
   }

   started_at = mctf_monotonic_ms();
   stable_since = started_at;

   while (mctf_monotonic_ms() - started_at <= timeout_ms)
   {
      if (mctf_get_pgmoneta_log_size(&current_size) != 0)
      {
         return 1;
      }

      if (current_size != last_size)
      {
         last_size = current_size;
         stable_since = mctf_monotonic_ms();
      }
      else if (mctf_monotonic_ms() - stable_since >= stable_ms)
      {
         *out_size = current_size;
         return 0;
      }

      usleep((useconds_t)(poll_ms * 1000));
   }

   *out_size = last_size;
   return 1;
}

/* Capture a log boundary using quiet-wait, with a best-effort fallback. */
void
mctf_capture_log_boundary(off_t* out_size)
{
   if (out_size == NULL)
   {
      return;
   }

   if (mctf_wait_for_pgmoneta_log_quiet(MCTF_LOG_QUIET_STABLE_MS, MCTF_LOG_QUIET_TIMEOUT_MS, out_size) != 0)
   {
      usleep((useconds_t)(MCTF_LOG_QUIET_STABLE_MS * 1000));
      (void)mctf_get_pgmoneta_log_size(out_size);
   }
}

/**
 * Append a string to a fixed-size buffer. Returns true if appended.
 */
static bool
append_to_buf(char* buf, size_t buf_max, size_t* used, const char* s)
{
   size_t len = strlen(s);
   if (*used + len + 1 <= buf_max)
   {
      memcpy(buf + *used, s, len + 1);
      *used += len;
      return true;
   }
   return false;
}

/**
 * Append a prefixed line to a buffer (for log summary lines).
 */
static bool
append_log_line_to_summary(char* buf, size_t buf_max, size_t* used,
                           const char* prefix, const char* line)
{
   return append_to_buf(buf, buf_max, used, prefix) && append_to_buf(buf, buf_max, used, line);
}

/**
 * Analyze a log slice for a test.
 *
 * Writes the slice to a per-test log file, sets out_has_error when ERROR lines
 * are found, and optionally returns a heap-allocated summary string.
 */
void
mctf_analyze_and_write_test_log_slice(const char* module,
                                      const char* test_name,
                                      off_t start_offset,
                                      off_t end_offset,
                                      bool* out_has_error,
                                      char** out_error_summary)
{
   char log_path[MAX_PATH];
   char test_log_path[MAX_PATH];
   struct stat st;
   FILE* src = NULL;
   FILE* dst = NULL;
   char line[4096];
   bool has_error_line = false;
   bool has_warn_line = false;
   char errors_buf[1536];
   char warnings_buf[1536];
   size_t errors_used = 0;
   size_t warnings_used = 0;

   if (out_has_error != NULL)
   {
      *out_has_error = false;
   }

   if (out_error_summary != NULL)
   {
      *out_error_summary = NULL;
   }

   if (module == NULL || test_name == NULL)
   {
      return;
   }

   if (mctf_build_pgmoneta_log_path(log_path, sizeof(log_path)) != 0)
   {
      return;
   }

   if (stat(log_path, &st) != 0)
   {
      return;
   }

   if (start_offset >= st.st_size)
   {
      return;
   }

   if (end_offset <= 0 || end_offset > st.st_size)
   {
      end_offset = st.st_size;
   }

   if (start_offset >= end_offset)
   {
      return;
   }


   char dir[MAX_PATH];
   char* slash = NULL;

   memset(dir, 0, sizeof(dir));
   strncpy(dir, log_path, sizeof(dir) - 1);
   dir[sizeof(dir) - 1] = '\0';

   slash = strrchr(dir, '/');
   if (slash != NULL)
   {
      *slash = '\0';
   }

   if (snprintf(test_log_path, sizeof(test_log_path), "%s/%s__%s.pgmoneta.log", dir, module, test_name) <= 0)
   {
      return;
   }
   

   src = fopen(log_path, "r");
   if (!src)
   {
      return;
   }

   if (fseeko(src, start_offset, SEEK_SET) != 0)
   {
      fclose(src);
      return;
   }

   dst = fopen(test_log_path, "w");
   if (!dst)
   {
      fclose(src);
      return;
   }

   errors_buf[0] = '\0';
   warnings_buf[0] = '\0';

   /* Collect ERROR and WARN lines into separate sections; only ERROR causes test failure. */
   while (true)
   {
      off_t pos = ftello(src);
      if (pos < 0 || pos >= end_offset)
      {
         break;
      }

      if (fgets(line, sizeof(line), src) == NULL)
      {
         break;
      }

      pos = ftello(src);
      if (pos > end_offset)
      {
         break;
      }

      fputs(line, dst);

      if (strstr(line, " ERROR") != NULL)
      {
         has_error_line = true;
         if (out_error_summary != NULL)
         {
            if (!append_log_line_to_summary(errors_buf, sizeof(errors_buf), &errors_used, "      ", line))
            {
               append_to_buf(errors_buf, sizeof(errors_buf), &errors_used, "...\n");
            }
         }
      }
      else if (strstr(line, " WARN") != NULL)
      {
         has_warn_line = true;
         if (out_error_summary != NULL)
         {
            if (!append_log_line_to_summary(warnings_buf, sizeof(warnings_buf), &warnings_used, "      ", line))
            {
               append_to_buf(warnings_buf, sizeof(warnings_buf), &warnings_used, "...\n");
            }
         }
      }
   }

   fclose(src);
   fclose(dst);

   /* Only ERROR causes failure; WARN is reported in a separate section but does not fail */
   if (out_has_error != NULL)
   {
      *out_has_error = has_error_line;
   }

   if (out_error_summary != NULL && (has_error_line || has_warn_line))
   {
      const char* err_hdr = "    Errors:\n";
      const char* warn_hdr = "    Warnings:\n";
      size_t total = 0;
      if (has_error_line)
      {
         total += strlen(err_hdr) + errors_used;
      }
      if (has_warn_line)
      {
         total += strlen(warn_hdr) + warnings_used;
      }
      if (total > 0)
      {
         char* summary = malloc(total + 1);
         if (summary)
         {
            size_t used = 0;
            summary[0] = '\0';
            if (has_error_line)
            {
               append_to_buf(summary, total + 1, &used, err_hdr);
               append_to_buf(summary, total + 1, &used, errors_buf);
            }
            if (has_warn_line)
            {
               append_to_buf(summary, total + 1, &used, warn_hdr);
               append_to_buf(summary, total + 1, &used, warnings_buf);
            }
            *out_error_summary = summary;
         }
      }
   }
}
