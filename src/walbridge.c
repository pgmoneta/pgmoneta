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
   printf("pgmoneta-walbridge %s\n", VERSION);
   printf("  WAL protocol proxy from upstream Postgres to a durable local WAL store\n");
   printf("\n");
   printf("Usage:\n");
   printf("  pgmoneta-walbridge [ -c CONFIG_FILE ] [ -u USERS_FILE ] [ -A ADMINS_FILE ] [ -D DIRECTORY ] [ -s SERVER ]\n");
   printf("\n");
   printf("Options:\n");
   printf("  -c, --config CONFIG_FILE  Set the path to the pgmoneta.conf file\n");
   printf("  -u, --users USERS_FILE    Set the path to the pgmoneta_users.conf file\n");
   printf("  -A, --admins ADMINS_FILE  Set the path to the pgmoneta_admins.conf file\n");
   printf("  -D, --directory DIRECTORY Set the directory containing all configuration files\n");
   printf("  -s, --server SERVER       Server index to use for WAL bridging (default 0)\n");
   printf("  -V, --version             Display version information\n");
   printf("  -?, --help                Display help\n");
}

static void
version(void)
{
   printf("pgmoneta-walbridge %s\n", VERSION);
   exit(0);
}

int
main(int argc, char** argv)
{
   int server = 0;
   int num_options = 0;
   int num_results = 0;
   int optind = 0;
   char* filepath = NULL;
   char* server_arg = NULL;
   char* config_arg = NULL;
   char* users_arg = NULL;
   char* admins_arg = NULL;
   char* directory_arg = NULL;
   int i = 0;
   cli_option options[] = {
      {"c", "config", true},
      {"u", "users", true},
      {"A", "admins", true},
      {"D", "directory", true},
      {"s", "server", true},
      {"V", "version", false},
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
      else if (pgmoneta_compare_string(optname, "s") || pgmoneta_compare_string(optname, "server"))
      {
         server_arg = optarg;
      }
      else if (pgmoneta_compare_string(optname, "c") || pgmoneta_compare_string(optname, "config"))
      {
         config_arg = optarg;
      }
      else if (pgmoneta_compare_string(optname, "u") || pgmoneta_compare_string(optname, "users"))
      {
         users_arg = optarg;
      }
      else if (pgmoneta_compare_string(optname, "A") || pgmoneta_compare_string(optname, "admins"))
      {
         admins_arg = optarg;
      }
      else if (pgmoneta_compare_string(optname, "D") || pgmoneta_compare_string(optname, "directory"))
      {
         directory_arg = optarg;
      }
      else if (pgmoneta_compare_string(optname, "V") || pgmoneta_compare_string(optname, "version"))
      {
         version();
      }
      else if (pgmoneta_compare_string(optname, "?") || pgmoneta_compare_string(optname, "help"))
      {
         usage();
         exit(0);
      }
   }

   if (server_arg != NULL)
   {
      char* end = NULL;
      long parsed = strtol(server_arg, &end, 10);
      if (end == NULL || *end != '\0' || parsed < 0)
      {
         errx(1, "Invalid server index: %s\n", server_arg);
      }
      server = (int)parsed;
   }

   return pgmoneta_walbridge_start(server, config_arg, users_arg, admins_arg, directory_arg, argv);
}
