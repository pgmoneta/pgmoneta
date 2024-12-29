/*
 * Copyright (C) 2024 The pgmoneta community
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
#include <hot_standby.h>
#include <logging.h>
#include <manifest.h>
#include <utils.h>
#include <workers.h>

/* system */
#include <stdlib.h>
#include <string.h>

static int hot_standby_setup(int, char*, struct deque*);
static int hot_standby_execute(int, char*, struct deque*);
static int hot_standby_teardown(int, char*, struct deque*);

struct workflow*
pgmoneta_create_hot_standby(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->setup = &hot_standby_setup;
   wf->execute = &hot_standby_execute;
   wf->teardown = &hot_standby_teardown;
   wf->next = NULL;

   return wf;
}

static int
hot_standby_setup(int server, char* identifier, struct deque* nodes)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Hot standby (setup): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   return 0;
}

static int
hot_standby_execute(int server, char* identifier, struct deque* nodes)
{
   char* root = NULL;
   char* base = NULL;
   char* source = NULL;
   char* destination = NULL;
   char* old_manifest = NULL;
   char* new_manifest = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double hot_standby_elapsed_time;
   int hours;
   int minutes;
   double seconds;
   char elapsed[128];
   int number_of_workers = 0;
   char* f = NULL;
   char* from = NULL;
   char* to = NULL;
   struct art* deleted_files = NULL;
   struct art_iterator* deleted_iter = NULL;
   struct art* changed_files = NULL;
   struct art_iterator* changed_iter = NULL;
   struct art* added_files = NULL;
   struct art_iterator* added_iter = NULL;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   struct workers* workers = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Hot standby (execute): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   if (strlen(config->servers[server].hot_standby) > 0)
   {
      number_of_workers = pgmoneta_get_number_of_workers(server);
      if (number_of_workers > 0)
      {
         pgmoneta_workers_initialize(number_of_workers, &workers);
      }

      clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);

      base = pgmoneta_get_server_backup(server);

      pgmoneta_get_backups(base, &number_of_backups, &backups);

      root = pgmoneta_append(root, config->servers[server].hot_standby);
      if (!pgmoneta_ends_with(root, "/"))
      {
         root = pgmoneta_append_char(root, '/');
      }

      destination = pgmoneta_append(destination, root);
      destination = pgmoneta_append(destination, config->servers[server].name);

      if (pgmoneta_exists(destination) && number_of_backups >= 2)
      {
         source = pgmoneta_append(source, base);
         if (!pgmoneta_ends_with(source, "/"))
         {
            source = pgmoneta_append_char(source, '/');
         }
         source = pgmoneta_append(source, backups[number_of_backups - 1]->label);
         if (!pgmoneta_ends_with(source, "/"))
         {
            source = pgmoneta_append_char(source, '/');
         }

         old_manifest = pgmoneta_append(old_manifest, base);
         if (!pgmoneta_ends_with(old_manifest, "/"))
         {
            old_manifest = pgmoneta_append_char(old_manifest, '/');
         }
         old_manifest = pgmoneta_append(old_manifest, backups[number_of_backups - 2]->label);
         if (!pgmoneta_ends_with(old_manifest, "/"))
         {
            old_manifest = pgmoneta_append_char(old_manifest, '/');
         }
         old_manifest = pgmoneta_append(old_manifest, "backup.manifest");

         new_manifest = pgmoneta_append(new_manifest, source);
         new_manifest = pgmoneta_append(new_manifest, "backup.manifest");

         pgmoneta_log_trace("old_manifest: %s", old_manifest);
         pgmoneta_log_trace("new_manifest: %s", new_manifest);

         pgmoneta_compare_manifests(old_manifest, new_manifest, &deleted_files, &changed_files, &added_files);

         pgmoneta_art_iterator_create(deleted_files, &deleted_iter);
         pgmoneta_art_iterator_create(changed_files, &changed_iter);
         pgmoneta_art_iterator_create(added_files, &added_iter);

         while (pgmoneta_art_iterator_next(deleted_iter))
         {
            f = pgmoneta_append(f, destination);
            if (!pgmoneta_ends_with(f, "/"))
            {
               f = pgmoneta_append_char(f, '/');
            }
            f = pgmoneta_append(f, (char*)deleted_iter->key);

            if (pgmoneta_exists(f))
            {
               pgmoneta_log_trace("hot_standby delete: %s", f);
               pgmoneta_delete_file(f, true, workers);
            }

            free(f);
            f = NULL;
         }

         while (pgmoneta_art_iterator_next(changed_iter))
         {
            from = pgmoneta_append(from, source);
            if (!pgmoneta_ends_with(from, "/"))
            {
               from = pgmoneta_append_char(from, '/');
            }
            from = pgmoneta_append(from, "data/");
            from = pgmoneta_append(from, (char*)changed_iter->key);

            to = pgmoneta_append(to, destination);
            if (!pgmoneta_ends_with(to, "/"))
            {
               to = pgmoneta_append_char(to, '/');
            }
            to = pgmoneta_append(to, (char*)changed_iter->key);

            pgmoneta_log_trace("hot_standby changed: %s -> %s", from, to);

            pgmoneta_copy_file(from, to, workers);

            free(from);
            from = NULL;

            free(to);
            to = NULL;
         }

         while (pgmoneta_art_iterator_next(added_iter))
         {
            from = pgmoneta_append(from, source);
            if (!pgmoneta_ends_with(from, "/"))
            {
               from = pgmoneta_append_char(from, '/');
            }
            from = pgmoneta_append(from, "data/");
            from = pgmoneta_append(from, (char*)added_iter->key);

            to = pgmoneta_append(to, destination);
            if (!pgmoneta_ends_with(to, "/"))
            {
               to = pgmoneta_append_char(to, '/');
            }
            to = pgmoneta_append(to, (char*)added_iter->key);

            pgmoneta_log_trace("hot_standby new: %s -> %s", from, to);

            pgmoneta_copy_file(from, to, workers);

            free(from);
            from = NULL;

            free(to);
            to = NULL;
         }
      }
      else
      {
         if (pgmoneta_exists(destination))
         {
            pgmoneta_delete_directory(destination);
         }

         source = pgmoneta_append(source, base);
         source = pgmoneta_append(source, identifier);
         source = pgmoneta_append_char(source, '/');
         source = pgmoneta_append(source, "data");

         pgmoneta_mkdir(root);
         pgmoneta_mkdir(destination);

         pgmoneta_copy_postgresql_hotstandby(source, destination, config->servers[server].hot_standby_tablespaces, backups[number_of_backups - 1], workers);
      }

      pgmoneta_log_debug("hot_standby source:      %s", source);
      pgmoneta_log_debug("hot_standby destination: %s", destination);

      if (number_of_workers > 0)
      {
         pgmoneta_workers_wait(workers);
      }

      if (strlen(config->servers[server].hot_standby_overrides) > 0 &&
          pgmoneta_exists(config->servers[server].hot_standby_overrides) &&
          pgmoneta_is_directory(config->servers[server].hot_standby_overrides))
      {
         pgmoneta_log_debug("hot_standby_overrides source:      %s", config->servers[server].hot_standby_overrides);
         pgmoneta_log_debug("hot_standby_overrides destination: %s", destination);

         pgmoneta_copy_directory(config->servers[server].hot_standby_overrides,
                                 destination,
                                 NULL,
                                 workers);
      }

      if (number_of_workers > 0)
      {
         pgmoneta_workers_wait(workers);
         pgmoneta_workers_destroy(workers);
      }

      clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);

      hot_standby_elapsed_time = pgmoneta_compute_duration(start_t, end_t);
      hours = (int)hot_standby_elapsed_time / 3600;
      minutes = ((int)hot_standby_elapsed_time % 3600) / 60;
      seconds = (int)hot_standby_elapsed_time % 60 + (hot_standby_elapsed_time - ((long)hot_standby_elapsed_time));

      memset(&elapsed[0], 0, sizeof(elapsed));
      sprintf(&elapsed[0], "%02i:%02i:%.4f", hours, minutes, seconds);

      pgmoneta_log_debug("Hot standby: %s/%s (Elapsed: %s)", config->servers[server].name, identifier, &elapsed[0]);
   }

   free(old_manifest);
   free(new_manifest);

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   pgmoneta_art_iterator_destroy(deleted_iter);
   pgmoneta_art_iterator_destroy(changed_iter);
   pgmoneta_art_iterator_destroy(added_iter);

   pgmoneta_art_destroy(deleted_files);
   pgmoneta_art_destroy(changed_files);
   pgmoneta_art_destroy(added_files);

   free(root);
   free(base);
   free(source);
   free(destination);

   return 0;
}

static int
hot_standby_teardown(int server, char* identifier, struct deque* nodes)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Hot standby (teardown): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   return 0;
}
