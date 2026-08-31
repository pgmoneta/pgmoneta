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
#include <deque.h>
#include <files.h>
#include <http.h>
#include <logging.h>
#include <manifest.h>
#include <progress.h>
#include <se_object.h>
#include <security.h>
#include <utils.h>
#include <vfile.h>
#include <workers.h>

/* system */
#include <libgen.h>
#include <stdlib.h>
#include <string.h>

struct object_transfer_task
{
   struct worker_common common;
   const struct object_storage_ops* ops;
   int server;
   bool progress_enabled;
   char root[MAX_PATH];
   char remote_path[MAX_PATH];
   char local_root[MAX_PATH];
   char local_path[MAX_PATH];
};

struct object_download_context
{
   struct vfile* file;
   char* path;
   size_t bytes_written;
};

static int object_create_transfer_task(const struct object_storage_ops* ops, int server,
                                       char* root, char* remote_path,
                                       char* local_root, char* local_path,
                                       struct workers* workers, struct object_transfer_task** task);
static int object_download_one_file(struct object_transfer_task* task);
static void do_download_file(struct worker_common* wc);
static size_t object_download_write_cb(void* buffer, size_t size, void* userdata);
static int object_fetch_to_file(const struct object_storage_ops* ops, char* root, int server,
                                char* remote_name, char* local_root, char* local_name);

static size_t
object_download_write_cb(void* buffer, size_t size, void* userdata)
{
   struct object_download_context* ctx = (struct object_download_context*)userdata;

   if (ctx == NULL || ctx->file == NULL)
   {
      return 0;
   }

   if (ctx->file->write(ctx->file, buffer, size, false))
   {
      pgmoneta_log_error("Object download: failed to write chunk to %s", ctx->path);
      return 0;
   }

   ctx->bytes_written += size;

   return size;
}

static int
object_create_transfer_task(const struct object_storage_ops* ops, int server,
                            char* root, char* remote_path,
                            char* local_root, char* local_path,
                            struct workers* workers, struct object_transfer_task** task)
{
   struct object_transfer_task* t = NULL;

   *task = NULL;

   if (root == NULL || remote_path == NULL || local_path == NULL)
   {
      goto error;
   }

   if (strlen(root) >= MAX_PATH || strlen(remote_path) >= MAX_PATH || strlen(local_path) >= MAX_PATH)
   {
      pgmoneta_log_error("%s transfer path too long", ops->name);
      goto error;
   }

   if (local_root != NULL && strlen(local_root) >= MAX_PATH)
   {
      pgmoneta_log_error("%s local root path too long", ops->name);
      goto error;
   }

   t = (struct object_transfer_task*)malloc(sizeof(struct object_transfer_task));
   if (t == NULL)
   {
      goto error;
   }

   memset(t, 0, sizeof(struct object_transfer_task));
   pgmoneta_snprintf(t->root, sizeof(t->root), "%s", root);
   pgmoneta_snprintf(t->remote_path, sizeof(t->remote_path), "%s", remote_path);
   pgmoneta_snprintf(t->local_path, sizeof(t->local_path), "%s", local_path);
   if (local_root != NULL)
   {
      pgmoneta_snprintf(t->local_root, sizeof(t->local_root), "%s", local_root);
   }

   t->common.workers = workers;
   t->ops = ops;
   t->server = server;
   t->progress_enabled = (server >= 0 && pgmoneta_is_progress_enabled(server));

   *task = t;

   return 0;

error:
   free(t);
   return 1;
}

static int
object_download_one_file(struct object_transfer_task* task)
{
   struct http_response* response = NULL;
   struct object_download_context ctx = {0};
   char* full_local = NULL;
   char* tmp_local = NULL;
   char* parent_copy = NULL;
   char* parent = NULL;

   full_local = pgmoneta_append(full_local, task->local_root);
   if (!pgmoneta_ends_with(task->local_root, "/"))
   {
      full_local = pgmoneta_append(full_local, "/");
   }
   full_local = pgmoneta_append(full_local, task->local_path);

   tmp_local = pgmoneta_append(tmp_local, full_local);
   tmp_local = pgmoneta_append(tmp_local, ".tmp");

   parent_copy = pgmoneta_append(parent_copy, tmp_local);
   parent = dirname(parent_copy);
   if (pgmoneta_mkdir(parent))
   {
      pgmoneta_log_error("%s download: failed to create parent directory for %s",
                         task->ops->name, tmp_local);
      goto error;
   }

   if (pgmoneta_exists(tmp_local))
   {
      pgmoneta_delete_file(tmp_local, NULL);
   }

   if (pgmoneta_vfile_create_local(tmp_local, "wb", &ctx.file))
   {
      pgmoneta_log_error("%s download: failed to create local file %s", task->ops->name, tmp_local);
      goto error;
   }
   ctx.path = tmp_local;

   response = (struct http_response*)malloc(sizeof(struct http_response));
   if (response == NULL)
   {
      goto error;
   }

   memset(response, 0, sizeof(struct http_response));
   response->write_cb = object_download_write_cb;
   response->write_userdata = &ctx;

   if (task->ops->get_object(task->server, task->root, task->remote_path, &response))
   {
      pgmoneta_log_error("%s download: failed to GET %s", task->ops->name, task->remote_path);
      goto error;
   }

   if (response->status_code < 200 || response->status_code >= 300)
   {
      pgmoneta_log_error("%s download: %s returned status %d",
                         task->ops->name, task->remote_path, response->status_code);
      goto error;
   }

   pgmoneta_vfile_destroy(ctx.file);
   ctx.file = NULL;

   if (pgmoneta_move_file(tmp_local, full_local))
   {
      pgmoneta_log_error("%s download: failed to rename %s to %s",
                         task->ops->name, tmp_local, full_local);
      goto error;
   }

   if (task->progress_enabled)
   {
      pgmoneta_progress_increment(task->server, 1);
   }

   pgmoneta_log_debug("%s download: %s", task->ops->name, task->remote_path);

   pgmoneta_http_response_destroy(response);
   free(full_local);
   free(tmp_local);
   free(parent_copy);

   return 0;

error:

   if (ctx.file != NULL)
   {
      pgmoneta_vfile_destroy(ctx.file);
      ctx.file = NULL;
   }

   if (pgmoneta_exists(tmp_local))
   {
      pgmoneta_delete_file(tmp_local, NULL);
   }

   pgmoneta_http_response_destroy(response);
   free(parent_copy);
   free(full_local);
   free(tmp_local);

   return 1;
}

static void
do_download_file(struct worker_common* wc)
{
   struct object_transfer_task* task = (struct object_transfer_task*)wc;

   if (object_download_one_file(task))
   {
      pgmoneta_record_failure(task->common.workers != NULL ? task->common.workers->outcome : NULL,
                              "%s download failed: %s", task->ops->name, task->remote_path);
   }

   free(task);
}

/**
 * Fetch a single metadata object into local_root, replacing any previous copy
 */
static int
object_fetch_to_file(const struct object_storage_ops* ops, char* root, int server,
                     char* remote_name, char* local_root, char* local_name)
{
   char* path = NULL;
   struct http_response* response = NULL;

   if (ops->get_object(server, root, remote_name, &response))
   {
      pgmoneta_log_error("%s bootstrap: failed to GET %s", ops->name, remote_name);
      goto error;
   }

   if (response->status_code != 200)
   {
      pgmoneta_log_error("%s bootstrap: %s returned status %d",
                         ops->name, remote_name, response->status_code);
      goto error;
   }

   path = pgmoneta_append(path, local_root);
   path = pgmoneta_append(path, local_name);

   if (pgmoneta_exists(path))
   {
      pgmoneta_delete_file(path, NULL);
   }

   if (pgmoneta_append_file_chunk(path, response->payload.data, response->payload.data_size, 0))
   {
      pgmoneta_log_error("%s bootstrap: failed to write %s", ops->name, remote_name);
      goto error;
   }

   pgmoneta_log_debug("%s bootstrap: downloaded %s", ops->name, remote_name);

   pgmoneta_http_response_destroy(response);
   free(path);

   if (pgmoneta_is_progress_enabled(server))
   {
      pgmoneta_progress_increment(server, 1);
   }

   return 0;

error:

   pgmoneta_http_response_destroy(response);
   free(path);

   return 1;
}

int
pgmoneta_object_bootstrap(const struct object_storage_ops* ops, char* root, int server, char* local_root)
{
   char buffer[4096];
   char* expected_hash = NULL;
   char* computed_hash = NULL;
   char* sha512_path = NULL;
   char* info_path = NULL;
   FILE* sha512_file = NULL;

   pgmoneta_log_debug("%s bootstrap: downloading root files", ops->name);

   if (pgmoneta_is_progress_enabled(server))
   {
      pgmoneta_progress_set_total(server, 3);
   }

   if (object_fetch_to_file(ops, root, server, "backup.sha512", local_root, "backup.sha512.tmp"))
   {
      goto error;
   }

   if (object_fetch_to_file(ops, root, server, "backup.info", local_root, "backup.info.tmp"))
   {
      goto error;
   }

   sha512_path = pgmoneta_append(sha512_path, local_root);
   sha512_path = pgmoneta_append(sha512_path, "backup.sha512.tmp");
   info_path = pgmoneta_append(info_path, local_root);
   info_path = pgmoneta_append(info_path, "backup.info.tmp");

   sha512_file = fopen(sha512_path, "r");
   if (sha512_file == NULL)
   {
      pgmoneta_log_error("%s bootstrap: could not open %s", ops->name, sha512_path);
      goto error;
   }

   while (fgets(&buffer[0], sizeof(buffer), sha512_file) != NULL)
   {
      char* eol = strchr(&buffer[0], '\n');

      if (eol != NULL)
      {
         *eol = '\0';
      }

      if (pgmoneta_ends_with(&buffer[0], " *./backup.info"))
      {
         expected_hash = strtok(&buffer[0], " ");
         break;
      }
   }

   fclose(sha512_file);
   sha512_file = NULL;

   if (expected_hash == NULL)
   {
      pgmoneta_log_error("%s bootstrap: no backup.info entry in backup.sha512", ops->name);
      goto error;
   }

   if (pgmoneta_create_sha512_file(info_path, &computed_hash))
   {
      pgmoneta_log_error("%s bootstrap: could not compute SHA512 of backup.info", ops->name);
      goto error;
   }

   if (strcmp(expected_hash, computed_hash))
   {
      pgmoneta_log_error("%s bootstrap: backup.info SHA512 mismatch", ops->name);
      pgmoneta_log_error("%s bootstrap: expected %s", ops->name, expected_hash);
      pgmoneta_log_error("%s bootstrap: computed %s", ops->name, computed_hash);
      goto error;
   }

   pgmoneta_log_info("%s bootstrap: backup.info integrity verified", ops->name);

   if (object_fetch_to_file(ops, root, server, "backup.manifest", local_root, "backup.manifest.tmp"))
   {
      goto error;
   }

   free(sha512_path);
   free(info_path);
   free(computed_hash);

   return 0;

error:

   if (sha512_file != NULL)
   {
      fclose(sha512_file);
   }

   free(sha512_path);
   free(info_path);
   free(computed_hash);

   return 1;
}

int
pgmoneta_object_download_files(const struct object_storage_ops* ops, char* root, char* local_root,
                               int server, int compression, int encryption)
{
   int number_of_workers = 0;
   char* manifest_path = NULL;
   char* file_path = NULL;
   char* relative_file = NULL;
   char* suffix = NULL;
   struct deque* paths = NULL;
   struct deque_iterator* iter = NULL;
   struct workers* workers = NULL;
   struct object_transfer_task* task = NULL;

   manifest_path = pgmoneta_append(manifest_path, local_root);
   manifest_path = pgmoneta_append(manifest_path, "backup.manifest.tmp");

   if (pgmoneta_extraction_get_suffix(compression, encryption, &suffix))
   {
      pgmoneta_log_error("%s download: failed to determine file suffix", ops->name);
      goto error;
   }

   pgmoneta_log_debug("%s download: file suffix is '%s'", ops->name, suffix != NULL ? suffix : "(none)");

   number_of_workers = pgmoneta_get_number_of_workers(server);
   if (number_of_workers > 0)
   {
      pgmoneta_workers_initialize(number_of_workers, &workers);
   }

   if (pgmoneta_manifest_get_paths(manifest_path, &paths))
   {
      pgmoneta_log_error("%s download: failed to read manifest %s", ops->name, manifest_path);
      goto error;
   }

   pgmoneta_deque_iterator_create(paths, &iter);

   if (pgmoneta_is_progress_enabled(server))
   {
      pgmoneta_progress_set_total(server, pgmoneta_deque_size(paths));
   }

   while (pgmoneta_deque_iterator_next(iter))
   {
      file_path = iter->tag;

      relative_file = NULL;
      relative_file = pgmoneta_append(relative_file, "data/");
      relative_file = pgmoneta_append(relative_file, file_path);

      if (suffix != NULL &&
          !pgmoneta_ends_with(file_path, "backup_label") &&
          !pgmoneta_ends_with(file_path, "backup_manifest"))
      {
         relative_file = pgmoneta_append(relative_file, suffix);
      }

      if (object_create_transfer_task(ops, server, root, relative_file, local_root, relative_file,
                                      workers, &task))
      {
         pgmoneta_log_error("%s download: failed to create transfer task", ops->name);
         free(relative_file);
         goto error;
      }

      if (workers != NULL && pgmoneta_workers_outcome_ok(workers))
      {
         if (pgmoneta_workers_add(workers, do_download_file, (struct worker_common*)task))
         {
            free(task);
            task = NULL;
            pgmoneta_log_error("%s download: failed to queue worker task", ops->name);
            free(relative_file);
            goto error;
         }
         task = NULL;
      }
      else
      {
         if (object_download_one_file(task))
         {
            free(task);
            task = NULL;
            free(relative_file);
            goto error;
         }
         free(task);
         task = NULL;
      }

      free(relative_file);
      relative_file = NULL;
   }

   pgmoneta_workers_wait(workers);
   if (workers != NULL && !pgmoneta_workers_outcome_ok(workers))
   {
      pgmoneta_workers_log_failures(workers);
      goto error;
   }
   pgmoneta_workers_destroy(workers);

   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_deque_destroy(paths);
   free(manifest_path);
   free(suffix);

   return 0;

error:

   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_deque_destroy(paths);
   pgmoneta_workers_wait(workers);
   pgmoneta_workers_destroy(workers);
   free(manifest_path);
   free(suffix);
   free(task);

   return 1;
}
