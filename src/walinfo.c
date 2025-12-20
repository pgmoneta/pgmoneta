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
#include <stddef.h>
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

/* Column widths for WAL statistics table */
#define COL_WIDTH_COUNT         9
#define COL_WIDTH_COUNT_PCT     8
#define COL_WIDTH_RECORD_SIZE   14
#define COL_WIDTH_RECORD_PCT    8
#define COL_WIDTH_FPI_SIZE      10
#define COL_WIDTH_FPI_PCT       8
#define COL_WIDTH_COMBINED_SIZE 14
#define COL_WIDTH_COMBINED_PCT  10

/**
 * Center-align a string within a given width
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param src Source string to center
 * @param width Total width for alignment
 */
static void
center_align(char* dest, size_t dest_size, const char* src, int width)
{
   int src_len = strlen(src);
   int padding = (width - src_len) / 2;
   int written = 0;

   if (width >= (int)dest_size)
   {
      width = dest_size - 1;
   }

   for (int i = 0; i < padding && written < width; i++, written++)
   {
      dest[written] = ' ';
   }

   for (int i = 0; i < src_len && written < width; i++, written++)
   {
      dest[written] = src[i];
   }

   while (written < width)
   {
      dest[written++] = ' ';
   }

   dest[written] = '\0';
}

/**
 * Right-align a string within a given width
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param src Source string to right-align
 * @param width Total width for alignment
 */
static void
right_align(char* dest, size_t dest_size, const char* src, int width)
{
   int src_len = strlen(src);
   int padding = width - src_len;
   int written = 0;

   if (width >= (int)dest_size)
   {
      width = dest_size - 1;
   }

   if (padding < 0)
   {
      padding = 0;
   }

   for (int i = 0; i < padding && written < width; i++, written++)
   {
      dest[written] = ' ';
   }

   for (int i = 0; i < src_len && written < width; i++, written++)
   {
      dest[written] = src[i];
   }

   dest[written] = '\0';
}

/**
 * Print WAL statistics summary
 * @param out Output file stream
 * @param type Output format type (ValueJSON or ValueString)
 */
static void
print_wal_statistics(FILE* out, enum value_type type)
{
   uint64_t total_count = 0;
   uint64_t total_record_size = 0;
   uint64_t total_fpi_size = 0;
   uint64_t total_combined_size = 0;
   int max_rmgr_name_length = 20;
   bool first = true;

   for (int i = 0; i < RM_MAX_ID + 1; i++)
   {
      if (rmgr_stats_table[i].count > 0)
      {
         total_count += rmgr_stats_table[i].count;
         total_record_size += rmgr_stats_table[i].record_size;
         total_fpi_size += rmgr_stats_table[i].fpi_size;
         total_combined_size += rmgr_stats_table[i].combined_size;
         max_rmgr_name_length = MAX(max_rmgr_name_length, (int)strlen(rmgr_stats_table[i].name));
      }
   }

   if (type == ValueJSON)
   {
      fprintf(out, "{\"wal_stats\": [\n");
      for (int i = 0; i < RM_MAX_ID + 1; i++)
      {
         if (rmgr_stats_table[i].count == 0)
         {
            continue;
         }

         double count_pct = total_count > 0 ? (100.0 * rmgr_stats_table[i].count) / total_count : 0.0;
         double rec_size_pct = total_record_size > 0 ? (100.0 * rmgr_stats_table[i].record_size) / total_record_size : 0.0;
         double fpi_size_pct = total_fpi_size > 0 ? (100.0 * rmgr_stats_table[i].fpi_size) / total_fpi_size : 0.0;
         double combined_size_pct = total_combined_size > 0 ? (100.0 * rmgr_stats_table[i].combined_size) / total_combined_size : 0.0;

         if (!first)
         {
            fprintf(out, ",\n");
         }
         first = false;

         fprintf(out, "  {\n");
         fprintf(out, "    \"resource_manager\": \"%s\",\n", rmgr_stats_table[i].name);
         fprintf(out, "    \"count\": %" PRIu64 ",\n", rmgr_stats_table[i].count);
         fprintf(out, "    \"count_percentage\": %.2f,\n", count_pct);
         fprintf(out, "    \"record_size\": %" PRIu64 ",\n", rmgr_stats_table[i].record_size);
         fprintf(out, "    \"record_size_percentage\": %.2f,\n", rec_size_pct);
         fprintf(out, "    \"fpi_size\": %" PRIu64 ",\n", rmgr_stats_table[i].fpi_size);
         fprintf(out, "    \"fpi_size_percentage\": %.2f,\n", fpi_size_pct);
         fprintf(out, "    \"combined_size\": %" PRIu64 ",\n", rmgr_stats_table[i].combined_size);
         fprintf(out, "    \"combined_size_percentage\": %.2f\n", combined_size_pct);
         fprintf(out, "  }");
      }
      fprintf(out, "\n]}\n");
   }
   else
   {
      char count_header[COL_WIDTH_COUNT + 1], count_pct_header[COL_WIDTH_COUNT_PCT + 1];
      char rec_size_header[COL_WIDTH_RECORD_SIZE + 1], rec_pct_header[COL_WIDTH_RECORD_PCT + 1];
      char fpi_size_header[COL_WIDTH_FPI_SIZE + 1], fpi_pct_header[COL_WIDTH_FPI_PCT + 1];
      char comb_size_header[COL_WIDTH_COMBINED_SIZE + 1], comb_pct_header[COL_WIDTH_COMBINED_PCT + 1];

      center_align(count_header, sizeof(count_header), "Count", COL_WIDTH_COUNT);
      center_align(count_pct_header, sizeof(count_pct_header), "Count %", COL_WIDTH_COUNT_PCT);
      center_align(rec_size_header, sizeof(rec_size_header), "Record Size", COL_WIDTH_RECORD_SIZE);
      center_align(rec_pct_header, sizeof(rec_pct_header), "Record %", COL_WIDTH_RECORD_PCT);
      center_align(fpi_size_header, sizeof(fpi_size_header), "FPI Size", COL_WIDTH_FPI_SIZE);
      center_align(fpi_pct_header, sizeof(fpi_pct_header), "FPI %", COL_WIDTH_FPI_PCT);
      center_align(comb_size_header, sizeof(comb_size_header), "Combined Size", COL_WIDTH_COMBINED_SIZE);
      center_align(comb_pct_header, sizeof(comb_pct_header), "Combined %", COL_WIDTH_COMBINED_PCT);

      fprintf(out, "%-*s | %s | %s | %s | %s | %s | %s | %s | %s |\n",
              max_rmgr_name_length, "Resource Manager",
              count_header, count_pct_header, rec_size_header, rec_pct_header,
              fpi_size_header, fpi_pct_header, comb_size_header, comb_pct_header);

      int total_width = max_rmgr_name_length + COL_WIDTH_COUNT + COL_WIDTH_COUNT_PCT +
                        COL_WIDTH_RECORD_SIZE + COL_WIDTH_RECORD_PCT + COL_WIDTH_FPI_SIZE +
                        COL_WIDTH_FPI_PCT + COL_WIDTH_COMBINED_SIZE + COL_WIDTH_COMBINED_PCT + 24;
      for (int i = 0; i < total_width; i++)
      {
         fprintf(out, "-");
      }
      fprintf(out, "\n");

      for (int i = 0; i < RM_MAX_ID + 1; i++)
      {
         if (rmgr_stats_table[i].count == 0)
         {
            continue;
         }

         double count_pct = total_count > 0 ? (100.0 * rmgr_stats_table[i].count) / total_count : 0.0;
         double rec_size_pct = total_record_size > 0 ? (100.0 * rmgr_stats_table[i].record_size) / total_record_size : 0.0;
         double fpi_size_pct = total_fpi_size > 0 ? (100.0 * rmgr_stats_table[i].fpi_size) / total_fpi_size : 0.0;
         double combined_size_pct = total_combined_size > 0 ? (100.0 * rmgr_stats_table[i].combined_size) / total_combined_size : 0.0;

         char count_str[COL_WIDTH_COUNT + 1], count_pct_str[COL_WIDTH_COUNT_PCT + 1];
         char rec_size_str[COL_WIDTH_RECORD_SIZE + 1], rec_pct_str[COL_WIDTH_RECORD_PCT + 1];
         char fpi_size_str[COL_WIDTH_FPI_SIZE + 1], fpi_pct_str[COL_WIDTH_FPI_PCT + 1];
         char comb_size_str[COL_WIDTH_COMBINED_SIZE + 1], comb_pct_str[COL_WIDTH_COMBINED_PCT + 1];
         char temp_str[64];

         snprintf(temp_str, sizeof(temp_str), "%" PRIu64, rmgr_stats_table[i].count);
         right_align(count_str, sizeof(count_str), temp_str, COL_WIDTH_COUNT);

         snprintf(temp_str, sizeof(temp_str), "%.2f%%", count_pct);
         right_align(count_pct_str, sizeof(count_pct_str), temp_str, COL_WIDTH_COUNT_PCT);

         snprintf(temp_str, sizeof(temp_str), "%" PRIu64, rmgr_stats_table[i].record_size);
         right_align(rec_size_str, sizeof(rec_size_str), temp_str, COL_WIDTH_RECORD_SIZE);

         snprintf(temp_str, sizeof(temp_str), "%.2f%%", rec_size_pct);
         right_align(rec_pct_str, sizeof(rec_pct_str), temp_str, COL_WIDTH_RECORD_PCT);

         snprintf(temp_str, sizeof(temp_str), "%" PRIu64, rmgr_stats_table[i].fpi_size);
         right_align(fpi_size_str, sizeof(fpi_size_str), temp_str, COL_WIDTH_FPI_SIZE);

         snprintf(temp_str, sizeof(temp_str), "%.2f%%", fpi_size_pct);
         right_align(fpi_pct_str, sizeof(fpi_pct_str), temp_str, COL_WIDTH_FPI_PCT);

         snprintf(temp_str, sizeof(temp_str), "%" PRIu64, rmgr_stats_table[i].combined_size);
         right_align(comb_size_str, sizeof(comb_size_str), temp_str, COL_WIDTH_COMBINED_SIZE);

         snprintf(temp_str, sizeof(temp_str), "%.2f%%", combined_size_pct);
         right_align(comb_pct_str, sizeof(comb_pct_str), temp_str, COL_WIDTH_COMBINED_PCT);

         fprintf(out, "%-*s | %s | %s | %s | %s | %s | %s | %s | %s |\n",
                 max_rmgr_name_length, rmgr_stats_table[i].name,
                 count_str, count_pct_str, rec_size_str, rec_pct_str,
                 fpi_size_str, fpi_pct_str, comb_size_str, comb_pct_str);
      }

      for (int i = 0; i < total_width; i++)
      {
         fprintf(out, "-");
      }
      fprintf(out, "\n");

      char count_str[COL_WIDTH_COUNT + 1], count_pct_str[COL_WIDTH_COUNT_PCT + 1];
      char rec_size_str[COL_WIDTH_RECORD_SIZE + 1], rec_pct_str[COL_WIDTH_RECORD_PCT + 1];
      char fpi_size_str[COL_WIDTH_FPI_SIZE + 1], fpi_pct_str[COL_WIDTH_FPI_PCT + 1];
      char comb_size_str[COL_WIDTH_COMBINED_SIZE + 1], comb_pct_str[COL_WIDTH_COMBINED_PCT + 1];
      char temp_str[64];

      snprintf(temp_str, sizeof(temp_str), "%" PRIu64, total_count);
      right_align(count_str, sizeof(count_str), temp_str, COL_WIDTH_COUNT);

      snprintf(temp_str, sizeof(temp_str), "%.2f%%", 100.0);
      right_align(count_pct_str, sizeof(count_pct_str), temp_str, COL_WIDTH_COUNT_PCT);

      snprintf(temp_str, sizeof(temp_str), "%" PRIu64, total_record_size);
      right_align(rec_size_str, sizeof(rec_size_str), temp_str, COL_WIDTH_RECORD_SIZE);

      snprintf(temp_str, sizeof(temp_str), "%.2f%%", 100.0);
      right_align(rec_pct_str, sizeof(rec_pct_str), temp_str, COL_WIDTH_RECORD_PCT);

      snprintf(temp_str, sizeof(temp_str), "%" PRIu64, total_fpi_size);
      right_align(fpi_size_str, sizeof(fpi_size_str), temp_str, COL_WIDTH_FPI_SIZE);

      snprintf(temp_str, sizeof(temp_str), "%.2f%%", 100.0);
      right_align(fpi_pct_str, sizeof(fpi_pct_str), temp_str, COL_WIDTH_FPI_PCT);

      snprintf(temp_str, sizeof(temp_str), "%" PRIu64, total_combined_size);
      right_align(comb_size_str, sizeof(comb_size_str), temp_str, COL_WIDTH_COMBINED_SIZE);

      snprintf(temp_str, sizeof(temp_str), "%.2f%%", 100.0);
      right_align(comb_pct_str, sizeof(comb_pct_str), temp_str, COL_WIDTH_COMBINED_PCT);

      fprintf(out, "%-*s | %s | %s | %s | %s | %s | %s | %s | %s |\n",
              max_rmgr_name_length, "Total",
              count_str, count_pct_str, rec_size_str, rec_pct_str,
              fpi_size_str, fpi_pct_str, comb_size_str, comb_pct_str);
   }

   for (int i = 0; i < RM_MAX_ID + 1; i++)
   {
      rmgr_stats_table[i].count = 0;
      rmgr_stats_table[i].record_size = 0;
      rmgr_stats_table[i].fpi_size = 0;
      rmgr_stats_table[i].combined_size = 0;
   }
}

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
   printf("  -S,  --summary     Show detailed WAL statistics including counts, sizes, and percentages by resource manager\n");
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
            start_lsn = strtoull(optarg, NULL, 10); // Assuming optarg is a decimal number
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
            end_lsn = strtoull(optarg, NULL, 10); // Assuming optarg is a decimal number
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
      else if (!strcmp(optname, "S") || !strcmp(optname, "summary"))
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
      if (!pgmoneta_exists(configuration_path))
      {
         errx(1, "Configuration file not found: %s", configuration_path);
      }

      if (!pgmoneta_is_file(configuration_path))
      {
         errx(1, "Configuration path is not a file: %s", configuration_path);
      }

      if (access(configuration_path, R_OK) != 0)
      {
         errx(1, "Can't read configuration file: %s", configuration_path);
      }

      int cfg_ret = pgmoneta_validate_config_file(configuration_path);
      if (cfg_ret == 4)
      {
         errx(1, "Configuration file contains binary data: %s", configuration_path);
      }
      else if (cfg_ret != 0)
      {
         goto error;
      }

      loaded = pgmoneta_read_walinfo_configuration(shmem, configuration_path);

      if (loaded)
      {
         errx(1, "Failed to read configuration file: %s", configuration_path);
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
         memcpy(&config->common.log_path[0], logfile, MIN((size_t)MISC_LENGTH - 1, strlen(logfile)));
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
      memcpy(&config->common.users_path[0], users_path, MIN(strlen(users_path), (size_t)MAX_PATH - 1));
   }
   else
   {
      users_path = PGMONETA_DEFAULT_USERS_FILE_PATH;
      ret = pgmoneta_read_users_configuration(shmem, users_path);
      if (ret == 0)
      {
         memcpy(&config->common.users_path[0], users_path, MIN(strlen(users_path), (size_t)MAX_PATH - 1));
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
      print_wal_statistics(out, type);
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
