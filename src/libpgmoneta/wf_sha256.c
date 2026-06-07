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
#include <logging.h>
#include <progress.h>
#include <security.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <dirent.h>

static char* sha256_name(void);
static int sha256_execute(char*, struct art*);

static void do_sha256(struct worker_common* wc);
static int dispatch_sha256_tasks(int server, char* root, char* relative_path,
                                 struct workers* workers, struct deque* all_deque);

struct workflow*
pgmoneta_create_sha256(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->name = &sha256_name;
   wf->setup = &pgmoneta_common_setup;
   wf->execute = &sha256_execute;
   wf->teardown = &pgmoneta_common_teardown;
   wf->next = NULL;

   return wf;
}

static char*
sha256_name(void)
{
   return "SHA-256";
}

static int
sha256_execute(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   char* root = NULL;
   char* d = NULL;
   char* sha256_path = NULL;
   int number_of_workers = 0;
   struct workers* workers = NULL;
   struct deque* all_deque = NULL;
   struct deque_iterator* iter = NULL;
   FILE* sha256_file = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   pgmoneta_dump_art(nodes);

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("SHA256 (execute): %s/%s", config->common.servers[server].name, label);

   root = pgmoneta_get_server_backup_identifier(server, label);
   if (root == NULL)
   {
      goto error;
   }

   sha256_path = pgmoneta_append(sha256_path, root);
   sha256_path = pgmoneta_append(sha256_path, "backup.sha256");

   d = pgmoneta_get_server_backup_identifier_data(server, label);

   if (pgmoneta_deque_create(true, &all_deque))
   {
      goto error;
   }

   number_of_workers = pgmoneta_get_number_of_workers(server);
   if (number_of_workers > 0)
   {
      pgmoneta_workers_initialize(number_of_workers, &workers);
   }

   if (pgmoneta_is_progress_enabled(server))
   {
      int file_count = pgmoneta_count_files(d);
      pgmoneta_progress_set_total(server, file_count);
   }

   if (dispatch_sha256_tasks(server, d, "", workers, all_deque))
   {
      goto error;
   }

   pgmoneta_workers_wait(workers);
   if (workers != NULL && !pgmoneta_workers_outcome_ok(workers))
   {
      pgmoneta_workers_transfer_failures(workers, nodes);
      goto error;
   }
   pgmoneta_workers_destroy(workers);
   workers = NULL;

   sha256_file = fopen(sha256_path, "w");
   if (sha256_file == NULL)
   {
      goto error;
   }

   if (pgmoneta_deque_iterator_create(all_deque, &iter))
   {
      goto error;
   }

   while (pgmoneta_deque_iterator_next(iter))
   {
      struct json* result = (struct json*)pgmoneta_value_data(iter->value);
      char* path = (char*)pgmoneta_json_get(result, "Path");
      char* hash = (char*)pgmoneta_json_get(result, "Hash");
      char* line = NULL;

      line = pgmoneta_append(line, path);
      line = pgmoneta_append(line, ":");
      line = pgmoneta_append(line, hash);
      line = pgmoneta_append(line, "\n");
      fputs(line, sha256_file);
      fflush(sha256_file);
      free(line);
   }

   pgmoneta_deque_iterator_destroy(iter);
   iter = NULL;

   pgmoneta_deque_destroy(all_deque);
   all_deque = NULL;

   pgmoneta_permission(sha256_path, 6, 0, 0);

   fflush(sha256_file);
   fclose(sha256_file);

   free(sha256_path);
   free(root);
   free(d);

   return 0;

error:

   if (workers != NULL)
   {
      pgmoneta_workers_destroy(workers);
   }

   if (sha256_file != NULL)
   {
      fflush(sha256_file);
      fclose(sha256_file);
   }

   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_deque_destroy(all_deque);

   free(sha256_path);
   free(root);
   free(d);

   return 1;
}

static void
do_sha256(struct worker_common* wc)
{
   struct worker_input* wi = (struct worker_input*)wc;
   char* sha256 = NULL;
   struct json* result = NULL;

   if (pgmoneta_create_sha256_file(wi->from, &sha256))
   {
      pgmoneta_record_failure(wi->common.workers->outcome, "SHA256 failed: %s", wi->from);
      goto done;
   }

   if (pgmoneta_json_create(&result))
   {
      pgmoneta_record_failure(wi->common.workers->outcome, "SHA256 allocation failed: %s", wi->from);
      goto done;
   }

   pgmoneta_json_put(result, "Path", (uintptr_t)wi->to, ValueString);
   pgmoneta_json_put(result, "Hash", (uintptr_t)sha256, ValueString);

   pgmoneta_deque_add(wi->all, wi->from, (uintptr_t)result, ValueJSON);
   result = NULL;

   if (pgmoneta_is_progress_enabled(wi->level))
   {
      pgmoneta_progress_increment(wi->level, 1);
   }

done:
   pgmoneta_json_destroy(result);
   free(sha256);
   wi->all = NULL;
   free(wi);
}

static int
dispatch_sha256_tasks(int server, char* root, char* relative_path,
                      struct workers* workers, struct deque* all_deque)
{
   char* dir_path = NULL;
   DIR* dir = NULL;
   struct dirent* entry;

   dir_path = pgmoneta_append(dir_path, root);
   dir_path = pgmoneta_append(dir_path, relative_path);

   if (!(dir = opendir(dir_path)))
   {
      goto error;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      char entry_path[MAX_PATH];
      bool is_dir;

      if (pgmoneta_compare_string(entry->d_name, ".") || pgmoneta_compare_string(entry->d_name, ".."))
      {
         continue;
      }

      pgmoneta_snprintf(entry_path, sizeof(entry_path), "%s/%s", dir_path, entry->d_name);

      is_dir = (entry->d_type == DT_DIR) ||
               ((entry->d_type == DT_LNK || entry->d_type == DT_UNKNOWN) &&
                pgmoneta_is_directory(entry_path));

      if (is_dir)
      {
         char relative_dir[MAX_PATH];
         pgmoneta_snprintf(relative_dir, sizeof(relative_dir), "%s/%s", relative_path, entry->d_name);

         if (dispatch_sha256_tasks(server, root, relative_dir, workers, all_deque))
         {
            goto error;
         }
      }
      else
      {
         char relative_file[MAX_PATH];
         char absolute_file[MAX_PATH];
         struct worker_input* payload = NULL;

         pgmoneta_snprintf(relative_file, sizeof(relative_file), "%s/%s", relative_path, entry->d_name);
         pgmoneta_snprintf(absolute_file, sizeof(absolute_file), "%s%s", root, relative_file);

         if (pgmoneta_create_worker_input(NULL, absolute_file, relative_file, server, workers, &payload))
         {
            goto error;
         }

         payload->all = all_deque;

         if (workers != NULL)
         {
            if (pgmoneta_workers_outcome_ok(workers))
            {
               if (pgmoneta_workers_add(workers, do_sha256, (struct worker_common*)payload))
               {
                  free(payload);
                  goto error;
               }
            }
            else
            {
               free(payload);
            }
         }
         else
         {
            do_sha256((struct worker_common*)payload);
         }
      }
   }

   closedir(dir);

   free(dir_path);

   return 0;

error:

   if (dir != NULL)
   {
      closedir(dir);
   }

   free(dir_path);

   return 1;
}
