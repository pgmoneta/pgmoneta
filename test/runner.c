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
#include <tscommon.h>
#include <utils.h>
#include <logging.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <errno.h>

static void
usage(const char* progname)
{
   printf("Usage: %s [OPTIONS]\n", progname);
   printf("Options:\n");
   printf("  -t, --test NAME    Run only tests matching NAME (test name pattern)\n");
   printf("  -m, --module NAME Run all tests in module NAME\n");
   printf("  -h, --help         Show this help message\n");
   printf("\n");
   printf("Examples:\n");
   printf("  %s                 Run full test suite\n", progname);
   printf("  %s -m backup       Run all tests in 'backup' module\n", progname);
   printf("  %s -t backup_full  Run test matching 'backup_full'\n", progname);
   printf("\n");
}

/**
 * Signal handler for SIGABRT (assertion failures)
 * - pgmoneta_backtrace_string(): Gets backtrace as string for immediate display
 * - pgmoneta_backtrace(): Logs backtrace to debug log (if logging is initialized)
 * - pgmoneta_os_kernel_version(): Provides system information (OS, kernel version)
 */
static void
sigabrt_handler(int sig)
{
   char* bt = NULL;
   char* os = NULL;
   int kernel_major = 0, kernel_minor = 0, kernel_patch = 0;
   (void)sig;

   fprintf(stderr, "\n========================================\n");
   fprintf(stderr, "FATAL: Received SIGABRT (assertion failure)\n");
   fprintf(stderr, "========================================\n\n");

   // Get system information (OS and kernel version)
   if (pgmoneta_os_kernel_version(&os, &kernel_major, &kernel_minor, &kernel_patch) == 0)
   {
      fprintf(stderr, "System: %s %d.%d.%d\n\n", os ? os : "Unknown",
              kernel_major, kernel_minor, kernel_patch);
      if (os)
      {
         free(os);
      }
   }

   // Get backtrace string for immediate display
   if (pgmoneta_backtrace_string(&bt) == 0 && bt != NULL)
   {
      fprintf(stderr, "%s\n", bt);

      if (pgmoneta_log_is_enabled(PGMONETA_LOGGING_LEVEL_DEBUG1))
      {
         pgmoneta_backtrace();
      }

      free(bt);
   }
   else
   {
      fprintf(stderr, "Failed to generate backtrace\n");
   }

   fprintf(stderr, "\n========================================\n");
   fflush(stderr);

   signal(SIGABRT, SIG_DFL);
   abort();
}

/**
 * Signal handler for SIGSEGV (segmentation faults)
 * - pgmoneta_backtrace_string(): Gets backtrace as string for immediate display
 * - pgmoneta_backtrace(): Logs backtrace to debug log (if logging is initialized)
 * - pgmoneta_os_kernel_version(): Provides system information (OS, kernel version)
 */
static void
sigsegv_handler(int sig)
{
   char* bt = NULL;
   char* os = NULL;
   int kernel_major = 0, kernel_minor = 0, kernel_patch = 0;
   (void)sig;

   fprintf(stderr, "\n========================================\n");
   fprintf(stderr, "FATAL: Received SIGSEGV (segmentation fault)\n");
   fprintf(stderr, "========================================\n\n");

   // Get system information (OS and kernel version)
   if (pgmoneta_os_kernel_version(&os, &kernel_major, &kernel_minor, &kernel_patch) == 0)
   {
      fprintf(stderr, "System: %s %d.%d.%d\n\n", os ? os : "Unknown",
              kernel_major, kernel_minor, kernel_patch);
      if (os)
      {
         free(os);
      }
   }

   // Get backtrace string for immediate display
   if (pgmoneta_backtrace_string(&bt) == 0 && bt != NULL)
   {
      fprintf(stderr, "%s\n", bt);

      if (pgmoneta_log_is_enabled(PGMONETA_LOGGING_LEVEL_DEBUG1))
      {
         pgmoneta_backtrace();
      }

      free(bt);
   }
   else
   {
      fprintf(stderr, "Failed to generate backtrace\n");
   }

   fprintf(stderr, "\n========================================\n");
   fflush(stderr);

   signal(SIGSEGV, SIG_DFL);
   raise(SIGSEGV);
}

/**
 * Setup signal handlers for better error reporting
 */
static void
setup_signal_handlers(void)
{
   struct sigaction sa_abrt, sa_segv;

   memset(&sa_abrt, 0, sizeof(sa_abrt));
   sa_abrt.sa_handler = sigabrt_handler;
   sigemptyset(&sa_abrt.sa_mask);
   sa_abrt.sa_flags = 0;
   if (sigaction(SIGABRT, &sa_abrt, NULL) != 0)
   {
      fprintf(stderr, "Warning: Failed to setup SIGABRT handler: %s\n", strerror(errno));
   }

   memset(&sa_segv, 0, sizeof(sa_segv));
   sa_segv.sa_handler = sigsegv_handler;
   sigemptyset(&sa_segv.sa_mask);
   sa_segv.sa_flags = 0;
   if (sigaction(SIGSEGV, &sa_segv, NULL) != 0)
   {
      fprintf(stderr, "Warning: Failed to setup SIGSEGV handler: %s\n", strerror(errno));
   }
}

static int
build_mctf_log_path(char* path, size_t size)
{
   char base[MAX_PATH];
   char* slash = NULL;
   int n;

   if (path == NULL || size == 0)
   {
      return 1;
   }

   memset(base, 0, sizeof(base));

   if (TEST_BASE_DIR[0] == '\0')
   {
      return 1;
   }

   strncpy(base, TEST_BASE_DIR, sizeof(base) - 1);
   base[sizeof(base) - 1] = '\0';

   slash = strrchr(base, '/');
   if (slash == NULL)
   {
      return 1;
   }

   *slash = '\0';

   n = snprintf(path, size, "%s/log/pgmoneta-test.log", base);
   if (n <= 0 || (size_t)n >= size)
   {
      return 1;
   }

   return 0;
}

int
main(int argc, char* argv[])
{
   int number_failed = 0;
   const char* filter = NULL;
   mctf_filter_type_t filter_type = MCTF_FILTER_NONE;
   int c;
   bool env_created = false;
   char mctf_log_path[MAX_PATH];

   static struct option long_options[] = {
      {"test", required_argument, 0, 't'},
      {"module", required_argument, 0, 'm'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};

   while ((c = getopt_long(argc, argv, "t:m:h", long_options, NULL)) != -1)
   {
      switch (c)
      {
         case 't':
         case 'm':
            if (filter_type != MCTF_FILTER_NONE)
            {
               fprintf(stderr, "Error: Cannot specify both -t and -m options\n");
               usage(argv[0]);
               return EXIT_FAILURE;
            }
            filter = optarg;
            filter_type = (c == 't') ? MCTF_FILTER_TEST : MCTF_FILTER_MODULE;
            break;
         case 'h':
            usage(argv[0]);
            return EXIT_SUCCESS;
         default:
            usage(argv[0]);
            return EXIT_FAILURE;
      }
   }

   setup_signal_handlers();

   if (getenv("PGMONETA_TEST_CONF") != NULL)
   {
      pgmoneta_test_environment_create();
      env_created = true;
   }

   if (build_mctf_log_path(mctf_log_path, sizeof(mctf_log_path)) == 0)
   {
      if (mctf_open_log(mctf_log_path) != 0)
      {
         fprintf(stderr, "Warning: Failed to open MCTF log file at '%s'\n", mctf_log_path);
      }
   }

   /* Log all environment variables before starting the test suite so that
    * the execution context is captured alongside the test output.
    */
   mctf_log_environment();

   number_failed = mctf_run_tests(filter_type, filter);
   mctf_print_summary();
   mctf_cleanup();

   mctf_close_log();

   if (env_created)
   {
      pgmoneta_test_environment_destroy();
   }

   return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
