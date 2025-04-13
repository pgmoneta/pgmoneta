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
#include <bzip2_compression.h>
#include <logging.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static char* bzip2_name(void);
static int bzip2_execute_compress(char*, struct art*);
static int bzip2_execute_uncompress(char*, struct art*);

struct workflow*
pgmoneta_create_bzip2(bool compress)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->name = &bzip2_name;
   wf->setup = &pgmoneta_common_setup;

   if (compress == true)
   {
      wf->execute = &bzip2_execute_compress;
   }
   else
   {
      wf->execute = &bzip2_execute_uncompress;
   }

   wf->teardown = &pgmoneta_common_teardown;
   wf->next = NULL;

   return wf;
}

static char*
bzip2_name(void)
{
   return "BZip2";
}

static int
bzip2_execute_compress(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   int ret = 0;
   struct timespec start_t;
   struct timespec end_t;
   double compression_bzip2_elapsed_time;
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

   pgmoneta_log_debug("BZip2 (compress): %s/%s", config->common.servers[server].name, label);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

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

      pgmoneta_bzip2_data(backup_data, workers);
      pgmoneta_bzip2_tablespaces(backup_base, workers);

      if (number_of_workers > 0)
      {
         pgmoneta_workers_wait(workers);
         if (!workers->outcome)
         {
            ret = 1;
         }
         pgmoneta_workers_destroy(workers);
      }
   }
   else
   {
      d = pgmoneta_append(d, tarfile);
      d = pgmoneta_append(d, ".bz2");

      if (pgmoneta_exists(d))
      {
         if (pgmoneta_exists(d))
         {
            pgmoneta_delete_file(d, NULL);
         }
         else
         {
            pgmoneta_log_debug("%s doesn't exists", d);
         }
      }

      ret = pgmoneta_bzip2_file(tarfile, d);
   }

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   compression_bzip2_elapsed_time = pgmoneta_compute_duration(start_t, end_t);

   hours = compression_bzip2_elapsed_time / 3600;
   minutes = ((int)compression_bzip2_elapsed_time % 3600) / 60;
   seconds = (int)compression_bzip2_elapsed_time % 60 + (compression_bzip2_elapsed_time - ((long)compression_bzip2_elapsed_time));

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%.4f", hours, minutes, seconds);

   pgmoneta_log_debug("Compression: %s/%s (Elapsed: %s)", config->common.servers[server].name, label, &elapsed[0]);

   pgmoneta_update_info_double(backup_base, INFO_COMPRESSION_BZIP2_ELAPSED, compression_bzip2_elapsed_time);

   free(d);

   return ret;
}

static int
bzip2_execute_uncompress(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   int ret = 0;
   char* base = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double decompression_bzip2_elapsed_time;
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

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("BZip2 (uncompress): %s/%s", config->common.servers[server].name, label);

   base = (char*)pgmoneta_art_search(nodes, NODE_TARGET_BASE);
   if (base == NULL)
   {
      base = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_BASE);
   }
   if (base == NULL)
   {
      base = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_DATA);
   }

   number_of_workers = pgmoneta_get_number_of_workers(server);
   if (number_of_workers > 0)
   {
      pgmoneta_workers_initialize(number_of_workers, &workers);
   }

   ret = pgmoneta_bunzip2_data(base, workers);

   if (number_of_workers > 0)
   {
      pgmoneta_workers_wait(workers);
      if (!workers->outcome)
      {
         ret = 1;
      }
      pgmoneta_workers_destroy(workers);
   }

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   decompression_bzip2_elapsed_time = pgmoneta_compute_duration(start_t, end_t);

   hours = decompression_bzip2_elapsed_time / 3600;
   minutes = ((int)decompression_bzip2_elapsed_time % 3600) / 60;
   seconds = (int)decompression_bzip2_elapsed_time % 60 + (decompression_bzip2_elapsed_time - ((long)decompression_bzip2_elapsed_time));

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%.4f", hours, minutes, seconds);

   pgmoneta_log_debug("Decompress: %s/%s (Elapsed: %s)", config->common.servers[server].name, label, &elapsed[0]);

   return ret;
}
