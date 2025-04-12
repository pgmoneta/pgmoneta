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
#include "backup.h"
#include <pgmoneta.h>
#include <link.h>
#include <logging.h>
#include <manifest.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static char* link_name(void);
static int link_execute(char*, struct art*);

struct workflow*
pgmoneta_create_link(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->name = &link_name;
   wf->setup = &pgmoneta_common_setup;
   wf->execute = &link_execute;
   wf->teardown = &pgmoneta_common_teardown;
   wf->next = NULL;

   return wf;
}

static char*
link_name(void)
{
   return "Link";
}

static int
link_execute(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double linking_elapsed_time;
   char* server_path = NULL;
   char* from = NULL;
   char* to = NULL;
   char* from_manifest = NULL;
   char* to_manifest = NULL;
   char* from_tablespaces = NULL;
   char* to_tablespaces = NULL;
   char* backup_base = NULL;
   int next_newest = -1;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   int hours;
   int minutes;
   double seconds;
   char elapsed[128];
   int number_of_workers = 0;
   int index = 0;
   struct workers* workers = NULL;
   struct main_configuration* config;
   struct art* deleted_files = NULL;
   struct art* changed_files = NULL;
   struct art* added_files = NULL;
   struct backup* backup = NULL;
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

   pgmoneta_log_debug("Link (execute): %s/%s", config->common.servers[server].name, label);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   server_path = pgmoneta_get_server_backup(server);

   pgmoneta_load_infos(server_path, &number_of_backups, &backups);

   if (number_of_backups >= 2)
   {
      for (int j = number_of_backups - 1; j >= 0; j--)
      {
         if (pgmoneta_compare_string(backups[j]->label, label))
         {
            index = j;
            break;
         }
      }
      for (int j = index - 1; j >= 0 && next_newest == -1; j--)
      {
         if (pgmoneta_is_backup_struct_valid(server, backups[j]) &&
             backups[j]->major_version == backups[number_of_backups - 1]->major_version)
         {
            if (next_newest == -1)
            {
               next_newest = j;
            }
         }
      }

      if (next_newest != -1)
      {
         number_of_workers = pgmoneta_get_number_of_workers(server);

         if (number_of_workers > 0)
         {
            pgmoneta_workers_initialize(number_of_workers, &workers);
         }

         from = pgmoneta_get_server_backup_identifier(server, label);

         to = pgmoneta_get_server_backup_identifier(server, backups[next_newest]->label);

         from_manifest = pgmoneta_append(from_manifest, from);
         from_manifest = pgmoneta_append(from_manifest, "backup.manifest");

         to_manifest = pgmoneta_append(to_manifest, to);
         to_manifest = pgmoneta_append(to_manifest, "backup.manifest");

         from = pgmoneta_append(from, "data/");
         to = pgmoneta_append(to, "data/");

         pgmoneta_compare_manifests(to_manifest, from_manifest, &deleted_files, &changed_files, &added_files);
         pgmoneta_link_manifest(from, to, from, changed_files, added_files, workers);

         pgmoneta_workers_wait(workers);
         if (workers != NULL && !workers->outcome)
         {
            goto error;
         }
         pgmoneta_workers_destroy(workers);

#ifdef HAVE_FREEBSD
         clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
         clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

         linking_elapsed_time = pgmoneta_compute_duration(start_t, end_t);

         hours = linking_elapsed_time / 3600;
         minutes = ((int)linking_elapsed_time % 3600) / 60;
         seconds = (int)linking_elapsed_time % 60 + (linking_elapsed_time - ((long)linking_elapsed_time));

         memset(&elapsed[0], 0, sizeof(elapsed));
         sprintf(&elapsed[0], "%02i:%02i:%.4f", hours, minutes, seconds);

         pgmoneta_log_debug("Link: %s/%s (Elapsed: %s)", config->common.servers[server].name, label, &elapsed[0]);

         backup_base = (char*)pgmoneta_art_search(nodes, NODE_SERVER_BACKUP);
         backup = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);
         backup->linking_elapsed_time = linking_elapsed_time;
         snprintf(backup->label, sizeof(backup->label), "%s", label);
         if (pgmoneta_save_info(backup_base, backup))
         {
            goto error;
         }
         free(backup);
         backup = NULL;
      }
   }

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);
   free(server_path);
   free(from);
   free(to);
   free(from_manifest);
   free(to_manifest);
   free(from_tablespaces);
   free(to_tablespaces);
   pgmoneta_art_destroy(changed_files);
   pgmoneta_art_destroy(added_files);
   pgmoneta_art_destroy(deleted_files);

   return 0;

error:

   pgmoneta_workers_destroy(workers);

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);
   free(backup);

   free(server_path);
   free(from);
   free(to);
   free(from_manifest);
   free(to_manifest);
   free(from_tablespaces);
   free(to_tablespaces);
   pgmoneta_art_destroy(changed_files);
   pgmoneta_art_destroy(added_files);
   pgmoneta_art_destroy(deleted_files);

   return 1;
}
