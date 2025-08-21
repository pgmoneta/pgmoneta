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

/* pgmoneta */
#include <cmd.h>
#include <configuration.h>
#include <deque.h>
#include <logging.h>
#include <management.h>
#include <pgmoneta.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>
#include <wal.h>
#include <walfile.h>
#include <walfile/rmgr.h>

/* system */
#include <err.h>
#include <getopt.h>
#include <inttypes.h>
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
   printf("  pgmoneta-walinfo <file|directory>\n");
   printf("\n");
   printf("Options:\n");
   printf("  -c,  --config      Set the path to the pgmoneta_walinfo.conf file\n");
   printf("  -u,  --users       Set the path to the pgmoneta_users.conf file \n");
   printf("  -RT, --tablespaces Filter on tablspaces\n");
   printf("  -RD, --databases   Filter on databases\n");
   printf("  -RT, --relations   Filter on relations\n");
   printf("  -R,  --filter      Combination of -RT, -RD, -RR\n");
   printf("  -o,  --output      Output file\n");
   printf("  -F,  --format      Output format (raw, json)\n");
   printf("  -L,  --logfile     Set the log file\n");
   printf("  -q,  --quiet       No output only result\n");
   printf("       --color       Use colors (on, off)\n");
   printf("  -r,  --rmgr        Filter on a resource manager\n");
   printf("  -s,  --start       Filter on a start LSN\n");
   printf("  -e,  --end         Filter on an end LSN\n");
   printf("  -x,  --xid         Filter on an XID\n");
   printf("  -l,  --limit       Limit number of outputs\n");
   printf("  -v,  --verbose     Output result\n");
   printf("  -S,  --summary     Show a summary of WAL record counts grouped by resource manager\n");
   printf("  -V,  --version     Display version information\n");
   printf("  -m,  --mapping     Provide mappings file for OID translation\n");
   printf("  -t,  --translate   Translate OIDs to object names in XLOG records\n");
   printf("  -?,  --help        Display help\n");
   printf("\n");
   printf("pgmoneta: %s\n", PGMONETA_HOMEPAGE);
   printf("Report bugs: %s\n", PGMONETA_ISSUES);
}

int
main(int argc, char** argv)
{
   int loaded = 1;
   char* configuration_path = NULL;
   char* users_path = NULL;
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
   bool summary = false;
   enum value_type type = ValueString;
   size_t size;
   struct walinfo_configuration* config = NULL;
   bool enable_mapping = false;
   char* mappings_path = NULL;
   int ret;
   char* tablespaces = NULL;
   char* databases = NULL;
   char* relations = NULL;
   char* filters = NULL;
   bool filtering_enabled = false;
   char** included_objects = NULL;
   char** databases_list = NULL;
   char** tablespaces_list = NULL;
   char** relations_list = NULL;
   int databases_count = 0;
   int tablespaces_count = 0;
   int relations_count = 0;
   char** temp_list = NULL;
   int cnt = 0;
   int optind = 0;
   char* filepath;
   int num_results = 0;
   int num_options = 0;
   FILE* out = NULL;

   cli_option options[] = {
      {"c", "config", true},
      {"o", "output", true},
      {"F", "format", true},
      {"u", "users", true},
      {"RT", "tablespaces", true},
      {"RD", "databases", true},
      {"RR", "relations", true},
      {"R", "filter", true},
      {"m", "mapping", true},
      {"t", "translate", false},
      {"L", "logfile", true},
      {"q", "quiet", false},
      {"", "color", true},
      {"r", "rmgr", true},
      {"s", "start", true},
      {"e", "end", true},
      {"x", "xid", true},
      {"l", "limit", true},
      {"v", "verbose", false},
      {"S", "summary", false},
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
      else if (!strcmp(optname, "c") || !strcmp(optname, "config"))
      {
         configuration_path = optarg;
      }
      else if (!strcmp(optname, "o") || !strcmp(optname, "output"))
      {
         output = optarg;
      }
      else if (!strcmp(optname, "F") || !strcmp(optname, "format"))
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
      else if (!strcmp(optname, "L") || !strcmp(optname, "logfile"))
      {
         logfile = optarg;
      }
      else if (!strcmp(optname, "q") || !strcmp(optname, "quiet"))
      {
         quiet = true;
      }
      else if (!strcmp(optname, "color"))
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
      else if (!strcmp(optname, "r") || !strcmp(optname, "rmgr"))
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
      else if (!strcmp(optname, "s") || !strcmp(optname, "start"))
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
      else if (!strcmp(optname, "e") || !strcmp(optname, "end"))
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
      else if (!strcmp(optname, "x") || !strcmp(optname, "xid"))
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
      else if (!strcmp(optname, "l") || !strcmp(optname, "limit"))
      {
         limit = pgmoneta_atoi(optarg);
      }
      else if (!strcmp(optname, "m") || !strcmp(optname, "mapping"))
      {
         enable_mapping = true;
         mappings_path = optarg;
      }
      else if (!strcmp(optname, "t") || !strcmp(optname, "translate"))
      {
         enable_mapping = true;
      }
      else if (!strcmp(optname, "RT") || !strcmp(optname, "tablespaces"))
      {
         tablespaces = optarg;
         filtering_enabled = true;
      }
      else if (!strcmp(optname, "RD") || !strcmp(optname, "databases"))
      {
         databases = optarg;
         filtering_enabled = true;
      }
      else if (!strcmp(optname, "RR") || !strcmp(optname, "relations"))
      {
         relations = optarg;
         filtering_enabled = true;
      }
      else if (!strcmp(optname, "R") || !strcmp(optname, "filter"))
      {
         filters = optarg;
         filtering_enabled = true;
      }
      else if (!strcmp(optname, "u") || !strcmp(optname, "users"))
      {
         users_path = optarg;
      }
      else if (!strcmp(optname, "v") || !strcmp(optname, "verbose"))
      {
         verbose = true;
      }
      else if (!strcmp(optname, "S") || !strcmp(optname, "summmary"))
      {
         summary = true;
      }
      else if (!strcmp(optname, "V") || !strcmp(optname, "version"))
      {
         version();
         exit(0);
      }
      else if (!strcmp(optname, "?") || !strcmp(optname, "help"))
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

   pgmoneta_init_walinfo_configuration(shmem);
   config = (struct walinfo_configuration*)shmem;

   if (configuration_path != NULL)
   {
      if (pgmoneta_exists(configuration_path))
      {
         loaded = pgmoneta_read_walinfo_configuration(shmem, configuration_path);
      }

      if (loaded)
      {
         warnx("Configuration not found: %s", configuration_path);
      }
   }

   if (loaded && pgmoneta_exists(PGMONETA_WALINFO_DEFAULT_CONFIG_FILE_PATH))
   {
      loaded = pgmoneta_read_walinfo_configuration(shmem, PGMONETA_WALINFO_DEFAULT_CONFIG_FILE_PATH);
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

   if (pgmoneta_validate_walinfo_configuration())
   {
      goto error;
   }

   if (pgmoneta_start_logging())
   {
      exit(1);
   }

   if (users_path != NULL)
   {
      ret = pgmoneta_read_users_configuration(shmem, users_path);
      if (ret == 1)
      {
         warnx("pgmoneta: USERS configuration not found: %s", users_path);
         goto error;
      }
      else if (ret == 2)
      {
         warnx("pgmoneta: Invalid master key file");
         goto error;
      }
      else if (ret == 3)
      {
         warnx("pgmoneta: USERS: Too many users defined %d (max %d)", config->common.number_of_users, NUMBER_OF_USERS);
         goto error;
      }
      memcpy(&config->common.users_path[0], users_path, MIN(strlen(users_path), MAX_PATH - 1));
   }
   else
   {
      users_path = PGMONETA_DEFAULT_USERS_FILE_PATH;
      ret = pgmoneta_read_users_configuration(shmem, users_path);
      if (ret == 0)
      {
         memcpy(&config->common.users_path[0], users_path, MIN(strlen(users_path), MAX_PATH - 1));
      }
   }

   if (enable_mapping)
   {
      if (mappings_path != NULL)
      {
         if (pgmoneta_read_mappings_from_json(mappings_path) != 0)
         {
            pgmoneta_log_error("Failed to read mappings file");
            goto error;
         }
      }
      else
      {
         if (config->common.number_of_servers == 0)
         {
            pgmoneta_log_error("No servers defined, user should provide exactly one server in the configuration file");
            goto error;
         }

         if (pgmoneta_read_mappings_from_server(0) != 0)
         {
            pgmoneta_log_error("Failed to read mappings from server");
            goto error;
         }
      }
   }

   if (filtering_enabled)
   {
      if (enable_mapping)
      {
         if (filters != NULL)
         {
            if (pgmoneta_split(filters, &temp_list, &cnt, '/'))
            {
               pgmoneta_log_error("Failed to parse filters");
               goto error;
            }
            else
            {
               if (pgmoneta_split(temp_list[0], &tablespaces_list, &tablespaces_count, ','))
               {
                  pgmoneta_log_error("Failed to parse tablespaces to be included");
                  goto error;
               }

               if (pgmoneta_split(temp_list[1], &databases_list, &databases_count, ','))
               {
                  pgmoneta_log_error("Failed to parse databases to be included");
                  goto error;
               }

               if (pgmoneta_split(temp_list[2], &relations_list, &relations_count, ','))
               {
                  pgmoneta_log_error("Failed to parse relations to be included");
                  goto error;
               }

               if (temp_list)
               {
                  for (int i = 0; temp_list[i] != NULL; i++)
                  {
                     free(temp_list[i]);
                  }
                  free(temp_list);
               }
            }
         }

         if (databases != NULL)
         {
            if (pgmoneta_split(databases, &databases_list, &databases_count, ','))
            {
               pgmoneta_log_error("Failed to parse databases to be included");
               goto error;
            }
         }

         if (tablespaces != NULL)
         {
            if (pgmoneta_split(tablespaces, &tablespaces_list, &tablespaces_count, ','))
            {
               pgmoneta_log_error("Failed to parse tablespaces to be included");
               goto error;
            }
         }

         if (relations != NULL)
         {
            if (pgmoneta_split(relations, &relations_list, &relations_count, ','))
            {
               pgmoneta_log_error("Failed to parse relations to be included");
               goto error;
            }
         }

         char** merged_lists[4] = {0};
         int idx = 0;

         if (databases_list != NULL)
         {
            merged_lists[idx++] = databases_list;
         }

         if (tablespaces_list != NULL)
         {
            merged_lists[idx++] = tablespaces_list;
         }

         if (relations_list != NULL)
         {
            merged_lists[idx++] = relations_list;
         }

         merged_lists[idx] = NULL;

         if (pgmoneta_merge_string_arrays(merged_lists, &included_objects))
         {
            pgmoneta_log_error("Failed to merge include lists");
            goto error;
         }

         if (databases_list)
         {
            for (int i = 0; i < databases_count; i++)
            {
               free(databases_list[i]);
            }
            free(databases_list);
         }

         if (tablespaces_list)
         {
            for (int i = 0; i < tablespaces_count; i++)
            {
               free(tablespaces_list[i]);
            }
            free(tablespaces_list);
         }

         if (relations_list)
         {
            for (int i = 0; i < relations_count; i++)
            {
               free(relations_list[i]);
            }
            free(relations_list);
         }
      }
      else
      {
         pgmoneta_log_error("OID mappings are not loaded, please provide a mappings file or server credentials and enable translation (-t)");
         goto error;
      }
   }

   if (output == NULL)
   {
      out = stdout;
   }
   else
   {
      out = fopen(output, "w");
      color = false;
   }

   if (filepath != NULL)
   {
      partial_record = malloc(sizeof(struct partial_xlog_record));
      partial_record->data_buffer_bytes_read = 0;
      partial_record->xlog_record_bytes_read = 0;
      partial_record->xlog_record = NULL;
      partial_record->data_buffer = NULL;
      if (pgmoneta_is_directory(filepath))
      {
         if (pgmoneta_describe_walfiles_in_directory(filepath, type, out, quiet, color,
                                                     rms, start_lsn, end_lsn, xids, limit, summary, included_objects))
         {
            fprintf(stderr, "Error while reading/describing WAL directory\n");
            goto error;
         }
      }

      else if (pgmoneta_describe_walfile(filepath, type, out, quiet, color,
                                         rms, start_lsn, end_lsn, xids, limit, summary, included_objects))
      {
         fprintf(stderr, "Error while reading/describing WAL file\n");
         goto error;
      }
      if (partial_record->xlog_record != NULL)
      {
         free(partial_record->xlog_record);
      }
      if (partial_record->data_buffer != NULL)
      {
         free(partial_record->data_buffer);
      }
      free(partial_record);
      partial_record = NULL;
   }
   else
   {
      fprintf(stderr, "Missing <file> argument\n");
      usage();
      goto error;
   }

   if (summary)
   {
      int max_rmgr_name_length = 6; // Corresponds to the length of string `Record`
      int max_digit_length = 6; // Corresponds to the length of string `Number`
      int total_records = 0;
      char* number_string = NULL;
      char* rmgr_name = NULL;

      for (int i = 0; i < RM_MAX_ID + 1; i++)
      {
         if (!rmgr_summary_table[i].number_of_records)
         {
            continue;
         }
         max_rmgr_name_length = MAX(max_rmgr_name_length, (int)strlen(rmgr_summary_table[i].name));
         total_records += rmgr_summary_table[i].number_of_records;
      }
      number_string = pgmoneta_append_int(number_string, total_records);
      max_digit_length = MAX(max_digit_length, (int)strlen(number_string));
      free(number_string);
      number_string = NULL;

      if (type == ValueJSON)
      {
         fprintf(out, "{\"Summary\": {\n");
         for (int i = 0; i < RM_MAX_ID + 1; i++)
         {
            if (!rmgr_summary_table[i].number_of_records)
            {
               continue;
            }
            if (i == 0)
            {
               fprintf(out, "\"%s\": %d", rmgr_summary_table[i].name, rmgr_summary_table[i].number_of_records);
            }
            else
            {
               fprintf(out, ",\"%s\": %d", rmgr_summary_table[i].name, rmgr_summary_table[i].number_of_records);
            }

            /* Revert the state of rmgr_summary_table */
            rmgr_summary_table[i].number_of_records = 0;
         }
         fprintf(out, "},\n\"Total\": %d}\n", total_records);
      }
      else
      {
         /* Print the first row */
         if (max_digit_length == 6)
         {
            fprintf(out, "Number | Record%*s\n", max_rmgr_name_length - 6, " ");
         }
         else
         {
            fprintf(out, "%*sNumber | Record%*s\n", max_digit_length - 6, " ", max_rmgr_name_length - 6, " ");
         }
         /* Print horizontal line '---------' */
         for (int i = 0; i < max_digit_length + max_rmgr_name_length + 3; i++)
         {
            fprintf(out, "-");
         }
         fprintf(out, "\n");

         for (int i = 0; i < RM_MAX_ID + 1; i++)
         {
            if (!rmgr_summary_table[i].number_of_records)
            {
               continue;
            }
            number_string = pgmoneta_append_int(number_string, rmgr_summary_table[i].number_of_records);
            rmgr_name = rmgr_summary_table[i].name;
            fprintf(out, "%*s%s | %s%*s\n", max_digit_length - (int)strlen(number_string), " ",
                    number_string, rmgr_name, max_rmgr_name_length - (int)strlen(rmgr_name), " ");
            free(number_string);
            number_string = NULL;

            /* Revert the state of rmgr_summary_table */
            rmgr_summary_table[i].number_of_records = 0;
         }

         /* Print horizontal line '---------' */
         for (int i = 0; i < max_digit_length + max_rmgr_name_length + 3; i++)
         {
            fprintf(out, "-");
         }
         fprintf(out, "\n");
         /* Print total records */
         number_string = pgmoneta_append_int(number_string, total_records);
         if (max_digit_length == 6)
         {
            fprintf(out, "%*s%s | Total%*s\n", max_digit_length - (int)strlen(number_string), " ",
                    number_string, max_rmgr_name_length - 5, " ");
         }
         else
         {
            fprintf(out, "%s | Total%*s\n",
                    number_string, max_rmgr_name_length - 5, " ");
         }
         free(number_string);
         number_string = NULL;
      }
   }

   if (included_objects)
   {
      for (int i = 0; included_objects[i] != NULL; i++)
      {
         free(included_objects[i]);
      }
      free(included_objects);
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

   if (out != NULL)
   {
      fflush(out);
      fclose(out);
   }

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

   if (databases_list)
   {
      for (int i = 0; i < databases_count; i++)
      {
         free(databases_list[i]);
      }
      free(databases_list);
   }

   if (tablespaces_list)
   {
      for (int i = 0; i < tablespaces_count; i++)
      {
         free(tablespaces_list[i]);
      }
      free(tablespaces_list);
   }

   if (relations_list)
   {
      for (int i = 0; i < relations_count; i++)
      {
         free(relations_list[i]);
      }
      free(relations_list);
   }

   if (included_objects)
   {
      for (int i = 0; included_objects[i] != NULL; i++)
      {
         free(included_objects[i]);
      }
      free(included_objects);
   }

   if (temp_list)
   {
      for (int i = 0; temp_list[i] != NULL; i++)
      {
         free(temp_list[i]);
      }
      free(temp_list);
   }

   if (out)
   {
      fflush(out);
      fclose(out);
   }

   return 1;
}
