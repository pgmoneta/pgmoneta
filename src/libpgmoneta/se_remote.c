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

/*
 * se_remote: fans a backup out to all configured remote storage backends,
 * driven by the engines[] capability table.
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <info.h>
#include <logging.h>
#include <shmem.h>
#include <storage.h>
#include <utils.h>
#include <workers.h>
#include <workflow.h>

/* system */
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

/* Per-backend upload/cleanup entry points defined in their respective se_*.c */
extern int ssh_upload(int server, char* label, int compression, int encryption);
extern int s3_upload(int server, char* label, int compression, int encryption);
extern int s3_cleanup(int server, char* label);
extern int azure_upload(int server, char* label, int compression, int encryption);

/* The contract each remote backend must implement */
struct storage_engine
{
   const char* name;
   int engine_flag;
   int capabilities;
   int (*upload)(int server, char* label, int compression, int encryption);
   int (*cleanup)(int server, char* label);
};

static const struct storage_engine engines[] = {
   {"SSH", STORAGE_ENGINE_SSH,
    STORAGE_CAP_ATOMIC_RENAME,
    ssh_upload, NULL},
   {"S3", STORAGE_ENGINE_S3,
    STORAGE_CAP_RANGE_GET | STORAGE_CAP_BATCH_DELETE | STORAGE_CAP_MULTIPART | STORAGE_CAP_PARALLEL_SAFE,
    s3_upload, s3_cleanup},
   {"Azure", STORAGE_ENGINE_AZURE,
    STORAGE_CAP_RANGE_GET | STORAGE_CAP_PARALLEL_SAFE,
    azure_upload, NULL},
};

#define N_ENGINES ((int)(sizeof(engines) / sizeof(engines[0])))

struct remote_task
{
   struct worker_common common;
   const struct storage_engine* engine;
   int server;
   char* label;
   int compression;
   int encryption;
   int result;
   double elapsed;
};

static char* remote_upload_name(void);
static int remote_upload_setup(char*, struct art*);
static int remote_upload_execute(char*, struct art*);
static int remote_upload_teardown(char*, struct art*);
static void maybe_delete_local_data(int server, char* label);

static void
backend_task(struct worker_common* wc)
{
   struct remote_task* t = (struct remote_task*)wc;
   struct timespec start;
   struct timespec end;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start);
#endif

   pgmoneta_log_debug("remote_upload: %s started", t->engine->name);

   t->result = t->engine->upload(t->server, t->label, t->compression, t->encryption);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end);
#endif

   t->elapsed = pgmoneta_compute_duration(start, end);

   pgmoneta_log_debug("remote_upload: %s finished (%.2fs)", t->engine->name, t->elapsed);
}

struct workflow*
pgmoneta_storage_create_remote(void)
{
   struct workflow* wf = malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->name = &remote_upload_name;
   wf->setup = &remote_upload_setup;
   wf->execute = &remote_upload_execute;
   wf->teardown = &remote_upload_teardown;
   wf->next = NULL;

   return wf;
}

static char*
remote_upload_name(void)
{
   return "Remote";
}

static int
remote_upload_setup(char* name __attribute__((unused)), struct art* nodes)
{
   int server;
   char* label;
   struct main_configuration* config = (struct main_configuration*)shmem;

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("Remote storage engine (setup): %s/%s",
                      config->common.servers[server].name, label);
   return 0;
}

static int
remote_upload_execute(char* name __attribute__((unused)), struct art* nodes)
{
   int server;
   char* label;
   int n = 0;
   int n_parallel = 0;
   bool any_failed = false;
   struct remote_task tasks[N_ENGINES];
   struct workers* workers = NULL;
   struct backup* bck = NULL;
   struct main_configuration* config = (struct main_configuration*)shmem;

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);
   bck = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);

   pgmoneta_log_debug("Remote storage engine (execute): %s/%s",
                      config->common.servers[server].name, label);

   if (bck == NULL)
   {
      pgmoneta_log_error("remote_upload: no backup struct in nodes for %s/%s",
                         config->common.servers[server].name, label);
      return 1;
   }

   for (int i = 0; i < N_ENGINES; i++)
   {
      if (config->storage_engine & engines[i].engine_flag)
      {
         n++;
         if (engines[i].capabilities & STORAGE_CAP_PARALLEL_SAFE)
         {
            n_parallel++;
         }
      }
   }

   if (n == 0)
   {
      return 0;
   }

   if (n_parallel > 0)
   {
      if (pgmoneta_workers_initialize(n_parallel, &workers))
      {
         pgmoneta_log_warn("remote_upload: could not create worker pool; running all backends serially");
         workers = NULL;
      }
   }

   n = 0;

   /* Dispatch parallel-safe engines to the pool */
   for (int i = 0; i < N_ENGINES; i++)
   {
      if (!(config->storage_engine & engines[i].engine_flag) ||
          !(engines[i].capabilities & STORAGE_CAP_PARALLEL_SAFE) ||
          workers == NULL)
      {
         continue;
      }

      tasks[n].common.workers = workers;
      tasks[n].engine = &engines[i];
      tasks[n].server = server;
      tasks[n].label = label;
      tasks[n].compression = bck->compression;
      tasks[n].encryption = bck->encryption;
      tasks[n].result = 1;
      tasks[n].elapsed = 0.0;

      if (pgmoneta_workers_add(workers, backend_task, (struct worker_common*)&tasks[n]))
      {
         pgmoneta_log_error("remote_upload: failed to queue task for %s backend", engines[i].name);
      }
      n++;
   }

   /* Run the rest in-thread while the pool works */
   for (int i = 0; i < N_ENGINES; i++)
   {
      if (!(config->storage_engine & engines[i].engine_flag) ||
          ((engines[i].capabilities & STORAGE_CAP_PARALLEL_SAFE) && workers != NULL))
      {
         continue;
      }

      tasks[n].common.workers = NULL;
      tasks[n].engine = &engines[i];
      tasks[n].server = server;
      tasks[n].label = label;
      tasks[n].compression = bck->compression;
      tasks[n].encryption = bck->encryption;
      tasks[n].result = 1;
      tasks[n].elapsed = 0.0;

      backend_task((struct worker_common*)&tasks[n]);
      n++;
   }

   /* Wait for tasks to finish */
   pgmoneta_workers_wait(workers);
   pgmoneta_workers_destroy(workers);

   /* Collect results */
   for (int i = 0; i < n; i++)
   {
      if (tasks[i].result != 0)
      {
         pgmoneta_log_error("remote_upload: %s upload failed for %s/%s",
                            tasks[i].engine->name,
                            config->common.servers[server].name,
                            label);
         any_failed = true;
      }
   }

   if (any_failed)
   {
      /* Atomic policy: sweep every backend on any failure. TODO: partial-success policy. */
      for (int i = 0; i < n; i++)
      {
         if (tasks[i].engine->cleanup != NULL)
         {
            if (tasks[i].engine->cleanup(server, label) != 0)
            {
               pgmoneta_log_warn("remote_upload: cleanup of %s failed; orphaned objects may remain",
                                 tasks[i].engine->name);
            }
         }
         else
         {
            pgmoneta_log_warn("remote_upload: %s has no cleanup implementation; "
                              "orphaned objects may remain after upload failure",
                              tasks[i].engine->name);
         }
      }
      return 1;
   }

   /* Record per-backend timing on the live NODE_BACKUP struct */
   for (int i = 0; i < n; i++)
   {
      if (tasks[i].engine->engine_flag == STORAGE_ENGINE_SSH)
      {
         bck->remote_ssh_elapsed_time = tasks[i].elapsed;
      }
      else if (tasks[i].engine->engine_flag == STORAGE_ENGINE_S3)
      {
         bck->remote_s3_elapsed_time = tasks[i].elapsed;
      }
      else if (tasks[i].engine->engine_flag == STORAGE_ENGINE_AZURE)
      {
         bck->remote_azure_elapsed_time = tasks[i].elapsed;
      }
   }

   return 0;
}

static int
remote_upload_teardown(char* name __attribute__((unused)), struct art* nodes)
{
   int server;
   char* label;
   struct main_configuration* config = (struct main_configuration*)shmem;

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("Remote storage engine (teardown): %s/%s",
                      config->common.servers[server].name, label);

   maybe_delete_local_data(server, label);
   return 0;
}

/* Remove the local data/ dir unless local is a configured destination */
static void
maybe_delete_local_data(int server, char* label)
{
   char* root = NULL;

   if (pgmoneta_is_storage_engine_enabled(STORAGE_ENGINE_LOCAL))
   {
      return;
   }

   root = pgmoneta_get_server_backup_identifier_data(server, label);
   if (root != NULL)
   {
      pgmoneta_delete_directory(root);
      free(root);
   }
}
