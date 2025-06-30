/*
 * Copyright (C) 2025 The pgexporter community
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
#include <configuration.h>

#include <info.h>
#include <logging.h>
#include <pgmoneta.h>
#include <shmem.h>
#include <utils.h>
#include <yaml_utils.h>

/* system */
#include <err.h>
#include <stdio.h>

static void
usage(void)
{
   printf("pgmoneta-walfilter %s\n", VERSION);
   printf("  Command line utility to read and filter Write-Ahead Log (WAL) files based on user-defined criteria\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgmoneta-walfilter <yaml_config_file>\n");
   printf("\n");
   printf("pgmoneta: %s\n", PGMONETA_HOMEPAGE);
   printf("Report bugs: %s\n", PGMONETA_ISSUES);
}

int
main(int argc, char* argv[])
{
   if (argc != 2)
   {
      usage();
      return 1;
   }

   config_t yaml_config;
   struct walinfo_configuration* config = NULL;
   char* logfile = NULL;
   int file_count = 0;
   char** files = NULL;
   char* file_path = malloc(MAX_PATH);
   char* wal_files_path = NULL;
   int loaded = 1;
   char* configuration_path = NULL;
   size_t size;
   struct backup* backup = NULL;
   char* backup_label = NULL;
   char* parent_dir = NULL;
   char* last_slash = NULL;

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
      goto error;
   }

   if (pgmoneta_parse_yaml_config(argv[1], &yaml_config) != 0)
   {
      pgmoneta_log_error("Failed to parse configuration\n");
      goto error;
   }

   last_slash = strrchr(yaml_config.source_dir, '/');
   if (last_slash != NULL && *(last_slash + 1) != '\0')
   {
      backup_label = last_slash + 1;
   }
   else
   {
      backup_label = yaml_config.source_dir;
   }

   // Create a new path that points to the parent directory of yaml_config.source_dir
   last_slash = NULL;

   parent_dir = strdup(yaml_config.source_dir);
   if (parent_dir != NULL)
   {
      last_slash = strrchr(parent_dir, '/');
      if (last_slash != NULL && last_slash != parent_dir)
      {
         *last_slash = '\0';
      }
      else if (last_slash == parent_dir)
      {
         // The path is like "/foo", so keep "/"
         parent_dir[1] = '\0';
      }
      // else: no slash found, parent_dir stays as is (probably ".")
   }

   parent_dir = pgmoneta_append(parent_dir, "/");

   if (pgmoneta_load_info(parent_dir, backup_label, &backup))
   {
      pgmoneta_log_error("Failed to load backup information from %s\n", backup_label);
      goto error;
   }

   printf("end_lsn_hi32 = %u, end_lsn_lo32 = %u\n", backup->end_lsn_hi32, backup->end_lsn_lo32);

   wal_files_path = pgmoneta_append(NULL, yaml_config.source_dir);
   wal_files_path = pgmoneta_append(wal_files_path, "/data/pg_wal");

   if (pgmoneta_get_wal_files(wal_files_path, &file_count, &files))
   {
      pgmoneta_log_error("Failed to get WAL files from %s\n", wal_files_path);
      goto error;
   }

   for (int i = 0; i < file_count; i++)
   {
      snprintf(file_path, MAX_PATH, "%s/%s", wal_files_path, files[i]);
      printf("Processing WAL file: %s\n", file_path);
   }

   if (parent_dir != NULL)
   {
      free(parent_dir);
   }
   if (file_path != NULL)
   {
      free(file_path);
   }
   if (wal_files_path != NULL)
   {
      free(wal_files_path);
   }
   if (backup != NULL)
   {
      free(backup);
   }
   if (files)
   {
      for (int i = 0; i < file_count; i++)
      {
         free(files[i]);
      }
      free(files);
   }

   cleanup_config(&yaml_config);
   return 0;

error:
   if (parent_dir != NULL)
   {
      free(parent_dir);
   }
   if (file_path != NULL)
   {
      free(file_path);
   }
   if (wal_files_path != NULL)
   {
      free(wal_files_path);
   }
   if (backup != NULL)
   {
      free(backup);
   }
   if (files)
   {
      for (int i = 0; i < file_count; i++)
      {
         free(files[i]);
      }
      free(files);
   }

   cleanup_config(&yaml_config);   return 1;
}
