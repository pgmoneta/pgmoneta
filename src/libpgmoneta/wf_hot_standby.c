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
#include <art.h>
#include <compression.h>
#include <files.h>
#include <logging.h>
#include <manifest.h>
#include <restore.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static char* hot_standby_name(void);
static int hot_standby_execute(char*, struct art*);
static void copy_entry(char*, char*, char*, char*, struct workers*);
static bool same_tablespaces(struct backup*, struct backup*);

struct workflow*
pgmoneta_create_hot_standby(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->name = &hot_standby_name;
   wf->setup = &pgmoneta_common_setup;
   wf->execute = &hot_standby_execute;
   wf->teardown = &pgmoneta_common_teardown;
   wf->next = NULL;

   return wf;
}

static char*
hot_standby_name(void)
{
   return "Hot standby";
}

static int
hot_standby_execute(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   char* base = NULL;
   char* source_root = NULL;
   char* source = NULL;
   char* newest = NULL;
   char* suffix = NULL;
   bool incremental = false;
   struct timespec start_t;
   struct timespec end_t;
   double hot_standby_elapsed_time;
   int hours;
   int minutes;
   double seconds;
   char elapsed[128];
   int number_of_workers = 0;
   int number_of_backups = 0;
   int failed_count = 0;
   struct backup** backups = NULL;
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
   incremental = (bool)pgmoneta_art_contains_key(nodes, NODE_INCREMENTAL_BASE);

   pgmoneta_log_debug("Hot standby (execute): %s/%s", config->common.servers[server].name, label);

   if (config->common.servers[server].number_of_hot_standbys > 0)
   {
      number_of_workers = pgmoneta_get_number_of_workers(server);
#ifdef HAVE_FREEBSD
      clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
      clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

      base = pgmoneta_get_server_backup(server);

      pgmoneta_load_infos(base, &number_of_backups, &backups);

      for (int i = 0; i < config->common.servers[server].number_of_hot_standbys; i++)
      {
         char* root = NULL;
         char* destination = NULL;
         char* old_manifest = NULL;
         char* new_manifest = NULL;
         char* f = NULL;
         struct art* deleted_files = NULL;
         struct art_iterator* deleted_iter = NULL;
         struct art* changed_files = NULL;
         struct art_iterator* changed_iter = NULL;
         struct art* added_files = NULL;
         struct art_iterator* added_iter = NULL;
         bool error = false;

         root = pgmoneta_append(root, config->common.servers[server].hot_standby[i]);
         if (!pgmoneta_ends_with(root, "/"))
         {
            root = pgmoneta_append_char(root, '/');
         }

         pgmoneta_log_debug("Processing hot standby directory %d/%d: %s",
                            i + 1,
                            config->common.servers[server].number_of_hot_standbys,
                            config->common.servers[server].hot_standby[i]);

         destination = pgmoneta_append(destination, root);
         destination = pgmoneta_append(destination, config->common.servers[server].name);
         if (!incremental && pgmoneta_exists(destination) && number_of_backups >= 2 &&
             same_tablespaces(backups[number_of_backups - 1], backups[number_of_backups - 2]))
         {
            if (number_of_workers > 0 && workers == NULL)
            {
               pgmoneta_workers_initialize(number_of_workers, &workers);
            }

            if (newest == NULL)
            {
               if (pgmoneta_extraction_get_suffix(config->compression_type, config->common.encryption, &suffix))
               {
                  error = true;
                  goto cleanup;
               }

               newest = pgmoneta_append(newest, base);
               if (!pgmoneta_ends_with(newest, "/"))
               {
                  newest = pgmoneta_append_char(newest, '/');
               }
               newest = pgmoneta_append(newest, backups[number_of_backups - 1]->label);
               if (!pgmoneta_ends_with(newest, "/"))
               {
                  newest = pgmoneta_append_char(newest, '/');
               }
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

            new_manifest = pgmoneta_append(new_manifest, newest);
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
               f = pgmoneta_append(f, deleted_iter->key);

               if (pgmoneta_exists(f))
               {
                  if (pgmoneta_ends_with(f, "/"))
                  {
                     pgmoneta_delete_directory(f);
                  }
                  else
                  {
                     pgmoneta_delete_file(f, workers);
                  }
               }
               else
               {
                  pgmoneta_log_debug("%s doesn't exists", f);
               }

               free(f);
               f = NULL;
            }

            while (pgmoneta_art_iterator_next(changed_iter))
            {
               copy_entry(newest, destination, changed_iter->key, suffix, workers);
            }

            while (pgmoneta_art_iterator_next(added_iter))
            {
               copy_entry(newest, destination, added_iter->key, suffix, workers);
            }
         }
         else
         {
            /* incremental or  number of backups < 2 */
            if (incremental && source == NULL)
            {
               if (pgmoneta_extract_incremental_backup(server, label, &source_root, &source))
               {
                  pgmoneta_log_error("Hotstandby: Unable to extract backup %s", label);
                  error = true;
                  goto cleanup;
               }
            }
            else if (source == NULL)
            {
               source = pgmoneta_append(source, base);
               source = pgmoneta_append(source, label);
               source = pgmoneta_append_char(source, '/');
               source = pgmoneta_append(source, "data");
            }
            if (number_of_workers > 0 && workers == NULL)
            {
               pgmoneta_workers_initialize(number_of_workers, &workers);
            }

            if (pgmoneta_exists(destination))
            {
               pgmoneta_delete_directory(destination);
            }

            pgmoneta_mkdir(root);
            pgmoneta_mkdir(destination);

            if (pgmoneta_copy_postgresql_hotstandby(server, source, destination,
                                                    config->common.servers[server].hot_standby_tablespaces[i],
                                                    backups[number_of_backups - 1], workers))
            {
               error = true;
               goto cleanup;
            }
         }
         pgmoneta_log_debug("hot_standby source:      %s", source);
         pgmoneta_log_debug("hot_standby destination: %s", destination);
         pgmoneta_workers_wait(workers);
         if (workers != NULL && !pgmoneta_workers_outcome_ok(workers))
         {
            pgmoneta_workers_transfer_failures(workers, nodes);
            error = true;
            goto cleanup;
         }

         if (config->common.encryption != ENCRYPTION_NONE)
         {
            if (pgmoneta_decrypt_directory(-1, destination, workers, NULL))
            {
               error = true;
               goto cleanup;
            }
            if (workers != NULL)
            {
               pgmoneta_workers_wait(workers);
               if (!pgmoneta_workers_outcome_ok(workers))
               {
                  pgmoneta_workers_transfer_failures(workers, nodes);
                  error = true;
                  goto cleanup;
               }
            }
         }

         if (COMPRESSION_ALGORITHM(config->compression_type) != COMPRESSION_ALG_NONE)
         {
            if (pgmoneta_decompress_directory(-1, destination, config->compression_type, workers, NULL))
            {
               error = true;
               goto cleanup;
            }
            if (workers != NULL)
            {
               pgmoneta_workers_wait(workers);
               if (!pgmoneta_workers_outcome_ok(workers))
               {
                  pgmoneta_workers_transfer_failures(workers, nodes);
                  error = true;
                  goto cleanup;
               }
            }
         }

         if (strlen(config->common.servers[server].hot_standby_overrides[i]) > 0 &&
             pgmoneta_exists(config->common.servers[server].hot_standby_overrides[i]) &&
             pgmoneta_is_directory(config->common.servers[server].hot_standby_overrides[i]))
         {
            pgmoneta_log_debug("hot_standby_overrides source:      %s", config->common.servers[server].hot_standby_overrides[i]);
            pgmoneta_log_debug("hot_standby_overrides destination: %s", destination);

            pgmoneta_copy_directory(-1, config->common.servers[server].hot_standby_overrides[i],
                                    destination,
                                    NULL,
                                    workers);
         }
         else
         {
            pgmoneta_log_debug("No hot_standby_overrides %s", config->common.servers[server].hot_standby[i]);
         }
         pgmoneta_workers_wait(workers);
         if (workers != NULL && !pgmoneta_workers_outcome_ok(workers))
         {
            pgmoneta_workers_transfer_failures(workers, nodes);
            error = true;
            goto cleanup;
         }

cleanup:
         if (error)
         {
            ++failed_count;
            pgmoneta_log_error("Failed to process hot standby directory: %s",
                               config->common.servers[server].hot_standby[i]);

            /* The standby no longer matches a backup, so force a full copy next time */
            pgmoneta_workers_wait(workers);
            pgmoneta_delete_directory(destination);
         }
         free(old_manifest);
         free(new_manifest);
         free(root);
         free(destination);

         pgmoneta_art_iterator_destroy(deleted_iter);
         pgmoneta_art_iterator_destroy(changed_iter);
         pgmoneta_art_iterator_destroy(added_iter);

         pgmoneta_art_destroy(deleted_files);
         pgmoneta_art_destroy(changed_files);
         pgmoneta_art_destroy(added_files);
      }

      pgmoneta_workers_destroy(workers);

#ifdef HAVE_FREEBSD
      clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
      clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

      hot_standby_elapsed_time = pgmoneta_compute_duration(start_t, end_t);
      hours = (int)hot_standby_elapsed_time / 3600;
      minutes = ((int)hot_standby_elapsed_time % 3600) / 60;
      seconds = (int)hot_standby_elapsed_time % 60 + (hot_standby_elapsed_time - ((long)hot_standby_elapsed_time));

      memset(&elapsed[0], 0, sizeof(elapsed));
      sprintf(&elapsed[0], "%02i:%02i:%.4f", hours, minutes, seconds);

      pgmoneta_log_debug("Hot standby: %s/%s - Processed %d/%d directories successfully (Elapsed: %s)",
                         config->common.servers[server].name, label,
                         config->common.servers[server].number_of_hot_standbys - failed_count,
                         config->common.servers[server].number_of_hot_standbys,
                         &elapsed[0]);
   }

   if (source_root != NULL)
   {
      pgmoneta_delete_directory(source_root);
      free(source_root);
   }

   free(base);
   free(source);
   free(newest);
   free(suffix);
   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   return failed_count;
}

static void
copy_entry(char* source, char* destination, char* key, char* suffix, struct workers* workers)
{
   char* from = NULL;
   char* to = NULL;

   to = pgmoneta_append(to, destination);
   if (!pgmoneta_ends_with(to, "/"))
   {
      to = pgmoneta_append_char(to, '/');
   }
   to = pgmoneta_append(to, key);

   if (pgmoneta_ends_with(key, "/"))
   {
      pgmoneta_mkdir(to);
   }
   else
   {
      from = pgmoneta_append(from, source);
      from = pgmoneta_append(from, "data/");
      from = pgmoneta_append(from, key);

      if (suffix != NULL &&
          !pgmoneta_compare_string(key, "backup_label") &&
          !pgmoneta_compare_string(key, "backup_manifest"))
      {
         from = pgmoneta_append(from, suffix);
         to = pgmoneta_append(to, suffix);
      }

      pgmoneta_log_trace("hot_standby: %s -> %s", from, to);

      pgmoneta_copy_file(from, to, workers);
   }

   free(from);
   free(to);
}

static bool
same_tablespaces(struct backup* b1, struct backup* b2)
{
   if (b1->number_of_tablespaces != b2->number_of_tablespaces)
   {
      return false;
   }

   for (uint64_t i = 0; i < b1->number_of_tablespaces; i++)
   {
      if (!pgmoneta_compare_string(b1->tablespaces_oids[i], b2->tablespaces_oids[i]))
      {
         return false;
      }
   }

   return true;
}
