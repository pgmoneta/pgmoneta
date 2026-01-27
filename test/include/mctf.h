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
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

/* Special return codes */
#define MCTF_CODE_SKIPPED (-1)

/* Global error number for MCTF */
extern int mctf_errno;

/* Global error message for MCTF */
extern char* mctf_errmsg;

/* Test function type */
typedef int (*mctf_test_func_t)(void);

/**
 * Test registration structure
 */
typedef struct mctf_test
{
   const char* name;       /**< Test name */
   const char* module;     /**< Module name */
   const char* file;       /**< Source file name */
   mctf_test_func_t func;  /**< Test function pointer */
   struct mctf_test* next; /**< Next test in linked list */
} mctf_test_t;

/**
 * Test result structure
 */
typedef struct mctf_result
{
   const char* test_name;     /**< Name of the executed test */
   const char* file;          /**< Source file name */
   bool passed;               /**< True if test passed */
   bool skipped;              /**< True if test was skipped */
   int error_code;            /**< Error code or line number */
   const char* error_message; /**< Error message, if any */
} mctf_result_t;

/**
 * Test runner state
 */
typedef struct mctf_runner
{
   mctf_test_t* tests;     /**< Linked list of registered tests */
   mctf_result_t* results; /**< Array of test results */
   size_t test_count;      /**< Total number of tests */
   size_t result_count;    /**< Total number of results */
   size_t passed_count;    /**< Number of passed tests */
   size_t failed_count;    /**< Number of failed tests */
   size_t skipped_count;   /**< Number of skipped tests */
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
 * @param module The module name
 * @param file The source file name
 * @param func The test function
 */
void
mctf_register_test(const char* name, const char* module, const char* file, mctf_test_func_t func);

/**
 * Extract module name from file path (internal use)
 */
const char*
mctf_extract_module_name(const char* file_path);

/**
 * Extract filename from file path (internal use)
 */
const char*
mctf_extract_filename(const char* file_path);

/**
 * Filter type for test execution
 */
typedef enum {
   MCTF_FILTER_NONE,  /* Run all tests */
   MCTF_FILTER_TEST,  /* Filter by test name */
   MCTF_FILTER_MODULE /* Filter by module name */
} mctf_filter_type_t;

/**
 * Run all registered tests
 * @param filter_type Type of filter to apply
 * @param filter Filter string
 * @return Number of failed tests
 */
int
mctf_run_tests(mctf_filter_type_t filter_type, const char* filter);

/**
 * Print test results summary
 */
void
mctf_print_summary(void);

/**
 * Open a log file for MCTF output
 * All subsequent test runner output will be duplicated to this file.
 *
 * @param path Filesystem path of the log file
 * @return 0 on success, non-zero on failure
 */
int
mctf_open_log(const char* path);

/**
 * Close the MCTF log file if it is open
 */
void
mctf_close_log(void);

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
 * @param error_label Label to jump to on failure
 * @param ... Optional error message (format string) and printf-style format arguments
 * 
 * Usage examples:
 *   MCTF_ASSERT(result == 0, cleanup);  // No error message
 *   MCTF_ASSERT(result == 0, cleanup, "function should return 0");  // Simple message
 *   MCTF_ASSERT(result == 0, cleanup, "function returned %d, expected 0", result);  // With format argument
 *   MCTF_ASSERT(a == b, cleanup, "expected %d but got %d", expected, actual);  // Multiple format arguments
 * 
 * Note: Format arguments (like 'value' in the examples) are optional and only needed when
 * the message string contains format specifiers (%d, %s, etc.)
 */
#define MCTF_ASSERT(condition, error_label, ...) \
   MCTF_ASSERT_FMT(condition, error_label, ##__VA_ARGS__, NULL)

/**
 * Internal macro - do not use directly
 * @param condition The condition to check
 * @param error_label Label to jump to on failure
 * @param ... Optional format string and printf-style arguments
 */
#define MCTF_ASSERT_FMT(condition, error_label, format, ...)           \
   do                                                                  \
   {                                                                   \
      if (!(condition))                                                \
      {                                                                \
         mctf_errno = __LINE__;                                        \
         if (mctf_errmsg)                                              \
            free(mctf_errmsg);                                         \
         if (format != NULL)                                           \
         {                                                             \
            size_t len = snprintf(NULL, 0, format, ##__VA_ARGS__) + 1; \
            mctf_errmsg = malloc(len);                                 \
            if (mctf_errmsg)                                           \
            {                                                          \
               snprintf(mctf_errmsg, len, format, ##__VA_ARGS__);      \
            }                                                          \
         }                                                             \
         else                                                          \
         {                                                             \
            mctf_errmsg = NULL;                                        \
         }                                                             \
         goto error_label;                                             \
      }                                                                \
      mctf_errno = 0;                                                  \
      if (mctf_errmsg)                                                 \
      {                                                                \
         free(mctf_errmsg);                                            \
         mctf_errmsg = NULL;                                           \
      }                                                                \
   }                                                                   \
   while (0)

/**
 * Assert pointer is not null
 */
#define MCTF_ASSERT_PTR_NONNULL(ptr, error_label, ...) \
   MCTF_ASSERT((ptr) != NULL, error_label, ##__VA_ARGS__)

/**
 * Assert pointer is null
 */
#define MCTF_ASSERT_PTR_NULL(ptr, error_label, ...) \
   MCTF_ASSERT((ptr) == NULL, error_label, ##__VA_ARGS__)

/**
 * Assert two integers are equal
 */
#define MCTF_ASSERT_INT_EQ(actual, expected, error_label, ...) \
   MCTF_ASSERT((actual) == (expected), error_label, ##__VA_ARGS__)

/**
 * Assert two strings are equal
 */
#define MCTF_ASSERT_STR_EQ(actual, expected, error_label, ...) \
   MCTF_ASSERT(strcmp((actual), (expected)) == 0, error_label, ##__VA_ARGS__)

/**
 * Internal macro for skip with format
 */
#define MCTF_SKIP_FMT(format, ...)                                  \
   do                                                               \
   {                                                                \
      mctf_errno = __LINE__;                                        \
      if (mctf_errmsg)                                              \
         free(mctf_errmsg);                                         \
      if (format != NULL)                                           \
      {                                                             \
         size_t len = snprintf(NULL, 0, format, ##__VA_ARGS__) + 1; \
         mctf_errmsg = malloc(len);                                 \
         if (mctf_errmsg)                                           \
         {                                                          \
            snprintf(mctf_errmsg, len, format, ##__VA_ARGS__);      \
         }                                                          \
      }                                                             \
      else                                                          \
      {                                                             \
         mctf_errmsg = NULL;                                        \
      }                                                             \
      return MCTF_CODE_SKIPPED;                                     \
   }                                                                \
   while (0)

/**
 * Helper macros for argument selection - do not use directly
 */
#define MCTF_SKIP_GET_HELPER(_1, _2, _3, _4, NAME, ...) NAME
#define MCTF_SKIP_IMPL_0() \
   MCTF_SKIP_FMT(NULL)
#define MCTF_SKIP_IMPL_1(format, ...) \
   MCTF_SKIP_FMT(format, ##__VA_ARGS__)

/**
 * Skip a test
 * @param ... Optional format string and printf-style arguments for skip reason
 * 
 * Examples:
 *   MCTF_SKIP();                                    // Skip without message
 *   MCTF_SKIP("WAL files not available");           // Skip with simple message
 *   MCTF_SKIP("Authentication failed for user %s", username);  // Skip with formatted message
 * 
 * Implementation: Uses argument counting with sentinel to select between empty and non-empty cases.
 * When no arguments: selects MCTF_SKIP_IMPL_0 which passes NULL as format.
 * When arguments provided: selects MCTF_SKIP_IMPL_1 which uses first arg as format.
 */
#define MCTF_SKIP(...) \
   MCTF_SKIP_GET_HELPER(, ##__VA_ARGS__, MCTF_SKIP_IMPL_1, MCTF_SKIP_IMPL_1, MCTF_SKIP_IMPL_1, MCTF_SKIP_IMPL_0, dummy)(__VA_ARGS__)

/**
 * Finish a test function - returns mctf_errno
 */
#define MCTF_FINISH() \
   return mctf_errno

/**
 * Register a test function with auto-naming and auto-module detection
 * Usage: MCTF_TEST(test_function_name) { ... }
 * The module name is automatically extracted from the source file name.
 */
#define MCTF_TEST(name)                                                               \
   static int name(void);                                                             \
   static void __attribute__((constructor)) mctf_register_##name(void)                \
   {                                                                                  \
      const char* file_path = __FILE__;                                               \
      const char* filename = mctf_extract_filename(file_path);                        \
      mctf_register_test(#name, mctf_extract_module_name(file_path), filename, name); \
   }                                                                                  \
   static int name(void)

#ifdef __cplusplus
}
#endif

#endif
