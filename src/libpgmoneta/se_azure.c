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
#include <storage.h>
#include <utils.h>
#include <vfile.h>
#include <workers.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>

static char* azure_storage_name(void);
static int azure_storage_setup(char* name, struct art*);
static int azure_storage_execute(char* name, struct art*);
static int azure_storage_teardown(char* name, struct art*);

struct azure_transfer_task
{
   struct worker_common common;
   int server;
   bool progress_enabled;
   char azure_root[MAX_PATH];
   char remote_path[MAX_PATH];
   char local_root[MAX_PATH];
   char local_path[MAX_PATH];
};

static int azure_upload_files(char* local_root, char* azure_root, int server, int compression, int encryption);
static int azure_send_upload_request(char* local_root, char* azure_root, char* relative_path);
static int azure_add_request_headers(struct http_request* request, char* auth_value, char* utc_date, bool blob_type);
static void do_upload_file(struct worker_common* wc);
static int azure_create_transfer_task(int server, char* azure_root, char* remote_path,
                                      char* local_root, char* local_path,
                                      struct workers* workers, struct azure_transfer_task** task);
static int azure_upload_one_file(struct azure_transfer_task* task);

static int azure_send_request(int method, char* azure_root, char* relative_path, struct http_response** response);
static int azure_list_blobs(char* prefix, struct deque** names);
static int azure_delete_blob(char* name);
static int azure_object_exists(char* azure_root, char* relative_path, int server);
static char* azure_build_object_path(char* azure_root, char* relative_path);
static int azure_build_authorization(char* signed_headers_prefix, char* azure_path,
                                     char* canonical_query, char* utc_date, char** auth_value);
static int azure_http_prepare(char* azure_path, char** azure_host, char* request_path,
                              size_t request_path_size, struct http** connection);

static char* azure_get_host(void);
static char* azure_get_basepath(int server, char* identifier);

struct azure_upload_file_context
{
   struct vfile* file;
   char* path;
};

static size_t azure_upload_read_cb(void* buffer, size_t size, void* userdata);

struct workflow*
pgmoneta_storage_create_azure(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->name = &azure_storage_name;
   wf->setup = &azure_storage_setup;
   wf->execute = &azure_storage_execute;
   wf->teardown = &azure_storage_teardown;
   wf->next = NULL;

   return wf;
}

static char*
azure_storage_name(void)
{
   return "Azure";
}

static int
azure_storage_setup(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   pgmoneta_dump_art(nodes);

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("Azure storage engine (setup): %s/%s", config->common.servers[server].name, label);

   return 0;
}

static int
azure_storage_execute(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double remote_azure_elapsed_time;
   char* local_root = NULL;
   char* base_dir = NULL;
   char* azure_root = NULL;
   struct main_configuration* config;
   struct backup* temp_backup = NULL;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   pgmoneta_dump_art(nodes);

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("Azure storage engine (execute): %s/%s", config->common.servers[server].name, label);

   local_root = pgmoneta_get_server_backup_identifier(server, label);
   base_dir = pgmoneta_get_server_backup(server);
   azure_root = azure_get_basepath(server, label);

   if (pgmoneta_load_info(base_dir, label, &temp_backup))
   {
      pgmoneta_log_error("Unable to get backup for directory %s", base_dir);
      goto error;
   }

   if (azure_upload_files(local_root, azure_root, server, temp_backup->compression, temp_backup->encryption))
   {
      goto error;
   }

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   remote_azure_elapsed_time = pgmoneta_compute_duration(start_t, end_t);
   temp_backup->remote_azure_elapsed_time = remote_azure_elapsed_time;
   if (pgmoneta_save_info(base_dir, temp_backup))
   {
      pgmoneta_log_error("Unable to save backup info for directory %s", base_dir);
      goto error;
   }

   free(temp_backup);
   free(local_root);
   free(base_dir);
   free(azure_root);

   return 0;

error:

   free(temp_backup);
   free(local_root);
   free(base_dir);
   free(azure_root);

   return 1;
}

static int
azure_storage_teardown(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   pgmoneta_dump_art(nodes);

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("Azure storage engine (teardown): %s/%s", config->common.servers[server].name, label);

   return 0;
}

static int
azure_create_transfer_task(int server, char* azure_root, char* remote_path,
                           char* local_root, char* local_path,
                           struct workers* workers, struct azure_transfer_task** task)
{
   struct azure_transfer_task* t = NULL;

   *task = NULL;

   if (azure_root == NULL || remote_path == NULL || local_path == NULL)
   {
      goto error;
   }

   if (strlen(azure_root) >= MAX_PATH || strlen(remote_path) >= MAX_PATH || strlen(local_path) >= MAX_PATH)
   {
      pgmoneta_log_error("Azure transfer path too long");
      goto error;
   }

   if (local_root != NULL && strlen(local_root) >= MAX_PATH)
   {
      pgmoneta_log_error("Azure local root path too long");
      goto error;
   }

   t = (struct azure_transfer_task*)malloc(sizeof(struct azure_transfer_task));
   if (t == NULL)
   {
      goto error;
   }

   memset(t, 0, sizeof(struct azure_transfer_task));
   pgmoneta_snprintf(t->azure_root, sizeof(t->azure_root), "%s", azure_root);
   pgmoneta_snprintf(t->remote_path, sizeof(t->remote_path), "%s", remote_path);
   pgmoneta_snprintf(t->local_path, sizeof(t->local_path), "%s", local_path);
   if (local_root != NULL)
   {
      pgmoneta_snprintf(t->local_root, sizeof(t->local_root), "%s", local_root);
   }

   t->common.workers = workers;
   t->server = server;
   t->progress_enabled = (server >= 0 && pgmoneta_is_progress_enabled(server));

   *task = t;

   return 0;

error:
   free(t);
   return 1;
}

static int
azure_upload_one_file(struct azure_transfer_task* task)
{
   if (azure_send_upload_request(task->local_root, task->azure_root, task->remote_path))
   {
      pgmoneta_log_error("Azure upload: failed %s", task->remote_path);
      return 1;
   }

   if (task->progress_enabled)
   {
      pgmoneta_progress_increment(task->server, 1);
   }

   return 0;
}

static void
do_upload_file(struct worker_common* wc)
{
   struct azure_transfer_task* task = (struct azure_transfer_task*)wc;

   if (azure_upload_one_file(task))
   {
      pgmoneta_record_failure(task->common.workers != NULL ? task->common.workers->outcome : NULL,
                              "Azure upload failed: %s", task->remote_path);
   }

   free(task);
}

static int azure_upload_metadata_files(char* local_root, char* azure_root);

static int
azure_upload_metadata_files(char* local_root, char* azure_root)
{
   if (azure_send_upload_request(local_root, azure_root, "backup.manifest"))
   {
      pgmoneta_log_error("Azure upload: failed to upload backup.manifest");
      return 1;
   }
   if (azure_send_upload_request(local_root, azure_root, "backup.sha512"))
   {
      pgmoneta_log_error("Azure upload: failed to upload backup.sha512");
      return 1;
   }
   if (azure_send_upload_request(local_root, azure_root, "backup.info"))
   {
      pgmoneta_log_error("Azure upload: failed to upload backup.info");
      return 1;
   }

   return 0;
}

static int
azure_upload_files(char* local_root, char* azure_root, int server, int compression, int encryption)
{
   int number_of_workers = 0;
   char* manifest_path = NULL;
   char* file_path = NULL;
   char* relative_file = NULL;
   char* suffix = NULL;
   struct deque* paths = NULL;
   struct deque_iterator* iter = NULL;
   struct workers* workers = NULL;
   struct azure_transfer_task* task = NULL;

   manifest_path = pgmoneta_append(manifest_path, local_root);
   manifest_path = pgmoneta_append(manifest_path, "backup.manifest");

   if (pgmoneta_extraction_get_suffix(compression, encryption, &suffix))
   {
      pgmoneta_log_error("Azure upload: failed to determine file suffix");
      goto error;
   }

   number_of_workers = pgmoneta_get_number_of_workers(server);
   if (number_of_workers > 0)
   {
      pgmoneta_workers_initialize(number_of_workers, &workers);
   }

   if (pgmoneta_manifest_get_paths(manifest_path, &paths))
   {
      pgmoneta_log_error("Azure upload: failed to read manifest %s", manifest_path);
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

      if (azure_create_transfer_task(server, azure_root, relative_file, local_root, relative_file,
                                     workers, &task))
      {
         pgmoneta_log_error("Azure upload: failed to create transfer task");
         free(relative_file);
         goto error;
      }

      if (workers != NULL)
      {
         if (!pgmoneta_workers_outcome_ok(workers))
         {
            /* a worker already failed: stop queueing, the outcome check below fails the job */
            free(task);
            task = NULL;
            free(relative_file);
            relative_file = NULL;
            break;
         }

         if (pgmoneta_workers_add(workers, do_upload_file, (struct worker_common*)task))
         {
            free(task);
            task = NULL;
            pgmoneta_log_error("Azure upload: failed to queue worker task");
            free(relative_file);
            goto error;
         }
         task = NULL;
      }
      else
      {
         if (azure_upload_one_file(task))
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
   iter = NULL;
   paths = NULL;

   /* upload metadata files last (commit marker) */
   if (azure_upload_metadata_files(local_root, azure_root))
   {
      goto error;
   }

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

static char*
azure_build_object_path(char* azure_root, char* relative_path)
{
   char* azure_path = NULL;

   azure_path = pgmoneta_append(azure_path, azure_root);
   if (relative_path != NULL && strlen(relative_path) > 0)
   {
      if (!pgmoneta_ends_with(azure_root, "/"))
      {
         azure_path = pgmoneta_append(azure_path, "/");
      }
      azure_path = pgmoneta_append(azure_path, relative_path);
   }

   return azure_path;
}

/**
 * Build a SharedKey Authorization header.
 *
 * signed_headers_prefix must contain the method and header lines and end with
 * "x-ms-date:". This appends the UTC date and canonical resource, then signs.
 * A NULL azure_path addresses the container itself. canonical_query, when not
 * NULL, holds the canonicalized query parameters ("\nname:value" lines sorted
 * by name, values not URL-encoded).
 */
static int
azure_build_authorization(char* signed_headers_prefix, char* azure_path,
                          char* canonical_query, char* utc_date, char** auth_value)
{
   char* string_to_sign = NULL;
   char* signing_key = NULL;
   char* base64_signature = NULL;
   size_t base64_signature_length = 0;
   unsigned char* signature_hmac = NULL;
   int hmac_length = 0;
   size_t signing_key_length = 0;
   struct main_configuration* config;

   *auth_value = NULL;
   config = (struct main_configuration*)shmem;

   if (signed_headers_prefix == NULL || utc_date == NULL)
   {
      goto error;
   }

   if (strchr(config->azure_storage_account, ' ') != NULL)
   {
      pgmoneta_log_error("Azure storage account name contains spaces: '%s'. This is not allowed.",
                         config->azure_storage_account);
      goto error;
   }

   string_to_sign = pgmoneta_append(string_to_sign, signed_headers_prefix);
   string_to_sign = pgmoneta_append(string_to_sign, utc_date);
   string_to_sign = pgmoneta_append(string_to_sign, "\nx-ms-version:2021-08-06\n/");
   string_to_sign = pgmoneta_append(string_to_sign, config->azure_storage_account);
   string_to_sign = pgmoneta_append(string_to_sign, "/");
   /* For path-style endpoints (Azurite), the URI already contains /<account>/<container>/<blob>.
    * The canonical resource is "/" + account_name + uri_path, so the account appears twice. */
   if (strlen(config->azure_endpoint) > 0)
   {
      string_to_sign = pgmoneta_append(string_to_sign, config->azure_storage_account);
      string_to_sign = pgmoneta_append(string_to_sign, "/");
   }
   string_to_sign = pgmoneta_append(string_to_sign, config->azure_container);
   if (azure_path != NULL)
   {
      string_to_sign = pgmoneta_append(string_to_sign, "/");
      string_to_sign = pgmoneta_append(string_to_sign, azure_path);
   }
   if (canonical_query != NULL)
   {
      string_to_sign = pgmoneta_append(string_to_sign, canonical_query);
   }

   if (pgmoneta_base64_decode(config->azure_shared_key, strlen(config->azure_shared_key),
                              (void**)&signing_key, &signing_key_length))
   {
      goto error;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash(signing_key, signing_key_length, string_to_sign,
                                                 strlen(string_to_sign), &signature_hmac, &hmac_length))
   {
      goto error;
   }

   if (pgmoneta_base64_encode((char*)signature_hmac, hmac_length, &base64_signature,
                              &base64_signature_length))
   {
      goto error;
   }

   *auth_value = pgmoneta_append(*auth_value, "SharedKey ");
   *auth_value = pgmoneta_append(*auth_value, config->azure_storage_account);
   *auth_value = pgmoneta_append(*auth_value, ":");
   *auth_value = pgmoneta_append(*auth_value, base64_signature);

   free(string_to_sign);
   free(signing_key);
   free(base64_signature);
   free(signature_hmac);

   return 0;

error:

   free(string_to_sign);
   free(signing_key);
   free(base64_signature);
   free(signature_hmac);
   free(*auth_value);
   *auth_value = NULL;

   return 1;
}

static int
azure_http_prepare(char* azure_path, char** azure_host, char* request_path,
                   size_t request_path_size, struct http** connection)
{
   bool use_endpoint;
   int conn_port;
   bool conn_tls;
   struct main_configuration* config;

   *azure_host = NULL;
   *connection = NULL;
   config = (struct main_configuration*)shmem;

   *azure_host = azure_get_host();
   if (*azure_host == NULL)
   {
      goto error;
   }

   use_endpoint = (strlen(config->azure_endpoint) > 0);
   conn_port = use_endpoint ? (config->azure_port > 0 ? config->azure_port : 443) : 443;
   conn_tls = use_endpoint ? config->azure_use_tls : true;

   if (pgmoneta_http_create(*azure_host, conn_port, conn_tls, connection))
   {
      pgmoneta_log_error("Failed to connect to Azure host: %s:%d", *azure_host, conn_port);
      goto error;
   }

   if (use_endpoint)
   {
      pgmoneta_snprintf(request_path, request_path_size, "/%s/%s%s%s",
                        config->azure_storage_account, config->azure_container,
                        azure_path != NULL ? "/" : "", azure_path != NULL ? azure_path : "");
   }
   else
   {
      pgmoneta_snprintf(request_path, request_path_size, "/%s%s%s",
                        config->azure_container,
                        azure_path != NULL ? "/" : "", azure_path != NULL ? azure_path : "");
   }

   return 0;

error:

   if (*connection != NULL)
   {
      pgmoneta_http_destroy(*connection);
      *connection = NULL;
   }

   free(*azure_host);
   *azure_host = NULL;

   return 1;
}

static int
azure_send_upload_request(char* local_root, char* azure_root, char* relative_path)
{
   char utc_date[UTC_TIME_LENGTH];
   char* local_path = NULL;
   char* azure_path = NULL;
   char* azure_host = NULL;
   char* auth_value = NULL;
   char* signed_headers_prefix = NULL;
   struct azure_upload_file_context upload_ctx = {0};
   struct stat file_info;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   struct main_configuration* config;
   char size_str[64];
   char azure_put_path[MAX_PATH];

   config = (struct main_configuration*)shmem;

   local_path = pgmoneta_append(local_path, local_root);
   if (strlen(relative_path) > 0)
   {
      if (!pgmoneta_ends_with(local_root, "/"))
      {
         local_path = pgmoneta_append(local_path, "/");
      }
      local_path = pgmoneta_append(local_path, relative_path);
   }

   azure_path = azure_build_object_path(azure_root, relative_path);
   if (azure_path == NULL)
   {
      goto error;
   }

   memset(&utc_date[0], 0, sizeof(utc_date));

   if (pgmoneta_get_timestamp_UTC_format(utc_date))
   {
      goto error;
   }

   if (stat(local_path, &file_info) != 0)
   {
      pgmoneta_log_error("Azure upload: local file stat failed for %s", local_path);
      goto error;
   }

   if (file_info.st_size == 0)
   {
      signed_headers_prefix = pgmoneta_append(signed_headers_prefix,
                                              "PUT\n\n\n\n\napplication/octet-stream\n\n\n\n\n\n\nx-ms-blob-type:BlockBlob\nx-ms-date:");
   }
   else
   {
      pgmoneta_snprintf(size_str, sizeof(size_str), "%ld", (long)file_info.st_size);
      signed_headers_prefix = pgmoneta_append(signed_headers_prefix, "PUT\n\n\n");
      signed_headers_prefix = pgmoneta_append(signed_headers_prefix, size_str);
      signed_headers_prefix = pgmoneta_append(signed_headers_prefix,
                                              "\n\napplication/octet-stream\n\n\n\n\n\n\nx-ms-blob-type:BlockBlob\nx-ms-date:");
   }

   if (azure_build_authorization(signed_headers_prefix, azure_path, NULL, utc_date, &auth_value))
   {
      goto error;
   }

   if (azure_http_prepare(azure_path, &azure_host, azure_put_path, sizeof(azure_put_path), &connection))
   {
      goto error;
   }

   pgmoneta_log_debug("Azure upload request prepared: host=%s, path=%s, file=%s, size=%ld",
                      azure_host, azure_put_path, local_path, (long)file_info.st_size);

   if (pgmoneta_http_request_create(PGMONETA_HTTP_PUT, azure_put_path, &request))
   {
      goto error;
   }

   if (azure_add_request_headers(request, auth_value, utc_date, true))
   {
      goto error;
   }

   if (pgmoneta_http_request_add_header(request, "Content-Type", "application/octet-stream"))
   {
      goto error;
   }

   /* Stream the file instead of reading it into memory */
   pgmoneta_snprintf(size_str, sizeof(size_str), "%ld", (long)file_info.st_size);
   if (pgmoneta_http_request_add_header(request, "Content-Length", size_str))
   {
      goto error;
   }

   if (pgmoneta_vfile_create_local(local_path, "rb", &upload_ctx.file))
   {
      pgmoneta_log_error("Azure upload: failed to open local file %s", local_path);
      goto error;
   }
   upload_ctx.path = local_path;

   request->read_cb = azure_upload_read_cb;
   request->read_userdata = &upload_ctx;

   if (pgmoneta_http_invoke(connection, request, &response))
   {
      pgmoneta_log_error("Failed to execute HTTP PUT request for %s", local_path);
      goto error;
   }

   if (response->status_code >= 200 && response->status_code < 300)
   {
      char* azure_url = NULL;
      azure_url = pgmoneta_append(azure_url, "https://");
      azure_url = pgmoneta_append(azure_url, azure_host);
      azure_url = pgmoneta_append(azure_url, "/");
      azure_url = pgmoneta_append(azure_url, config->azure_container);
      azure_url = pgmoneta_append(azure_url, "/");
      azure_url = pgmoneta_append(azure_url, azure_path);

      pgmoneta_log_info("Successfully uploaded file to URL: %s", azure_url);
      free(azure_url);
   }
   else
   {
      pgmoneta_log_error("Azure upload failed with status code: %d. Failed to upload: %s to Azure path: %s. Azure container: %s, host: %s",
                         response->status_code, local_path, azure_path,
                         config->azure_container, azure_host);
      goto error;
   }

   free(local_path);
   free(azure_path);
   free(azure_host);
   free(auth_value);
   free(signed_headers_prefix);

   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);
   pgmoneta_vfile_destroy(upload_ctx.file);

   return 0;

error:

   free(local_path);
   free(azure_path);
   free(azure_host);
   free(auth_value);
   free(signed_headers_prefix);

   if (connection != NULL)
   {
      pgmoneta_http_destroy(connection);
   }

   if (request != NULL)
   {
      pgmoneta_http_request_destroy(request);
   }

   if (response != NULL)
   {
      pgmoneta_http_response_destroy(response);
   }

   if (upload_ctx.file != NULL)
   {
      pgmoneta_vfile_destroy(upload_ctx.file);
   }

   return 1;
}

static size_t
azure_upload_read_cb(void* buffer, size_t size, void* userdata)
{
   struct azure_upload_file_context* ctx = (struct azure_upload_file_context*)userdata;
   size_t bytes_read = 0;
   bool last_chunk = false;

   if (ctx == NULL || ctx->file == NULL)
   {
      return 0;
   }

   if (ctx->file->read(ctx->file, buffer, size, &bytes_read, &last_chunk))
   {
      pgmoneta_log_error("Azure upload: failed to read chunk from %s", ctx->path);
      return 0;
   }

   return bytes_read;
}

static char*
azure_method_name(int method)
{
   switch (method)
   {
      case PGMONETA_HTTP_GET:
         return "GET";
      case PGMONETA_HTTP_HEAD:
         return "HEAD";
      case PGMONETA_HTTP_DELETE:
         return "DELETE";
      default:
         return NULL;
   }
}

/*
 * Send a body-less request. A NULL azure_path addresses the container itself;
 * query is appended to the URI and canonical_query is its signed form (see
 * azure_build_authorization).
 */
static int
azure_send_container_request(int method, char* azure_path, char* query, char* canonical_query,
                             struct http_response** response)
{
   char utc_date[UTC_TIME_LENGTH];
   char* method_name = NULL;
   char* signed_headers_prefix = NULL;
   char* azure_host = NULL;
   char* auth_value = NULL;
   char* uri = NULL;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   char request_path[MAX_PATH];

   method_name = azure_method_name(method);
   if (method_name == NULL)
   {
      goto error;
   }

   memset(&utc_date[0], 0, sizeof(utc_date));

   if (pgmoneta_get_timestamp_UTC_format(utc_date))
   {
      goto error;
   }

   signed_headers_prefix = pgmoneta_format_and_append(NULL, "%s\n\n\n\n\n\n\n\n\n\n\n\nx-ms-date:", method_name);

   if (azure_build_authorization(signed_headers_prefix, azure_path, canonical_query, utc_date, &auth_value))
   {
      goto error;
   }

   if (azure_http_prepare(azure_path, &azure_host, request_path, sizeof(request_path), &connection))
   {
      goto error;
   }

   uri = pgmoneta_append(uri, request_path);
   if (query != NULL)
   {
      uri = pgmoneta_append(uri, query);
   }

   pgmoneta_log_debug("Azure %s request prepared: host=%s, path=%s", method_name, azure_host, uri);

   if (pgmoneta_http_request_create(method, uri, &request))
   {
      goto error;
   }

   if (azure_add_request_headers(request, auth_value, utc_date, false))
   {
      goto error;
   }

   if (pgmoneta_http_invoke(connection, request, response))
   {
      pgmoneta_log_error("Failed to execute HTTP %s request for %s", method_name, uri);
      goto error;
   }

   pgmoneta_log_debug("Azure %s response: path=%s, status=%d", method_name, uri,
                      (*response)->status_code);

   free(signed_headers_prefix);
   free(azure_host);
   free(auth_value);
   free(uri);

   pgmoneta_http_request_destroy(request);
   pgmoneta_http_destroy(connection);

   return 0;

error:

   free(signed_headers_prefix);
   free(azure_host);
   free(auth_value);
   free(uri);

   if (connection != NULL)
   {
      pgmoneta_http_destroy(connection);
   }

   if (request != NULL)
   {
      pgmoneta_http_request_destroy(request);
   }

   return 1;
}

static int
azure_send_request(int method, char* azure_root, char* relative_path, struct http_response** response)
{
   char* azure_path = NULL;
   int rc;

   azure_path = azure_build_object_path(azure_root, relative_path);
   if (azure_path == NULL)
   {
      return 1;
   }

   rc = azure_send_container_request(method, azure_path, NULL, NULL, response);

   free(azure_path);

   return rc;
}

static char*
azure_url_encode(char* str)
{
   char* encoded = NULL;
   char hex[4];

   for (int i = 0; str != NULL && str[i] != '\0'; i++)
   {
      unsigned char c = (unsigned char)str[i];

      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
      {
         char ch[2] = {(char)c, '\0'};
         encoded = pgmoneta_append(encoded, ch);
      }
      else
      {
         pgmoneta_snprintf(hex, sizeof(hex), "%%%02X", c);
         encoded = pgmoneta_append(encoded, hex);
      }
   }

   return encoded;
}

/* Undo the XML escaping of a List Blobs value */
static char*
azure_xml_unescape(char* start, char* end)
{
   static const char* entities[][2] = {
      {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}};
   char* out = NULL;
   char* p = start;
   size_t n = 0;

   /* Unescaping only shrinks the text */
   out = malloc((size_t)(end - start) + 1);
   if (out == NULL)
   {
      return NULL;
   }

   while (p < end)
   {
      bool matched = false;

      if (*p == '&')
      {
         for (size_t i = 0; !matched && i < sizeof(entities) / sizeof(entities[0]); i++)
         {
            size_t len = strlen(entities[i][0]);

            if ((size_t)(end - p) >= len && !strncmp(p, entities[i][0], len))
            {
               out[n++] = entities[i][1][0];
               p += len;
               matched = true;
            }
         }
      }

      if (!matched)
      {
         out[n++] = *p++;
      }
   }

   out[n] = '\0';

   return out;
}

/*
 * Add the text of every <tag>...</tag> in xml, unescaped, to values.
 * Only for the flat elements of a List Blobs response.
 */
static int
azure_xml_collect(char* xml, char* tag, struct deque* values)
{
   char open_tag[128];
   char close_tag[128];
   char* p = xml;

   pgmoneta_snprintf(open_tag, sizeof(open_tag), "<%s>", tag);
   pgmoneta_snprintf(close_tag, sizeof(close_tag), "</%s>", tag);

   while ((p = strstr(p, open_tag)) != NULL)
   {
      char* value_start = p + strlen(open_tag);
      char* value_end = strstr(value_start, close_tag);
      char* value = NULL;

      if (value_end == NULL)
      {
         return 1;
      }

      value = azure_xml_unescape(value_start, value_end);
      if (value == NULL || pgmoneta_deque_add(values, NULL, (uintptr_t)value, ValueString))
      {
         free(value);
         return 1;
      }
      free(value);

      p = value_end + strlen(close_tag);
   }

   return 0;
}

/*
 * List the names of all blobs starting with prefix, following NextMarker
 * until the listing is complete.
 */
static int
azure_list_blobs(char* prefix, struct deque** names)
{
   char* marker = NULL;
   char* encoded_prefix = NULL;
   char* encoded_marker = NULL;
   char* query = NULL;
   char* canonical_query = NULL;
   struct deque* markers = NULL;
   struct http_response* response = NULL;

   *names = NULL;

   if (pgmoneta_deque_create(false, names))
   {
      goto error;
   }

   encoded_prefix = azure_url_encode(prefix);
   if (encoded_prefix == NULL)
   {
      goto error;
   }

   do
   {
      /* Canonicalized parameters are sorted by name and not URL-encoded */
      query = pgmoneta_format_and_append(NULL, "?restype=container&comp=list&prefix=%s", encoded_prefix);
      canonical_query = pgmoneta_append(NULL, "\ncomp:list");
      if (marker != NULL)
      {
         encoded_marker = azure_url_encode(marker);
         query = pgmoneta_format_and_append(query, "&marker=%s", encoded_marker);
         canonical_query = pgmoneta_format_and_append(canonical_query, "\nmarker:%s", marker);
      }
      canonical_query = pgmoneta_format_and_append(canonical_query, "\nprefix:%s\nrestype:container", prefix);

      if (query == NULL || canonical_query == NULL)
      {
         goto error;
      }

      if (azure_send_container_request(PGMONETA_HTTP_GET, NULL, query, canonical_query, &response))
      {
         goto error;
      }

      if (response->status_code != 200 || response->payload.data == NULL)
      {
         pgmoneta_log_error("Azure list: unexpected status %d for prefix %s", response->status_code, prefix);
         goto error;
      }

      if (azure_xml_collect(response->payload.data, "Name", *names))
      {
         pgmoneta_log_error("Azure list: malformed response for prefix %s", prefix);
         goto error;
      }

      free(marker);
      marker = NULL;

      if (pgmoneta_deque_create(false, &markers) ||
          azure_xml_collect(response->payload.data, "NextMarker", markers))
      {
         goto error;
      }

      if (pgmoneta_deque_size(markers) > 0)
      {
         char* next = (char*)pgmoneta_deque_peek(markers, NULL);

         if (next != NULL && strlen(next) > 0)
         {
            marker = pgmoneta_append(NULL, next);
         }
      }

      pgmoneta_deque_destroy(markers);
      markers = NULL;
      pgmoneta_http_response_destroy(response);
      response = NULL;
      free(query);
      query = NULL;
      free(canonical_query);
      canonical_query = NULL;
      free(encoded_marker);
      encoded_marker = NULL;
   }
   while (marker != NULL);

   free(encoded_prefix);

   return 0;

error:

   pgmoneta_deque_destroy(markers);
   pgmoneta_http_response_destroy(response);
   free(marker);
   free(encoded_prefix);
   free(encoded_marker);
   free(query);
   free(canonical_query);
   pgmoneta_deque_destroy(*names);
   *names = NULL;

   return 1;
}

/* Delete one blob; a blob that is already gone counts as deleted */
static int
azure_delete_blob(char* name)
{
   struct http_response* response = NULL;
   int rc = 1;

   if (azure_send_request(PGMONETA_HTTP_DELETE, name, NULL, &response))
   {
      goto done;
   }

   if ((response->status_code >= 200 && response->status_code < 300) || response->status_code == 404)
   {
      rc = 0;
   }
   else
   {
      pgmoneta_log_error("Azure delete: unexpected status %d for %s", response->status_code, name);
   }

done:

   pgmoneta_http_response_destroy(response);

   return rc;
}

/*
 * Check whether a blob exists in the container. Returns 1 when it does, 0 when
 * it is missing, and -1 when the request could not be completed.
 */
static int
azure_object_exists(char* azure_root, char* relative_path, int server)
{
   struct http_response* response = NULL;
   int result = -1;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (azure_send_request(PGMONETA_HTTP_HEAD, azure_root, relative_path, &response))
   {
      pgmoneta_log_error("Azure verify: HEAD request failed for %s", relative_path);
      goto done;
   }

   if (response->status_code >= 200 && response->status_code < 300)
   {
      result = 1;
   }
   else if (response->status_code == 404)
   {
      result = 0;
   }
   else
   {
      pgmoneta_log_error("Azure verify: unexpected status %d for %s/%s",
                         response->status_code,
                         config->common.servers[server].name, relative_path);
   }

done:

   pgmoneta_http_response_destroy(response);

   return result;
}

static int
azure_get_object(int server __attribute__((unused)), char* root, char* relative_path,
                 struct http_response** response)
{
   return azure_send_request(PGMONETA_HTTP_GET, root, relative_path, response);
}

static const struct object_storage_ops azure_ops = {
   .name = "Azure",
   .get_object = &azure_get_object,
};

static char*
azure_get_host()
{
   char* host = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (strlen(config->azure_endpoint) > 0)
   {
      host = pgmoneta_append(host, config->azure_endpoint);
   }
   else
   {
      host = pgmoneta_append(host, config->azure_storage_account);
      host = pgmoneta_append(host, ".blob.core.windows.net");
   }

   return host;
}

static char*
azure_get_basepath(int server, char* identifier)
{
   char* d = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   d = pgmoneta_append(d, config->azure_base_dir);
   if (!pgmoneta_ends_with(config->azure_base_dir, "/"))
   {
      d = pgmoneta_append(d, "/");
   }
   d = pgmoneta_append(d, config->common.servers[server].name);
   d = pgmoneta_append(d, "/backup/");
   d = pgmoneta_append(d, identifier);

   return d;
}

static int
azure_add_request_headers(struct http_request* request, char* auth_value, char* utc_date, bool blob_type)
{
   if (pgmoneta_http_request_add_header(request, "Authorization", auth_value))
   {
      return 1;
   }

   if (blob_type && pgmoneta_http_request_add_header(request, "x-ms-blob-type", "BlockBlob"))
   {
      return 1;
   }

   if (pgmoneta_http_request_add_header(request, "x-ms-date", utc_date))
   {
      return 1;
   }

   if (pgmoneta_http_request_add_header(request, "x-ms-version", "2021-08-06"))
   {
      return 1;
   }

   return 0;
}

int
azure_upload(int server, char* label, int compression __attribute__((unused)), int encryption __attribute__((unused)))
{
   char* local_root = NULL;
   char* azure_root = NULL;
   int rc;

   local_root = pgmoneta_get_server_backup_identifier(server, label);
   azure_root = azure_get_basepath(server, label);

   rc = azure_upload_files(local_root, azure_root, server, compression, encryption);

   free(local_root);
   free(azure_root);
   return rc;
}

int
azure_download(int server, char* label, char* local_root)
{
   char* azure_root = NULL;
   int rc;

   azure_root = azure_get_basepath(server, label);

   rc = pgmoneta_object_download(&azure_ops, azure_root, server, label, local_root);

   free(azure_root);
   return rc;
}

/*
 * Scheduled verification for an Azure-only backup. The local data/ directory is
 * removed after upload, so check the blobs themselves instead of hashing local
 * files. Local metadata is always kept for every backup, so the manifest is
 * read from the local backup directory.
 */
int
azure_verify_backup(int server, struct backup* backup_info)
{
   char* azure_root = NULL;
   char* local_root = NULL;
   char* manifest_path = NULL;
   char* remote_path = NULL;
   char* suffix = NULL;
   struct deque* paths = NULL;
   struct deque_iterator* iter = NULL;
   struct main_configuration* config;
   int rc = 1;

   config = (struct main_configuration*)shmem;

   if (backup_info == NULL)
   {
      return 1;
   }

   pgmoneta_log_debug("Azure storage engine (verify): %s/%s",
                      config->common.servers[server].name, backup_info->label);

   azure_root = azure_get_basepath(server, backup_info->label);
   local_root = pgmoneta_get_server_backup_identifier(server, backup_info->label);

   if (pgmoneta_extraction_get_suffix(backup_info->compression, backup_info->encryption, &suffix))
   {
      pgmoneta_log_error("Azure verify: failed to get suffix for %s/%s",
                         config->common.servers[server].name, backup_info->label);
      goto cleanup;
   }

   manifest_path = pgmoneta_append(pgmoneta_append(NULL, local_root), "backup.manifest");

   if (pgmoneta_manifest_get_paths(manifest_path, &paths))
   {
      pgmoneta_log_error("Azure verify: failed to read manifest %s", manifest_path);
      goto cleanup;
   }

   if (pgmoneta_deque_iterator_create(paths, &iter))
   {
      pgmoneta_log_error("Azure verify: failed to create iterator");
      goto cleanup;
   }

   while (pgmoneta_deque_iterator_next(iter))
   {
      char* file_path = iter->tag;
      int exists = 0;

      remote_path = pgmoneta_append(remote_path, "data/");
      remote_path = pgmoneta_append(remote_path, file_path);

      /* Match the upload naming: backup_label and backup_manifest stay
       * uncompressed/unencrypted. */
      if (suffix != NULL &&
          !pgmoneta_ends_with(file_path, "backup_label") &&
          !pgmoneta_ends_with(file_path, "backup_manifest"))
      {
         remote_path = pgmoneta_append(remote_path, suffix);
      }

      exists = azure_object_exists(azure_root, remote_path, server);

      if (exists != 1)
      {
         pgmoneta_log_error("Azure verify: %s object %s for %s/%s",
                            exists == 0 ? "missing" : "could not check",
                            remote_path,
                            config->common.servers[server].name, backup_info->label);
         goto cleanup;
      }

      free(remote_path);
      remote_path = NULL;
   }

   pgmoneta_log_info("Azure verify: %s/%s completed",
                     config->common.servers[server].name, backup_info->label);
   rc = 0;

cleanup:

   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_deque_destroy(paths);
   free(azure_root);
   free(local_root);
   free(manifest_path);
   free(remote_path);
   free(suffix);

   return rc;
}

/*
 * Delete every blob of a backup. The blobs are found by listing the backup
 * prefix, so this also works once the local backup directory is gone.
 */
int
azure_cleanup(int server, char* label)
{
   char* azure_root = NULL;
   char* prefix = NULL;
   struct deque* names = NULL;
   struct deque_iterator* iter = NULL;
   struct main_configuration* config;
   int failed = 0;
   int deleted = 0;
   int rc = 1;

   config = (struct main_configuration*)shmem;

   azure_root = azure_get_basepath(server, label);
   prefix = pgmoneta_append(pgmoneta_append(NULL, azure_root), "/");
   if (azure_root == NULL || prefix == NULL)
   {
      goto done;
   }

   pgmoneta_log_debug("Azure storage engine (cleanup): %s/%s", config->common.servers[server].name, label);

   if (azure_list_blobs(prefix, &names))
   {
      pgmoneta_log_error("Azure cleanup: could not list %s", prefix);
      goto done;
   }

   if (pgmoneta_deque_iterator_create(names, &iter))
   {
      goto done;
   }

   while (pgmoneta_deque_iterator_next(iter))
   {
      char* name = (char*)pgmoneta_value_data(iter->value);

      if (azure_delete_blob(name))
      {
         failed++;
      }
      else
      {
         deleted++;
      }
   }

   if (failed > 0)
   {
      pgmoneta_log_error("Azure cleanup: %d of %d blobs not deleted for %s/%s",
                         failed, failed + deleted, config->common.servers[server].name, label);
      goto done;
   }

   pgmoneta_log_debug("Azure cleanup: deleted %d blobs for %s/%s",
                      deleted, config->common.servers[server].name, label);
   rc = 0;

done:

   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_deque_destroy(names);
   free(azure_root);
   free(prefix);

   return rc;
}
