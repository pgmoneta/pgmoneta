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

#ifndef PGMONETA_MCTF_H
#define PGMONETA_MCTF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <string.h>

/* Special return codes */
#define MCTF_CODE_SKIPPED (-1)

/* Global error number for MCTF */
extern int mctf_errno;

/* Test function type */
typedef int (*mctf_test_func_t)(void);

/* Test registration structure */
typedef struct mctf_test
{
   const char* name;
   mctf_test_func_t func;
   struct mctf_test* next;
} mctf_test_t;

/* Test result structure */
typedef struct mctf_result
{
   const char* test_name;
   bool passed;
   bool skipped;
   int error_code;
} mctf_result_t;

/* Test runner state */
typedef struct mctf_runner
{
   mctf_test_t* tests;
   mctf_result_t* results;
   size_t test_count;
   size_t result_count;
   size_t passed_count;
   size_t failed_count;
   size_t skipped_count;
} mctf_runner_t;

/**
 * Initialize the MCTF framework
 */
void
mctf_init(void);

/**
 * Cleanup the MCTF framework
 */
void
mctf_cleanup(void);

/**
 * Register a test function
 * @param name The test name
 * @param func The test function
 */
void
mctf_register_test(const char* name, mctf_test_func_t func);

/**
 * Run all registered tests
 * @param filter Test name filter (NULL to run all)
 * @return Number of failed tests
 */
int
mctf_run_tests(const char* filter);

/**
 * Print test results summary
 */
void
mctf_print_summary(void);

/**
 * Get test results
 * @param count [out] Number of results
 * @return Array of test results
 */
const mctf_result_t*
mctf_get_results(size_t* count);

/* Macros for test functions */

/**
 * Assert a condition, jump to error label on failure
 * @param condition The condition to check
 * @param message Error message
 * @param error_label Label to jump to on failure
 */
#define MCTF_ASSERT(condition, message, error_label) \
   do                                                \
   {                                                 \
      if (!(condition))                              \
      {                                              \
         mctf_errno = __LINE__;                      \
         goto error_label;                           \
      }                                              \
      mctf_errno = 0;                                \
   }                                                 \
   while (0)

/**
 * Assert pointer is not null
 */
#define MCTF_ASSERT_PTR_NONNULL(ptr, message, error_label) \
   MCTF_ASSERT((ptr) != NULL, message, error_label)

/**
 * Assert pointer is null
 */
#define MCTF_ASSERT_PTR_NULL(ptr, message, error_label) \
   MCTF_ASSERT((ptr) == NULL, message, error_label)

/**
 * Assert two integers are equal
 */
#define MCTF_ASSERT_INT_EQ(actual, expected, message, error_label) \
   do                                                              \
   {                                                               \
      if ((actual) != (expected))                                  \
      {                                                            \
         mctf_errno = __LINE__;                                    \
         goto error_label;                                         \
      }                                                            \
      mctf_errno = 0;                                              \
   }                                                               \
   while (0)

/**
 * Assert two strings are equal
 */
#define MCTF_ASSERT_STR_EQ(actual, expected, message, error_label) \
   do                                                              \
   {                                                               \
      if (strcmp((actual), (expected)) != 0)                       \
      {                                                            \
         mctf_errno = __LINE__;                                    \
         goto error_label;                                         \
      }                                                            \
      mctf_errno = 0;                                              \
   }                                                               \
   while (0)

/**
 * Skip a test
 */
#define MCTF_SKIP()                   \
   do                                 \
   {                                  \
      mctf_errno = MCTF_CODE_SKIPPED; \
      return MCTF_CODE_SKIPPED;       \
   }                                  \
   while (0)

/**
 * Finish a test function - returns mctf_errno
 */
#define MCTF_FINISH() \
   return mctf_errno

/**
 * Register a test function with auto-naming
 * Usage: MCTF_TEST(test_function_name) { ... }
 */
#define MCTF_TEST(name)                                                \
   static int name(void);                                              \
   static void __attribute__((constructor)) mctf_register_##name(void) \
   {                                                                   \
      mctf_register_test(#name, name);                                 \
   }                                                                   \
   static int name(void)

#ifdef __cplusplus
}
#endif

#endif
