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

/* bench */
#include <bench.h>

/* pgmoneta */
#include <pgmoneta.h>

/* test harness */
#include <tscommon.h>

/* system */
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(const char* progname)
{
   printf("pgmoneta-bench - performance comparison between branches\n");
   printf("\n");
   printf("Usage: %s run     [OPTIONS]\n", progname);
   printf("       %s compare BASELINE CANDIDATE [OPTIONS]\n", progname);
   printf("       %s list\n", progname);
   printf("\n");
   printf("Options:\n");
   printf("  -c, --case NAME       Only this case (default: all)\n");
   printf("  -i, --iterations N    Measured iterations (default: %d)\n", BENCH_DEFAULT_ITERATIONS);
   printf("  -r, --results DIR     Results directory (required)\n");
   printf("  -b, --branch NAME     Branch being measured (run only)\n");
   printf("  -g, --commit SHA      Commit being measured (run only)\n");
   printf("  -h, --help            Show this help\n");
   printf("\n");
   printf("Normally invoked through benchmarks/bench.sh, which resolves the\n");
   printf("branch and commit and guarantees a Release build.\n");
   printf("\n");
}

int
main(int argc, char* argv[])
{
   const char* subcommand = NULL;
   const char* case_name = NULL;
   const char* results_dir = NULL;
   const char* branch = "unknown";
   const char* commit = "unknown";
   int iterations = BENCH_DEFAULT_ITERATIONS;
   int rc = BENCH_OK;
   bool env_created = false;
   int c;

   static struct option long_options[] = {
      {"case", required_argument, 0, 'c'},
      {"iterations", required_argument, 0, 'i'},
      {"results", required_argument, 0, 'r'},
      {"branch", required_argument, 0, 'b'},
      {"commit", required_argument, 0, 'g'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};

   if (argc < 2)
   {
      usage(argv[0]);
      return EXIT_FAILURE;
   }

   subcommand = argv[1];

   if (!strcmp(subcommand, "help") || !strcmp(subcommand, "-h") ||
       !strcmp(subcommand, "--help"))
   {
      usage(argv[0]);
      return EXIT_SUCCESS;
   }

   if (strcmp(subcommand, "run") && strcmp(subcommand, "compare") &&
       strcmp(subcommand, "list"))
   {
      fprintf(stderr, "bench: unknown subcommand '%s'\n\n", subcommand);
      usage(argv[0]);
      return EXIT_FAILURE;
   }

   optind = 2;

   while ((c = getopt_long(argc, argv, "c:i:r:b:g:h", long_options, NULL)) != -1)
   {
      switch (c)
      {
         case 'c':
            case_name = optarg;
            break;
         case 'i':
            iterations = atoi(optarg);
            break;
         case 'r':
            results_dir = optarg;
            break;
         case 'b':
            branch = optarg;
            break;
         case 'g':
            commit = optarg;
            break;
         case 'h':
            usage(argv[0]);
            return EXIT_SUCCESS;
         default:
            usage(argv[0]);
            return EXIT_FAILURE;
      }
   }

   if (!strcmp(subcommand, "list"))
   {
      bench_list();
      return EXIT_SUCCESS;
   }

   if (results_dir == NULL)
   {
      fprintf(stderr, "bench: --results is required\n");
      return EXIT_FAILURE;
   }

   if (!strcmp(subcommand, "run"))
   {
      /*
       * mctf_se reads the server definition out of shared memory, so the
       * same environment the test runner builds has to exist here too.
       */
      if (getenv("PGMONETA_TEST_CONF") == NULL)
      {
         fprintf(stderr, "bench: PGMONETA_TEST_CONF is not set; run through bench.sh\n");
         return EXIT_FAILURE;
      }

      pgmoneta_test_environment_create();
      env_created = true;

      rc = bench_run(case_name, iterations, branch, commit, results_dir);

      if (env_created)
      {
         pgmoneta_test_environment_destroy();
      }

      return rc == BENCH_OK ? EXIT_SUCCESS : EXIT_FAILURE;
   }

   if (!strcmp(subcommand, "compare"))
   {
      if (optind + 1 >= argc)
      {
         fprintf(stderr, "bench: compare needs BASELINE and CANDIDATE\n");
         return EXIT_FAILURE;
      }

      if (case_name == NULL)
      {
         fprintf(stderr, "bench: compare needs --case\n");
         return EXIT_FAILURE;
      }

      rc = bench_compare(results_dir, argv[optind], argv[optind + 1], case_name);

      return rc == BENCH_OK ? EXIT_SUCCESS : EXIT_FAILURE;
   }

   return EXIT_FAILURE;
}
