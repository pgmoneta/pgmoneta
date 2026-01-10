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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <stdbool.h>

static void
usage(const char* progname)
{
   printf("Usage: %s [OPTIONS]\n", progname);
   printf("Options:\n");
   printf("  -t, --test NAME    Run only tests matching NAME\n");
   printf("  -h, --help         Show this help message\n");
   printf("\n");
}

int
main(int argc, char* argv[])
{
   int number_failed = 0;
   const char* test_filter = NULL;
   int c;
   bool env_created = false;

   static struct option long_options[] = {
      {"test", required_argument, 0, 't'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};

   while ((c = getopt_long(argc, argv, "t:h", long_options, NULL)) != -1)
   {
      switch (c)
      {
         case 't':
            test_filter = optarg;
            break;
         case 'h':
            usage(argv[0]);
            return EXIT_SUCCESS;
         default:
            usage(argv[0]);
            return EXIT_FAILURE;
      }
   }

   if (getenv("PGMONETA_TEST_CONF") != NULL)
   {
      pgmoneta_test_environment_create();
      env_created = true;
   }

   mctf_init();

   number_failed = mctf_run_tests(test_filter);
   mctf_print_summary();
   mctf_cleanup();

   if (env_created)
   {
      pgmoneta_test_environment_destroy();
   }

   return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
