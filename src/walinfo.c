/*
 * Copyright (C) 2025 The pgmoneta community
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
#include <cmd.h>
#include <configuration.h>
#include <deque.h>
#include <logging.h>
#include <shmem.h>
#include <utils.h>
#include <walfile.h>

#include <inttypes.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
   printf("  -c, --config CONFIG_FILE Set the path to the pgmoneta.conf file\n");
   printf("  -o, --output FILE        Output file\n");
   printf("  -F, --format             Output format (raw, json)\n");
   printf("  -L, --logfile FILE       Set the log file\n");
   printf("  -q, --quiet              No output only result\n");
   printf("      --color              Use colors (on, off)\n");
   printf("  -r, --rmgr               Filter on a resource manager\n");
   printf("  -s, --start              Filter on a start LSN\n");
   printf("  -e, --end                Filter on an end LSN\n");
   printf("  -x, --xid                Filter on an XID\n");
   printf("  -l, --limit              Limit number of outputs\n");
   printf("  -v, --verbose            Output result\n");
   printf("  -V, --version            Display version information\n");
   printf("  -?, --help               Display help\n");
   printf("\n");
   printf("pgmoneta: %s\n", PGMONETA_HOMEPAGE);
   printf("Report bugs: %s\n", PGMONETA_ISSUES);
}

int
main(int argc, char** argv)
{
   int loaded = 1;
   char* configuration_path = NULL;
   char* output = NULL;
   char* format = NULL;
   char* logfile = NULL;
   bool quiet = false;
   bool color = true;
   struct deque* rms = NULL;
   uint64_t start_lsn = 0;
   uint64_t end_lsn = 0;
   uint64_t start_lsn_high = 0;
   uint64_t start_lsn_low = 0;
   uint64_t end_lsn_high = 0;
   uint64_t end_lsn_low = 0;
   struct deque* xids = NULL;
   uint32_t limit = 0;
   bool verbose = false;
   enum value_type type = ValueString;
   size_t size;
   struct walinfo_configuration* config = NULL;
   int optind = 0;
   char* filepath;
   int num_results = 0;
   int num_options = 0;

   cli_option options[] = {
      {"c", "config", true},
      {"o", "output", true},
      {"F", "format", true},
      {"L", "logfile", true},
      {"q", "quiet", false},
      {"", "color", true},
      {"r", "rmgr", true},
      {"s", "start", true},
      {"e", "end", true},
      {"x", "xid", true},
      {"l", "limit", true},
      {"v", "verbose", false},
      {"V", "version", false},
      {"?", "help", false},
   };

   num_options = sizeof(options) / sizeof(options[0]);
   cli_result results[num_options];

   if (argc < 2)
   {
      usage();
      goto error;
   }

   num_results = cmd_parse(argc, argv, options, num_options, results, num_options, true, &filepath, &optind);

   if (num_results < 0)
   {
      errx(1, "Error parsing command line\n");
      return 1;
   }

   for (int i = 0; i < num_results; i++)
   {
      char* optname = results[i].option_name;
      char* optarg = results[i].argument;

      if (optname == NULL)
      {
         break;
      }
      else if (strcmp(optname, "c") == 0 || strcmp(optname, "config") == 0)
      {
         configuration_path = optarg;
      }
      else if (strcmp(optname, "o") == 0 || strcmp(optname, "output") == 0)
      {
         output = optarg;
      }
      else if (strcmp(optname, "F") == 0 || strcmp(optname, "format") == 0)
      {
         format = optarg;

         if (!strcmp(format, "json"))
         {
            type = ValueJSON;
         }
         else
         {
            type = ValueString;
         }
      }
      else if (strcmp(optname, "L") == 0 || strcmp(optname, "logfile") == 0)
      {
         logfile = optarg;
      }
      else if (strcmp(optname, "q") == 0 || strcmp(optname, "quiet") == 0)
      {
         quiet = true;
      }
      else if (strcmp(optname, "color") == 0)
      {
         if (!strcmp(optarg, "off"))
         {
            color = false;
         }
         else
         {
            color = true;
         }
      }
      else if (strcmp(optname, "r") == 0 || strcmp(optname, "rmgr") == 0)
      {
         if (rms == NULL)
         {
            if (pgmoneta_deque_create(false, &rms))
            {
               exit(1);
            }
         }

         pgmoneta_deque_add(rms, NULL, (uintptr_t)optarg, ValueString);
      }
      else if (strcmp(optname, "s") == 0 || strcmp(optname, "start") == 0)
      {
         if (strchr(optarg, '/'))
         {
            // Assuming optarg is a string like "16/B374D848"
            if (sscanf(optarg, "%" SCNx64 "/%" SCNx64, &start_lsn_high, &start_lsn_low) == 2)
            {
               start_lsn = (start_lsn_high << 32) + start_lsn_low;
            }
            else
            {
               fprintf(stderr, "Invalid start LSN format\n");
               exit(1);
            }
         }
         else
         {
            start_lsn = strtoull(optarg, NULL, 10);    // Assuming optarg is a decimal number
         }
      }
      else if (strcmp(optname, "e") == 0 || strcmp(optname, "end") == 0)
      {
         if (strchr(optarg, '/'))
         {
            // Assuming optarg is a string like "16/B374D848"
            if (sscanf(optarg, "%" SCNx64 "/%" SCNx64, &end_lsn_high, &end_lsn_low) == 2)
            {
               end_lsn = (end_lsn_high << 32) + end_lsn_low;
            }
            else
            {
               fprintf(stderr, "Invalid end LSN format\n");
               exit(1);
            }
         }
         else
         {
            end_lsn = strtoull(optarg, NULL, 10);    // Assuming optarg is a decimal number
         }
      }
      else if (strcmp(optname, "x") == 0 || strcmp(optname, "xid") == 0)
      {
         if (xids == NULL)
         {
            if (pgmoneta_deque_create(false, &xids))
            {
               exit(1);
            }
         }

         pgmoneta_deque_add(xids, NULL, (uintptr_t)pgmoneta_atoi(optarg), ValueUInt32);
      }
      else if (strcmp(optname, "l") == 0 || strcmp(optname, "limit") == 0)
      {
         limit = pgmoneta_atoi(optarg);
      }
      else if (strcmp(optname, "v") == 0 || strcmp(optname, "verbose") == 0)
      {
         verbose = true;
      }
      else if (strcmp(optname, "V") == 0 || strcmp(optname, "version") == 0)
      {
         version();
         exit(0);
      }
      else if (strcmp(optname, "?") == 0 || strcmp(optname, "help") == 0)
      {
         usage();
         exit(0);
      }
   }

   size = sizeof(struct walinfo_configuration);
   if (pgmoneta_create_shared_memory(size, HUGEPAGE_OFF, &shmem))
   {
      warnx("Error creating shared memory");
      goto error;
   }

   pgmoneta_init_main_configuration(shmem);
   config = (struct walinfo_configuration*)shmem;

   if (configuration_path != NULL)
   {
      if (pgmoneta_exists(configuration_path))
      {
         loaded = pgmoneta_read_main_configuration(shmem, configuration_path);
      }

      if (loaded)
      {
         warnx("Configuration not found: %s", configuration_path);
      }
   }

   if (loaded && pgmoneta_exists(PGMONETA_MAIN_CONFIG_FILE_PATH))
   {
      loaded = pgmoneta_read_main_configuration(shmem, PGMONETA_MAIN_CONFIG_FILE_PATH);
   }

   if (loaded)
   {
      config->common.log_type = PGMONETA_LOGGING_TYPE_CONSOLE;
   }
   else
   {
      if (logfile)
      {
         config->common.log_type = PGMONETA_LOGGING_TYPE_FILE;
         memset(&config->common.log_path[0], 0, MISC_LENGTH);
         memcpy(&config->common.log_path[0], logfile, MIN(MISC_LENGTH - 1, strlen(logfile)));
      }
   }

   if (pgmoneta_start_logging())
   {
      exit(1);
   }

   if (filepath != NULL)
   {
      if (pgmoneta_describe_walfile(filepath, type, output, quiet, color,
                                    rms, start_lsn, end_lsn, xids, limit))
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

   pgmoneta_deque_destroy(rms);
   pgmoneta_deque_destroy(xids);

   return 0;

error:
   if (logfile)
   {
      pgmoneta_stop_logging();
   }

   pgmoneta_deque_destroy(rms);
   pgmoneta_deque_destroy(xids);

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
