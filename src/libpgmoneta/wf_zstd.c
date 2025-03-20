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
#include <pgmoneta.h>
#include <art.h>
#include <logging.h>
#include <utils.h>
#include <zstandard_compression.h>
#include <workers.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

static char* zstd_name(void);
static int zstd_execute_compress(char*, struct art*);
static int zstd_execute_uncompress(char*, struct art*);

struct workflow*
pgmoneta_create_zstd(bool compress)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->name = &zstd_name;
   wf->setup = &pgmoneta_common_setup;

   if (compress == true)
   {
      wf->execute = &zstd_execute_compress;
   }
   else
   {
      wf->execute = &zstd_execute_uncompress;
   }

   wf->teardown = &pgmoneta_common_teardown;
   wf->next = NULL;

   return wf;
}

static char*
zstd_name(void)
{
   return "ZSTD";
}

static int
zstd_execute_compress(char* name, struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double compression_zstd_elapsed_time;
   char* d = NULL;
   char* backup_base = NULL;
   char* backup_data = NULL;
   char* tarfile = NULL;
   int hours;
   int minutes;
   double seconds;
   char elapsed[128];
   int number_of_workers = 0;
   struct workers* workers = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   if (pgmoneta_log_is_enabled(PGMONETA_LOGGING_LEVEL_DEBUG1))
   {
      char* a = NULL;
      a = pgmoneta_art_to_string(nodes, FORMAT_TEXT, NULL, 0);
      pgmoneta_log_debug("(Tree)\n%s", a);
      free(a);
   }
   assert(nodes != NULL);
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("ZSTD (compress): %s/%s", config->servers[server].name, label);

   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);

   tarfile = (char*)pgmoneta_art_search(nodes, NODE_TARGET_FILE);

   if (tarfile == NULL)
   {
      number_of_workers = pgmoneta_get_number_of_workers(server);
      if (number_of_workers > 0)
      {
         pgmoneta_workers_initialize(number_of_workers, &workers);
      }

      backup_base = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_BASE);
      backup_data = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_DATA);

      pgmoneta_zstandardc_data(backup_data, workers);
      pgmoneta_zstandardc_tablespaces(backup_base, workers);

      if (number_of_workers > 0)
      {
         pgmoneta_workers_wait(workers);
         if (!workers->outcome)
         {
            goto error;
         }
         pgmoneta_workers_destroy(workers);
      }
   }
   else
   {
      d = pgmoneta_append(d, tarfile);
      d = pgmoneta_append(d, ".zstd");

      if (pgmoneta_exists(d))
      {
         pgmoneta_delete_file(d, NULL);
      }
      else
      {
         pgmoneta_log_debug("%s doesn't exists", d);
      }

      pgmoneta_zstandardc_file(tarfile, d);
   }

   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
   compression_zstd_elapsed_time = pgmoneta_compute_duration(start_t, end_t);

   hours = compression_zstd_elapsed_time / 3600;
   minutes = ((int)compression_zstd_elapsed_time % 3600) / 60;
   seconds = (int)compression_zstd_elapsed_time % 60 + (compression_zstd_elapsed_time - ((long)compression_zstd_elapsed_time));

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%.4f", hours, minutes, seconds);

   pgmoneta_log_debug("Compression: %s/%s (Elapsed: %s)", config->servers[server].name, label, &elapsed[0]);
   pgmoneta_update_info_double(backup_base, INFO_COMPRESSION_ZSTD_ELAPSED, compression_zstd_elapsed_time);

   free(d);

   return 0;

error:

   if (number_of_workers > 0)
   {
      pgmoneta_workers_destroy(workers);
   }

   free(d);

   return 1;
}

static int
zstd_execute_uncompress(char* name, struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   char* base = NULL;
   time_t decompress_time;
   int total_seconds;
   int hours;
   int minutes;
   int seconds;
   char elapsed[128];
   int number_of_workers = 0;
   struct workers* workers = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   if (pgmoneta_log_is_enabled(PGMONETA_LOGGING_LEVEL_DEBUG1))
   {
      char* a = NULL;
      a = pgmoneta_art_to_string(nodes, FORMAT_TEXT, NULL, 0);
      pgmoneta_log_debug("(Tree)\n%s", a);
      free(a);
   }
   assert(nodes != NULL);
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("ZSTD (decompress): %s/%s", config->servers[server].name, label);

   base = (char*)pgmoneta_art_search(nodes, NODE_TARGET_BASE);
   if (base == NULL)
   {
      base = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_BASE);
   }
   if (base == NULL)
   {
      base = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_DATA);
   }

   decompress_time = time(NULL);

   number_of_workers = pgmoneta_get_number_of_workers(server);
   if (number_of_workers > 0)
   {
      pgmoneta_workers_initialize(number_of_workers, &workers);
   }

   pgmoneta_zstandardd_directory(base, workers);

   if (number_of_workers > 0)
   {
      pgmoneta_workers_wait(workers);
      if (!workers->outcome)
      {
         goto error;
      }
      pgmoneta_workers_destroy(workers);
   }

   total_seconds = (int)difftime(time(NULL), decompress_time);
   hours = total_seconds / 3600;
   minutes = (total_seconds % 3600) / 60;
   seconds = total_seconds % 60;

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

   pgmoneta_log_debug("Decompress: %s/%s (Elapsed: %s)", config->servers[server].name, label, &elapsed[0]);

   return 0;

error:

   if (number_of_workers > 0)
   {
      pgmoneta_workers_destroy(workers);
   }

   return 1;
}
