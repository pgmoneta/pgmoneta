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
static char* azure_restore_name(void);
static int azure_storage_setup(char* name, struct art*);
static int azure_storage_execute(char* name, struct art*);
static int azure_storage_teardown(char* name, struct art*);
static int azure_storage_noop_teardown(char* name, struct art*);
static int azure_storage_restore(char* name, struct art*);

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

/** @struct azure_download_file_context
 * Defines a streaming download target
 */
struct azure_download_file_context
{
   struct vfile* file;   /**< The target file */
   char* path;           /**< The target path */
   size_t bytes_written; /**< The bytes written */
};

static int azure_upload_files(char* local_root, char* azure_root, int server, int compression, int encryption);
static int azure_send_upload_request(char* local_root, char* azure_root, char* relative_path);
static int azure_send_get_request(char* relative_path, char* azure_root, struct http_response** response);
static int azure_add_request_headers(struct http_request* request, char* auth_value, char* utc_date);
static int azure_add_get_request_headers(struct http_request* request, char* auth_value, char* utc_date);
static void do_upload_file(struct worker_common* wc);
static void do_download_file(struct worker_common* wc);
static int azure_create_transfer_task(int server, char* azure_root, char* remote_path,
                                      char* local_root, char* local_path,
                                      struct workers* workers, struct azure_transfer_task** task);
static int azure_upload_one_file(struct azure_transfer_task* task);
static int azure_download_one_file(struct azure_transfer_task* task);
static int azure_download_files(char* azure_root, char* local_root, int server, int compression, int encryption);
static int azure_bootstrap(char* azure_root, int server, char* local_root);
static size_t azure_download_write_cb(void* buffer, size_t size, void* userdata);

static char* azure_get_host(void);
static char* azure_get_basepath(int server, char* identifier);

struct workflow*
pgmoneta_storage_create_azure(int workflow_type)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->setup = &azure_storage_setup;

   switch (workflow_type)
   {
      case WORKFLOW_TYPE_AZURE_RESTORE:
         wf->name = &azure_restore_name;
         wf->execute = &azure_storage_restore;
         wf->teardown = &azure_storage_noop_teardown;
         break;
      default:
         wf->name = &azure_storage_name;
         wf->execute = &azure_storage_execute;
         wf->teardown = &azure_storage_teardown;
         break;
   }

   wf->next = NULL;

   return wf;
}

static char*
azure_restore_name(void)
{
   return "Azure restore";
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
azure_storage_noop_teardown(char* name __attribute__((unused)), struct art* nodes __attribute__((unused)))
{
   return 0;
}

static int
azure_create_transfer_task(int server, char* azure_root, char* remote_path,
                           char* local_root, char* local_path,
                           struct workers* workers, struct azure_transfer_task** task)
{
   struct azure_transfer_task* t = NULL;

   *task = NULL;

   if (azure_root == NULL || remote_path == NULL || local_root == NULL || local_path == NULL)
   {
      goto error;
   }

   if (strlen(azure_root) >= MAX_PATH || strlen(remote_path) >= MAX_PATH ||
       strlen(local_root) >= MAX_PATH || strlen(local_path) >= MAX_PATH)
   {
      pgmoneta_log_error("Azure transfer path too long");
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
   pgmoneta_snprintf(t->local_root, sizeof(t->local_root), "%s", local_root);
   pgmoneta_snprintf(t->local_path, sizeof(t->local_path), "%s", local_path);

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

      if (workers != NULL && pgmoneta_workers_outcome_ok(workers))
      {
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

static int
azure_send_upload_request(char* local_root, char* azure_root, char* relative_path)
{
   char utc_date[UTC_TIME_LENGTH];
   char* string_to_sign = NULL;
   char* signing_key = NULL;
   char* base64_signature = NULL;
   size_t base64_signature_length;
   char* local_path = NULL;
   char* azure_path = NULL;
   char* azure_host = NULL;
   char* auth_value = NULL;
   unsigned char* signature_hmac = NULL;
   int hmac_length = 0;
   size_t signing_key_length = 0;
   FILE* file = NULL;
   struct stat file_info;
   void* file_data = NULL;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   struct main_configuration* config;
   char size_str[64];
   char azure_put_path[MAX_PATH];

   config = (struct main_configuration*)shmem;

   if (strchr(config->azure_storage_account, ' ') != NULL)
   {
      pgmoneta_log_error("Azure storage account name contains spaces: '%s'. This is not allowed.", config->azure_storage_account);
      goto error;
   }

   local_path = pgmoneta_append(local_path, local_root);
   if (strlen(relative_path) > 0)
   {
      if (!pgmoneta_ends_with(local_root, "/"))
      {
         local_path = pgmoneta_append(local_path, "/");
      }
      local_path = pgmoneta_append(local_path, relative_path);
   }

   azure_path = pgmoneta_append(azure_path, azure_root);
   if (strlen(relative_path) > 0)
   {
      if (!pgmoneta_ends_with(azure_root, "/"))
      {
         azure_path = pgmoneta_append(azure_path, "/");
      }
      azure_path = pgmoneta_append(azure_path, relative_path);
   }

   memset(&utc_date[0], 0, sizeof(utc_date));

   if (pgmoneta_get_timestamp_UTC_format(utc_date))
   {
      goto error;
   }

   file = fopen(local_path, "rb");
   if (file == NULL)
   {
      goto error;
   }

   if (fstat(fileno(file), &file_info) != 0)
   {
      goto error;
   }

   file_data = malloc(file_info.st_size);
   if (file_data == NULL)
   {
      goto error;
   }

   if (fread(file_data, 1, file_info.st_size, file) != (size_t)file_info.st_size)
   {
      goto error;
   }

   fclose(file);
   file = NULL;

   if (file_info.st_size == 0)
   {
      string_to_sign = pgmoneta_append(string_to_sign, "PUT\n\n\n\n\napplication/octet-stream\n\n\n\n\n\n\nx-ms-blob-type:BlockBlob\nx-ms-date:");
   }
   else
   {
      string_to_sign = pgmoneta_append(string_to_sign, "PUT\n\n\n");
      pgmoneta_snprintf(size_str, sizeof(size_str), "%ld", (long)file_info.st_size);
      string_to_sign = pgmoneta_append(string_to_sign, size_str);
      string_to_sign = pgmoneta_append(string_to_sign, "\n\napplication/octet-stream\n\n\n\n\n\n\nx-ms-blob-type:BlockBlob\nx-ms-date:");
   }

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
   string_to_sign = pgmoneta_append(string_to_sign, "/");
   string_to_sign = pgmoneta_append(string_to_sign, azure_path);

   if (pgmoneta_base64_decode(config->azure_shared_key, strlen(config->azure_shared_key), (void**)&signing_key, &signing_key_length))
   {
      goto error;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash(signing_key, signing_key_length, string_to_sign, strlen(string_to_sign), &signature_hmac, &hmac_length))
   {
      goto error;
   }

   if (pgmoneta_base64_encode((char*)signature_hmac, hmac_length, &base64_signature, &base64_signature_length))
   {
      goto error;
   }

   auth_value = pgmoneta_append(auth_value, "SharedKey ");
   auth_value = pgmoneta_append(auth_value, config->azure_storage_account);
   auth_value = pgmoneta_append(auth_value, ":");
   auth_value = pgmoneta_append(auth_value, base64_signature);

   azure_host = azure_get_host();

   {
      bool use_endpoint = (strlen(config->azure_endpoint) > 0);
      int conn_port = use_endpoint ? (config->azure_port > 0 ? config->azure_port : 443) : 443;
      bool conn_tls = use_endpoint ? config->azure_use_tls : true;

      if (pgmoneta_http_create(azure_host, conn_port, conn_tls, &connection))
      {
         pgmoneta_log_error("Failed to connect to Azure host: %s:%d", azure_host, conn_port);
         goto error;
      }

      if (use_endpoint)
      {
         pgmoneta_snprintf(azure_put_path, sizeof(azure_put_path), "/%s/%s/%s",
                           config->azure_storage_account, config->azure_container, azure_path);
      }
      else
      {
         pgmoneta_snprintf(azure_put_path, sizeof(azure_put_path), "/%s/%s",
                           config->azure_container, azure_path);
      }
   }

   if (pgmoneta_http_request_create(PGMONETA_HTTP_PUT, azure_put_path, &request))
   {
      goto error;
   }

   if (azure_add_request_headers(request, auth_value, utc_date))
   {
      goto error;
   }

   if (pgmoneta_http_request_add_header(request, "Content-Type", "application/octet-stream"))
   {
      goto error;
   }

   if (pgmoneta_http_set_data(request, file_data, file_info.st_size))
   {
      goto error;
   }

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
   free(base64_signature);
   free(signature_hmac);
   free(string_to_sign);
   free(auth_value);
   free(signing_key);
   free(file_data);

   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);

   return 0;

error:

   if (local_path != NULL)
   {
      free(local_path);
   }

   if (azure_path != NULL)
   {
      free(azure_path);
   }

   if (azure_host != NULL)
   {
      free(azure_host);
   }

   if (signing_key != NULL)
   {
      free(signing_key);
   }

   if (base64_signature != NULL)
   {
      free(base64_signature);
   }

   if (signature_hmac != NULL)
   {
      free(signature_hmac);
   }

   if (string_to_sign != NULL)
   {
      free(string_to_sign);
   }

   if (auth_value != NULL)
   {
      free(auth_value);
   }

   if (file_data != NULL)
   {
      free(file_data);
   }

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

   if (file != NULL)
   {
      fclose(file);
   }

   return 1;
}

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
azure_add_request_headers(struct http_request* request, char* auth_value, char* utc_date)
{
   if (pgmoneta_http_request_add_header(request, "Authorization", auth_value))
   {
      return 1;
   }

   if (pgmoneta_http_request_add_header(request, "x-ms-blob-type", "BlockBlob"))
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

static int
azure_add_get_request_headers(struct http_request* request, char* auth_value, char* utc_date)
{
   if (pgmoneta_http_request_add_header(request, "Authorization", auth_value))
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

static int
azure_bootstrap(char* azure_root, int server, char* local_root)
{
   char buffer[4096];
   char* expected_hash = NULL;
   char* computed_hash = NULL;
   char* sha512_path = NULL;
   char* info_path = NULL;
   char* manifest_path = NULL;
   FILE* sha512_file = NULL;
   struct http_response* response = NULL;

   if (pgmoneta_is_progress_enabled(server))
   {
      pgmoneta_progress_set_total(server, 3);
   }

   if (azure_send_get_request("backup.sha512", azure_root, &response) ||
       response->status_code != 200)
   {
      pgmoneta_log_error("Azure bootstrap: failed to GET backup.sha512");
      goto error;
   }

   sha512_path = pgmoneta_append(sha512_path, local_root);
   sha512_path = pgmoneta_append(sha512_path, "backup.sha512.tmp");

   if (pgmoneta_exists(sha512_path))
   {
      pgmoneta_delete_file(sha512_path, NULL);
   }

   if (pgmoneta_append_file_chunk(sha512_path, response->payload.data, response->payload.data_size, 0))
   {
      pgmoneta_log_error("Azure bootstrap: failed to write backup.sha512");
      goto error;
   }

   pgmoneta_http_response_destroy(response);
   response = NULL;
   if (pgmoneta_is_progress_enabled(server))
   {
      pgmoneta_progress_increment(server, 1);
   }

   if (azure_send_get_request("backup.info", azure_root, &response) ||
       response->status_code != 200)
   {
      pgmoneta_log_error("Azure bootstrap: failed to GET backup.info");
      goto error;
   }

   info_path = pgmoneta_append(info_path, local_root);
   info_path = pgmoneta_append(info_path, "backup.info.tmp");

   if (pgmoneta_exists(info_path))
   {
      pgmoneta_delete_file(info_path, NULL);
   }

   if (pgmoneta_append_file_chunk(info_path, response->payload.data, response->payload.data_size, 0))
   {
      pgmoneta_log_error("Azure bootstrap: failed to write backup.info");
      goto error;
   }

   pgmoneta_http_response_destroy(response);
   response = NULL;
   if (pgmoneta_is_progress_enabled(server))
   {
      pgmoneta_progress_increment(server, 1);
   }

   sha512_file = fopen(sha512_path, "r");
   if (sha512_file == NULL)
   {
      pgmoneta_log_error("Azure bootstrap: could not open %s", sha512_path);
      goto error;
   }

   if (fgets(&buffer[0], sizeof(buffer), sha512_file) == NULL)
   {
      pgmoneta_log_error("Azure bootstrap: backup.sha512 is empty");
      goto error;
   }

   fclose(sha512_file);
   sha512_file = NULL;

   expected_hash = strtok(&buffer[0], " ");
   if (expected_hash == NULL)
   {
      pgmoneta_log_error("Azure bootstrap: backup.sha512 format error");
      goto error;
   }

   if (pgmoneta_create_sha512_file(info_path, &computed_hash))
   {
      pgmoneta_log_error("Azure bootstrap: could not compute SHA512 of backup.info");
      goto error;
   }

   if (strcmp(expected_hash, computed_hash))
   {
      pgmoneta_log_error("Azure bootstrap: backup.info SHA512 mismatch");
      pgmoneta_log_error("Azure bootstrap: expected %s", expected_hash);
      pgmoneta_log_error("Azure bootstrap: computed %s", computed_hash);
      goto error;
   }

   pgmoneta_log_info("Azure bootstrap: backup.info integrity verified");

   if (azure_send_get_request("backup.manifest", azure_root, &response) ||
       response->status_code != 200)
   {
      pgmoneta_log_error("Azure bootstrap: failed to GET backup.manifest");
      goto error;
   }

   manifest_path = pgmoneta_append(manifest_path, local_root);
   manifest_path = pgmoneta_append(manifest_path, "backup.manifest.tmp");

   if (pgmoneta_exists(manifest_path))
   {
      pgmoneta_delete_file(manifest_path, NULL);
   }

   if (pgmoneta_append_file_chunk(manifest_path, response->payload.data, response->payload.data_size, 0))
   {
      pgmoneta_log_error("Azure bootstrap: failed to write backup.manifest");
      goto error;
   }

   pgmoneta_http_response_destroy(response);
   response = NULL;
   if (pgmoneta_is_progress_enabled(server))
   {
      pgmoneta_progress_increment(server, 1);
   }

   free(sha512_path);
   free(info_path);
   free(manifest_path);
   free(computed_hash);

   return 0;

error:

   if (sha512_file != NULL)
   {
      fclose(sha512_file);
   }

   pgmoneta_http_response_destroy(response);
   free(sha512_path);
   free(info_path);
   free(manifest_path);
   free(computed_hash);

   return 1;
}

static int
azure_storage_restore(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   char* azure_root = NULL;
   char* local_root = NULL;
   char* base_dir = NULL;
   char* manifest_tmp = NULL;
   char* manifest_final = NULL;
   char* sha512_tmp = NULL;
   char* sha512_final = NULL;
   char* info_tmp = NULL;
   char* info_final = NULL;
   struct backup* backup = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   pgmoneta_dump_art(nodes);

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("Azure storage engine (restore): %s/%s",
                      config->common.servers[server].name, label);

   azure_root = azure_get_basepath(server, label);
   local_root = pgmoneta_get_server_backup_identifier(server, label);
   info_tmp = pgmoneta_append(pgmoneta_append(NULL, local_root), "backup.info.tmp");
   info_final = pgmoneta_append(pgmoneta_append(NULL, local_root), "backup.info");

   if (azure_bootstrap(azure_root, server, local_root))
   {
      goto error;
   }

   if (pgmoneta_move_file(info_tmp, info_final))
   {
      pgmoneta_log_error("Azure restore: could not rename %s to %s", info_tmp, info_final);
      goto error;
   }

   base_dir = pgmoneta_get_server_backup(server);

   if (pgmoneta_load_info(base_dir, label, &backup))
   {
      pgmoneta_log_error("Azure restore: failed to load backup.info from %s", local_root);
      goto error;
   }

   if (azure_download_files(azure_root, local_root, server, backup->compression, backup->encryption))
   {
      goto error;
   }

   manifest_tmp = pgmoneta_append(pgmoneta_append(NULL, local_root), "backup.manifest.tmp");
   manifest_final = pgmoneta_append(pgmoneta_append(NULL, local_root), "backup.manifest");
   sha512_tmp = pgmoneta_append(pgmoneta_append(NULL, local_root), "backup.sha512.tmp");
   sha512_final = pgmoneta_append(pgmoneta_append(NULL, local_root), "backup.sha512");

   if (pgmoneta_move_file(manifest_tmp, manifest_final))
   {
      pgmoneta_log_error("Azure restore: could not rename %s to %s", manifest_tmp, manifest_final);
      goto error;
   }

   if (pgmoneta_move_file(sha512_tmp, sha512_final))
   {
      pgmoneta_log_error("Azure restore: could not rename %s to %s", sha512_tmp, sha512_final);
      goto error;
   }

   pgmoneta_log_info("Azure restore: %s/%s completed", config->common.servers[server].name, label);

   free(azure_root);
   free(local_root);
   free(base_dir);
   free(backup);
   free(manifest_tmp);
   free(manifest_final);
   free(sha512_tmp);
   free(sha512_final);
   free(info_tmp);
   free(info_final);

   return 0;

error:

   if (local_root != NULL)
   {
      char* cleanup = NULL;

      cleanup = pgmoneta_append(pgmoneta_append(NULL, local_root), "backup.manifest.tmp");
      if (pgmoneta_exists(cleanup))
      {
         pgmoneta_delete_file(cleanup, NULL);
      }
      free(cleanup);

      cleanup = pgmoneta_append(pgmoneta_append(NULL, local_root), "backup.sha512.tmp");
      if (pgmoneta_exists(cleanup))
      {
         pgmoneta_delete_file(cleanup, NULL);
      }
      free(cleanup);

      cleanup = pgmoneta_append(pgmoneta_append(NULL, local_root), "backup.info.tmp");
      if (pgmoneta_exists(cleanup))
      {
         pgmoneta_delete_file(cleanup, NULL);
      }
      free(cleanup);
   }

   free(azure_root);
   free(local_root);
   free(base_dir);
   free(backup);
   free(manifest_tmp);
   free(manifest_final);
   free(sha512_tmp);
   free(sha512_final);
   free(info_tmp);
   free(info_final);

   return 1;
}

static size_t
azure_download_write_cb(void* buffer, size_t size, void* userdata)
{
   struct azure_download_file_context* ctx = (struct azure_download_file_context*)userdata;

   if (ctx == NULL || ctx->file == NULL)
   {
      return 0;
   }

   if (ctx->file->write(ctx->file, buffer, size, false))
   {
      pgmoneta_log_error("Azure download: failed to write chunk to %s", ctx->path);
      return 0;
   }

   ctx->bytes_written += size;

   return size;
}

static int
azure_download_one_file(struct azure_transfer_task* task)
{
   struct azure_download_file_context ctx = {0};
   struct http_response* response = NULL;
   char* full_local = NULL;
   char* tmp_local = NULL;
   char* parent_copy = NULL;
   char* parent = NULL;

   full_local = pgmoneta_append(full_local, task->local_root);
   full_local = pgmoneta_append(full_local, task->local_path);

   tmp_local = pgmoneta_append(tmp_local, full_local);
   tmp_local = pgmoneta_append(tmp_local, ".tmp");

   parent_copy = pgmoneta_append(parent_copy, tmp_local);
   parent = dirname(parent_copy);
   if (pgmoneta_mkdir(parent))
   {
      pgmoneta_log_error("Azure download: failed to create directory for %s", tmp_local);
      goto error;
   }

   if (pgmoneta_exists(tmp_local))
   {
      pgmoneta_delete_file(tmp_local, NULL);
   }

   if (pgmoneta_vfile_create_local(tmp_local, "wb", &ctx.file))
   {
      pgmoneta_log_error("Azure download: failed to create %s", tmp_local);
      goto error;
   }
   ctx.path = tmp_local;

   response = (struct http_response*)malloc(sizeof(struct http_response));
   if (response == NULL)
   {
      goto error;
   }
   memset(response, 0, sizeof(struct http_response));
   response->write_cb = azure_download_write_cb;
   response->write_userdata = &ctx;

   if (azure_send_get_request(task->remote_path, task->azure_root, &response))
   {
      pgmoneta_log_error("Azure download: failed to GET %s", task->remote_path);
      goto error;
   }

   if (response->status_code != 200)
   {
      pgmoneta_log_error("Azure download: %s returned status %d", task->remote_path, response->status_code);
      goto error;
   }

   pgmoneta_vfile_destroy(ctx.file);
   ctx.file = NULL;

   if (pgmoneta_move_file(tmp_local, full_local))
   {
      pgmoneta_log_error("Azure download: failed to rename %s to %s", tmp_local, full_local);
      goto error;
   }

   if (task->progress_enabled)
   {
      pgmoneta_progress_increment(task->server, 1);
   }

   pgmoneta_log_debug("Azure download: %s", task->remote_path);

   pgmoneta_http_response_destroy(response);
   free(full_local);
   free(tmp_local);
   free(parent_copy);

   return 0;

error:

   if (ctx.file != NULL)
   {
      pgmoneta_vfile_destroy(ctx.file);
   }

   if (pgmoneta_exists(tmp_local))
   {
      pgmoneta_delete_file(tmp_local, NULL);
   }

   pgmoneta_http_response_destroy(response);
   free(full_local);
   free(tmp_local);
   free(parent_copy);

   return 1;
}

static void
do_download_file(struct worker_common* wc)
{
   struct azure_transfer_task* task = (struct azure_transfer_task*)wc;

   if (azure_download_one_file(task))
   {
      pgmoneta_record_failure(task->common.workers != NULL ? task->common.workers->outcome : NULL,
                              "Azure download failed: %s", task->remote_path);
   }

   free(task);
}

static int
azure_download_files(char* azure_root, char* local_root, int server, int compression, int encryption)
{
   char* manifest_path = NULL;
   char* suffix = NULL;
   char* filename = NULL;
   char* azure_path = NULL;
   char* local_file_path = NULL;
   char* file_path = NULL;
   int number_of_workers = 0;
   struct deque* paths = NULL;
   struct deque_iterator* iter = NULL;
   struct workers* workers = NULL;
   struct azure_transfer_task* task = NULL;

   manifest_path = pgmoneta_append(manifest_path, local_root);
   manifest_path = pgmoneta_append(manifest_path, "backup.manifest.tmp");

   if (pgmoneta_extraction_get_suffix(compression, encryption, &suffix))
   {
      pgmoneta_log_error("Azure download: failed to determine file suffix");
      goto error;
   }

   number_of_workers = pgmoneta_get_number_of_workers(server);
   if (number_of_workers > 0)
   {
      pgmoneta_workers_initialize(number_of_workers, &workers);
   }

   if (pgmoneta_manifest_get_paths(manifest_path, &paths))
   {
      pgmoneta_log_error("Azure download: failed to read manifest %s", manifest_path);
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

      filename = NULL;
      filename = pgmoneta_append(filename, file_path);

      if (suffix != NULL &&
          !pgmoneta_ends_with(file_path, "backup_label") &&
          !pgmoneta_ends_with(file_path, "backup_manifest"))
      {
         filename = pgmoneta_append(filename, suffix);
      }

      azure_path = NULL;
      azure_path = pgmoneta_append(azure_path, "data/");
      azure_path = pgmoneta_append(azure_path, filename);

      local_file_path = NULL;
      local_file_path = pgmoneta_append(local_file_path, "data/");
      local_file_path = pgmoneta_append(local_file_path, filename);

      if (azure_create_transfer_task(server, azure_root, azure_path, local_root,
                                     local_file_path, workers, &task))
      {
         pgmoneta_log_error("Azure download: failed to create transfer task");
         goto error;
      }

      if (workers != NULL && pgmoneta_workers_outcome_ok(workers))
      {
         if (pgmoneta_workers_add(workers, do_download_file, (struct worker_common*)task))
         {
            free(task);
            task = NULL;
            pgmoneta_log_error("Azure download: failed to queue worker task");
            goto error;
         }
         task = NULL;
      }
      else
      {
         if (azure_download_one_file(task))
         {
            free(task);
            task = NULL;
            goto error;
         }
         free(task);
         task = NULL;
      }

      free(filename);
      filename = NULL;
      free(azure_path);
      azure_path = NULL;
      free(local_file_path);
      local_file_path = NULL;
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
   free(filename);
   free(azure_path);
   free(local_file_path);
   free(task);

   return 1;
}

static int
azure_send_get_request(char* relative_path, char* azure_root, struct http_response** response)
{
   char utc_date[UTC_TIME_LENGTH];
   char azure_get_path[MAX_PATH];
   char* string_to_sign = NULL;
   char* signing_key = NULL;
   char* base64_signature = NULL;
   char* azure_path = NULL;
   char* azure_host = NULL;
   char* auth_value = NULL;
   unsigned char* signature_hmac = NULL;
   size_t base64_signature_length;
   size_t signing_key_length = 0;
   int hmac_length = 0;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   azure_path = pgmoneta_append(azure_path, azure_root);
   if (strlen(relative_path) > 0)
   {
      if (!pgmoneta_ends_with(azure_root, "/"))
      {
         azure_path = pgmoneta_append(azure_path, "/");
      }
      azure_path = pgmoneta_append(azure_path, relative_path);
   }

   memset(&utc_date[0], 0, sizeof(utc_date));

   if (pgmoneta_get_timestamp_UTC_format(utc_date))
   {
      goto error;
   }

   string_to_sign = pgmoneta_append(string_to_sign, "GET\n\n\n\n\n\n\n\n\n\n\n\nx-ms-date:");
   string_to_sign = pgmoneta_append(string_to_sign, utc_date);
   string_to_sign = pgmoneta_append(string_to_sign, "\nx-ms-version:2021-08-06\n/");
   string_to_sign = pgmoneta_append(string_to_sign, config->azure_storage_account);
   string_to_sign = pgmoneta_append(string_to_sign, "/");
   if (strlen(config->azure_endpoint) > 0)
   {
      string_to_sign = pgmoneta_append(string_to_sign, config->azure_storage_account);
      string_to_sign = pgmoneta_append(string_to_sign, "/");
   }
   string_to_sign = pgmoneta_append(string_to_sign, config->azure_container);
   string_to_sign = pgmoneta_append(string_to_sign, "/");
   string_to_sign = pgmoneta_append(string_to_sign, azure_path);

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

   if (pgmoneta_base64_encode((char*)signature_hmac, hmac_length, &base64_signature, &base64_signature_length))
   {
      goto error;
   }

   auth_value = pgmoneta_append(auth_value, "SharedKey ");
   auth_value = pgmoneta_append(auth_value, config->azure_storage_account);
   auth_value = pgmoneta_append(auth_value, ":");
   auth_value = pgmoneta_append(auth_value, base64_signature);

   azure_host = azure_get_host();

   {
      bool use_endpoint = (strlen(config->azure_endpoint) > 0);
      int conn_port = use_endpoint ? (config->azure_port > 0 ? config->azure_port : 443) : 443;
      bool conn_tls = use_endpoint ? config->azure_use_tls : true;

      if (pgmoneta_http_create(azure_host, conn_port, conn_tls, &connection))
      {
         pgmoneta_log_error("Azure download: failed to connect to %s:%d", azure_host, conn_port);
         goto error;
      }

      if (use_endpoint)
      {
         pgmoneta_snprintf(azure_get_path, sizeof(azure_get_path), "/%s/%s/%s",
                           config->azure_storage_account, config->azure_container, azure_path);
      }
      else
      {
         pgmoneta_snprintf(azure_get_path, sizeof(azure_get_path), "/%s/%s",
                           config->azure_container, azure_path);
      }
   }

   if (pgmoneta_http_request_create(PGMONETA_HTTP_GET, azure_get_path, &request))
   {
      goto error;
   }

   if (azure_add_get_request_headers(request, auth_value, utc_date))
   {
      goto error;
   }

   if (pgmoneta_http_invoke(connection, request, response))
   {
      pgmoneta_log_error("Azure download: failed to execute GET for %s", azure_path);
      goto error;
   }

   free(azure_path);
   free(azure_host);
   free(base64_signature);
   free(signature_hmac);
   free(string_to_sign);
   free(auth_value);
   free(signing_key);

   pgmoneta_http_request_destroy(request);
   pgmoneta_http_destroy(connection);

   return 0;

error:

   free(azure_path);
   free(azure_host);
   free(base64_signature);
   free(signature_hmac);
   free(string_to_sign);
   free(auth_value);
   free(signing_key);

   if (request != NULL)
   {
      pgmoneta_http_request_destroy(request);
   }

   if (connection != NULL)
   {
      pgmoneta_http_destroy(connection);
   }

   return 1;
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
