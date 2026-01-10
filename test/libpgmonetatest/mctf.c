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

/* Global error number */
int mctf_errno = 0;

/* Global test runner */
static mctf_runner_t g_runner = {0};
static bool g_initialized = false;

void
mctf_init(void)
{
   if (!g_initialized)
   {
      g_initialized = true;
   }
}

void
mctf_cleanup(void)
{
   mctf_test_t* test = g_runner.tests;
   while (test)
   {
      mctf_test_t* next = test->next;
      free((void*)test->name);
      free(test);
      test = next;
   }

   if (g_runner.results)
   {
      free(g_runner.results);
   }

   g_initialized = false;
   memset(&g_runner, 0, sizeof(g_runner));
}

void
mctf_register_test(const char* name, mctf_test_func_t func)
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

   test->func = func;
   test->next = g_runner.tests;
   g_runner.tests = test;
   g_runner.test_count++;
}

static bool
matches_filter(const char* test_name, const char* filter)
{
   if (!filter || filter[0] == '\0')
   {
      return true;
   }
   return strstr(test_name, filter) != NULL;
}

int
mctf_run_tests(const char* filter)
{
   size_t tests_to_run = 0;
   mctf_test_t* test;

   if (!g_initialized)
   {
      mctf_init();
   }
   for (test = g_runner.tests; test; test = test->next)
   {
      if (matches_filter(test->name, filter))
      {
         tests_to_run++;
      }
   }

   if (tests_to_run == 0)
   {
      if (filter)
      {
         fprintf(stderr, "MCTF: No tests found matching filter '%s'\n", filter);
      }
      else
      {
         fprintf(stderr, "MCTF: No tests registered (total registered: %zu)\n", g_runner.test_count);
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
   if (filter)
   {
      printf("Filter: %s\n", filter);
   }
   printf("Total tests to run: %zu\n\n", tests_to_run);

   /* Run tests */
   for (test = g_runner.tests; test; test = test->next)
   {
      if (!matches_filter(test->name, filter))
      {
         continue;
      }

      mctf_result_t* result = &g_runner.results[g_runner.result_count];
      result->test_name = test->name;
      result->passed = false;
      result->skipped = false;
      result->error_code = 0;

      printf("[RUN] %s\n", test->name);

      mctf_errno = 0;
      int ret = test->func();

      if (ret == MCTF_CODE_SKIPPED)
      {
         result->skipped = true;
         g_runner.skipped_count++;
         printf("[SKIP] %s\n", test->name);
      }
      else if (ret == 0 && mctf_errno == 0)
      {
         result->passed = true;
         g_runner.passed_count++;
         printf("[PASS] %s\n", test->name);
      }
      else
      {
         result->passed = false;
         result->error_code = (ret != 0) ? ret : mctf_errno;
         g_runner.failed_count++;
         printf("[FAIL] %s (error code: %d)\n", test->name, result->error_code);
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
            printf("  - %s (error code: %d)\n",
                   g_runner.results[i].test_name,
                   g_runner.results[i].error_code);
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

