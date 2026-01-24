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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>

/* Global error number */
int mctf_errno = 0;

/* Global error message */
char* mctf_errmsg = NULL;

/* Global test runner */
static mctf_runner_t g_runner = {0};
static bool g_initialized = false;

void
mctf_init(void)
{
   g_initialized = true;
}

void
mctf_cleanup(void)
{
   mctf_test_t* test = g_runner.tests;
   while (test)
   {
      mctf_test_t* next = test->next;
      free((void*)test->name);
      free((void*)test->module);
      free((void*)test->file);
      free(test);
      test = next;
   }

   if (g_runner.results)
   {
      for (size_t i = 0; i < g_runner.result_count; i++)
      {
         if (g_runner.results[i].error_message)
         {
            free((void*)g_runner.results[i].error_message);
         }
      }
      free(g_runner.results);
   }

   if (mctf_errmsg)
   {
      free(mctf_errmsg);
      mctf_errmsg = NULL;
   }

   g_initialized = false;
   memset(&g_runner, 0, sizeof(g_runner));
}

void
mctf_register_test(const char* name, const char* module, const char* file, mctf_test_func_t func)
{
   if (!g_initialized)
   {
      mctf_init();
   }

   mctf_test_t* test = calloc(1, sizeof(mctf_test_t));
   if (!test)
   {
      fprintf(stderr, "MCTF: Failed to allocate memory for test '%s'\n", name);
      return;
   }

   test->name = strdup(name);
   if (!test->name)
   {
      fprintf(stderr, "MCTF: Failed to allocate memory for test name '%s'\n", name);
      free(test);
      return;
   }

   test->module = module ? strdup(module) : strdup("unknown");
   if (!test->module)
   {
      fprintf(stderr, "MCTF: Failed to allocate memory for test module '%s'\n", module);
      free((void*)test->name);
      free(test);
      return;
   }

   test->file = file ? strdup(file) : strdup("unknown");
   if (!test->file)
   {
      fprintf(stderr, "MCTF: Failed to allocate memory for test file '%s'\n", file);
      free((void*)test->name);
      free((void*)test->module);
      free(test);
      return;
   }

   test->func = func;
   test->next = g_runner.tests;
   g_runner.tests = test;
   g_runner.test_count++;
}

const char*
mctf_extract_filename(const char* file_path)
{
   const char* basename = strrchr(file_path, '/');
   return basename ? basename + 1 : file_path;
}

const char*
mctf_extract_module_name(const char* file_path)
{
   /* Extract filename first, then process it to get module name */
   const char* basename = mctf_extract_filename(file_path);

   /* Remove "test_" prefix if present */
   if (strncmp(basename, "test_", 5) == 0)
   {
      basename += 5;
   }

   /* Remove ".c" suffix if present */
   size_t len = strlen(basename);
   if (len > 2 && strcmp(basename + len - 2, ".c") == 0)
   {
      len -= 2;
   }

   static char module[256];
   strncpy(module, basename, len);
   module[len] = '\0';
   return module;
}


static bool
matches_filter(mctf_filter_type_t filter_type, const char* test_name, const char* module, const char* filter)
{
   if (filter_type == MCTF_FILTER_NONE || !filter || filter[0] == '\0')
   {
      return true;
   }

   switch (filter_type)
   {
      case MCTF_FILTER_MODULE:
         return module && strstr(module, filter) != NULL;
      case MCTF_FILTER_TEST:
         return strstr(test_name, filter) != NULL;
      default:
         return false;
   }
}


int
mctf_run_tests(mctf_filter_type_t filter_type, const char* filter)
{
   size_t tests_to_run = 0;
   mctf_test_t* test;

   if (!g_initialized)
   {
      mctf_init();
   }
   for (test = g_runner.tests; test; test = test->next)
   {
      if (matches_filter(filter_type, test->name, test->module, filter))
      {
         tests_to_run++;
      }
   }

   if (tests_to_run == 0)
   {
      switch (filter_type)
      {
         case MCTF_FILTER_NONE:
            fprintf(stderr, "MCTF: No tests registered (total registered: %zu)\n", g_runner.test_count);
            break;
         case MCTF_FILTER_MODULE:
            fprintf(stderr, "MCTF: No tests found in module '%s'\n", filter);
            break;
         case MCTF_FILTER_TEST:
            fprintf(stderr, "MCTF: No tests found matching filter '%s'\n", filter);
            break;
      }
      return 0;
   }

   /* Allocate results array */
   g_runner.results = calloc(tests_to_run, sizeof(mctf_result_t));
   if (!g_runner.results)
   {
      fprintf(stderr, "MCTF: Failed to allocate memory for results\n");
      return -1;
   }

   g_runner.result_count = 0;
   g_runner.passed_count = 0;
   g_runner.failed_count = 0;
   g_runner.skipped_count = 0;

   printf("\n=== Running MCTF Tests ===\n");
   switch (filter_type)
   {
      case MCTF_FILTER_MODULE:
         printf("Module: %s\n", filter);
         break;
      case MCTF_FILTER_TEST:
         printf("Test filter: %s\n", filter);
         break;
      default:
         break;
   }
   printf("Total tests to run: %zu\n\n", tests_to_run);

   const char* current_module = NULL;
   for (test = g_runner.tests; test; test = test->next)
   {
      if (!matches_filter(filter_type, test->name, test->module, filter))
      {
         continue;
      }

      if (!current_module || strcmp(current_module, test->module) != 0)
      {
         if (current_module)
         {
            printf("\n");
         }
         printf("--- %s ---\n", test->module);
         current_module = test->module;
      }

      mctf_result_t* result = &g_runner.results[g_runner.result_count];
      result->test_name = test->name;
      result->file = test->file;
      result->passed = false;
      result->skipped = false;
      result->error_code = 0;
      result->error_message = NULL;

      struct timespec start_time, end_time;
      clock_gettime(CLOCK_MONOTONIC, &start_time);

      mctf_errno = 0;
      if (mctf_errmsg) { free(mctf_errmsg); mctf_errmsg = NULL; }
      int ret = test->func();

      clock_gettime(CLOCK_MONOTONIC, &end_time);

      long elapsed_ms = (end_time.tv_sec - start_time.tv_sec) * 1000 +
                        (end_time.tv_nsec - start_time.tv_nsec) / 1000000;

      long total_seconds = elapsed_ms / 1000;
      long hours = total_seconds / 3600;
      long minutes = (total_seconds % 3600) / 60;
      long seconds = total_seconds % 60;
      long milliseconds = elapsed_ms % 1000;

      if (ret == MCTF_CODE_SKIPPED)
      {
         result->skipped = true;
         g_runner.skipped_count++;
         printf("%s (%02ld:%02ld:%02ld,%03ld) [SKIP]\n",
                test->name, hours, minutes, seconds, milliseconds);
         mctf_errno = 0;
         if (mctf_errmsg) { free(mctf_errmsg); mctf_errmsg = NULL; }
      }
      else if (ret == 0 && mctf_errno == 0)
      {
         result->passed = true;
         g_runner.passed_count++;
         printf("%s (%02ld:%02ld:%02ld,%03ld) [PASS]\n",
                test->name, hours, minutes, seconds, milliseconds);
      }
      else
      {
         result->passed = false;
         result->error_code = (ret != 0) ? ret : mctf_errno;
         result->error_message = mctf_errmsg ? strdup(mctf_errmsg) : NULL;
         if (mctf_errmsg)
         {
            free(mctf_errmsg);
            mctf_errmsg = NULL;
         }
         g_runner.failed_count++;
         printf("  %s (%02ld:%02ld:%02ld,%03ld) [FAIL] (%s:%d)\n",
                test->name, hours, minutes, seconds, milliseconds,
                test->file, result->error_code);
      }

      g_runner.result_count++;
   }

   return (int)g_runner.failed_count;
}

void
mctf_print_summary(void)
{
   printf("\n=== Test Summary ===\n");
   printf("Total tests: %zu\n", g_runner.result_count);
   printf("Passed: %zu\n", g_runner.passed_count);
   printf("Failed: %zu\n", g_runner.failed_count);
   printf("Skipped: %zu\n", g_runner.skipped_count);

   if (g_runner.skipped_count > 0)
   {
      printf("\nSkipped tests:\n");
      for (size_t i = 0; i < g_runner.result_count; i++)
      {
         if (g_runner.results[i].skipped)
         {
            printf("  - %s\n", g_runner.results[i].test_name);
         }
      }
   }

   if (g_runner.failed_count > 0)
   {
      printf("\nFailed tests:\n");
      for (size_t i = 0; i < g_runner.result_count; i++)
      {
         if (!g_runner.results[i].passed && !g_runner.results[i].skipped)
         {
            if (g_runner.results[i].error_message)
            {
               printf("  - %s (%s:%d) - %s\n",
                      g_runner.results[i].test_name,
                      g_runner.results[i].file,
                      g_runner.results[i].error_code,
                      g_runner.results[i].error_message);
            }
            else
            {
               printf("  - %s (%s:%d)\n",
                      g_runner.results[i].test_name,
                      g_runner.results[i].file,
                      g_runner.results[i].error_code);
            }
         }
      }
   }

   printf("\n");
}

const mctf_result_t*
mctf_get_results(size_t* count)
{
   if (count)
   {
      *count = g_runner.result_count;
   }
   return g_runner.results;
}

