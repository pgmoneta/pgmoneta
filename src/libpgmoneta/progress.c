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
#include <art.h>
#include <backup.h>
#include <info.h>
#include <logging.h>
#include <progress.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void
normalize_weights(int n_phases, int* weights)
{
   int total_weight = 0;
   for (int i = 0; i < n_phases; i++)
   {
      total_weight += weights[i];
   }
   if (total_weight != 100 && total_weight > 0)
   {
      int diff = 100 - total_weight;
      weights[n_phases - 1] += diff;
   }
}

static void
calculate_adaptive_weights(int workflow_type, void* info, int* phases, int n_phases, int* weights)
{
   switch (workflow_type)
   {
      case WORKFLOW_TYPE_BACKUP:
      case WORKFLOW_TYPE_INCREMENTAL_BACKUP:
      {
         struct backup* prev = (struct backup*)info;
         double total_time = prev->total_elapsed_time;
         int total_weight = 0;

         for (int i = 0; i < n_phases; i++)
         {
            double elapsed = 0;
            switch (phases[i])
            {
               case PHASE_BASEBACKUP:
                  elapsed = prev->basebackup_elapsed_time;
                  break;
               case PHASE_MANIFEST:
                  elapsed = prev->manifest_elapsed_time;
                  break;
               case PHASE_SHA512:
                  elapsed = prev->hash_elapsed_time;
                  break;
               case PHASE_LINKING:
                  elapsed = prev->linking_elapsed_time;
                  break;
               case PHASE_COMPRESSION:
                  elapsed = prev->compression_zstd_elapsed_time +
                            prev->compression_gzip_elapsed_time +
                            prev->compression_lz4_elapsed_time +
                            prev->compression_bzip2_elapsed_time;
                  break;
               case PHASE_ENCRYPTION:
                  elapsed = prev->encryption_elapsed_time;
                  break;
               default:
                  break;
            }
            weights[i] = (int)((elapsed / total_time) * 100.0);
            if (weights[i] < 1)
            {
               weights[i] = 1;
            }
            total_weight += weights[i];
         }

         /* Cap basebackup at 85% to shoot over */
         for (int i = 0; i < n_phases; i++)
         {
            if (phases[i] == PHASE_BASEBACKUP && weights[i] > 85)
            {
               int surplus = weights[i] - 85;
               weights[i] = 85;
               int others = total_weight - weights[i] - surplus;
               for (int j = 0; j < n_phases; j++)
               {
                  if (j != i && others > 0)
                  {
                     weights[j] = weights[j] + (int)((double)surplus * weights[j] / others);
                  }
               }
               break;
            }
         }
         break;
      }
      default:
         break;
   }

   normalize_weights(n_phases, weights);
}

static void
calculate_fallback_weights(int workflow_type, int* phases, int n_phases, int* weights)
{
   switch (workflow_type)
   {
      case WORKFLOW_TYPE_BACKUP:
      case WORKFLOW_TYPE_INCREMENTAL_BACKUP:
      {
         static const int full_backup_phases[] = {
            PHASE_BASEBACKUP, PHASE_MANIFEST,
            PHASE_LINKING, PHASE_SHA512};
         static const int full_backup_weights[] = {77, 7, 2, 14};

         static const int incr_backup_phases[] = {
            PHASE_BASEBACKUP, PHASE_MANIFEST,
            PHASE_COMPRESSION, PHASE_ENCRYPTION,
            PHASE_LINKING, PHASE_SHA512};
         static const int incr_backup_weights[] = {84, 7, 2, 1, 2, 4};

         const int* fb_phases = full_backup_phases;
         const int* fb_weights = full_backup_weights;
         int fb_count = 4;

         if (workflow_type == WORKFLOW_TYPE_INCREMENTAL_BACKUP)
         {
            bool has_compression = false;
            bool has_encryption = false;

            for (int i = 0; i < n_phases; i++)
            {
               if (phases[i] == PHASE_COMPRESSION)
               {
                  has_compression = true;
               }
               if (phases[i] == PHASE_ENCRYPTION)
               {
                  has_encryption = true;
               }
            }

            if (has_compression || has_encryption)
            {
               fb_phases = incr_backup_phases;
               fb_weights = incr_backup_weights;
               fb_count = 6;
            }
         }

         for (int i = 0; i < n_phases; i++)
         {
            weights[i] = 1;
            for (int j = 0; j < fb_count; j++)
            {
               if (phases[i] == fb_phases[j])
               {
                  weights[i] = fb_weights[j];
                  break;
               }
            }
         }
         break;
      }
      case WORKFLOW_TYPE_RESTORE:
         /* TODO: Implement fallback weights for restore */
         break;
      case WORKFLOW_TYPE_ARCHIVE:
         /* TODO: Implement fallback weights for archive */
         break;
      case WORKFLOW_TYPE_VERIFY:
         /* TODO: Implement fallback weights for verify */
         break;
      case WORKFLOW_TYPE_S3_LIST:
      case WORKFLOW_TYPE_S3_DELETE:
      case WORKFLOW_TYPE_S3_RESTORE:
         /* TODO: Implement adaptive/fallback weights for S3 */
         break;
      default:
         break;
   }

   normalize_weights(n_phases, weights);
}

bool
pgmoneta_is_progress_enabled(int server)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (config->common.servers[server].progress_enabled == 1)
   {
      return true;
   }
   else if (config->common.servers[server].progress_enabled == 0)
   {
      return false;
   }

   return config->progress;
}

void
pgmoneta_progress_setup(int server, struct workflow* workflow, struct art* nodes, int workflow_type)
{
   int phase = -1;
   int n_phases = 0;
   int phases[8];
   int weights[8];
   int cumulative = 0;
   char* server_backup = NULL;
   struct backup* prev = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (!pgmoneta_is_progress_enabled(server))
   {
      return;
   }

   /* Collect tracked phases from the workflow chain */
   struct workflow* current = workflow;
   while (current != NULL)
   {
      phase = pgmoneta_progress_phase_from_workflow_name(current->name());
      if (phase > 0)
      {
         bool found = false;
         for (int i = 0; i < n_phases; i++)
         {
            if (phases[i] == phase)
            {
               found = true;
               break;
            }
         }
         if (!found && n_phases < 8)
         {
            phases[n_phases++] = phase;
         }
      }
      current = current->next;
   }

   if (n_phases == 0)
   {
      return;
   }

   switch (workflow_type)
   {
      case WORKFLOW_TYPE_BACKUP:
      case WORKFLOW_TYPE_INCREMENTAL_BACKUP:
      {
         /* Try adaptive weights from previous backup */
         assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_BACKUP));
         server_backup = (char*)pgmoneta_art_search(nodes, NODE_SERVER_BACKUP);
         if (pgmoneta_load_info(server_backup, "newest", &prev) == 0 &&
             prev != NULL && prev->total_elapsed_time > 0)
         {
            calculate_adaptive_weights(workflow_type, prev, phases, n_phases, weights);
         }
         else
         {
            calculate_fallback_weights(workflow_type, phases, n_phases, weights);
         }
         break;
      }
      case WORKFLOW_TYPE_RESTORE:
         /* TODO: Implement adaptive/fallback weights for restore */
         break;
      case WORKFLOW_TYPE_ARCHIVE:
         /* TODO: Implement adaptive/fallback weights for archive */
         break;
      case WORKFLOW_TYPE_VERIFY:
         /* TODO: Implement adaptive/fallback weights for verify */
         break;
      case WORKFLOW_TYPE_S3_LIST:
      case WORKFLOW_TYPE_S3_DELETE:
      case WORKFLOW_TYPE_S3_RESTORE:
         /* TODO: Implement adaptive/fallback weights for S3 */
         break;
      default:
         break;
   }

   if (prev != NULL)
   {
      free(prev);
   }

   /* Store cumulative limits in art nodes */
   cumulative = 0;
   for (int i = 0; i < n_phases; i++)
   {
      cumulative += weights[i];
      char* key = pgmoneta_progress_limit_node_key(phases[i]);
      assert(key);
      pgmoneta_art_insert(nodes, key, (uintptr_t)cumulative, ValueInt32);
   }

   /* Initialize shared memory progress fields */
   atomic_store(&config->common.servers[server].progress.state, WORKFLOW_PROGRESS_RUNNING);
   atomic_store(&config->common.servers[server].progress.workflow_type, workflow_type);
   atomic_store(&config->common.servers[server].progress.current_phase, phases[0]);
   atomic_store(&config->common.servers[server].progress.done, 0);
   atomic_store(&config->common.servers[server].progress.total, 0);
   atomic_store(&config->common.servers[server].progress.elapsed, 0);
   atomic_store(&config->common.servers[server].progress.start_time, time(NULL));
   atomic_store(&config->common.servers[server].progress.percentage, 0);

   pgmoneta_log_debug("Progress: Workflow \"%s\" started for server %s",
                      pgmoneta_workflow_name(workflow_type),
                      config->common.servers[server].name);
}

void
pgmoneta_progress_next_phase(int server, int phase, struct art* nodes)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   atomic_store(&config->common.servers[server].progress.current_phase, phase);
   atomic_store(&config->common.servers[server].progress.done, 0);
   atomic_store(&config->common.servers[server].progress.total, 0);

   struct progress* p = &config->common.servers[server].progress;
   int pct = atomic_load(&p->percentage);
   atomic_store(&p->prev_phase_limit, pct);

   if (nodes != NULL && phase > 0)
   {
      char* key = pgmoneta_progress_limit_node_key(phase);
      assert(key);
      assert(pgmoneta_art_contains_key(nodes, key));

      atomic_store(&p->phase_limit, (int)(uintptr_t)pgmoneta_art_search(nodes, key));
   }

   pgmoneta_log_debug("Progress: Phase \"%s\" started for server %s",
                      pgmoneta_phase_name(phase),
                      config->common.servers[server].name);
}

int64_t
pgmoneta_progress_remaining(int server)
{
   struct main_configuration* config;
   struct progress* p;

   config = (struct main_configuration*)shmem;
   p = &config->common.servers[server].progress;

   int pct = atomic_load(&p->percentage);
   int64_t elapsed = atomic_load(&p->elapsed);

   if (pct > 0 && pct < 100)
   {
      return (int64_t)((double)elapsed * (100.0 - pct) / pct);
   }

   return 0;
}

void
pgmoneta_progress_report(int server)
{
   struct main_configuration* config;
   struct progress* p;

   config = (struct main_configuration*)shmem;
   p = &config->common.servers[server].progress;

   int64_t done = atomic_load(&p->done);
   int64_t total = atomic_load(&p->total);
   int phase = atomic_load(&p->current_phase);

   if (total > 0)
   {
      if (done > total)
      {
         total = done;
      }
      atomic_store(&p->done, done);
      atomic_store(&p->total, total);

      int prev_limit = atomic_load(&p->prev_phase_limit);
      int phase_limit = atomic_load(&p->phase_limit);

      /* Get limits from the workflow_execute context if available */
      /* These are stored by the workflow engine during phase transitions */

      int64_t now = time(NULL);
      int64_t start = atomic_load(&p->start_time);
      int64_t elapsed_s = now - start;
      atomic_store(&p->elapsed, elapsed_s);

      int pct = prev_limit + (int)((double)done / (double)total * (phase_limit - prev_limit));
      if (pct > 100)
      {
         pct = 100;
      }
      atomic_store(&p->percentage, pct);

      int64_t remaining = pgmoneta_progress_remaining(server);

      pgmoneta_log_debug("Progress: %s %d%% (%lld/%lld, elapsed %llds, remaining ~%llds)",
                         pgmoneta_phase_name(phase),
                         pct, (long long)done, (long long)total,
                         (long long)elapsed_s, (long long)remaining);
   }
}

void
pgmoneta_progress_increment(int server, int64_t amount)
{
   struct main_configuration* config;
   struct progress* p;

   config = (struct main_configuration*)shmem;
   p = &config->common.servers[server].progress;

   atomic_fetch_add(&p->done, amount);
   pgmoneta_progress_report(server);
}

void
pgmoneta_progress_set_total(int server, int64_t total)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   atomic_store(&config->common.servers[server].progress.total, total);
}

int64_t
pgmoneta_progress_get_total(int server)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   return atomic_load(&config->common.servers[server].progress.total);
}

void
pgmoneta_progress_update_done(int server, int64_t done)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   atomic_store(&config->common.servers[server].progress.done, done);
   pgmoneta_progress_report(server);
}

void
pgmoneta_progress_teardown(int server)
{
   struct main_configuration* config;
   struct progress* p;
   int workflow_type;

   config = (struct main_configuration*)shmem;
   p = &config->common.servers[server].progress;
   workflow_type = atomic_load(&p->workflow_type);

   pgmoneta_log_debug("Progress: Workflow \"%s\" completed for server %s",
                      pgmoneta_workflow_name(workflow_type),
                      config->common.servers[server].name);

   atomic_store(&p->state, WORKFLOW_PROGRESS_NONE);
   atomic_store(&p->workflow_type, 0);
   atomic_store(&p->current_phase, PHASE_NONE);
   atomic_store(&p->percentage, 0);
   atomic_store(&p->done, 0);
   atomic_store(&p->total, 0);
   atomic_store(&p->prev_phase_limit, 0);
   atomic_store(&p->phase_limit, 0);
}

int
pgmoneta_progress_phase_from_workflow_name(char* name)
{
   if (!strcmp(name, PHASE_NAME_BASEBACKUP) || !strcmp(name, PHASE_NAME_INCREMENTAL_BACKUP))
   {
      return PHASE_BASEBACKUP;
   }
   if (!strcmp(name, PHASE_NAME_MANIFEST))
   {
      return PHASE_MANIFEST;
   }
   if (!strcmp(name, PHASE_NAME_SHA512))
   {
      return PHASE_SHA512;
   }
   if (!strcmp(name, PHASE_NAME_LINK))
   {
      return PHASE_LINKING;
   }
   if (!strcmp(name, PHASE_NAME_ZSTD) || !strcmp(name, PHASE_NAME_GZIP) || !strcmp(name, PHASE_NAME_LZ4) || !strcmp(name, PHASE_NAME_BZIP2))
   {
      return PHASE_COMPRESSION;
   }
   if (!strcmp(name, PHASE_NAME_ENCRYPTION))
   {
      return PHASE_ENCRYPTION;
   }
   return -1;
}

char*
pgmoneta_progress_limit_node_key(int phase)
{
   switch (phase)
   {
      case PHASE_BASEBACKUP:
         return NODE_PROGRESS_LIMIT_BACKUP;
      case PHASE_MANIFEST:
         return NODE_PROGRESS_LIMIT_MANIFEST;
      case PHASE_SHA512:
         return NODE_PROGRESS_LIMIT_SHA512;
      case PHASE_LINKING:
         return NODE_PROGRESS_LIMIT_LINK;
      case PHASE_COMPRESSION:
         return NODE_PROGRESS_LIMIT_COMPRESSION;
      case PHASE_ENCRYPTION:
         return NODE_PROGRESS_LIMIT_ENCRYPTION;
      default:
         return NULL;
   }
}
