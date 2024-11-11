/*
 * Copyright (C) 2024 The pgmoneta community
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
#include <configuration.h>
#include <logging.h>
#include <shmem.h>
#include <utils.h>
#include <walfile.h>

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPT_COLOR 1000

static void
version(void)
{
   printf("pgmoneta-walinfo %s\n", VERSION);
   exit(1);
}

static void
usage(void)
{
   printf("pgmoneta-walinfo %s\n", VERSION);
   printf("  Command line utility to read and display Write-Ahead Log (WAL) files\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgmoneta-walinfo <file>\n");
   printf("\n");
   printf("Options:\n");
   printf("  -o, --output FILE   Output file\n");
   printf("  -F, --format        Output format (raw, json)\n");
   printf("  -L, --logfile FILE  Set the log file\n");
   printf("  -q, --quiet         No output only result\n");
   printf("      --color         Use colors (on, off)\n");
   printf("  -v, --verbose       Output result\n");
   printf("  -V, --version       Display version information\n");
   printf("  -?, --help          Display help\n");
   printf("\n");
   printf("pgmoneta: %s\n", PGMONETA_HOMEPAGE);
   printf("Report bugs: %s\n", PGMONETA_ISSUES);
}

int
main(int argc, char** argv)
{
   int c;
   int option_index = 0;
   char* output = NULL;
   char* format = NULL;
   char* logfile = NULL;
   bool quiet = false;
   bool color = true;
   bool verbose = false;
   enum value_type type = ValueString;
   size_t size;
   struct configuration* config = NULL;

   if (argc < 2)
   {
      usage();
      goto error;
   }

   while (1)
   {
      static struct option long_options[] =
      {
         {"output", required_argument, 0, 'o'},
         {"format", required_argument, 0, 'F'},
         {"logfile", required_argument, 0, 'L'},
         {"quiet", no_argument, 0, 'q'},
         {"color", required_argument, 0, OPT_COLOR},
         {"verbose", no_argument, 0, 'v'},
         {"version", no_argument, 0, 'V'},
         {"help", no_argument, 0, '?'},
         {0, 0, 0, 0}
      };

      c = getopt_long(argc, argv, "qvV?:o:F:L:",
                      long_options, &option_index);

      if (c == -1)
      {
         break;
      }

      switch (c)
      {
         case 'o':
            output = optarg;
            break;
         case 'F':
            format = optarg;

            if (!strcmp(format, "json"))
            {
               type = ValueJSON;
            }
            else
            {
               type = ValueString;
            }

            break;
         case 'L':
            logfile = optarg;
            break;
         case 'q':
            quiet = true;
            break;
         case OPT_COLOR:
            if (!strcmp(optarg, "off"))
            {
               color = false;
            }
            else
            {
               color = true;
            }
            break;
         case 'v':
            verbose = true;
            break;
         case 'V':
            version();
            exit(0);
         case '?':
            usage();
            exit(0);
         default:
            break;
      }
   }

   size = sizeof(struct configuration);
   if (pgmoneta_create_shared_memory(size, HUGEPAGE_OFF, &shmem))
   {
      warnx("pgmoneta-cli: Error creating shared memory");
      goto error;
   }
   pgmoneta_init_configuration(shmem);

   if (logfile)
   {
      config = (struct configuration*)shmem;

      config->log_type = PGMONETA_LOGGING_TYPE_FILE;
      memset(&config->log_path[0], 0, MISC_LENGTH);
      memcpy(&config->log_path[0], logfile, MIN(MISC_LENGTH - 1, strlen(logfile)));

      if (pgmoneta_start_logging())
      {
         exit(1);
      }
   }

   if (optind < argc)
   {
      char* file_path = argv[optind];

      if (pgmoneta_describe_walfile(file_path, type, output, quiet, color))
      {
         fprintf(stderr, "Error while reading/describing WAL file\n");
         goto error;
      }
   }
   else
   {
      fprintf(stderr, "Missing <file> argument\n");
      usage();
      goto error;
   }
   pgmoneta_destroy_shared_memory(shmem, size);

   if (logfile)
   {
      pgmoneta_stop_logging();
   }

   if (verbose)
   {
      printf("Success\n");
   }

   return 0;

error:
   if (logfile)
   {
      pgmoneta_stop_logging();
   }

   if (shmem != NULL)
   {
      pgmoneta_destroy_shared_memory(shmem, size);
   }

   if (verbose)
   {
      printf("Failure\n");
   }

   return 1;
}
