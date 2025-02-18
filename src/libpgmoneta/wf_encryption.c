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
#include <aes.h>
#include <deque.h>
#include <info.h>
#include <link.h>
#include <logging.h>
#include <pgmoneta.h>
#include <utils.h>
#include <workers.h>
#include <workflow.h>

/* system */
#include <stdlib.h>

static int encryption_setup(int, char*, struct deque*);
static int encryption_execute(int, char*, struct deque*);
static int decryption_execute(int, char*, struct deque*);
static int encryption_teardown(int, char*, struct deque*);

struct workflow*
pgmoneta_encryption(bool encrypt)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->setup = &encryption_setup;

   if (encrypt)
   {
      wf->execute = &encryption_execute;
   }
   else
   {
      wf->execute = &decryption_execute;
   }

   wf->teardown = &encryption_teardown;
   wf->next = NULL;

   return wf;
}

static int
encryption_setup(int server, char* identifier, struct deque* nodes)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Encryption (setup): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   return 0;
}

static int
encryption_execute(int server, char* identifier, struct deque* nodes)
{
   struct timespec start_t;
   struct timespec end_t;
   double encryption_elapsed_time;
   char* d = NULL;
   char* enc_file = NULL;
   char* backup_base = NULL;
   char* backup_data = NULL;
   char* compress_suffix = NULL;
   char* tarfile = NULL;
   int hours;
   int minutes;
   double seconds;
   char elapsed[128];
   int number_of_workers = 0;
   struct workers* workers = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Encryption (execute): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   tarfile = (char*)pgmoneta_deque_get(nodes, NODE_TARFILE);

   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);

   if (tarfile == NULL)
   {
      number_of_workers = pgmoneta_get_number_of_workers(server);
      if (number_of_workers > 0)
      {
         pgmoneta_workers_initialize(number_of_workers, &workers);
      }

      backup_base = (char*)pgmoneta_deque_get(nodes, NODE_BACKUP_BASE);
      backup_data = (char*)pgmoneta_deque_get(nodes, NODE_BACKUP_DATA);

      if (pgmoneta_encrypt_data(backup_data, workers))
      {
         goto error;
      }
      if (pgmoneta_encrypt_tablespaces(backup_base, workers))
      {
         goto error;
      }

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
      switch (config->compression_type)
      {
         case COMPRESSION_CLIENT_GZIP:
         case COMPRESSION_SERVER_GZIP:
            compress_suffix = ".gz";
            break;
         case COMPRESSION_CLIENT_ZSTD:
         case COMPRESSION_SERVER_ZSTD:
            compress_suffix = ".zstd";
            break;
         case COMPRESSION_CLIENT_LZ4:
         case COMPRESSION_SERVER_LZ4:
            compress_suffix = ".lz4";
            break;
         case COMPRESSION_CLIENT_BZIP2:
            compress_suffix = ".bz2";
            break;
         case COMPRESSION_NONE:
            compress_suffix = "";
            break;
         default:
            pgmoneta_log_error("encryption_execute: Unknown compression type");
            break;
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
      if (pgmoneta_encrypt_file(enc_file, d))
      {
         goto error;
      }
   }

   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
   encryption_elapsed_time = pgmoneta_compute_duration(start_t, end_t);

   hours = encryption_elapsed_time / 3600;
   minutes = ((int)encryption_elapsed_time % 3600) / 60;
   seconds = (int)encryption_elapsed_time % 60 + (encryption_elapsed_time - ((long)encryption_elapsed_time));

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%.4f", hours, minutes, seconds);

   pgmoneta_log_debug("Encryption: %s/%s (Elapsed: %s)", config->servers[server].name, identifier, &elapsed[0]);

   pgmoneta_update_info_double(backup_base, INFO_ENCRYPTION_ELAPSED, encryption_elapsed_time);

   free(d);
   free(enc_file);

   return 0;

error:

   if (number_of_workers > 0)
   {
      pgmoneta_workers_destroy(workers);
   }

   free(d);
   free(enc_file);

   return 1;
}

static int
decryption_execute(int server, char* identifier, struct deque* nodes)
{
   char* base = NULL;
   time_t decrypt_time;
   int total_seconds;
   int hours;
   int minutes;
   int seconds;
   char elapsed[128];
   int number_of_workers = 0;
   struct workers* workers = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Decryption (execute): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   base = (char*)pgmoneta_deque_get(nodes, NODE_DESTINATION);
   if (base == NULL)
   {
      base = (char*)pgmoneta_deque_get(nodes, NODE_BACKUP_BASE);
   }
   if (base == NULL)
   {
      base = (char*)pgmoneta_deque_get(nodes, NODE_BACKUP_DATA);
   }

   decrypt_time = time(NULL);

   number_of_workers = pgmoneta_get_number_of_workers(server);
   if (number_of_workers > 0)
   {
      pgmoneta_workers_initialize(number_of_workers, &workers);
   }

   pgmoneta_decrypt_directory(base, workers);

   if (number_of_workers > 0)
   {
      pgmoneta_workers_wait(workers);
      pgmoneta_workers_destroy(workers);
   }

   total_seconds = (int)difftime(time(NULL), decrypt_time);
   hours = total_seconds / 3600;
   minutes = (total_seconds % 3600) / 60;
   seconds = total_seconds % 60;

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

   pgmoneta_log_debug("Decryption: %s/%s (Elapsed: %s)", config->servers[server].name, identifier, &elapsed[0]);

   return 0;
}

static int
encryption_teardown(int server, char* identifier, struct deque* nodes)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Encryption (teardown): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   return 0;
}
