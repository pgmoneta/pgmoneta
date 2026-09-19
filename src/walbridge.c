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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <cmd.h>
#include <logging.h>
#include <utils.h>
#include <walbridge/walbridge.h>

/* system */
#include <err.h>

static void
usage(void)
{
   printf("  WAL protocol proxy from upstream Postgres to a durable local WAL store\n");
   printf("\n");
   printf("Usage:\n");
   printf("  pgmoneta-walbridge [ -c CONFIG_FILE ]\n");
   printf("\n");
   printf("Options:\n");
   printf("  -c, --config CONFIG_FILE  Set the path to the pgmoneta.conf file\n");
   printf("  -?, --help                Display help\n");
}

int
main(int argc, char** argv)
{
   int num_options = 0;
   int num_results = 0;
   int optind = 0;
   char* filepath = NULL;
   char* config_arg = NULL;
   int i = 0;
   cli_option options[] = {
      {"c", "config", true},
      {"?", "help", false},
   };
   cli_result* results = NULL;

   num_options = sizeof(options) / sizeof(options[0]);
   results = calloc(num_options, sizeof(cli_result));

   num_results = cmd_parse(argc, argv, options, num_options, results, num_options, false, &filepath, &optind);
   if (num_results < 0)
   {
      errx(1, "Error parsing command line\n");
      return 1;
   }

   for (i = 0; i < num_results; i++)
   {
      char* optname = results[i].option_name;
      char* optarg = results[i].argument;

      if (optname == NULL)
      {
         break;
      }
      else if (pgmoneta_compare_string(optname, "c") || pgmoneta_compare_string(optname, "config"))
      {
         config_arg = optarg;
      }
      else if (pgmoneta_compare_string(optname, "?") || pgmoneta_compare_string(optname, "help"))
      {
         usage();
         exit(0);
      }
   }

   return pgmoneta_walbridge_start(config_arg, argv);
}
