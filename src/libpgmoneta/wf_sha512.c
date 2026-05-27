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
#include <verify.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

static char* sha512_name(void);
static int sha512_execute(char*, struct art*);

static void do_sha512(struct worker_common* wc);
static int dispatch_sha512_tasks(int server, char* root, char* relative_path,
                                 struct workers* workers, struct deque* all_deque);

struct workflow*
pgmoneta_create_sha512(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->name = &sha512_name;
   wf->setup = &pgmoneta_common_setup;
   wf->execute = &sha512_execute;
   wf->teardown = &pgmoneta_common_teardown;
   wf->next = NULL;

   return wf;
}

static char*
sha512_name(void)
{
   return PHASE_NAME_SHA512;
}

static int
sha512_execute(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   struct timespec start_t;
   struct timespec end_t;
   char* root = NULL;
   char* sha512_path = NULL;
   char* server_backup = NULL;
   struct backup* backup = NULL;
   int number_of_workers = 0;
   struct workers* workers = NULL;
   struct deque* all_deque = NULL;
   struct deque_iterator* iter = NULL;
   FILE* sha512_file = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   //Start timer
#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

#ifdef DEBUG
   pgmoneta_dump_art(nodes);

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);
   backup = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);
   server_backup = (char*)pgmoneta_art_search(nodes, NODE_SERVER_BACKUP);

   pgmoneta_log_debug("SHA512 (execute): %s/%s", config->common.servers[server].name, label);

   root = pgmoneta_get_server_backup_identifier(server, label);
   if (root == NULL)
   {
      goto error;
   }

   sha512_path = pgmoneta_append(sha512_path, root);
   if (!pgmoneta_ends_with(sha512_path, "/"))
   {
      sha512_path = pgmoneta_append_char(sha512_path, '/');
   }
   sha512_path = pgmoneta_append(sha512_path, "backup.sha512");

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
      int file_count = pgmoneta_count_files(root);
      pgmoneta_progress_set_total(server, file_count);
   }

   if (dispatch_sha512_tasks(server, root, "", workers, all_deque))
   {
      pgmoneta_log_error("SHA512: dispatch failed for %s", root);
      goto error;
   }

   pgmoneta_workers_wait(workers);
   if (workers != NULL && !pgmoneta_workers_outcome_ok(workers))
   {
      pgmoneta_log_error("SHA512: one or more worker tasks failed for %s", root);
      pgmoneta_workers_transfer_failures(workers, nodes);
      goto error;
   }
   pgmoneta_workers_destroy(workers);
   workers = NULL;

   sha512_file = fopen(sha512_path, "w");
   if (sha512_file == NULL)
   {
      pgmoneta_log_error("SHA512: could not open %s for writing (%s)", sha512_path, strerror(errno));
      errno = 0;
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

      line = pgmoneta_append(line, hash);
      line = pgmoneta_append(line, " *.");
      line = pgmoneta_append(line, path);
      line = pgmoneta_append(line, "\n");
      fputs(line, sha512_file);
      fflush(sha512_file);
      free(line);
   }

   pgmoneta_deque_iterator_destroy(iter);
   iter = NULL;

   pgmoneta_deque_destroy(all_deque);
   all_deque = NULL;

   pgmoneta_permission(sha512_path, 6, 0, 0);

   //Stop timer
#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (backup != NULL)
   {
      backup->hash_elapsed_time = pgmoneta_compute_duration(start_t, end_t);
   }

   if (server_backup != NULL && backup != NULL)
   {
      pgmoneta_save_info(server_backup, backup);
   }

   fflush(sha512_file);
   fsync(fileno(sha512_file));
   fclose(sha512_file);

   free(sha512_path);
   free(root);

   return 0;

error:

   if (workers != NULL)
   {
      pgmoneta_workers_destroy(workers);
   }

   if (sha512_file != NULL)
   {
      fflush(sha512_file);
      fclose(sha512_file);
   }

   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_deque_destroy(all_deque);

   free(sha512_path);
   free(root);

   return 1;
}

static void
do_sha512(struct worker_common* wc)
{
   struct worker_input* wi = (struct worker_input*)wc;
   char* sha512 = NULL;
   struct json* result = NULL;

   if (pgmoneta_create_sha512_file(wi->from, &sha512))
   {
      char* msg = NULL;
      pgmoneta_log_error("SHA512 failed for file: %s", wi->from);
      msg = pgmoneta_format_and_append(msg, "SHA512 failed: %s", wi->from);
      pgmoneta_workers_record_failure(wi->common.workers, msg);
      free(msg);
      goto done;
   }

   if (pgmoneta_json_create(&result))
   {
      char* msg = NULL;
      msg = pgmoneta_format_and_append(msg, "SHA512 allocation failed: %s", wi->from);
      pgmoneta_workers_record_failure(wi->common.workers, msg);
      free(msg);
      goto done;
   }

   pgmoneta_json_put(result, "Path", (uintptr_t)wi->to, ValueString);
   pgmoneta_json_put(result, "Hash", (uintptr_t)sha512, ValueString);

   pgmoneta_deque_add(wi->all, wi->from, (uintptr_t)result, ValueJSON);
   result = NULL;

   if (pgmoneta_is_progress_enabled(wi->level))
   {
      pgmoneta_progress_increment(wi->level, 1);
   }

done:
   pgmoneta_json_destroy(result);
   free(sha512);
   wi->all = NULL;
   free(wi);
}

static int
dispatch_sha512_tasks(int server, char* root, char* relative_path,
                      struct workers* workers, struct deque* all_deque)
{
   char* dir_path = NULL;
   DIR* dir = NULL;
   struct dirent* entry;

   dir_path = pgmoneta_append(dir_path, root);
   dir_path = pgmoneta_append(dir_path, relative_path);

   if (!(dir = opendir(dir_path)))
   {
      pgmoneta_log_error("dispatch_sha512_tasks: opendir failed for %s (%s)", dir_path, strerror(errno));
      errno = 0;
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

         if (dispatch_sha512_tasks(server, root, relative_dir, workers, all_deque))
         {
            goto error;
         }
      }
      else if (!pgmoneta_compare_string(entry->d_name, "backup.sha512"))
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
               if (pgmoneta_workers_add(workers, do_sha512, (struct worker_common*)payload))
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
            do_sha512((struct worker_common*)payload);
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

int
pgmoneta_update_sha512(char* root_dir, char* filename)
{
   char buffer[4096];
   char line[4096];
   bool found = false;
   char* sha512_path = NULL;
   char* sha512_tmp_path = NULL;
   FILE* source_file = NULL;
   FILE* dest_file = NULL;
   char* absolute_file_path = NULL;
   char* new_sha512 = NULL;
   char* new_line = NULL;

   sha512_path = pgmoneta_append(sha512_path, root_dir);
   if (!pgmoneta_ends_with(sha512_path, "/"))
   {
      sha512_path = pgmoneta_append_char(sha512_path, '/');
   }
   sha512_path = pgmoneta_append(sha512_path, "backup.sha512");

   sha512_tmp_path = pgmoneta_append(sha512_tmp_path, root_dir);
   if (!pgmoneta_ends_with(sha512_tmp_path, "/"))
   {
      sha512_tmp_path = pgmoneta_append_char(sha512_tmp_path, '/');
   }
   sha512_tmp_path = pgmoneta_append(sha512_tmp_path, "backup.sha512.tmp");

   absolute_file_path = pgmoneta_append(absolute_file_path, root_dir);
   absolute_file_path = pgmoneta_append(absolute_file_path, "/");
   absolute_file_path = pgmoneta_append(absolute_file_path, filename);

   if (pgmoneta_create_sha512_file(absolute_file_path, &new_sha512))
   {
      pgmoneta_log_error("Could not create SHA512 hash for %s", absolute_file_path);
      goto error;
   }

   source_file = fopen(sha512_path, "r");
   if (source_file == NULL)
   {
      source_file = fopen(sha512_path, "w");
      if (source_file == NULL)
      {
         pgmoneta_log_error("Could not create file %s due to %s", sha512_path, strerror(errno));
         errno = 0;
         goto error;
      }
      fflush(source_file);
      fclose(source_file);
      source_file = fopen(sha512_path, "r");
      if (source_file == NULL)
      {
         pgmoneta_log_error("Could not open file %s due to %s", sha512_path, strerror(errno));
         errno = 0;
         goto error;
      }
   }

   dest_file = fopen(sha512_tmp_path, "w");
   if (dest_file == NULL)
   {
      pgmoneta_log_error("Could not open file %s due to %s", sha512_tmp_path, strerror(errno));
      errno = 0;
      goto error;
   }

   while ((fgets(&buffer[0], sizeof(buffer), source_file)) != NULL)
   {
      char* file_entry;

      memset(&line[0], 0, sizeof(line));
      memcpy(&line[0], &buffer[0], strlen(&buffer[0]));

      file_entry = strstr(&buffer[0], filename);

      if (file_entry != NULL)
      {
         new_line = pgmoneta_append(new_line, new_sha512);
         new_line = pgmoneta_append(new_line, " *./");
         new_line = pgmoneta_append(new_line, filename);
         new_line = pgmoneta_append(new_line, "\n");

         fputs(new_line, dest_file);
         found = true;
         free(new_line);
         new_line = NULL;
      }
      else
      {
         fputs(&line[0], dest_file);
      }
      fflush(dest_file);
   }

   if (!found)
   {
      new_line = pgmoneta_append(new_line, new_sha512);
      new_line = pgmoneta_append(new_line, " *.");
      new_line = pgmoneta_append(new_line, filename);
      new_line = pgmoneta_append(new_line, "\n");

      fputs(new_line, dest_file);
      fflush(dest_file);
      pgmoneta_log_trace("Added new SHA512 entry for %s", filename);
      free(new_line);
      new_line = NULL;
   }

   if (source_file != NULL)
   {
      fsync(fileno(source_file));
      fclose(source_file);
   }

   if (dest_file != NULL)
   {
      fflush(dest_file);
      fsync(fileno(dest_file));
      fclose(dest_file);
   }

   pgmoneta_move_file(sha512_tmp_path, sha512_path);
   pgmoneta_permission(sha512_path, 6, 0, 0);

   pgmoneta_log_trace("Updated SHA512 hash for %s", filename);

   free(sha512_path);
   free(sha512_tmp_path);
   free(absolute_file_path);
   free(new_sha512);

   return 0;

error:
   if (source_file != NULL)
   {
      fclose(source_file);
   }

   if (dest_file != NULL)
   {
      fflush(dest_file);
      fclose(dest_file);
   }

   free(sha512_path);
   free(sha512_tmp_path);
   free(absolute_file_path);
   free(new_sha512);
   free(new_line);

   return 1;
}
