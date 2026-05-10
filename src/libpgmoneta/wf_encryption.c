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
#include <aes.h>
#include <compression.h>
#include <deque.h>
#include <logging.h>
#include <progress.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <stdlib.h>

static char* encryption_name(void);
static int encryption_execute(char*, struct art*);
static int decryption_execute(char*, struct art*);

struct workflow*
pgmoneta_encryption(bool encrypt)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->name = &encryption_name;
   wf->setup = &pgmoneta_common_setup;

   if (encrypt)
   {
      wf->execute = &encryption_execute;
   }
   else
   {
      wf->execute = &decryption_execute;
   }

   wf->teardown = &pgmoneta_common_teardown;
   wf->next = NULL;

   return wf;
}

static char*
encryption_name(void)
{
   return PHASE_NAME_ENCRYPTION;
}

static int
encryption_execute(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double encryption_elapsed_time;
   char* d = NULL;
   char* enc_file = NULL;
   char* backup_base = NULL;
   char* server_backup = NULL;
   char* compress_suffix = NULL;
   char* tarfile = NULL;
   int hours;
   int minutes;
   double seconds;
   char elapsed[128];
   int number_of_workers = 0;
   struct workers* workers = NULL;
   struct deque* excludes = NULL;
   struct main_configuration* config;
   struct backup* backup = NULL;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   pgmoneta_dump_art(nodes);

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP_BASE));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP_DATA));
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_BACKUP));
#endif

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);
   backup_base = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_BASE);
   server_backup = (char*)pgmoneta_art_search(nodes, NODE_SERVER_BACKUP);
   backup = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);
   tarfile = (char*)pgmoneta_art_search(nodes, NODE_TARGET_FILE);

   pgmoneta_log_debug("Encryption (execute): %s/%s", config->common.servers[server].name, label);

   if (tarfile == NULL)
   {
      number_of_workers = pgmoneta_get_number_of_workers(server);
      if (number_of_workers > 0)
      {
         pgmoneta_workers_initialize(number_of_workers, false, &workers);
      }

      if (pgmoneta_deque_create(true, &excludes))
      {
         goto error;
      }
      pgmoneta_deque_add(excludes, "backup.info", 0, ValueString);
      pgmoneta_deque_add(excludes, "backup.manifest", 0, ValueString);
      pgmoneta_deque_add(excludes, "backup.sha512", 0, ValueString);
      pgmoneta_deque_add(excludes, "backup.sha512.tmp", 0, ValueString);
      pgmoneta_deque_add(excludes, "backup.sha256", 0, ValueString);

      if (pgmoneta_is_progress_enabled(server))
      {
         int file_count = pgmoneta_count_files(backup_base);
         pgmoneta_progress_set_total(server, file_count);
      }

      if (pgmoneta_encrypt_directory(server, backup_base, workers, excludes))
      {
         goto error;
      }

      if (workers != NULL)
      {
         pgmoneta_workers_wait(workers);
         if (!workers->outcome)
         {
            goto error;
         }
      }
      pgmoneta_workers_destroy(workers);

      pgmoneta_deque_destroy(excludes);
      excludes = NULL;
   }
   else
   {
      const char* alg_suffix = NULL;

      pgmoneta_compression_get_suffix(config->compression_type, &alg_suffix);
      if (alg_suffix != NULL)
      {
         compress_suffix = (char*)alg_suffix;
      }
      else
      {
         compress_suffix = "";
      }

      d = pgmoneta_append(d, tarfile);
      d = pgmoneta_append(d, compress_suffix);
      d = pgmoneta_append(d, ".aes");
      if (pgmoneta_exists(d))
      {
         pgmoneta_delete_file(d, NULL);
      }
      else
      {
         pgmoneta_log_debug("%s doesn't exists", d);
      }

      enc_file = pgmoneta_append(enc_file, tarfile);
      enc_file = pgmoneta_append(enc_file, compress_suffix);
      if (pgmoneta_encrypt_file(enc_file, d, NULL))
      {
         goto error;
      }
   }

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   encryption_elapsed_time = pgmoneta_compute_duration(start_t, end_t);

   hours = encryption_elapsed_time / 3600;
   minutes = ((int)encryption_elapsed_time % 3600) / 60;
   seconds = (int)encryption_elapsed_time % 60 + (encryption_elapsed_time - ((long)encryption_elapsed_time));

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%.4f", hours, minutes, seconds);

   pgmoneta_log_debug("Encryption: %s/%s (Elapsed: %s)", config->common.servers[server].name, label, &elapsed[0]);

   backup->encryption_elapsed_time = encryption_elapsed_time;
   if (pgmoneta_save_info(server_backup, backup))
   {
      goto error;
   }

   pgmoneta_clear_aes_cache();
   free(d);
   free(enc_file);

   return 0;

error:

   if (excludes != NULL)
   {
      pgmoneta_deque_destroy(excludes);
   }

   if (number_of_workers > 0)
   {
      pgmoneta_workers_destroy(workers);
   }

   pgmoneta_clear_aes_cache();
   free(d);
   free(enc_file);

   return 1;
}

static int
decryption_execute(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   int ret = 0;
   char* label = NULL;
   char* base = NULL;
   time_t decrypt_time;
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
   pgmoneta_dump_art(nodes);

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("Decryption (execute): %s/%s", config->common.servers[server].name, label);

   base = (char*)pgmoneta_art_search(nodes, NODE_TARGET_BASE);
   if (base == NULL)
   {
      base = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_BASE);
   }
   if (base == NULL)
   {
      base = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_DATA);
   }

   decrypt_time = time(NULL);

   number_of_workers = pgmoneta_get_number_of_workers(server);
   if (number_of_workers > 0)
   {
      pgmoneta_workers_initialize(number_of_workers, false, &workers);
   }

   pgmoneta_decrypt_directory(base, workers, NULL);

   if (workers != NULL)
   {
      pgmoneta_workers_wait(workers);
      if (!workers->outcome)
      {
         ret = 1;
         goto error;
      }
   }
   pgmoneta_workers_destroy(workers);

   total_seconds = (int)difftime(time(NULL), decrypt_time);
   hours = total_seconds / 3600;
   minutes = (total_seconds % 3600) / 60;
   seconds = total_seconds % 60;

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

   pgmoneta_log_debug("Decryption: %s/%s (Elapsed: %s)", config->common.servers[server].name, label, &elapsed[0]);

   pgmoneta_clear_aes_cache();
   return 0;

error:

   if (workers != NULL)
   {
      pgmoneta_workers_destroy(workers);
   }

   pgmoneta_clear_aes_cache();
   return ret;
}
