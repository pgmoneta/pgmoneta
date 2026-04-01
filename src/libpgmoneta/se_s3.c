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
#include <extraction.h>
#include <http.h>
#include <info.h>
#include <logging.h>
#include <manifest.h>
#include <security.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* s3_storage_name(void);
static int s3_storage_setup(char*, struct art*);
static int s3_storage_execute(char*, struct art*);
static int s3_storage_restore(char*, struct art*);
static int s3_storage_list(char*, struct art*);
static int s3_storage_teardown(char*, struct art*);
static int s3_storage_noop_teardown(char*, struct art*);
static int s3_storage_cleanup(char*, struct art*);
static int s3_upload_files(char* local_root, char* s3_root, int server, int compression, int encryption);
static int s3_bootstrap(char* s3_root, int server, char* local_root);
static int s3_download_files(char* s3_root, char* local_root, int server, int compression, int encryption);
static int s3_send_upload_request(char* local_root, char* s3_root, char* relative_path, int server);
static int s3_list_objects(char* relative_path, char* s3_list, int server, struct deque** objects);
static int s3_delete_all_objects(char* relative_path, char* s3_list, int server, struct art*) __attribute__((unused));
static int s3_send_list_request(char* relative_path, char* s3_list, int server, char* continuationToken, struct http_response** response);
static int s3_send_delete_request(char* relative_path, char* s3_list, int server, char* xml_body, struct http_response** response);
static int s3_add_request_headers(struct http_request* request, char* auth_value, char* file_sha256, char* long_date, char* storage_class);
static int s3_send_get_request(char* relative_path, char* s3_root, int server, long range_start, long range_end, struct http_response** response);

static char* s3_get_host(int server);
static char* s3_get_basepath(int server, char* identifier);
static char* s3_url_encode(char* str);
static int xml_parse_s3_delete_result(char* xml, bool* has_fatal_error);
static int xml_s3_build_delete_key(char** xml, char* key);
static int xml_s3_build_delete_list(char** xml, struct deque* keys, size_t max_keys);
static int xml_parse_s3_list_truncated(char* xml, bool* is_truncated, char** continuationToken);
static int xml_parse_s3_list(char* xml, struct deque** keys);

static void do_download_file(struct worker_common* wc);
static void do_upload_file(struct worker_common* wc);

struct workflow*
pgmoneta_storage_create_s3(int workflow_type)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->name = &s3_storage_name;
   wf->setup = &s3_storage_setup;

   switch (workflow_type)
   {
      case WORKFLOW_TYPE_BACKUP:
         wf->execute = &s3_storage_execute;
         wf->teardown = &s3_storage_teardown;
         break;
      case WORKFLOW_TYPE_S3_LIST:
         wf->execute = &s3_storage_list;
         wf->teardown = &s3_storage_noop_teardown;
         break;
      case WORKFLOW_TYPE_DELETE_BACKUP:
      case WORKFLOW_TYPE_S3_DELETE:
         wf->execute = &s3_storage_cleanup;
         wf->teardown = &s3_storage_noop_teardown;
         break;
      case WORKFLOW_TYPE_S3_RESTORE:
         wf->execute = &s3_storage_restore;
         wf->teardown = &s3_storage_noop_teardown;
         break;
      default:
         break;
   }

   wf->next = NULL;

   return wf;
}

static char*
s3_storage_name(void)
{
   return "S3";
}

static int
s3_storage_setup(char* name __attribute__((unused)), struct art* nodes)
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

   pgmoneta_log_debug("S3 storage engine (setup): %s/%s", config->common.servers[server].name, label);

   return 0;
}

static int
s3_get_effective_port(int server)
{
   struct main_configuration* config;
   struct server* srv;

   config = (struct main_configuration*)shmem;
   srv = &config->common.servers[server];

   if (srv != NULL && srv->s3.port != 0)
   {
      return srv->s3.port;
   }

   return config->s3.port;
}

static bool
s3_get_effective_use_tls(int server)
{
   struct main_configuration* config;
   struct server* srv;

   config = (struct main_configuration*)shmem;
   srv = &config->common.servers[server];

   if (srv != NULL && (srv->s3.port != 0 || strlen(srv->s3.endpoint) > 0))
   {
      return srv->s3.use_tls;
   }

   return config->s3.use_tls;
}

static char*
s3_get_effective_storage_class(int server)
{
   struct main_configuration* config;
   struct server* srv;

   config = (struct main_configuration*)shmem;
   srv = &config->common.servers[server];

   if (srv != NULL && strlen(srv->s3.storage_class) > 0)
   {
      return srv->s3.storage_class;
   }

   return config->s3.storage_class;
}

static char*
s3_get_effective_endpoint(int server)
{
   struct main_configuration* config;
   struct server* srv;

   config = (struct main_configuration*)shmem;
   srv = &config->common.servers[server];

   if (srv != NULL && strlen(srv->s3.endpoint) > 0)
   {
      return srv->s3.endpoint;
   }

   return config->s3.endpoint;
}

static char*
s3_get_effective_region(int server)
{
   struct main_configuration* config;
   struct server* srv;

   config = (struct main_configuration*)shmem;
   srv = &config->common.servers[server];

   if (srv != NULL && strlen(srv->s3.region) > 0)
   {
      return srv->s3.region;
   }

   return config->s3.region;
}

static char*
s3_get_effective_access_key_id(int server)
{
   struct main_configuration* config;
   struct server* srv;

   config = (struct main_configuration*)shmem;
   srv = &config->common.servers[server];

   if (srv != NULL && strlen(srv->s3.access_key_id) > 0)
   {
      return srv->s3.access_key_id;
   }

   return config->s3.access_key_id;
}

static char*
s3_get_effective_secret_access_key(int server)
{
   struct main_configuration* config;
   struct server* srv;

   config = (struct main_configuration*)shmem;
   srv = &config->common.servers[server];

   if (srv != NULL && strlen(srv->s3.secret_access_key) > 0)
   {
      return srv->s3.secret_access_key;
   }

   return config->s3.secret_access_key;
}

static char*
s3_get_effective_bucket(int server)
{
   struct main_configuration* config;
   struct server* srv;

   config = (struct main_configuration*)shmem;
   srv = &config->common.servers[server];

   if (srv != NULL && strlen(srv->s3.bucket) > 0)
   {
      return srv->s3.bucket;
   }

   return config->s3.bucket;
}

static char*
s3_get_effective_base_dir(int server)
{
   struct main_configuration* config;
   struct server* srv;

   config = (struct main_configuration*)shmem;
   srv = &config->common.servers[server];

   if (srv != NULL && strlen(srv->s3.base_dir) > 0)
   {
      return srv->s3.base_dir;
   }

   return config->s3.base_dir;
}

static int
s3_storage_execute(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double remote_s3_elapsed_time;
   char* local_root = NULL;
   char* base_dir = NULL;
   char* s3_root = NULL;
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

   pgmoneta_log_debug("S3 storage engine (execute): %s/%s",
                      config->common.servers[server].name, label);
   pgmoneta_log_debug("S3 effective config: bucket=%s, region=%s, endpoint=%s",
                      s3_get_effective_bucket(server),
                      s3_get_effective_region(server),
                      s3_get_effective_endpoint(server));

   local_root = pgmoneta_get_server_backup_identifier(server, label);
   base_dir = pgmoneta_get_server_backup(server);
   s3_root = s3_get_basepath(server, label);

   if (pgmoneta_load_info(base_dir, label, &temp_backup))
   {
      pgmoneta_log_error("S3 storage: unable to load backup info for %s/%s", base_dir, label);
      goto error;
   }

   if (s3_upload_files(local_root, s3_root, server, temp_backup->compression, temp_backup->encryption))
   {
      goto error;
   }

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   remote_s3_elapsed_time = pgmoneta_compute_duration(start_t, end_t);

   temp_backup->remote_s3_elapsed_time = remote_s3_elapsed_time;
   if (pgmoneta_save_info(base_dir, temp_backup))
   {
      pgmoneta_log_error("Unable to save backup info for directory %s", base_dir);
      goto error;
   }

   free(temp_backup);
   free(local_root);
   free(base_dir);
   free(s3_root);

   return 0;

error:
   free(temp_backup);
   free(local_root);
   free(base_dir);
   free(s3_root);

   return 1;
}

static int
s3_storage_list(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   char* s3_root = NULL;
   struct deque* objects = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   pgmoneta_dump_art(nodes);

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("S3 storage engine (list): %s/%s",
                      config->common.servers[server].name, label);
   pgmoneta_log_debug("S3 effective config: bucket=%s, region=%s, endpoint=%s",
                      s3_get_effective_bucket(server),
                      s3_get_effective_region(server),
                      s3_get_effective_endpoint(server));

   s3_root = s3_get_basepath(server, label);

   if (s3_list_objects("", s3_root, server, &objects))
   {
      goto error;
   }
   pgmoneta_art_insert(nodes, NODE_S3_OBJECTS, (uintptr_t)objects, ValueDeque);

   free(s3_root);

   return 0;

error:
   free(s3_root);
   pgmoneta_deque_destroy(objects);
   return 1;
}

static int
s3_storage_teardown(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   char* root = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   pgmoneta_dump_art(nodes);

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("S3 storage engine (teardown): %s/%s", config->common.servers[server].name, label);

   root = pgmoneta_get_server_backup_identifier_data(server, label);

   pgmoneta_delete_directory(root);

   free(root);

   return 0;
}

static int
s3_storage_noop_teardown(char* name __attribute__((unused)), struct art* nodes __attribute__((unused)))
{
   return 0;
}

static int
s3_storage_cleanup(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   char* s3_root = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   pgmoneta_dump_art(nodes);

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("S3 storage engine (cleanup): %s/%s",
                      config->common.servers[server].name, label);
   pgmoneta_log_debug("S3 effective config: bucket=%s, region=%s, endpoint=%s",
                      s3_get_effective_bucket(server),
                      s3_get_effective_region(server),
                      s3_get_effective_endpoint(server));

   s3_root = s3_get_basepath(server, label);

   if (s3_delete_all_objects("", s3_root, server, nodes))
   {
      goto error;
   }

   free(s3_root);

   return 0;

error:
   free(s3_root);
   return 1;
}
static int
s3_storage_restore(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   char* s3_root = NULL;
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

   pgmoneta_log_debug("S3 storage engine (restore): %s/%s",
                      config->common.servers[server].name, label);
   pgmoneta_log_debug("S3 effective config: bucket=%s, region=%s, endpoint=%s",
                      s3_get_effective_bucket(server),
                      s3_get_effective_region(server),
                      s3_get_effective_endpoint(server));

   s3_root = s3_get_basepath(server, label);
   local_root = pgmoneta_get_server_backup_identifier(server, label);

   if (s3_bootstrap(s3_root, server, local_root))
   {
      goto error;
   }

   base_dir = pgmoneta_get_server_backup(server);

   if (pgmoneta_load_info(base_dir, label, &backup))
   {
      pgmoneta_log_error("S3 restore: failed to load backup.info from %s", local_root);
      goto error;
   }

   pgmoneta_log_debug("S3 restore: compression=%d encryption=%d", backup->compression, backup->encryption);

   if (s3_download_files(s3_root, local_root, server, backup->compression, backup->encryption))
   {
      goto error;
   }

   manifest_tmp = pgmoneta_append(pgmoneta_append(NULL, local_root), "backup.manifest.tmp");
   manifest_final = pgmoneta_append(pgmoneta_append(NULL, local_root), "backup.manifest");
   sha512_tmp = pgmoneta_append(pgmoneta_append(NULL, local_root), "backup.sha512.tmp");
   sha512_final = pgmoneta_append(pgmoneta_append(NULL, local_root), "backup.sha512");
   info_tmp = pgmoneta_append(pgmoneta_append(NULL, local_root), "backup.info.tmp");
   info_final = pgmoneta_append(pgmoneta_append(NULL, local_root), "backup.info");

   if (rename(manifest_tmp, manifest_final))
   {
      pgmoneta_log_error("S3 restore: could not rename %s to %s", manifest_tmp, manifest_final);
      goto error;
   }

   if (rename(sha512_tmp, sha512_final))
   {
      pgmoneta_log_error("S3 restore: could not rename %s to %s", sha512_tmp, sha512_final);
      goto error;
   }

   if (rename(info_tmp, info_final))
   {
      pgmoneta_log_error("S3 restore: could not rename %s to %s", info_tmp, info_final);
      goto error;
   }

   pgmoneta_log_info("S3 restore: %s/%s completed", config->common.servers[server].name, label);

   free(s3_root);
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

   free(s3_root);
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

static int
s3_bootstrap(char* s3_root, int server, char* local_root)
{
   char buffer[4096];
   char* expected_hash = NULL;
   char* computed_hash = NULL;
   char* sha512_path = NULL;
   char* info_path = NULL;
   char* manifest_path = NULL;
   FILE* sha512_file = NULL;
   struct http_response* response = NULL;

   pgmoneta_log_debug("S3 bootstrap: downloading root files");

   // download backup.sha512 file

   if (s3_send_get_request("backup.sha512", s3_root, server, -1, -1, &response))
   {
      pgmoneta_log_error("S3 bootstrap: failed to GET backup.sha512");
      goto error;
   }

   if (response->status_code != 200)
   {
      pgmoneta_log_error("S3 bootstrap: backup.sha512 returned status %d", response->status_code);
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
      pgmoneta_log_error("S3 bootstrap: failed to write backup.sha512");
      goto error;
   }

   pgmoneta_log_debug("S3 bootstrap: downloaded backup.sha512");
   pgmoneta_http_response_destroy(response);
   response = NULL;

   if (s3_send_get_request("backup.info", s3_root, server, -1, -1, &response))
   {
      pgmoneta_log_error("S3 bootstrap: failed to GET backup.info");
      goto error;
   }

   if (response->status_code != 200)
   {
      pgmoneta_log_error("S3 bootstrap: backup.info returned status %d", response->status_code);
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
      pgmoneta_log_error("S3 bootstrap: failed to write backup.info");
      goto error;
   }

   pgmoneta_log_debug("S3 bootstrap: downloaded backup.info");
   pgmoneta_http_response_destroy(response);
   response = NULL;

   // verify SHA512 of backup.info
   sha512_file = fopen(sha512_path, "r");
   if (sha512_file == NULL)
   {
      pgmoneta_log_error("S3 bootstrap: could not open %s", sha512_path);
      goto error;
   }

   if (fgets(&buffer[0], sizeof(buffer), sha512_file) == NULL)
   {
      pgmoneta_log_error("S3 bootstrap: backup.sha512 is empty");
      goto error;
   }

   fclose(sha512_file);
   sha512_file = NULL;

   expected_hash = strtok(&buffer[0], " ");
   if (expected_hash == NULL)
   {
      pgmoneta_log_error("S3 bootstrap: backup.sha512 format error");
      goto error;
   }

   if (pgmoneta_create_sha512_file(info_path, &computed_hash))
   {
      pgmoneta_log_error("S3 bootstrap: could not compute SHA512 of backup.info");
      goto error;
   }

   if (strcmp(expected_hash, computed_hash))
   {
      pgmoneta_log_error("S3 bootstrap: backup.info SHA512 mismatch");
      pgmoneta_log_error("S3 bootstrap: expected %s", expected_hash);
      pgmoneta_log_error("S3 bootstrap: computed %s", computed_hash);
      goto error;
   }

   pgmoneta_log_info("S3 bootstrap: backup.info integrity verified");

   //download backup.manifest (CSV)
   if (s3_send_get_request("backup.manifest", s3_root, server, -1, -1, &response))
   {
      pgmoneta_log_error("S3 bootstrap: failed to GET backup.manifest");
      goto error;
   }

   if (response->status_code != 200)
   {
      pgmoneta_log_error("S3 bootstrap: backup.manifest returned status %d", response->status_code);
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
      pgmoneta_log_error("S3 bootstrap: failed to write backup.manifest");
      goto error;
   }

   pgmoneta_log_info("S3 bootstrap: downloaded backup.manifest");
   pgmoneta_http_response_destroy(response);
   response = NULL;

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
s3_download_files(char* s3_root, char* local_root, int server, int compression, int encryption)
{
   char* s3_path = NULL;
   char* local_file_path = NULL;
   char* suffix = NULL;
   char* filename = NULL;
   char* manifest_path = NULL;
   char* file_path = NULL;
   int number_of_workers = 0;
   struct deque* paths = NULL;
   struct deque_iterator* iter = NULL;
   struct workers* workers = NULL;
   struct worker_input* payload = NULL;

   manifest_path = pgmoneta_append(manifest_path, local_root);
   manifest_path = pgmoneta_append(manifest_path, "backup.manifest.tmp");

   if (pgmoneta_extraction_get_suffix(compression, encryption, &suffix))
   {
      pgmoneta_log_error("S3 download: failed to determine file suffix");
      goto error;
   }

   pgmoneta_log_debug("S3 download: file suffix is '%s'", suffix != NULL ? suffix : "(none)");

   number_of_workers = pgmoneta_get_number_of_workers(server);
   if (number_of_workers > 0)
   {
      pgmoneta_workers_initialize(number_of_workers, &workers);
   }

   if (pgmoneta_manifest_get_paths(manifest_path, &paths))
   {
      pgmoneta_log_error("S3 download: failed to read manifest %s", manifest_path);
      goto error;
   }

   pgmoneta_deque_iterator_create(paths, &iter);

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

      s3_path = NULL;
      s3_path = pgmoneta_append(s3_path, "data/");
      s3_path = pgmoneta_append(s3_path, filename);

      local_file_path = NULL;
      local_file_path = pgmoneta_append(local_file_path, local_root);
      local_file_path = pgmoneta_append(local_file_path, "data/");
      local_file_path = pgmoneta_append(local_file_path, filename);

      if (pgmoneta_create_worker_input(s3_root, s3_path, local_file_path, server, workers, &payload))
      {
         pgmoneta_log_error("S3 download: failed to create worker input");
         goto error;
      }

      if (workers != NULL && workers->outcome)
      {
         pgmoneta_workers_add(workers, do_download_file, (struct worker_common*)payload);
      }
      else
      {
         do_download_file((struct worker_common*)payload);
      }

      free(filename);
      filename = NULL;
      free(s3_path);
      s3_path = NULL;
      free(local_file_path);
      local_file_path = NULL;
   }

   pgmoneta_workers_wait(workers);
   if (workers != NULL && !workers->outcome)
   {
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
   free(s3_path);
   free(local_file_path);

   return 1;
}
static void
do_download_file(struct worker_common* wc)
{
   struct worker_input* wi = (struct worker_input*)wc;
   struct http_response* response = NULL;
   char* s3_root = wi->directory;
   char* s3_path = wi->from;
   char* local_path = wi->to;
   int server = wi->level;

   if (s3_send_get_request(s3_path, s3_root, server, -1, -1, &response))
   {
      pgmoneta_log_error("S3 download: failed to GET %s", s3_path);
      goto error;
   }

   if (response->status_code != 200)
   {
      pgmoneta_log_error("S3 download: %s returned status %d", s3_path, response->status_code);
      goto error;
   }

   if (pgmoneta_exists(local_path))
   {
      pgmoneta_delete_file(local_path, NULL);
   }

   if (pgmoneta_append_file_chunk(local_path, response->payload.data, response->payload.data_size, 0))
   {
      pgmoneta_log_error("S3 download: failed to write %s", local_path);
      goto error;
   }

   pgmoneta_log_debug("S3 download: %s", s3_path);
   pgmoneta_http_response_destroy(response);
   free(wi);
   return;

error:
   if (wi->common.workers != NULL)
   {
      wi->common.workers->outcome = false;
   }
   pgmoneta_http_response_destroy(response);
   free(wi);
}
static void
do_upload_file(struct worker_common* wc)
{
   struct worker_input* wi = (struct worker_input*)wc;

   if (s3_send_upload_request(wi->directory, wi->from, wi->to, wi->level))
   {
      pgmoneta_log_error("S3 upload: failed %s", wi->to);
      if (wi->common.workers != NULL)
      {
         wi->common.workers->outcome = false;
      }
   }

   free(wi);
}
static int
s3_upload_files(char* local_root, char* s3_root, int server, int compression, int encryption)
{
   int number_of_workers = 0;
   char* manifest_path = NULL;
   char* file_path = NULL;
   char* relative_file = NULL;
   char* suffix = NULL;
   struct deque* paths = NULL;
   struct deque_iterator* iter = NULL;
   struct workers* workers = NULL;
   struct worker_input* payload = NULL;

   manifest_path = pgmoneta_append(manifest_path, local_root);
   manifest_path = pgmoneta_append(manifest_path, "backup.manifest");

   if (pgmoneta_extraction_get_suffix(compression, encryption, &suffix))
   {
      pgmoneta_log_error("S3 upload: failed to determine file suffix");
      goto error;
   }

   number_of_workers = pgmoneta_get_number_of_workers(server);
   if (number_of_workers > 0)
   {
      pgmoneta_workers_initialize(number_of_workers, &workers);
   }

   if (pgmoneta_manifest_get_paths(manifest_path, &paths))
   {
      pgmoneta_log_error("S3 upload: failed to read manifest %s", manifest_path);
      goto error;
   }

   pgmoneta_deque_iterator_create(paths, &iter);

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

      if (pgmoneta_create_worker_input(local_root, s3_root, relative_file, server, workers, &payload))
      {
         pgmoneta_log_error("S3 upload: failed to create worker input");
         free(relative_file);
         goto error;
      }

      if (workers != NULL && workers->outcome)
      {
         pgmoneta_workers_add(workers, do_upload_file, (struct worker_common*)payload);
      }
      else
      {
         do_upload_file((struct worker_common*)payload);
      }

      free(relative_file);
      relative_file = NULL;
   }

   pgmoneta_workers_wait(workers);
   if (workers != NULL && !workers->outcome)
   {
      goto error;
   }
   pgmoneta_workers_destroy(workers);

   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_deque_destroy(paths);
   iter = NULL;
   paths = NULL;

   /* upload metadata file last (commit marker) */
   if (s3_send_upload_request(local_root, s3_root, "backup.manifest", server))
   {
      pgmoneta_log_error("S3 upload: failed to upload backup.manifest");
      goto error;
   }
   if (s3_send_upload_request(local_root, s3_root, "backup.sha512", server))
   {
      pgmoneta_log_error("S3 upload: failed to upload backup.sha512");
      goto error;
   }
   if (s3_send_upload_request(local_root, s3_root, "backup.info", server))
   {
      pgmoneta_log_error("S3 upload: failed to upload backup.info");
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

   return 1;
}
static int
s3_list_objects(char* relative_path, char* s3_root, int server, struct deque** objects)
{
   struct http_response* response = NULL;
   char* continuationToken = NULL;
   bool is_truncated = true;

   while (is_truncated)
   {
      if (s3_send_list_request(relative_path, s3_root, server, continuationToken, &response))
      {
         goto error;
      }

      free(continuationToken);
      continuationToken = NULL;

      if (xml_parse_s3_list_truncated(response->payload.data, &is_truncated, &continuationToken))
      {
         goto error;
      }

      if (xml_parse_s3_list(response->payload.data, objects))
      {
         goto error;
      }

      pgmoneta_http_response_destroy(response);
      response = NULL;
   }

   return 0;

error:
   pgmoneta_http_response_destroy(response);
   free(continuationToken);
   return 1;
}

static int
s3_delete_all_objects(char* relative_path, char* s3_root, int server, struct art* nodes)
{
   struct http_response* list_response = NULL;
   struct http_response* delete_response = NULL;
   struct deque* objects = NULL;
   char* continuation_token = NULL;
   char* delete_xml = NULL;
   bool is_truncated = true;

   (void)nodes;

   while (is_truncated)
   {
      if (s3_send_list_request(relative_path, s3_root, server, continuation_token, &list_response))
      {
         goto error;
      }

      free(continuation_token);
      continuation_token = NULL;

      if (xml_parse_s3_list_truncated(list_response->payload.data, &is_truncated, &continuation_token))
      {
         goto error;
      }

      if (xml_parse_s3_list(list_response->payload.data, &objects))
      {
         goto error;
      }

      pgmoneta_http_response_destroy(list_response);
      list_response = NULL;

      if (objects != NULL && pgmoneta_deque_size(objects) > 0)
      {
         size_t sample_size = pgmoneta_deque_size(objects);
         if (xml_s3_build_delete_list(&delete_xml, objects, sample_size))
         {
            goto error;
         }

         pgmoneta_deque_destroy(objects);
         objects = NULL;

         if (s3_send_delete_request(relative_path, s3_root, server, delete_xml, &delete_response))
         {
            goto error;
         }
         pgmoneta_http_response_destroy(delete_response);
         delete_response = NULL;

         free(delete_xml);
         delete_xml = NULL;
      }
      else
      {
         pgmoneta_deque_destroy(objects);
         objects = NULL;
      }
   }

   free(continuation_token);
   return 0;

error:
   pgmoneta_http_response_destroy(list_response);
   pgmoneta_http_response_destroy(delete_response);
   pgmoneta_deque_destroy(objects);
   free(delete_xml);
   free(continuation_token);
   return 1;
}
static int
s3_send_list_request(char* relative_path, char* s3_root, int server, char* continuationToken, struct http_response** response)
{
   char short_date[SHORT_TIME_LENGTH];
   char long_date[LONG_TIME_LENGTH];
   char* canonical_request = NULL;
   char* auth_value = NULL;
   char* string_to_sign = NULL;
   char* s3_host = NULL;
   char* s3_path = NULL;
   char* request_path = NULL;
   char* canonical_request_sha256 = NULL;
   char* key = NULL;
   char* query_string = NULL;
   unsigned char* date_key_hmac = NULL;
   unsigned char* date_region_key_hmac = NULL;
   unsigned char* date_region_service_key_hmac = NULL;
   unsigned char* signing_key_hmac = NULL;
   unsigned char* signature_hmac = NULL;
   unsigned char* signature_hex = NULL;
   int hmac_length = 0;
   struct http* connection = NULL;
   struct http_request* request = NULL;

   char* effective_storage_class = s3_get_effective_storage_class(server);
   char* effective_endpoint = s3_get_effective_endpoint(server);
   char* effective_region = s3_get_effective_region(server);
   char* effective_access_key_id = s3_get_effective_access_key_id(server);
   char* effective_bucket = s3_get_effective_bucket(server);
   char* effective_secret_access_key = s3_get_effective_secret_access_key(server);
   int effective_port = s3_get_effective_port(server);
   bool effective_use_tls = s3_get_effective_use_tls(server);
   bool path_style = (strlen(effective_endpoint) > 0);

   s3_path = pgmoneta_append(s3_path, s3_root);
   if (strlen(relative_path) > 0)
   {
      if (!pgmoneta_ends_with(s3_root, "/"))
      {
         s3_path = pgmoneta_append(s3_path, "/");
      }
      s3_path = pgmoneta_append(s3_path, relative_path);
   }

   char* prefix = s3_path;
   if (path_style)
   {
      char* slash = strchr(s3_path, '/');
      if (slash)
      {
         prefix = slash + 1;
      }
   }

   memset(&short_date[0], 0, sizeof(short_date));
   memset(&long_date[0], 0, sizeof(long_date));

   if (pgmoneta_get_timestamp_ISO8601_format(short_date, long_date))
   {
      goto error;
   }

   s3_host = s3_get_host(server);

   if (continuationToken != NULL)
   {
      char* encoded_token = s3_url_encode(continuationToken);
      query_string = pgmoneta_append(query_string, "continuation-token=");
      query_string = pgmoneta_append(query_string, encoded_token);
      query_string = pgmoneta_append(query_string, "&");
      free(encoded_token);
   }
   char* encoded_prefix = s3_url_encode(prefix);
   query_string = pgmoneta_append(query_string, "list-type=2&prefix=");
   query_string = pgmoneta_append(query_string, encoded_prefix);
   free(encoded_prefix);

   canonical_request = pgmoneta_append(canonical_request, "GET\n/");
   if (path_style)
   {
      canonical_request = pgmoneta_append(canonical_request, effective_bucket);
   }
   canonical_request = pgmoneta_append(canonical_request, "\n");
   canonical_request = pgmoneta_append(canonical_request, query_string);
   canonical_request = pgmoneta_append(canonical_request, "\n");
   canonical_request = pgmoneta_append(canonical_request, "host:");
   canonical_request = pgmoneta_append(canonical_request, s3_host);
   canonical_request = pgmoneta_append(canonical_request, "\nx-amz-content-sha256:UNSIGNED-PAYLOAD");
   canonical_request = pgmoneta_append(canonical_request, "\nx-amz-date:");
   canonical_request = pgmoneta_append(canonical_request, long_date);
   canonical_request = pgmoneta_append(canonical_request, "\n\nhost;x-amz-content-sha256;x-amz-date\n");
   canonical_request = pgmoneta_append(canonical_request, "UNSIGNED-PAYLOAD");

   pgmoneta_generate_string_sha256_hash(canonical_request, &canonical_request_sha256);

   string_to_sign = pgmoneta_append(string_to_sign, "AWS4-HMAC-SHA256\n");
   string_to_sign = pgmoneta_append(string_to_sign, long_date);
   string_to_sign = pgmoneta_append(string_to_sign, "\n");
   string_to_sign = pgmoneta_append(string_to_sign, short_date);
   string_to_sign = pgmoneta_append(string_to_sign, "/");
   string_to_sign = pgmoneta_append(string_to_sign, effective_region);
   string_to_sign = pgmoneta_append(string_to_sign, "/s3/aws4_request\n");
   string_to_sign = pgmoneta_append(string_to_sign, canonical_request_sha256);

   key = pgmoneta_append(key, "AWS4");
   key = pgmoneta_append(key, effective_secret_access_key);

   if (pgmoneta_generate_string_hmac_sha256_hash(key, strlen(key), short_date, SHORT_TIME_LENGTH - 1, &date_key_hmac, &hmac_length))
   {
      goto error;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash((char*)date_key_hmac, hmac_length, effective_region, strlen(effective_region), &date_region_key_hmac, &hmac_length))
   {
      goto error;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash((char*)date_region_key_hmac, hmac_length, "s3", strlen("s3"), &date_region_service_key_hmac, &hmac_length))
   {
      goto error;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash((char*)date_region_service_key_hmac, hmac_length, "aws4_request", strlen("aws4_request"), &signing_key_hmac, &hmac_length))
   {
      goto error;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash((char*)signing_key_hmac, hmac_length, string_to_sign, strlen(string_to_sign), &signature_hmac, &hmac_length))
   {
      goto error;
   }

   pgmoneta_convert_base32_to_hex(signature_hmac, hmac_length, &signature_hex);

   // Build authorization header with matching signed headers
   auth_value = pgmoneta_append(auth_value, "AWS4-HMAC-SHA256 Credential=");
   auth_value = pgmoneta_append(auth_value, effective_access_key_id);
   auth_value = pgmoneta_append(auth_value, "/");
   auth_value = pgmoneta_append(auth_value, short_date);
   auth_value = pgmoneta_append(auth_value, "/");
   auth_value = pgmoneta_append(auth_value, effective_region);
   auth_value = pgmoneta_append(auth_value, "/s3/aws4_request,SignedHeaders=host;x-amz-content-sha256;x-amz-date,Signature=");
   auth_value = pgmoneta_append(auth_value, (char*)signature_hex);

   int s3_port;

   if (effective_port != 0)
   {
      s3_port = effective_port;
   }
   else
   {
      s3_port = effective_use_tls ? 443 : 80;
   }

   bool use_tls = effective_use_tls;
   if (s3_port == 443)
   {
      use_tls = true;
   }

   if (pgmoneta_http_create(s3_host, s3_port, use_tls, &connection))
   {
      goto error;
   }

   if (path_style)
   {
      request_path = pgmoneta_append(request_path, "/");
      request_path = pgmoneta_append(request_path, effective_bucket);
      request_path = pgmoneta_append(request_path, "?");
   }
   else
   {
      request_path = pgmoneta_append(request_path, "/?");
   }
   request_path = pgmoneta_append(request_path, query_string);

   if (pgmoneta_http_request_create(PGMONETA_HTTP_GET, request_path, &request))
   {
      goto error;
   }

   if (s3_add_request_headers(request, auth_value, "UNSIGNED-PAYLOAD", long_date, effective_storage_class))
   {
      goto error;
   }

   if (pgmoneta_http_invoke(connection, request, response))
   {
      goto error;
   }

   if ((*response)->status_code >= 200 && (*response)->status_code < 300)
   {
      pgmoneta_log_info("Successfully listed files to URL: https://%s/%s", s3_host, s3_path);
   }
   else
   {
      pgmoneta_log_error("S3 listed failed with status code: %d. Failed to list files to S3 path: %s",
                         (*response)->status_code, s3_path);
      goto error;
   }

   free(s3_host);
   free(request_path);
   free(signature_hex);
   free(signature_hmac);
   free(signing_key_hmac);
   free(date_region_service_key_hmac);
   free(date_region_key_hmac);
   free(date_key_hmac);
   free(key);
   free(s3_path);
   free(canonical_request_sha256);
   free(canonical_request);
   free(string_to_sign);
   free(auth_value);
   free(query_string);

   pgmoneta_http_request_destroy(request);
   pgmoneta_http_destroy(connection);

   return 0;

error:

   free(s3_host);
   free(request_path);
   free(signature_hex);
   free(signature_hmac);

   free(signing_key_hmac);
   free(date_region_service_key_hmac);
   free(date_region_key_hmac);
   free(date_key_hmac);
   free(key);
   free(s3_path);
   free(canonical_request_sha256);
   free(canonical_request);
   free(string_to_sign);
   free(auth_value);
   free(query_string);

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
s3_send_delete_request(char* relative_path, char* s3_root, int server, char* xml_body, struct http_response** response)
{
   char short_date[SHORT_TIME_LENGTH];
   char long_date[LONG_TIME_LENGTH];
   char* canonical_request = NULL;
   char* auth_value = NULL;
   char* string_to_sign = NULL;
   char* s3_host = NULL;
   char* s3_path = NULL;
   char* request_path = NULL;
   char* canonical_request_sha256 = NULL;
   char* key = NULL;
   char* query_string = NULL;
   char* body_hash = NULL;
   unsigned char* date_key_hmac = NULL;
   unsigned char* date_region_key_hmac = NULL;
   unsigned char* date_region_service_key_hmac = NULL;
   unsigned char* signing_key_hmac = NULL;
   unsigned char* signature_hmac = NULL;
   unsigned char* signature_hex = NULL;
   int hmac_length = 0;
   struct http* connection = NULL;
   struct http_request* request = NULL;

   char* effective_endpoint = s3_get_effective_endpoint(server);
   char* effective_region = s3_get_effective_region(server);
   char* effective_access_key_id = s3_get_effective_access_key_id(server);
   char* effective_secret_access_key = s3_get_effective_secret_access_key(server);
   char* effective_bucket = s3_get_effective_bucket(server);
   int effective_port = s3_get_effective_port(server);
   bool effective_use_tls = s3_get_effective_use_tls(server);
   bool path_style = (strlen(effective_endpoint) > 0);

   s3_path = pgmoneta_append(s3_path, s3_root);
   if (strlen(relative_path) > 0)
   {
      if (!pgmoneta_ends_with(s3_root, "/"))
      {
         s3_path = pgmoneta_append(s3_path, "/");
      }
      s3_path = pgmoneta_append(s3_path, relative_path);
   }

   memset(&short_date[0], 0, sizeof(short_date));
   memset(&long_date[0], 0, sizeof(long_date));

   if (pgmoneta_get_timestamp_ISO8601_format(short_date, long_date))
   {
      goto error;
   }

   if (xml_body == NULL || strlen(xml_body) == 0)
   {
      pgmoneta_log_error("S3 delete request requires a non-empty XML body");
      goto error;
   }

   query_string = pgmoneta_append(query_string, "delete=");

   if (pgmoneta_generate_string_sha256_hash(xml_body, &body_hash))
   {
      goto error;
   }

   s3_host = s3_get_host(server);

   canonical_request = pgmoneta_append(canonical_request, "POST\n/");
   if (path_style)
   {
      canonical_request = pgmoneta_append(canonical_request, effective_bucket);
   }
   canonical_request = pgmoneta_append(canonical_request, "\n");
   canonical_request = pgmoneta_append(canonical_request, query_string);
   canonical_request = pgmoneta_append(canonical_request, "\n");
   canonical_request = pgmoneta_append(canonical_request, "host:");
   canonical_request = pgmoneta_append(canonical_request, s3_host);
   canonical_request = pgmoneta_append(canonical_request, "\nx-amz-content-sha256:");
   canonical_request = pgmoneta_append(canonical_request, body_hash);
   canonical_request = pgmoneta_append(canonical_request, "\nx-amz-date:");
   canonical_request = pgmoneta_append(canonical_request, long_date);
   canonical_request = pgmoneta_append(canonical_request, "\n\nhost;x-amz-content-sha256;x-amz-date\n");
   canonical_request = pgmoneta_append(canonical_request, body_hash);

   pgmoneta_generate_string_sha256_hash(canonical_request, &canonical_request_sha256);

   string_to_sign = pgmoneta_append(string_to_sign, "AWS4-HMAC-SHA256\n");
   string_to_sign = pgmoneta_append(string_to_sign, long_date);
   string_to_sign = pgmoneta_append(string_to_sign, "\n");
   string_to_sign = pgmoneta_append(string_to_sign, short_date);
   string_to_sign = pgmoneta_append(string_to_sign, "/");
   string_to_sign = pgmoneta_append(string_to_sign, effective_region);
   string_to_sign = pgmoneta_append(string_to_sign, "/s3/aws4_request\n");
   string_to_sign = pgmoneta_append(string_to_sign, canonical_request_sha256);

   key = pgmoneta_append(key, "AWS4");
   key = pgmoneta_append(key, effective_secret_access_key);

   if (pgmoneta_generate_string_hmac_sha256_hash(key, strlen(key), short_date, SHORT_TIME_LENGTH - 1, &date_key_hmac, &hmac_length))
   {
      goto error;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash((char*)date_key_hmac, hmac_length, effective_region, strlen(effective_region), &date_region_key_hmac, &hmac_length))
   {
      goto error;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash((char*)date_region_key_hmac, hmac_length, "s3", strlen("s3"), &date_region_service_key_hmac, &hmac_length))
   {
      goto error;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash((char*)date_region_service_key_hmac, hmac_length, "aws4_request", strlen("aws4_request"), &signing_key_hmac, &hmac_length))
   {
      goto error;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash((char*)signing_key_hmac, hmac_length, string_to_sign, strlen(string_to_sign), &signature_hmac, &hmac_length))
   {
      goto error;
   }

   pgmoneta_convert_base32_to_hex(signature_hmac, hmac_length, &signature_hex);

   // Build authorization header with matching signed headers
   auth_value = pgmoneta_append(auth_value, "AWS4-HMAC-SHA256 Credential=");
   auth_value = pgmoneta_append(auth_value, effective_access_key_id);
   auth_value = pgmoneta_append(auth_value, "/");
   auth_value = pgmoneta_append(auth_value, short_date);
   auth_value = pgmoneta_append(auth_value, "/");
   auth_value = pgmoneta_append(auth_value, effective_region);
   auth_value = pgmoneta_append(auth_value, "/s3/aws4_request,SignedHeaders=host;x-amz-content-sha256;x-amz-date,Signature=");
   auth_value = pgmoneta_append(auth_value, (char*)signature_hex);

   int s3_port;

   if (effective_port != 0)
   {
      s3_port = effective_port;
   }
   else
   {
      s3_port = effective_use_tls ? 443 : 80;
   }

   bool use_tls = effective_use_tls;
   if (s3_port == 443)
   {
      use_tls = true;
   }

   if (pgmoneta_http_create(s3_host, s3_port, use_tls, &connection))
   {
      goto error;
   }

   if (path_style)
   {
      request_path = pgmoneta_append(request_path, "/");
      request_path = pgmoneta_append(request_path, effective_bucket);
      request_path = pgmoneta_append(request_path, "?");
   }
   else
   {
      request_path = pgmoneta_append(request_path, "/?");
   }
   request_path = pgmoneta_append(request_path, query_string);

   if (pgmoneta_http_request_create(PGMONETA_HTTP_POST, request_path, &request))
   {
      goto error;
   }

   if (s3_add_request_headers(request, auth_value, body_hash, long_date, ""))
   {
      goto error;
   }

   if (pgmoneta_http_request_add_header(request, "Content-Type", "application/xml"))
   {
      goto error;
   }

   if (pgmoneta_http_set_data(request, xml_body, strlen(xml_body)))
   {
      goto error;
   }

   if (pgmoneta_http_invoke(connection, request, response))
   {
      goto error;
   }

   if ((*response)->status_code >= 200 && (*response)->status_code < 300)
   {
      bool has_fatal_error = false;

      if (xml_parse_s3_delete_result((*response)->payload.data, &has_fatal_error))
      {
         goto error;
      }

      if (has_fatal_error)
      {
         pgmoneta_log_error("S3 delete response contains object-level errors for path: %s", s3_path);
         goto error;
      }

      pgmoneta_log_info("Successfully deleted files from URL: https://%s/%s", s3_host, s3_path);
   }
   else
   {
      pgmoneta_log_error("S3 delete failed with status code: %d. Failed to delete files from S3 path: %s",
                         (*response)->status_code, s3_path);
      goto error;
   }

   free(s3_host);
   free(request_path);
   free(signature_hex);
   free(signature_hmac);
   free(signing_key_hmac);
   free(date_region_service_key_hmac);
   free(date_region_key_hmac);
   free(date_key_hmac);
   free(key);
   free(s3_path);
   free(body_hash);
   free(canonical_request_sha256);
   free(canonical_request);
   free(string_to_sign);
   free(auth_value);
   free(query_string);

   pgmoneta_http_request_destroy(request);
   pgmoneta_http_destroy(connection);

   return 0;

error:

   free(s3_host);
   free(request_path);
   free(signature_hex);
   free(signature_hmac);

   free(signing_key_hmac);
   free(date_region_service_key_hmac);
   free(date_region_key_hmac);
   free(date_key_hmac);
   free(key);
   free(s3_path);
   free(body_hash);
   free(canonical_request_sha256);
   free(canonical_request);
   free(string_to_sign);
   free(auth_value);
   free(query_string);

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
s3_send_get_request(char* relative_path, char* s3_root, int server,
                    long range_start, long range_end,
                    struct http_response** response)
{
   char short_date[SHORT_TIME_LENGTH];
   char long_date[LONG_TIME_LENGTH];
   char* canonical_request = NULL;
   char* auth_value = NULL;
   char* string_to_sign = NULL;
   char* s3_host = NULL;
   char* s3_path = NULL;
   char* request_path = NULL;
   char* canonical_request_sha256 = NULL;
   char* key = NULL;
   char* body_hash = NULL;
   char* range_header = NULL;
   unsigned char* date_key_hmac = NULL;
   unsigned char* date_region_key_hmac = NULL;
   unsigned char* date_region_service_key_hmac = NULL;
   unsigned char* signing_key_hmac = NULL;
   unsigned char* signature_hmac = NULL;
   unsigned char* signature_hex = NULL;
   int hmac_length = 0;
   struct http* connection = NULL;
   struct http_request* request = NULL;

   char* effective_endpoint = s3_get_effective_endpoint(server);
   char* effective_region = s3_get_effective_region(server);
   char* effective_access_key_id = s3_get_effective_access_key_id(server);
   char* effective_secret_access_key = s3_get_effective_secret_access_key(server);
   int effective_port = s3_get_effective_port(server);
   bool effective_use_tls = s3_get_effective_use_tls(server);
   bool path_style = (strlen(effective_endpoint) > 0);

   s3_path = pgmoneta_append(s3_path, s3_root);
   if (relative_path != NULL && strlen(relative_path) > 0)
   {
      if (!pgmoneta_ends_with(s3_root, "/"))
      {
         s3_path = pgmoneta_append(s3_path, "/");
      }
      s3_path = pgmoneta_append(s3_path, relative_path);
   }

   memset(&short_date[0], 0, sizeof(short_date));
   memset(&long_date[0], 0, sizeof(long_date));

   if (pgmoneta_get_timestamp_ISO8601_format(short_date, long_date))
   {
      goto error;
   }

   if (pgmoneta_generate_string_sha256_hash("", &body_hash))
   {
      goto error;
   }

   s3_host = s3_get_host(server);

   if (range_start >= 0 && range_end > range_start)
   {
      char range_buf[64];
      range_header = pgmoneta_append(range_header, "bytes=");
      pgmoneta_snprintf(range_buf, sizeof(range_buf), "%ld-%ld", range_start, range_end);
      range_header = pgmoneta_append(range_header, range_buf);
   }

   canonical_request = pgmoneta_append(canonical_request, "GET\n/");
   canonical_request = pgmoneta_append(canonical_request, s3_path);
   canonical_request = pgmoneta_append(canonical_request, "\n\n");

   canonical_request = pgmoneta_append(canonical_request, "host:");
   canonical_request = pgmoneta_append(canonical_request, s3_host);
   canonical_request = pgmoneta_append(canonical_request, "\n");

   if (range_header != NULL)
   {
      canonical_request = pgmoneta_append(canonical_request, "range:");
      canonical_request = pgmoneta_append(canonical_request, range_header);
      canonical_request = pgmoneta_append(canonical_request, "\n");
   }

   canonical_request = pgmoneta_append(canonical_request, "x-amz-content-sha256:");
   canonical_request = pgmoneta_append(canonical_request, body_hash);
   canonical_request = pgmoneta_append(canonical_request, "\n");

   canonical_request = pgmoneta_append(canonical_request, "x-amz-date:");
   canonical_request = pgmoneta_append(canonical_request, long_date);
   canonical_request = pgmoneta_append(canonical_request, "\n\n");

   if (range_header != NULL)
   {
      canonical_request = pgmoneta_append(canonical_request, "host;range;x-amz-content-sha256;x-amz-date\n");
   }
   else
   {
      canonical_request = pgmoneta_append(canonical_request, "host;x-amz-content-sha256;x-amz-date\n");
   }

   canonical_request = pgmoneta_append(canonical_request, body_hash);

   pgmoneta_generate_string_sha256_hash(canonical_request, &canonical_request_sha256);

   string_to_sign = pgmoneta_append(string_to_sign, "AWS4-HMAC-SHA256\n");
   string_to_sign = pgmoneta_append(string_to_sign, long_date);
   string_to_sign = pgmoneta_append(string_to_sign, "\n");
   string_to_sign = pgmoneta_append(string_to_sign, short_date);
   string_to_sign = pgmoneta_append(string_to_sign, "/");
   string_to_sign = pgmoneta_append(string_to_sign, effective_region);
   string_to_sign = pgmoneta_append(string_to_sign, "/s3/aws4_request\n");
   string_to_sign = pgmoneta_append(string_to_sign, canonical_request_sha256);

   key = pgmoneta_append(key, "AWS4");
   key = pgmoneta_append(key, effective_secret_access_key);

   if (pgmoneta_generate_string_hmac_sha256_hash(key, strlen(key), short_date, SHORT_TIME_LENGTH - 1, &date_key_hmac, &hmac_length))
   {
      goto error;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash((char*)date_key_hmac, hmac_length, effective_region, strlen(effective_region), &date_region_key_hmac, &hmac_length))
   {
      goto error;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash((char*)date_region_key_hmac, hmac_length, "s3", strlen("s3"), &date_region_service_key_hmac, &hmac_length))
   {
      goto error;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash((char*)date_region_service_key_hmac, hmac_length, "aws4_request", strlen("aws4_request"), &signing_key_hmac, &hmac_length))
   {
      goto error;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash((char*)signing_key_hmac, hmac_length, string_to_sign, strlen(string_to_sign), &signature_hmac, &hmac_length))
   {
      goto error;
   }

   pgmoneta_convert_base32_to_hex(signature_hmac, hmac_length, &signature_hex);

   if (range_start >= 0 && range_end > range_start)
   {
      auth_value = pgmoneta_append(auth_value, "AWS4-HMAC-SHA256 Credential=");
      auth_value = pgmoneta_append(auth_value, effective_access_key_id);
      auth_value = pgmoneta_append(auth_value, "/");
      auth_value = pgmoneta_append(auth_value, short_date);
      auth_value = pgmoneta_append(auth_value, "/");
      auth_value = pgmoneta_append(auth_value, effective_region);
      auth_value = pgmoneta_append(auth_value, "/s3/aws4_request,SignedHeaders=host;range;x-amz-content-sha256;x-amz-date,Signature=");
      auth_value = pgmoneta_append(auth_value, (char*)signature_hex);
   }
   else
   {
      auth_value = pgmoneta_append(auth_value, "AWS4-HMAC-SHA256 Credential=");
      auth_value = pgmoneta_append(auth_value, effective_access_key_id);
      auth_value = pgmoneta_append(auth_value, "/");
      auth_value = pgmoneta_append(auth_value, short_date);
      auth_value = pgmoneta_append(auth_value, "/");
      auth_value = pgmoneta_append(auth_value, effective_region);
      auth_value = pgmoneta_append(auth_value, "/s3/aws4_request,SignedHeaders=host;x-amz-content-sha256;x-amz-date,Signature=");
      auth_value = pgmoneta_append(auth_value, (char*)signature_hex);
   }

   int s3_port;
   if (effective_port != 0)
   {
      s3_port = effective_port;
   }
   else
   {
      s3_port = effective_use_tls ? 443 : 80;
   }

   bool use_tls = effective_use_tls;
   if (s3_port == 443)
   {
      use_tls = true;
   }

   if (pgmoneta_http_create(s3_host, s3_port, use_tls, &connection))
   {
      goto error;
   }

   (void)path_style;
   request_path = pgmoneta_append(request_path, "/");
   request_path = pgmoneta_append(request_path, s3_path);

   if (pgmoneta_http_request_create(PGMONETA_HTTP_GET, request_path, &request))
   {
      goto error;
   }

   if (s3_add_request_headers(request, auth_value, body_hash, long_date, ""))
   {
      goto error;
   }

   if (range_header != NULL)
   {
      if (pgmoneta_http_request_add_header(request, "Range", range_header))
      {
         goto error;
      }
   }

   if (pgmoneta_http_invoke(connection, request, response))
   {
      goto error;
   }

   free(s3_host);
   free(request_path);
   free(range_header);
   free(signature_hex);
   free(signature_hmac);
   free(signing_key_hmac);
   free(date_region_service_key_hmac);
   free(date_region_key_hmac);
   free(date_key_hmac);
   free(key);
   free(s3_path);
   free(body_hash);
   free(canonical_request_sha256);
   free(canonical_request);
   free(string_to_sign);
   free(auth_value);

   pgmoneta_http_request_destroy(request);
   pgmoneta_http_destroy(connection);

   return 0;

error:

   free(s3_host);
   free(request_path);
   free(range_header);
   free(signature_hex);
   free(signature_hmac);
   free(signing_key_hmac);
   free(date_region_service_key_hmac);
   free(date_region_key_hmac);
   free(date_key_hmac);
   free(key);
   free(s3_path);
   free(body_hash);
   free(canonical_request_sha256);
   free(canonical_request);
   free(string_to_sign);
   free(auth_value);

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
s3_send_upload_request(char* local_root, char* s3_root, char* relative_path, int server)
{
   char short_date[SHORT_TIME_LENGTH];
   char long_date[LONG_TIME_LENGTH];
   char* canonical_request = NULL;
   char* auth_value = NULL;
   char* string_to_sign = NULL;
   char* s3_host = NULL;
   char* s3_path = NULL;
   char* request_path = NULL;
   char* file_sha256 = NULL;
   char* canonical_request_sha256 = NULL;
   char* key = NULL;
   char* local_path = NULL;
   unsigned char* date_key_hmac = NULL;
   unsigned char* date_region_key_hmac = NULL;
   unsigned char* date_region_service_key_hmac = NULL;
   unsigned char* signing_key_hmac = NULL;
   unsigned char* signature_hmac = NULL;
   unsigned char* signature_hex = NULL;
   int hmac_length = 0;
   FILE* file = NULL;
   struct stat file_info;
   void* file_data = NULL;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   bool use_storage_class = false;

   char* effective_storage_class = s3_get_effective_storage_class(server);
   char* effective_endpoint = s3_get_effective_endpoint(server);
   char* effective_region = s3_get_effective_region(server);
   char* effective_access_key_id = s3_get_effective_access_key_id(server);
   char* effective_secret_access_key = s3_get_effective_secret_access_key(server);
   int effective_port = s3_get_effective_port(server);
   bool effective_use_tls = s3_get_effective_use_tls(server);

   if (strlen(effective_storage_class) > 0 && strlen(effective_endpoint) == 0)
   {
      use_storage_class = true;
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

   s3_path = pgmoneta_append(s3_path, s3_root);
   if (strlen(relative_path) > 0)
   {
      if (!pgmoneta_ends_with(s3_root, "/"))
      {
         s3_path = pgmoneta_append(s3_path, "/");
      }
      s3_path = pgmoneta_append(s3_path, relative_path);
   }

   memset(&short_date[0], 0, sizeof(short_date));
   memset(&long_date[0], 0, sizeof(long_date));

   if (pgmoneta_get_timestamp_ISO8601_format(short_date, long_date))
   {
      goto error;
   }

   pgmoneta_create_sha256_file(local_path, &file_sha256);

   s3_host = s3_get_host(server);

   // Build canonical request
   canonical_request = pgmoneta_append(canonical_request, "PUT\n/");
   canonical_request = pgmoneta_append(canonical_request, s3_path);
   canonical_request = pgmoneta_append(canonical_request, "\n\nhost:");
   canonical_request = pgmoneta_append(canonical_request, s3_host);
   canonical_request = pgmoneta_append(canonical_request, "\nx-amz-content-sha256:");
   canonical_request = pgmoneta_append(canonical_request, file_sha256);
   canonical_request = pgmoneta_append(canonical_request, "\nx-amz-date:");
   canonical_request = pgmoneta_append(canonical_request, long_date);

   if (use_storage_class)
   {
      canonical_request = pgmoneta_append(canonical_request, "\nx-amz-storage-class:");
      canonical_request = pgmoneta_append(canonical_request, effective_storage_class);
      canonical_request = pgmoneta_append(canonical_request, "\n\nhost;x-amz-content-sha256;x-amz-date;x-amz-storage-class\n");
   }
   else
   {
      canonical_request = pgmoneta_append(canonical_request, "\n\nhost;x-amz-content-sha256;x-amz-date\n");
   }

   canonical_request = pgmoneta_append(canonical_request, file_sha256);

   pgmoneta_generate_string_sha256_hash(canonical_request, &canonical_request_sha256);

   string_to_sign = pgmoneta_append(string_to_sign, "AWS4-HMAC-SHA256\n");
   string_to_sign = pgmoneta_append(string_to_sign, long_date);
   string_to_sign = pgmoneta_append(string_to_sign, "\n");
   string_to_sign = pgmoneta_append(string_to_sign, short_date);
   string_to_sign = pgmoneta_append(string_to_sign, "/");
   string_to_sign = pgmoneta_append(string_to_sign, effective_region);
   string_to_sign = pgmoneta_append(string_to_sign, "/s3/aws4_request\n");
   string_to_sign = pgmoneta_append(string_to_sign, canonical_request_sha256);

   key = pgmoneta_append(key, "AWS4");
   key = pgmoneta_append(key, effective_secret_access_key);

   if (pgmoneta_generate_string_hmac_sha256_hash(key, strlen(key), short_date, SHORT_TIME_LENGTH - 1, &date_key_hmac, &hmac_length))
   {
      goto error;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash((char*)date_key_hmac, hmac_length, effective_region, strlen(effective_region), &date_region_key_hmac, &hmac_length))
   {
      goto error;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash((char*)date_region_key_hmac, hmac_length, "s3", strlen("s3"), &date_region_service_key_hmac, &hmac_length))
   {
      goto error;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash((char*)date_region_service_key_hmac, hmac_length, "aws4_request", strlen("aws4_request"), &signing_key_hmac, &hmac_length))
   {
      goto error;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash((char*)signing_key_hmac, hmac_length, string_to_sign, strlen(string_to_sign), &signature_hmac, &hmac_length))
   {
      goto error;
   }

   pgmoneta_convert_base32_to_hex(signature_hmac, hmac_length, &signature_hex);

   // Build authorization header with matching signed headers
   auth_value = pgmoneta_append(auth_value, "AWS4-HMAC-SHA256 Credential=");
   auth_value = pgmoneta_append(auth_value, effective_access_key_id);
   auth_value = pgmoneta_append(auth_value, "/");
   auth_value = pgmoneta_append(auth_value, short_date);
   auth_value = pgmoneta_append(auth_value, "/");
   auth_value = pgmoneta_append(auth_value, effective_region);

   if (use_storage_class)
   {
      auth_value = pgmoneta_append(auth_value, "/s3/aws4_request,SignedHeaders=host;x-amz-content-sha256;x-amz-date;x-amz-storage-class,Signature=");
   }
   else
   {
      auth_value = pgmoneta_append(auth_value, "/s3/aws4_request,SignedHeaders=host;x-amz-content-sha256;x-amz-date,Signature=");
   }

   auth_value = pgmoneta_append(auth_value, (char*)signature_hex);
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

   int s3_port;

   if (effective_port != 0)
   {
      s3_port = effective_port;
   }
   else
   {
      s3_port = effective_use_tls ? 443 : 80;
   }

   bool use_tls = effective_use_tls;
   if (s3_port == 443)
   {
      use_tls = true;
   }

   if (pgmoneta_http_create(s3_host, s3_port, use_tls, &connection))
   {
      goto error;
   }

   request_path = pgmoneta_append(request_path, "/");
   request_path = pgmoneta_append(request_path, s3_path);

   if (pgmoneta_http_request_create(PGMONETA_HTTP_PUT, request_path, &request))
   {
      goto error;
   }

   if (s3_add_request_headers(request, auth_value, file_sha256, long_date, effective_storage_class))
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
      goto error;
   }

   if (response->status_code >= 200 && response->status_code < 300)
   {
      pgmoneta_log_info("Successfully uploaded file to URL: https://%s/%s", s3_host, s3_path);
   }
   else
   {
      pgmoneta_log_error("S3 upload failed with status code: %d. Failed to upload: %s to S3 path: %s",
                         response->status_code, local_path, s3_path);
      goto error;
   }

   free(s3_host);
   free(request_path);
   free(file_sha256);
   free(signature_hex);
   free(signature_hmac);
   free(signing_key_hmac);
   free(date_region_service_key_hmac);
   free(date_region_key_hmac);
   free(date_key_hmac);
   free(key);
   free(local_path);
   free(s3_path);
   free(canonical_request_sha256);
   free(canonical_request);
   free(string_to_sign);
   free(auth_value);
   free(file_data);

   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);

   return 0;

error:

   free(s3_host);
   free(request_path);
   free(signature_hex);
   free(signature_hmac);

   free(signing_key_hmac);
   free(date_region_service_key_hmac);
   free(date_region_key_hmac);
   free(date_key_hmac);
   free(key);
   free(local_path);
   free(s3_path);
   free(canonical_request_sha256);
   free(file_sha256);
   free(canonical_request);
   free(string_to_sign);
   free(auth_value);
   free(file_data);

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
s3_get_host(int server)
{
   char* host = NULL;
   char* endpoint = NULL;
   char* effective_endpoint;
   char* effective_bucket;
   char* effective_region;

   effective_endpoint = s3_get_effective_endpoint(server);
   effective_bucket = s3_get_effective_bucket(server);
   effective_region = s3_get_effective_region(server);

   if (strlen(effective_endpoint) > 0)
   {
      endpoint = effective_endpoint;
      if (!strncmp(endpoint, "http://", 7))
      {
         endpoint += 7;
      }
      else if (!strncmp(endpoint, "https://", 8))
      {
         endpoint += 8;
      }
      host = pgmoneta_append(host, endpoint);
      return host;
   }

   host = pgmoneta_append(host, effective_bucket);
   host = pgmoneta_append(host, ".s3.");
   host = pgmoneta_append(host, effective_region);
   host = pgmoneta_append(host, ".amazonaws.com");

   return host;
}

static char*
s3_get_basepath(int server, char* identifier)
{
   char* d = NULL;
   struct main_configuration* config;
   char* effective_endpoint;
   char* effective_bucket;
   char* effective_base_dir;

   config = (struct main_configuration*)shmem;

   effective_endpoint = s3_get_effective_endpoint(server);
   effective_bucket = s3_get_effective_bucket(server);
   effective_base_dir = s3_get_effective_base_dir(server);

   if (strlen(effective_endpoint) > 0)
   {
      d = pgmoneta_append(d, effective_bucket);
      d = pgmoneta_append(d, "/");
   }

   if (strlen(effective_base_dir) > 0)
   {
      d = pgmoneta_append(d, effective_base_dir);
      if (!pgmoneta_ends_with(effective_base_dir, "/"))
      {
         d = pgmoneta_append(d, "/");
      }
   }

   d = pgmoneta_append(d, config->common.servers[server].name);
   d = pgmoneta_append(d, "/backup/");
   d = pgmoneta_append(d, identifier);

   return d;
}

static int
s3_add_request_headers(struct http_request* request, char* auth_value, char* file_sha256, char* long_date, char* storage_class)
{
   if (pgmoneta_http_request_add_header(request, "Authorization", auth_value))
   {
      return 1;
   }

   if (pgmoneta_http_request_add_header(request, "x-amz-content-sha256", file_sha256))
   {
      return 1;
   }

   if (pgmoneta_http_request_add_header(request, "x-amz-date", long_date))
   {
      return 1;
   }

   if (strlen(storage_class) && pgmoneta_http_request_add_header(request, "x-amz-storage-class", storage_class))
   {
      return 1;
   }

   return 0;
}
static char*
s3_url_encode(char* str)
{
   char* encoded = NULL;
   char hex[4];
   if (str == NULL)
      return NULL;
   for (int i = 0; str[i] != '\0'; i++)
   {
      unsigned char c = (unsigned char)str[i];
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
      {
         char ch[2] = {c, '\0'};
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

static int
xml_extract_tag(char* xml, char* tag, struct deque** values)
{
   char open_tag[1024];
   char close_tag[1024];
   char* ptr = xml;

   if (xml == NULL || tag == NULL || values == NULL)
   {
      return 1;
   }

   if (*values == NULL)
   {
      if (pgmoneta_deque_create(false, values))
      {
         return 1;
      }
   }

   pgmoneta_snprintf(open_tag, sizeof(open_tag), "<%s>", tag);
   pgmoneta_snprintf(close_tag, sizeof(close_tag), "</%s>", tag);

   while ((ptr = strstr(ptr, open_tag)) != NULL)
   {
      ptr += strlen(open_tag);
      char* end = strstr(ptr, close_tag);
      if (end != NULL)
      {
         int len = end - ptr;
         char* val = malloc(len + 1);
         if (val == NULL)
         {
            return 1;
         }
         memcpy(val, ptr, len);
         val[len] = '\0';
         pgmoneta_deque_add(*values, NULL, (uintptr_t)val, ValueString);
         ptr = end + strlen(close_tag);
         free(val);
      }
      else
      {
         break;
      }
   }

   return 0;
}

static int
xml_parse_s3_list(char* xml, struct deque** keys)
{
   return xml_extract_tag(xml, "Key", keys);
}

static int
xml_parse_s3_list_truncated(char* xml, bool* is_truncated, char** continuation_token)
{
   struct deque* trunc_values = NULL;
   struct deque* token_values = NULL;

   if (xml == NULL || is_truncated == NULL || continuation_token == NULL)
   {
      return 1;
   }

   *is_truncated = false;
   *continuation_token = NULL;

   if (xml_extract_tag(xml, "IsTruncated", &trunc_values) == 0 && trunc_values != NULL && pgmoneta_deque_size(trunc_values) > 0)
   {
      char* val = (char*)pgmoneta_deque_peek(trunc_values, NULL);
      if (val != NULL && strcmp(val, "true") == 0)
      {
         *is_truncated = true;
      }
   }

   if (xml_extract_tag(xml, "NextContinuationToken", &token_values) == 0 && token_values != NULL && pgmoneta_deque_size(token_values) > 0)
   {
      char* val = (char*)pgmoneta_deque_peek(token_values, NULL);
      if (val != NULL)
      {
         *continuation_token = strdup(val);
      }
   }

   if (trunc_values != NULL)
   {
      pgmoneta_deque_destroy(trunc_values);
   }
   if (token_values != NULL)
   {
      pgmoneta_deque_destroy(token_values);
   }

   return 0;
}

static int
xml_parse_s3_delete_result(char* xml, bool* has_fatal_error)
{
   char* cursor = NULL;

   if (xml == NULL || has_fatal_error == NULL)
   {
      return 1;
   }

   *has_fatal_error = false;
   cursor = xml;

   while ((cursor = strstr(cursor, "<Error>")) != NULL)
   {
      char* end = strstr(cursor, "</Error>");
      char* error_block = NULL;
      struct deque* key_values = NULL;
      struct deque* code_values = NULL;
      struct deque* message_values = NULL;
      char* key = NULL;
      char* code = NULL;
      char* message = NULL;
      size_t block_len = 0;

      if (end == NULL)
      {
         return 1;
      }

      end += strlen("</Error>");
      block_len = (size_t)(end - cursor);

      error_block = (char*)malloc(block_len + 1);
      if (error_block == NULL)
      {
         return 1;
      }
      memcpy(error_block, cursor, block_len);
      error_block[block_len] = '\0';

      if (xml_extract_tag(error_block, "Key", &key_values))
      {
         free(error_block);
         return 1;
      }
      if (xml_extract_tag(error_block, "Code", &code_values))
      {
         pgmoneta_deque_destroy(key_values);
         free(error_block);
         return 1;
      }
      if (xml_extract_tag(error_block, "Message", &message_values))
      {
         pgmoneta_deque_destroy(key_values);
         pgmoneta_deque_destroy(code_values);
         free(error_block);
         return 1;
      }

      key = key_values != NULL && pgmoneta_deque_size(key_values) > 0 ? (char*)pgmoneta_deque_peek(key_values, NULL) : "unknown";
      code = code_values != NULL && pgmoneta_deque_size(code_values) > 0 ? (char*)pgmoneta_deque_peek(code_values, NULL) : "unknown";
      message = message_values != NULL && pgmoneta_deque_size(message_values) > 0 ? (char*)pgmoneta_deque_peek(message_values, NULL) : "unknown";

      pgmoneta_log_warn("S3 delete object error: key=%s code=%s message=%s", key, code, message);

      if (strcmp(code, "NoSuchKey"))
      {
         *has_fatal_error = true;
      }

      pgmoneta_deque_destroy(key_values);
      pgmoneta_deque_destroy(code_values);
      pgmoneta_deque_destroy(message_values);
      free(error_block);

      cursor = end;
   }

   return 0;
}

static int
xml_s3_build_delete_key(char** xml, char* key)
{
   const char* open_tag = "<Object><Key>";
   const char* close_tag = "</Key></Object>";

   if (xml == NULL || key == NULL)
   {
      return 1;
   }

   *xml = pgmoneta_append(*xml, open_tag);
   *xml = pgmoneta_append(*xml, key);
   *xml = pgmoneta_append(*xml, close_tag);

   return 0;
}

static int
xml_s3_build_delete_list(char** xml, struct deque* keys, size_t max_keys)
{
   if (xml == NULL || keys == NULL || max_keys == 0)
   {
      goto error;
   }

   const char* quiet_tag = "<Quiet>true</Quiet>";
   const char* header_tag = "<Delete xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">";
   *xml = pgmoneta_append(*xml, header_tag);
   *xml = pgmoneta_append(*xml, quiet_tag);

   while (max_keys > 0 && pgmoneta_deque_size(keys) > 0)
   {
      char* key = (char*)pgmoneta_deque_poll(keys, NULL);
      if (key == NULL)
      {
         goto error;
      }

      if (xml_s3_build_delete_key(xml, key))
      {
         free(key);
         goto error;
      }

      free(key);
      max_keys--;
   }

   *xml = pgmoneta_append(*xml, "</Delete>");
   return 0;

error:
   return 1;
}
