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
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <cmd.h>
#include <configuration.h>
#include <logging.h>
#include <memory.h>
#include <migration.h>
#include <shmem.h>
#include <utils.h>
/* system */
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <libgen.h>
#include <time.h>

static bool match_opt(char* optname, char* optshort, char* optlong);
static void version(void);
static void usage(void);
static bool backup_exists(char* directory, char* backup_id);
static void get_workspace(char* base, char** workspace);

int
main(int argc, char** argv)
{
   int ret = 0;
   int num_options = 0;
   int num_results = 0;
   char* filepath = NULL;
   int optind = 0;
   char* backup_id = NULL;
   char* configuration_path = NULL;
   char* logfile = NULL;
   char* server_name = NULL;
   char* workspace = NULL;

   struct muse_configuration* config = NULL;

   cli_option options[] = {
      {"d", "dry-run", false},
      {"c", "config", true},
      {"b", "backup-id", true},
      {"L", "logfile", true},
      {"s", "server", true},
      {"V", "version", false},
      {"?", "help", false},
   };

   num_options = sizeof(options) / sizeof(options[0]);
   cli_result results[num_options];

   num_results = cmd_parse(argc, argv, options, num_options, results, num_options, false, &filepath, &optind);

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
      else if (match_opt(optname, "c", "config"))
      {
         configuration_path = optarg;
      }
      else if (match_opt(optname, "b", "backup-id"))
      {
         backup_id = optarg;
      }
      else if (match_opt(optname, "L", "logfile"))
      {
         logfile = optarg;
      }
      else if (match_opt(optname, "s", "server"))
      {
         server_name = optarg;
      }
      else if (match_opt(optname, "V", "version"))
      {
         version();
      }
      else if (match_opt(optname, "?", "help"))
      {
         usage();
         exit(0);
      }
   }

   // argument validation

   if (server_name == NULL)
   {
      errx(1, "Target server name needs to be specified");
   }

   if (configuration_path == NULL)
   {
      warnx("Default configuration path to %s", PGMONETA_MUSE_DEFAULT_CONFIG_FILE_PATH);
      configuration_path = PGMONETA_MUSE_DEFAULT_CONFIG_FILE_PATH;
   }

   ret = pgmoneta_validate_config_file(configuration_path);
   if (ret)
   {
      switch (ret)
      {
         case ENOENT:
            errx(1, "Configuration file not found or not a regular file: %s", configuration_path);
            break;

         case EACCES:
            errx(1, "Can't read configuration file: %s", configuration_path);
            break;

         case EINVAL:
            errx(1, "Configuration file contains binary data or invalid path: %s", configuration_path);
            break;

         default:
            errx(1, "Configuration file validation failed: %s", configuration_path);
      }
   }

   if (pgmoneta_create_shared_memory(sizeof(struct muse_configuration), HUGEPAGE_OFF, &shmem))
   {
      warnx("pgmoneta-muse: Failed to allocate shared memory. Check system resources and permissions.");
      exit(1);
   }

   pgmoneta_init_muse_configuration(shmem);

   if (pgmoneta_read_muse_configuration(shmem, configuration_path))
   {
      errx(1, "pgmoneta-muse: Failed to read configuration file: %s", configuration_path);
   }

   pgmoneta_validate_muse_configuration(shmem);

   config = (struct muse_configuration*)shmem;
   if (backup_id != NULL)
   {
      if (!backup_exists(config->source_data_dir, backup_id))
      {
         errx(1, "Unable to find backup %s at %s", backup_id, config->source_data_dir);
      }
   }
   else
   {
      errx(1, "Must specify the ending backup");
   }

   if (logfile)
   {
      config->common.log_type = PGMONETA_LOGGING_TYPE_FILE;
      memset(&config->common.log_path[0], 0, MISC_LENGTH);
      memcpy(&config->common.log_path[0], logfile, MIN((size_t)MISC_LENGTH - 1, strlen(logfile)));
   }

   get_workspace(config->workspace, &workspace);
   if (pgmoneta_mkdir(workspace))
   {
      errx(1, "pgmoneta-muse: Unable to create workspace");
   }

   if (pgmoneta_start_logging())
   {
      errx(1, "pgmoneta-muse: Unable to start logging");
   }

   if (pgmoneta_migrate(backup_id, server_name, workspace))
   {
      pgmoneta_log_error("Failed to migrate source directory");
   }
   else
   {
      pgmoneta_log_info("pgmoneta-muse: migration finished");
   }

   pgmoneta_stop_logging();
   pgmoneta_destroy_shared_memory(shmem, sizeof(struct muse_configuration));

   pgmoneta_delete_directory(workspace);

   free(workspace);
   return 0;
}

static bool
match_opt(char* optname, char* optshort, char* optlong)
{
   return pgmoneta_compare_string(optname, optshort) || pgmoneta_compare_string(optname, optlong);
}

static void
version(void)
{
   printf("pgmoneta-muse %s\n", VERSION);
   exit(0);
}

static void
usage(void)
{
   printf("pgmoneta-muse %s\n", VERSION);
   printf("  pgmoneta migrator\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgmoneta-muse {-s SERVER} {-b BACKUP_ID} [ -c CONFIG_FILE ]\n");
   printf("\n");
   printf("Options:\n");
   printf("  -c, --config CONFIG_FILE  Set the path to the pgmoneta_muse.conf file\n");
   printf("  -b, --backup-id BACKUP_ID The final backup in the backup chain, all backups in the chain will be migrated\n");
   printf("  -L, --logfile FILE        Set the log file\n");
   printf("  -s, --server SERVER       Set the target server name the backups correspond to post migration\n");
   printf("  -V, --version             Display version information\n");
   printf("  -?, --help                Display help\n");
   printf("\n");
   printf("pgmoneta: %s\n", PGMONETA_HOMEPAGE);
   printf("Report bugs: %s\n", PGMONETA_ISSUES);
}

static bool
backup_exists(char* directory, char* backup_id)
{
   char* path = NULL;
   bool exists = false;
   path = pgmoneta_append(path, directory);
   if (!pgmoneta_ends_with(path, "/"))
   {
      path = pgmoneta_append(path, "/");
   }
   path = pgmoneta_append(path, backup_id);
   exists = pgmoneta_exists(path);

   free(path);
   return exists;
}

static void
get_workspace(char* base, char** workspace)
{
   char* ws = NULL;
   *workspace = NULL;
   time_t curr_t;
   struct tm* time_info;
   char date_str[128];

   curr_t = time(NULL);
   memset(&date_str[0], 0, sizeof(date_str));
   time_info = localtime(&curr_t);

   strftime(&date_str[0], sizeof(date_str), "%Y%m%d%H%M%S", time_info);

   ws = pgmoneta_append(ws, base);
   if (!pgmoneta_ends_with(ws, "/"))
   {
      ws = pgmoneta_append(ws, "/");
   }
   ws = pgmoneta_append(ws, "muse_");
   ws = pgmoneta_append(ws, &date_str[0]);
   ws = pgmoneta_append(ws, "/");
   *workspace = ws;
}