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
#include <server.h>
#include <shmem.h>
#include <utils.h>
#include <wal.h>
#include <walfile.h>
#include <walfile/wal_reader.h>

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
   printf("  pgmoneta-walinfo <file>\n");
   printf("\n");
   printf("Options:\n");
   printf("  -c,   --config      Set the path to the pgmoneta_walinfo.conf file\n");
   printf("  -u,   --users       Set the path to the pgmoneta_users.conf file \n");
   printf("  -RT, --tablespaces  Filter on tablspaces\n");
   printf("  -RD, --databases    Filter on databases\n");
   printf("  -RT, --relations    Filter on relations\n");
   printf("  -R,   --filter      Combination of -RT, -RD, -RR\n");
   printf("  -o,   --output      Output file\n");
   printf("  -F,   --format      Output format (raw, json)\n");
   printf("  -L,   --logfile     Set the log file\n");
   printf("  -q,   --quiet       No output only result\n");
   printf("        --color       Use colors (on, off)\n");
   printf("  -r,   --rmgr        Filter on a resource manager\n");
   printf("  -s,   --start       Filter on a start LSN\n");
   printf("  -e,   --end         Filter on an end LSN\n");
   printf("  -x,   --xid         Filter on an XID\n");
   printf("  -l,   --limit       Limit number of outputs\n");
   printf("  -v,   --verbose     Output result\n");
   printf("  -V,   --version     Display version information\n");
   printf("  -m,   --mapping     Provide mappings file for OID translation\n");
   printf("  -t,   --translate   Translate OIDs to object names in XLOG records\n");
   printf("  -?,   --help        Display help\n");
   printf("\n");
   printf("pgmoneta: %s\n", PGMONETA_HOMEPAGE);
   printf("Report bugs: %s\n", PGMONETA_ISSUES);
}

static int compare_walfile(struct walfile* wf1, struct walfile* wf2);
static bool compare_long_page_headers(struct xlog_long_page_header_data* h1, struct xlog_long_page_header_data* h2);
static bool compare_deque(struct deque* dq1, struct deque* dq2, bool (*compare)(void*, void*));
static bool compare_xlog_page_header(void* a, void* b);
static bool compare_xlog_record(void* a, void* b);

static int
compare_walfile(struct walfile* wf1, struct walfile* wf2)
{
   // Handle NULL cases
   if (wf1 == NULL || wf2 == NULL)
   {
      return (wf1 == wf2) ? 0 : -1;
   }

   // Compare magic number (already in your code)
   // if (wf1->magic_number != wf2->magic_number)
   // {
   //    printf("Magic number mismatch: %u != %u\n", wf1->magic_number, wf2->magic_number);
   //    return -1;
   // }

   // Compare long page headers (long_phd)
   if (!compare_long_page_headers(wf1->long_phd, wf2->long_phd))
   {
      printf("Long page header mismatch\n");
      return -1;
   }

   // Compare page headers deque
   if (!compare_deque(wf1->page_headers, wf2->page_headers, compare_xlog_page_header))
   {
      printf("Page headers deque mismatch\n");
      return -1;
   }

   // Compare records deque
   if (!compare_deque(wf1->records, wf2->records, compare_xlog_record))
   {
      printf("Records deque mismatch\n");
      return -1;
   }

   return 0;
}

// Helper: Compare XLogLongPageHeaderData
static bool
compare_long_page_headers(struct xlog_long_page_header_data* h1, struct xlog_long_page_header_data* h2)
{
   if (h1 == NULL || h2 == NULL)
   {
      return (h1 == h2);
   }

   return (h1->std.xlp_magic == h2->std.xlp_magic &&
           h1->std.xlp_info == h2->std.xlp_info &&
           h1->std.xlp_tli == h2->std.xlp_tli &&
           h1->std.xlp_pageaddr == h2->std.xlp_pageaddr &&
           h1->xlp_seg_size == h2->xlp_seg_size &&
           h1->xlp_xlog_blcksz == h2->xlp_xlog_blcksz);
}

// Helper: Generic deque comparison with custom comparator
static bool
compare_deque(struct deque* dq1, struct deque* dq2, bool (*compare)(void*, void*))
{
   if (dq1 == NULL || dq2 == NULL)
   {
      return (dq1 == dq2);
   }

   if (pgmoneta_deque_size(dq1) != pgmoneta_deque_size(dq2))
   {
      printf("Deque sizes mismatch: %u != %u\n", pgmoneta_deque_size(dq1), pgmoneta_deque_size(dq2));
      return false;
   }

   struct deque_iterator* iter1 = NULL;
   struct deque_iterator* iter2 = NULL;
   bool equal = true;

   if (pgmoneta_deque_iterator_create(dq1, &iter1) != 0 ||
       pgmoneta_deque_iterator_create(dq2, &iter2) != 0)
   {
      equal = false;
      goto cleanup;
   }

   while (pgmoneta_deque_iterator_next(iter1) && pgmoneta_deque_iterator_next(iter2))
   {
      uintptr_t data1 = iter1->value->data;
      uintptr_t data2 = iter2->value->data;

      if (!compare((void*) data1, (void*) data2))
      {
         printf("Deque elements mismatch: %p != %p\n", (void*)data1, (void*)data2);
         equal = false;
         goto cleanup;
      }
   }

   // Ensure both iterators are exhausted
   if (pgmoneta_deque_iterator_next(iter1) || pgmoneta_deque_iterator_next(iter2))
   {
      equal = false;
   }

cleanup:
   pgmoneta_deque_iterator_destroy(iter1);
   pgmoneta_deque_iterator_destroy(iter2);
   return equal;
}

// Helper: Compare individual XLogPageHeaderData
static bool
compare_xlog_page_header(void* a, void* b)
{
   struct xlog_page_header_data* ph1 = (struct xlog_page_header_data*)a;
   struct xlog_page_header_data* ph2 = (struct xlog_page_header_data*)b;

   return (ph1->xlp_magic == ph2->xlp_magic &&
           ph1->xlp_info == ph2->xlp_info &&
           ph1->xlp_tli == ph2->xlp_tli &&
           ph1->xlp_pageaddr == ph2->xlp_pageaddr);
}

// Helper: Compare decoded XLogRecord and its data
static bool
compare_xlog_record(void* a, void* b)
{
   struct decoded_xlog_record* rec1 = (struct decoded_xlog_record*)a;
   struct decoded_xlog_record* rec2 = (struct decoded_xlog_record*)b;

   // Compare header
   if (memcmp(&rec1->header, &rec2->header, sizeof(struct xlog_record)) != 0)
   {
      printf("xlog_record header mismatch\n");
      return false;
   }

   if (rec1->main_data_len != rec2->main_data_len)
   {
      printf("xlog_record length mismatch\n");
      return false;
   }

   // Compare data payload
   if (rec1->main_data_len != 0 && memcmp(rec1->main_data, rec2->main_data, rec1->main_data_len) != 0)
   {
      printf("xlog_record data mismatch\n");
      return false;
   }

   return true;
}

void
test_walfile(char* path)
{
// 1. Prepare walfile structure
   struct walfile* wf = NULL;
   struct xlog_long_page_header_data* long_phd = NULL;
   struct deque* page_headers = NULL; // deque of xlog_page_header_data
   struct deque* records = NULL; // deque of decoded_xlog_record

// Allocate and initialize walfile
   wf = malloc(sizeof(struct walfile));
   memset(wf, 0, sizeof(struct walfile));

// Initialize long page header
   long_phd = malloc(sizeof(struct xlog_long_page_header_data));
   long_phd->std.xlp_magic = 0xD116;
   long_phd->std.xlp_info = 0;
   long_phd->std.xlp_tli = 1;
   long_phd->std.xlp_pageaddr = 0x0000000100000001;
   long_phd->std.xlp_rem_len = 0;
   long_phd->xlp_xlog_blcksz = DEFAULT_WAL_SEGZ_BYTES;   // 16MB block size
   long_phd->xlp_seg_size = 1234;
   wf->long_phd = long_phd;

   // Initialize page headers deque with sample data
   if (pgmoneta_deque_create(false, &page_headers))
   {
      errx(1, "Error creating page headers deque\n");
      return;
   }

   struct xlog_page_header_data* ph = malloc(sizeof(struct xlog_page_header_data));
   ph->xlp_magic = XLOG_PAGE_MAGIC;
   ph->xlp_info = 0;
   ph->xlp_tli = 1;
   ph->xlp_pageaddr = 0x0000000100000001;
   ph->xlp_rem_len = 0;
   pgmoneta_deque_add(page_headers, NULL, (uintptr_t)ph, ValueRef);
   wf->page_headers = page_headers;

   // Initialize records deque with a sample record
   if (pgmoneta_deque_create(false, &records))
   {
      errx(1, "Error creating records deque\n");
      return;
   }

   struct decoded_xlog_record* rec = calloc(1, sizeof(struct decoded_xlog_record));
   rec->header.xl_rmid = 0;                              // Sample resource manager (XLOG)
   rec->header.xl_tot_len = SIZE_OF_XLOG_RECORD;
   rec->lsn = 0x0000000100000001;
   rec->partial = false;
   char* temp = "Sample data for the main data section";
   rec->main_data_len = strlen(temp);
   rec->main_data = malloc(rec->main_data_len);
   memcpy(rec->main_data, temp, rec->main_data_len);
   pgmoneta_deque_add(records, NULL, (uintptr_t)rec, ValueRef);
   wf->records = records;

   printf("Walfile structure prepared\n");

   // 2. Write this structure to disk
   if (pgmoneta_write_walfile(wf, 0, path))
   {
      errx(1, "Error writing walfile to disk\n");
      return;
   }

   printf("Walfile written to disk\n");

   // 3. Read the walfile from disk
   struct walfile* read_wf = NULL;
   if (pgmoneta_read_walfile(0, path, &read_wf))
   {
      errx(1, "Error reading walfile from disk\n");
      return;
   }

   printf("Walfile read from disk\n");

   // 4. Validate the read data against the original walfile structure
   if (compare_walfile(wf, read_wf) != 0)
   {
      errx(1, "Walfile data mismatch\n");
      return;
   }

   printf("Walfile data match\n");
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
      {"V", "version", false},
      {"?", "help", false},
   };

   num_options = sizeof(options) / sizeof(options[0]);
   cli_result results[num_options];

   // if (argc < 2)
   // {
   //    usage();
   //    goto error;
   // }

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

   if (pgmoneta_validate_walinfo_configuration(shmem))
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

   pgmoneta_memory_init();
   pgmoneta_server_info(0);

   char* p = "/home/pgmoneta/00000001000000000000001D";
   test_walfile(p);
   return 0;

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

   if (filepath != NULL)
   {
      if (pgmoneta_describe_walfile(filepath, type, output, quiet, color,
                                    rms, start_lsn, end_lsn, xids, limit, included_objects))
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

   return 1;
}