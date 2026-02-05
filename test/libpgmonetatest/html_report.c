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

#include <mctf.h>
#include <html_report.h>
#include <tscommon.h>
#include <utils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
html_report_build_path(char* path, size_t size)
{
   char base[MAX_PATH];
   char* slash = NULL;
   int n;

   if (path == NULL || size == 0)
   {
      return 1;
   }

   memset(base, 0, sizeof(base));

   /* Try TEST_BASE_DIR first (set by test environment), then fall back to env var */
   if (TEST_BASE_DIR[0] != '\0')
   {
      strncpy(base, TEST_BASE_DIR, sizeof(base) - 1);
      base[sizeof(base) - 1] = '\0';
   }
   else
   {
      char* env_base_dir = getenv("PGMONETA_TEST_BASE_DIR");
      if (env_base_dir == NULL || env_base_dir[0] == '\0')
      {
         return 1;
      }
      strncpy(base, env_base_dir, sizeof(base) - 1);
      base[sizeof(base) - 1] = '\0';
   }

   slash = strrchr(base, '/');
   if (slash == NULL)
   {
      return 1;
   }

   *slash = '\0';

   n = snprintf(path, size, "%s/log/pgmoneta-test-report.html", base);
   if (n <= 0 || (size_t)n >= size)
   {
      return 1;
   }

   return 0;
}

int
html_report_generate(const char* path, mctf_filter_type_t filter_type, const char* filter)
{
   if (path == NULL || path[0] == '\0')
   {
      return 1;
   }

   size_t count = 0;
   const mctf_result_t* results = mctf_get_results(&count);

   if (results == NULL || count == 0)
   {
      /* Nothing to report */
      return 1;
   }

   /* Ensure the directory exists */
   char* dir_path = strdup(path);
   if (dir_path != NULL)
   {
      char* last_slash = strrchr(dir_path, '/');
      if (last_slash != NULL)
      {
         *last_slash = '\0';
         pgmoneta_mkdir(dir_path);
      }
      free(dir_path);
   }

   FILE* f = fopen(path, "w");
   if (f == NULL)
   {
      fprintf(stderr, "Warning: Failed to open HTML report file at '%s'\n", path);
      return 1;
   }

   /* Aggregate counts */
   size_t passed = 0;
   size_t failed = 0;
   size_t skipped = 0;

   for (size_t i = 0; i < count; i++)
   {
      if (results[i].skipped)
      {
         skipped++;
      }
      else if (results[i].passed)
      {
         passed++;
      }
      else
      {
         failed++;
      }
   }

   /* Basic, self-contained HTML with simple styling suitable for CI artifacts */
   fprintf(f, "<!DOCTYPE html>\n");
   fprintf(f, "<html lang=\"en\">\n");
   fprintf(f, "<head>\n");
   fprintf(f, "  <meta charset=\"UTF-8\" />\n");
   fprintf(f, "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\" />\n");
   fprintf(f, "  <title>pgmoneta Test Report</title>\n");
   fprintf(f, "  <style>\n");
   fprintf(f, "    body { font-family: system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; background-color: #0b1020; color: #f5f5f7; margin: 0; padding: 24px; }\n");
   fprintf(f, "    h1 { margin-top: 0; font-size: 24px; }\n");
   fprintf(f, "    .summary { display: flex; flex-wrap: wrap; gap: 16px; margin-bottom: 24px; }\n");
   fprintf(f, "    .card { border-radius: 8px; padding: 12px 16px; background: linear-gradient(135deg, #151a30, #13162a); box-shadow: 0 10px 30px rgba(0,0,0,0.35); min-width: 140px; }\n");
   fprintf(f, "    .card-label { font-size: 12px; text-transform: uppercase; letter-spacing: 0.08em; color: #a0a4c0; margin-bottom: 4px; }\n");
   fprintf(f, "    .card-value { font-size: 18px; font-weight: 600; }\n");
   fprintf(f, "    .card-value.pass { color: #4ade80; }\n");
   fprintf(f, "    .card-value.fail { color: #fb7185; }\n");
   fprintf(f, "    .card-value.skip { color: #fbbf24; }\n");
   fprintf(f, "    .filter { margin-bottom: 24px; font-size: 14px; color: #a0a4c0; }\n");
   fprintf(f, "    table { border-collapse: collapse; width: 100%%; background-color: #0f172a; border-radius: 10px; overflow: hidden; box-shadow: 0 8px 24px rgba(0,0,0,0.35); }\n");
   fprintf(f, "    thead { background: linear-gradient(90deg, #1d2640, #111827); }\n");
   fprintf(f, "    th, td { padding: 10px 12px; font-size: 13px; text-align: left; }\n");
   fprintf(f, "    th { font-weight: 600; color: #e5e7eb; border-bottom: 1px solid rgba(148, 163, 184, 0.5); }\n");
   fprintf(f, "    tbody tr { background-color: #020617; }\n");
   fprintf(f, "    tbody tr:hover { background-color: #111827; }\n");
   fprintf(f, "    .status-pill { display: inline-flex; align-items: center; padding: 2px 8px; border-radius: 999px; font-size: 11px; font-weight: 600; letter-spacing: 0.05em; text-transform: uppercase; }\n");
   fprintf(f, "    .status-pass { background-color: rgba(22, 163, 74, 0.15); color: #4ade80; border: 1px solid rgba(34, 197, 94, 0.4); }\n");
   fprintf(f, "    .status-fail { background-color: rgba(220, 38, 38, 0.18); color: #fb7185; border: 1px solid rgba(248, 113, 113, 0.5); }\n");
   fprintf(f, "    .status-skip { background-color: rgba(245, 158, 11, 0.18); color: #fbbf24; border: 1px solid rgba(251, 191, 36, 0.5); }\n");
   fprintf(f, "    .file { color: #9ca3af; font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, 'Liberation Mono', 'Courier New', monospace; font-size: 12px; }\n");
   fprintf(f, "    .time { color: #60a5fa; font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, 'Liberation Mono', 'Courier New', monospace; font-size: 12px; font-weight: 500; }\n");
   fprintf(f, "    .message { color: #e5e7eb; }\n");
   fprintf(f, "    .no-message { color: #6b7280; font-style: italic; }\n");
   fprintf(f, "    .footer { margin-top: 24px; font-size: 12px; color: #6b7280; }\n");
   fprintf(f, "  </style>\n");
   fprintf(f, "</head>\n");
   fprintf(f, "<body>\n");
   fprintf(f, "  <h1>pgmoneta Test Report</h1>\n");

   /* Filter information (what was run) */
   fprintf(f, "  <div class=\"filter\">\n");
   fprintf(f, "    <strong>Executed tests:</strong> ");
   switch (filter_type)
   {
      case MCTF_FILTER_MODULE:
         fprintf(f, "Module filter = <code>%s</code>\n", filter ? filter : "");
         break;
      case MCTF_FILTER_TEST:
         fprintf(f, "Test name filter = <code>%s</code>\n", filter ? filter : "");
         break;
      case MCTF_FILTER_NONE:
      default:
         fprintf(f, "Full suite (no filter)\n");
         break;
   }
   fprintf(f, "  </div>\n");

   /* Summary cards */
   fprintf(f, "  <div class=\"summary\">\n");
   fprintf(f, "    <div class=\"card\">\n");
   fprintf(f, "      <div class=\"card-label\">Total</div>\n");
   fprintf(f, "      <div class=\"card-value\">%zu</div>\n", count);
   fprintf(f, "    </div>\n");
   fprintf(f, "    <div class=\"card\">\n");
   fprintf(f, "      <div class=\"card-label\">Passed</div>\n");
   fprintf(f, "      <div class=\"card-value pass\">%zu</div>\n", passed);
   fprintf(f, "    </div>\n");
   fprintf(f, "    <div class=\"card\">\n");
   fprintf(f, "      <div class=\"card-label\">Failed</div>\n");
   fprintf(f, "      <div class=\"card-value fail\">%zu</div>\n", failed);
   fprintf(f, "    </div>\n");
   fprintf(f, "    <div class=\"card\">\n");
   fprintf(f, "      <div class=\"card-label\">Skipped</div>\n");
   fprintf(f, "      <div class=\"card-value skip\">%zu</div>\n", skipped);
   fprintf(f, "    </div>\n");
   fprintf(f, "  </div>\n");

   /* Detailed table */
   fprintf(f, "  <table>\n");
   fprintf(f, "    <thead>\n");
   fprintf(f, "      <tr>\n");
   fprintf(f, "        <th style=\"width: 22%%;\">Test</th>\n");
   fprintf(f, "        <th style=\"width: 12%%;\">Status</th>\n");
   fprintf(f, "        <th style=\"width: 10%%;\">Time</th>\n");
   fprintf(f, "        <th style=\"width: 26%%;\">File</th>\n");
   fprintf(f, "        <th style=\"width: 10%%;\">Code</th>\n");
   fprintf(f, "        <th>Message</th>\n");
   fprintf(f, "      </tr>\n");
   fprintf(f, "    </thead>\n");
   fprintf(f, "    <tbody>\n");

   for (size_t i = 0; i < count; i++)
   {
      const mctf_result_t* r = &results[i];
      const char* status_class = "status-fail";
      const char* status_label = "FAIL";

      if (r->skipped)
      {
         status_class = "status-skip";
         status_label = "SKIP";
      }
      else if (r->passed)
      {
         status_class = "status-pass";
         status_label = "PASS";
      }

      /* Format elapsed time */
      long total_seconds = r->elapsed_ms / 1000;
      long hours = total_seconds / 3600;
      long minutes = (total_seconds % 3600) / 60;
      long seconds = total_seconds % 60;
      long milliseconds = r->elapsed_ms % 1000;
      char time_str[32];
      if (hours > 0)
      {
         snprintf(time_str, sizeof(time_str), "%02ld:%02ld:%02ld.%03ld", hours, minutes, seconds, milliseconds);
      }
      else if (minutes > 0)
      {
         snprintf(time_str, sizeof(time_str), "%02ld:%02ld.%03ld", minutes, seconds, milliseconds);
      }
      else if (seconds > 0)
      {
         snprintf(time_str, sizeof(time_str), "%ld.%03lds", seconds, milliseconds);
      }
      else
      {
         snprintf(time_str, sizeof(time_str), "%ldms", milliseconds);
      }

      fprintf(f, "      <tr>\n");
      fprintf(f, "        <td>%s</td>\n", r->test_name ? r->test_name : "(unknown)");
      fprintf(f, "        <td><span class=\"status-pill %s\">%s</span></td>\n", status_class, status_label);
      fprintf(f, "        <td class=\"time\">%s</td>\n", time_str);
      fprintf(f, "        <td class=\"file\">%s</td>\n", r->file ? r->file : "(unknown)");

      if (r->error_code != 0)
      {
         fprintf(f, "        <td>%d</td>\n", r->error_code);
      }
      else
      {
         fprintf(f, "        <td>&ndash;</td>\n");
      }

      if (r->error_message && r->error_message[0] != '\0')
      {
         fprintf(f, "        <td class=\"message\">%s</td>\n", r->error_message);
      }
      else
      {
         fprintf(f, "        <td class=\"no-message\">No additional message</td>\n");
      }

      fprintf(f, "      </tr>\n");
   }

   fprintf(f, "    </tbody>\n");
   fprintf(f, "  </table>\n");

   fprintf(f, "  <div class=\"footer\">\n");
   fprintf(f, "    Generated by pgmoneta MCTF test runner.\n");
   fprintf(f, "  </div>\n");

   fprintf(f, "</body>\n");
   fprintf(f, "</html>\n");

   fclose(f);
   return 0;
}
