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
 * Google Cloud Storage backend: backup (upload) only.
 *
 * Authentication: a service account JSON key (gcs_credentials_file) is used to
 * mint short-lived OAuth2 access tokens via the RFC 7523 JWT Bearer flow
 * (self-signed JWT, RS256, exchanged at https://oauth2.googleapis.com/token).
 * The access token is cached in memory and refreshed shortly before it expires.
 *
 * When no credentials file is configured, requests are sent without an
 * Authorization header. This is only useful against an unauthenticated
 * emulator reached via gcs_endpoint (e.g. fake-gcs-server) -- the same
 * convention the official Google client libraries use when STORAGE_EMULATOR_HOST
 * is set. Talking to real GCS without a credentials file will simply fail
 * with 401.
 *
 * Wire protocol is the GCS JSON API objects.insert (simple media upload),
 * addressed as https://{host}/upload/storage/v1/b/{bucket}/o?uploadType=media&name=... .
 * Unlike S3, the bucket is always a distinct path segment, so there is no
 * path-style/virtual-hosted-style split to account for.
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <deque.h>
#include <files.h>
#include <http.h>
#include <info.h>
#include <json.h>
#include <logging.h>
#include <manifest.h>
#include <progress.h>
#include <storage.h>
#include <utils.h>
#include <value.h>
#include <vfile.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* OpenSSL */
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#define GCS_DEFAULT_HOST      "storage.googleapis.com"
#define GCS_TOKEN_HOST        "oauth2.googleapis.com"
#define GCS_TOKEN_PATH        "/token"
#define GCS_SCOPE             "https://www.googleapis.com/auth/devstorage.read_write"
#define GCS_TOKEN_LIFETIME    3600
#define GCS_TOKEN_SKEW        60
#define GCS_TOKEN_BUFFER_SIZE 4096

static char* gcs_backup_name(void);
static int gcs_storage_setup(char*, struct art*);
static int gcs_storage_execute(char*, struct art*);
static int gcs_storage_teardown(char*, struct art*);

static int gcs_upload_files(char* local_root, char* gcs_root, int server, int compression, int encryption);
static int gcs_send_upload_request(char* local_root, char* gcs_root, char* relative_path, char* file_sha512, int server);

static char* gcs_get_host(int server);
static char* gcs_get_basepath(int server, char* identifier);
static char* gcs_url_encode(char* str);
static int gcs_apply_auth_header(struct http_request* request, char* auth_token);

static int gcs_get_effective_port(int server);
static bool gcs_get_effective_use_tls(int server);
static char* gcs_get_effective_endpoint(int server);
static char* gcs_get_effective_bucket(int server);
static char* gcs_get_effective_base_dir(int server);
static char* gcs_get_effective_credentials_file(int server);

static int gcs_get_access_token(int server, char** token);
static int gcs_fetch_access_token(char* credentials_file, char** token, time_t* expires_at);
static int gcs_sign_jwt(char* client_email, char* private_key_pem, char* audience, char** jwt);
static int gcs_rsa_sha256_sign(char* private_key_pem, char* data, unsigned char** signature, size_t* signature_length);
static char* gcs_base64url_encode(void* data, size_t length);
static char* gcs_normalize_pem_newlines(char* key);

struct gcs_transfer_task
{
   struct worker_common common;
   int server;
   bool progress_enabled;
   char gcs_root[MAX_PATH];
   char remote_path[MAX_PATH];
   char local_root[MAX_PATH];
   char local_path[MAX_PATH];
   char file_sha512[MISC_LENGTH];
};

struct gcs_upload_file_context
{
   struct vfile* file;
   char* path;
};

static void do_upload_file(struct worker_common* wc);
static int gcs_create_transfer_task(int server, char* gcs_root, char* remote_path,
                                    char* local_root, char* local_path, char* file_sha512,
                                    struct workers* workers, struct gcs_transfer_task** task);
static int gcs_upload_one_file(struct gcs_transfer_task* task);
static size_t gcs_upload_read_cb(void* buffer, size_t size, void* userdata);

/* Cross-process-thread token cache: one service account, one cached bearer token. */
static pthread_mutex_t gcs_token_mutex = PTHREAD_MUTEX_INITIALIZER;
static char gcs_cached_token[GCS_TOKEN_BUFFER_SIZE];
static char gcs_cached_credentials_file[MAX_PATH];
static time_t gcs_token_expires_at = 0;

struct workflow*
pgmoneta_storage_create_gcs(int workflow_type)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->setup = &gcs_storage_setup;

   switch (workflow_type)
   {
      case WORKFLOW_TYPE_BACKUP:
         wf->name = &gcs_backup_name;
         wf->execute = &gcs_storage_execute;
         wf->teardown = &gcs_storage_teardown;
         break;
      default:
         break;
   }

   wf->next = NULL;

   return wf;
}

static char*
gcs_backup_name(void)
{
   return PHASE_NAME_BASEBACKUP;
}

static int
gcs_storage_setup(char* name __attribute__((unused)), struct art* nodes)
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

   pgmoneta_log_debug("GCS storage engine (setup): %s/%s", config->common.servers[server].name, label);

   return 0;
}

static int
gcs_get_effective_port(int server)
{
   struct main_configuration* config;
   struct server* srv;

   config = (struct main_configuration*)shmem;
   srv = &config->common.servers[server];

   if (srv != NULL && srv->gcs.port != 0)
   {
      return srv->gcs.port;
   }

   return config->gcs.port;
}

static bool
gcs_get_effective_use_tls(int server)
{
   struct main_configuration* config;
   struct server* srv;

   config = (struct main_configuration*)shmem;
   srv = &config->common.servers[server];

   if (srv != NULL && (srv->gcs.port != 0 || strlen(srv->gcs.endpoint) > 0))
   {
      return srv->gcs.use_tls;
   }

   return config->gcs.use_tls;
}

static char*
gcs_get_effective_endpoint(int server)
{
   struct main_configuration* config;
   struct server* srv;

   config = (struct main_configuration*)shmem;
   srv = &config->common.servers[server];

   if (srv != NULL && strlen(srv->gcs.endpoint) > 0)
   {
      return srv->gcs.endpoint;
   }

   return config->gcs.endpoint;
}

static char*
gcs_get_effective_bucket(int server)
{
   struct main_configuration* config;
   struct server* srv;

   config = (struct main_configuration*)shmem;
   srv = &config->common.servers[server];

   if (srv != NULL && strlen(srv->gcs.bucket) > 0)
   {
      return srv->gcs.bucket;
   }

   return config->gcs.bucket;
}

static char*
gcs_get_effective_base_dir(int server)
{
   struct main_configuration* config;
   struct server* srv;

   config = (struct main_configuration*)shmem;
   srv = &config->common.servers[server];

   if (srv != NULL && strlen(srv->gcs.base_dir) > 0)
   {
      return srv->gcs.base_dir;
   }

   return config->gcs.base_dir;
}

static char*
gcs_get_effective_credentials_file(int server)
{
   struct main_configuration* config;
   struct server* srv;

   config = (struct main_configuration*)shmem;
   srv = &config->common.servers[server];

   if (srv != NULL && strlen(srv->gcs.credentials_file) > 0)
   {
      return srv->gcs.credentials_file;
   }

   return config->gcs.credentials_file;
}

static int
gcs_storage_execute(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double remote_gcs_elapsed_time;
   char* local_root = NULL;
   char* base_dir = NULL;
   char* gcs_root = NULL;
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

   pgmoneta_log_debug("GCS storage engine (execute): %s/%s",
                      config->common.servers[server].name, label);
   pgmoneta_log_debug("GCS effective config: bucket=%s, endpoint=%s",
                      gcs_get_effective_bucket(server),
                      gcs_get_effective_endpoint(server));

   local_root = pgmoneta_get_server_backup_identifier(server, label);
   base_dir = pgmoneta_get_server_backup(server);
   gcs_root = gcs_get_basepath(server, label);

   if (pgmoneta_load_info(base_dir, label, &temp_backup))
   {
      pgmoneta_log_error("GCS storage: unable to load backup info for %s/%s", base_dir, label);
      goto error;
   }

   if (gcs_upload_files(local_root, gcs_root, server, temp_backup->compression, temp_backup->encryption))
   {
      goto error;
   }

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   remote_gcs_elapsed_time = pgmoneta_compute_duration(start_t, end_t);

   temp_backup->remote_gcs_elapsed_time = remote_gcs_elapsed_time;
   if (pgmoneta_save_info(base_dir, temp_backup))
   {
      pgmoneta_log_error("Unable to save backup info for directory %s", base_dir);
      goto error;
   }

   free(temp_backup);
   free(local_root);
   free(base_dir);
   free(gcs_root);

   return 0;

error:
   free(temp_backup);
   free(local_root);
   free(base_dir);
   free(gcs_root);

   return 1;
}

static int
gcs_storage_teardown(char* name __attribute__((unused)), struct art* nodes)
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

   pgmoneta_log_debug("GCS storage engine (teardown): %s/%s", config->common.servers[server].name, label);

   return 0;
}

static int
gcs_create_transfer_task(int server, char* gcs_root, char* remote_path,
                         char* local_root, char* local_path, char* file_sha512,
                         struct workers* workers, struct gcs_transfer_task** task)
{
   struct gcs_transfer_task* t = NULL;

   *task = NULL;

   if (gcs_root == NULL || remote_path == NULL || local_path == NULL)
   {
      goto error;
   }

   if (strlen(gcs_root) >= MAX_PATH || strlen(remote_path) >= MAX_PATH || strlen(local_path) >= MAX_PATH)
   {
      pgmoneta_log_error("GCS transfer path too long");
      goto error;
   }

   if (local_root != NULL && strlen(local_root) >= MAX_PATH)
   {
      pgmoneta_log_error("GCS local root path too long");
      goto error;
   }

   if (file_sha512 != NULL && strlen(file_sha512) >= MISC_LENGTH)
   {
      pgmoneta_log_error("GCS sha512 value too long");
      goto error;
   }

   t = (struct gcs_transfer_task*)malloc(sizeof(struct gcs_transfer_task));
   if (t == NULL)
   {
      goto error;
   }

   memset(t, 0, sizeof(struct gcs_transfer_task));
   pgmoneta_snprintf(t->gcs_root, sizeof(t->gcs_root), "%s", gcs_root);
   pgmoneta_snprintf(t->remote_path, sizeof(t->remote_path), "%s", remote_path);
   pgmoneta_snprintf(t->local_path, sizeof(t->local_path), "%s", local_path);
   if (local_root != NULL)
   {
      pgmoneta_snprintf(t->local_root, sizeof(t->local_root), "%s", local_root);
   }
   if (file_sha512 != NULL)
   {
      pgmoneta_snprintf(t->file_sha512, sizeof(t->file_sha512), "%s", file_sha512);
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

static size_t
gcs_upload_read_cb(void* buffer, size_t size, void* userdata)
{
   struct gcs_upload_file_context* ctx = (struct gcs_upload_file_context*)userdata;
   if (ctx == NULL || ctx->file == NULL)
   {
      return 0;
   }
   size_t bytes_read = 0;
   bool last_chunk = false;
   if (ctx->file->read(ctx->file, buffer, size, &bytes_read, &last_chunk))
   {
      pgmoneta_log_error("GCS upload: failed to read chunk from %s", ctx->path);
      return 0;
   }
   return bytes_read;
}

static int
gcs_upload_one_file(struct gcs_transfer_task* task)
{
   if (gcs_send_upload_request(task->local_root, task->gcs_root, task->remote_path,
                               strlen(task->file_sha512) > 0 ? task->file_sha512 : NULL, task->server))
   {
      pgmoneta_log_error("GCS upload: failed %s", task->remote_path);
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
   struct gcs_transfer_task* task = (struct gcs_transfer_task*)wc;

   if (gcs_upload_one_file(task))
   {
      pgmoneta_record_failure(task->common.workers != NULL ? task->common.workers->outcome : NULL, "GCS upload failed: %s", task->remote_path);
   }

   free(task);
}

static int
gcs_upload_files(char* local_root, char* gcs_root, int server, int compression, int encryption)
{
   int number_of_workers = 0;
   char* manifest_path = NULL;
   char* file_path = NULL;
   char* relative_file = NULL;
   char* suffix = NULL;
   struct deque* paths = NULL;
   struct deque_iterator* iter = NULL;
   struct workers* workers = NULL;
   struct gcs_transfer_task* task = NULL;

   manifest_path = pgmoneta_append(manifest_path, local_root);
   manifest_path = pgmoneta_append(manifest_path, "backup.manifest");

   if (pgmoneta_extraction_get_suffix(compression, encryption, &suffix))
   {
      pgmoneta_log_error("GCS upload: failed to determine file suffix");
      goto error;
   }

   number_of_workers = pgmoneta_get_number_of_workers(server);
   if (number_of_workers > 0)
   {
      pgmoneta_workers_initialize(number_of_workers, &workers);
   }

   if (pgmoneta_manifest_get_paths(manifest_path, &paths))
   {
      pgmoneta_log_error("GCS upload: failed to read manifest %s", manifest_path);
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

      /* the manifest's per-file sha512 travels in the task but GCS's simple media
       * upload has no per-request custom-metadata support (unlike S3 PUT headers),
       * so it is accepted for structural parity but not put on the wire. */
      if (gcs_create_transfer_task(server, gcs_root, relative_file, local_root, relative_file,
                                   (char*)iter->cur->data, workers, &task))
      {
         pgmoneta_log_error("GCS upload: failed to create transfer task");
         free(relative_file);
         goto error;
      }

      if (workers != NULL && pgmoneta_workers_outcome_ok(workers))
      {
         if (pgmoneta_workers_add(workers, do_upload_file, (struct worker_common*)task))
         {
            free(task);
            task = NULL;
            pgmoneta_log_error("GCS upload: failed to queue worker task");
            free(relative_file);
            goto error;
         }
         task = NULL;
      }
      else
      {
         if (gcs_upload_one_file(task))
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

   /* upload metadata file last (commit marker) */
   if (gcs_send_upload_request(local_root, gcs_root, "backup.manifest", NULL, server))
   {
      pgmoneta_log_error("GCS upload: failed to upload backup.manifest");
      goto error;
   }
   if (gcs_send_upload_request(local_root, gcs_root, "backup.sha512", NULL, server))
   {
      pgmoneta_log_error("GCS upload: failed to upload backup.sha512");
      goto error;
   }
   if (gcs_send_upload_request(local_root, gcs_root, "backup.info", NULL, server))
   {
      pgmoneta_log_error("GCS upload: failed to upload backup.info");
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

/* ---------------------------------------------------------------------- */
/* Authentication: service-account JWT Bearer flow (RFC 7523) + token cache */
/* ---------------------------------------------------------------------- */

static char*
gcs_base64url_encode(void* data, size_t length)
{
   char* std_b64 = NULL;
   size_t std_len = 0;
   char* out = NULL;
   size_t out_len = 0;

   if (pgmoneta_base64_encode(data, length, &std_b64, &std_len))
   {
      return NULL;
   }

   out = malloc(std_len + 1);
   if (out == NULL)
   {
      free(std_b64);
      return NULL;
   }

   for (size_t i = 0; i < std_len; i++)
   {
      char c = std_b64[i];

      if (c == '+')
      {
         c = '-';
      }
      else if (c == '/')
      {
         c = '_';
      }
      else if (c == '=')
      {
         continue;
      }

      out[out_len++] = c;
   }
   out[out_len] = '\0';

   free(std_b64);
   return out;
}

/*
 * Service account keys store the PEM private key as a JSON string with
 * literal "\n" escape sequences. The JSON parser may or may not unescape
 * them into real newlines, and OpenSSL's PEM parser requires real newlines,
 * so this normalizes unconditionally (real newlines pass through unchanged).
 */
static char*
gcs_normalize_pem_newlines(char* key)
{
   char* result = NULL;
   size_t len;
   size_t out = 0;

   if (key == NULL)
   {
      return NULL;
   }

   len = strlen(key);
   result = malloc(len + 1);
   if (result == NULL)
   {
      return NULL;
   }

   for (size_t i = 0; i < len; i++)
   {
      if (key[i] == '\\' && i + 1 < len && key[i + 1] == 'n')
      {
         result[out++] = '\n';
         i++;
      }
      else
      {
         result[out++] = key[i];
      }
   }
   result[out] = '\0';

   return result;
}

static int
gcs_rsa_sha256_sign(char* private_key_pem, char* data, unsigned char** signature, size_t* signature_length)
{
   BIO* bio = NULL;
   EVP_PKEY* pkey = NULL;
   EVP_MD_CTX* mdctx = NULL;
   unsigned char* sig = NULL;
   size_t sig_len = 0;

   *signature = NULL;
   *signature_length = 0;

   bio = BIO_new_mem_buf(private_key_pem, -1);
   if (bio == NULL)
   {
      goto error;
   }

   pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
   if (pkey == NULL)
   {
      pgmoneta_log_error("GCS: failed to parse service account private key");
      goto error;
   }

   mdctx = EVP_MD_CTX_new();
   if (mdctx == NULL)
   {
      goto error;
   }

   if (EVP_DigestSignInit(mdctx, NULL, EVP_sha256(), NULL, pkey) != 1)
   {
      goto error;
   }

   if (EVP_DigestSignUpdate(mdctx, data, strlen(data)) != 1)
   {
      goto error;
   }

   if (EVP_DigestSignFinal(mdctx, NULL, &sig_len) != 1)
   {
      goto error;
   }

   sig = malloc(sig_len);
   if (sig == NULL)
   {
      goto error;
   }

   if (EVP_DigestSignFinal(mdctx, sig, &sig_len) != 1)
   {
      goto error;
   }

   *signature = sig;
   *signature_length = sig_len;

   EVP_MD_CTX_free(mdctx);
   EVP_PKEY_free(pkey);
   BIO_free(bio);

   return 0;

error:
   free(sig);

   if (mdctx != NULL)
   {
      EVP_MD_CTX_free(mdctx);
   }
   if (pkey != NULL)
   {
      EVP_PKEY_free(pkey);
   }
   if (bio != NULL)
   {
      BIO_free(bio);
   }

   return 1;
}

static int
gcs_sign_jwt(char* client_email, char* private_key_pem, char* audience, char** jwt)
{
   char claims[2048];
   char* header_b64 = NULL;
   char* claims_b64 = NULL;
   char* signing_input = NULL;
   char* signature_b64 = NULL;
   char* normalized_key = NULL;
   unsigned char* signature = NULL;
   size_t signature_length = 0;
   time_t iat;

   *jwt = NULL;

   header_b64 = gcs_base64url_encode("{\"alg\":\"RS256\",\"typ\":\"JWT\"}", strlen("{\"alg\":\"RS256\",\"typ\":\"JWT\"}"));
   if (header_b64 == NULL)
   {
      goto error;
   }

   iat = time(NULL);

   if (pgmoneta_snprintf(claims, sizeof(claims),
                         "{\"iss\":\"%s\",\"scope\":\"%s\",\"aud\":\"%s\",\"iat\":%lld,\"exp\":%lld}",
                         client_email, GCS_SCOPE, audience,
                         (long long)iat, (long long)(iat + GCS_TOKEN_LIFETIME)) <= 0)
   {
      goto error;
   }

   claims_b64 = gcs_base64url_encode(claims, strlen(claims));
   if (claims_b64 == NULL)
   {
      goto error;
   }

   signing_input = pgmoneta_append(signing_input, header_b64);
   signing_input = pgmoneta_append(signing_input, ".");
   signing_input = pgmoneta_append(signing_input, claims_b64);

   normalized_key = gcs_normalize_pem_newlines(private_key_pem);
   if (normalized_key == NULL)
   {
      goto error;
   }

   if (gcs_rsa_sha256_sign(normalized_key, signing_input, &signature, &signature_length))
   {
      goto error;
   }

   signature_b64 = gcs_base64url_encode(signature, signature_length);
   if (signature_b64 == NULL)
   {
      goto error;
   }

   *jwt = pgmoneta_append(*jwt, signing_input);
   *jwt = pgmoneta_append(*jwt, ".");
   *jwt = pgmoneta_append(*jwt, signature_b64);

   free(header_b64);
   free(claims_b64);
   free(signing_input);
   free(signature_b64);
   free(signature);
   free(normalized_key);

   return 0;

error:
   free(header_b64);
   free(claims_b64);
   free(signing_input);
   free(signature_b64);
   free(signature);
   free(normalized_key);
   free(*jwt);
   *jwt = NULL;

   return 1;
}

static int
gcs_fetch_access_token(char* credentials_file, char** token, time_t* expires_at)
{
   struct json* creds = NULL;
   struct json* token_response = NULL;
   char* client_email = NULL;
   char* private_key = NULL;
   char* token_uri = NULL;
   char* jwt = NULL;
   char* body = NULL;
   char* access_token = NULL;
   int64_t expires_in = GCS_TOKEN_LIFETIME;
   time_t iat;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;

   *token = NULL;
   *expires_at = 0;

   if (pgmoneta_json_read_file(credentials_file, &creds) || creds == NULL)
   {
      pgmoneta_log_error("GCS: failed to read service account key %s", credentials_file);
      goto error;
   }

   client_email = (char*)pgmoneta_json_get(creds, "client_email");
   private_key = (char*)pgmoneta_json_get(creds, "private_key");
   token_uri = (char*)pgmoneta_json_get(creds, "token_uri");

   if (client_email == NULL || strlen(client_email) == 0 || private_key == NULL || strlen(private_key) == 0)
   {
      pgmoneta_log_error("GCS: service account key %s is missing client_email or private_key", credentials_file);
      goto error;
   }

   if (token_uri == NULL || strlen(token_uri) == 0)
   {
      token_uri = "https://" GCS_TOKEN_HOST GCS_TOKEN_PATH;
   }

   iat = time(NULL);

   if (gcs_sign_jwt(client_email, private_key, token_uri, &jwt))
   {
      pgmoneta_log_error("GCS: failed to sign JWT for %s", client_email);
      goto error;
   }

   body = pgmoneta_append(body, "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer&assertion=");
   body = pgmoneta_append(body, jwt);

   if (pgmoneta_http_create(GCS_TOKEN_HOST, 443, true, &connection))
   {
      goto error;
   }

   if (pgmoneta_http_request_create(PGMONETA_HTTP_POST, GCS_TOKEN_PATH, &request))
   {
      goto error;
   }

   if (pgmoneta_http_request_add_header(request, "Content-Type", "application/x-www-form-urlencoded"))
   {
      goto error;
   }

   if (pgmoneta_http_set_data(request, body, strlen(body)))
   {
      goto error;
   }

   if (pgmoneta_http_invoke(connection, request, &response))
   {
      goto error;
   }

   if (response->status_code != 200)
   {
      pgmoneta_log_error("GCS: token exchange failed with status %d: %s", response->status_code,
                         response->payload.data != NULL ? (char*)response->payload.data : "");
      goto error;
   }

   if (pgmoneta_json_parse_string(response->payload.data, &token_response) || token_response == NULL)
   {
      pgmoneta_log_error("GCS: failed to parse token response");
      goto error;
   }

   access_token = (char*)pgmoneta_json_get(token_response, "access_token");
   if (access_token == NULL || strlen(access_token) == 0)
   {
      pgmoneta_log_error("GCS: token response has no access_token");
      goto error;
   }

   if (pgmoneta_json_contains_key(token_response, "expires_in"))
   {
      expires_in = (int64_t)pgmoneta_json_get(token_response, "expires_in");
      if (expires_in <= 0)
      {
         expires_in = GCS_TOKEN_LIFETIME;
      }
   }

   *token = pgmoneta_append(NULL, access_token);
   *expires_at = iat + (time_t)expires_in;

   pgmoneta_json_destroy(creds);
   pgmoneta_json_destroy(token_response);
   free(jwt);
   free(body);
   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);

   return 0;

error:
   pgmoneta_json_destroy(creds);
   pgmoneta_json_destroy(token_response);
   free(jwt);
   free(body);

   if (request != NULL)
   {
      pgmoneta_http_request_destroy(request);
   }
   if (response != NULL)
   {
      pgmoneta_http_response_destroy(response);
   }
   if (connection != NULL)
   {
      pgmoneta_http_destroy(connection);
   }

   return 1;
}

static int
gcs_get_access_token(int server, char** token)
{
   char* credentials_file = gcs_get_effective_credentials_file(server);
   char* fetched = NULL;
   time_t expires_at = 0;
   time_t now = time(NULL);

   *token = NULL;

   if (credentials_file == NULL || strlen(credentials_file) == 0)
   {
      /* Unauthenticated mode: only useful against an emulator via gcs_endpoint. */
      return 0;
   }

   pthread_mutex_lock(&gcs_token_mutex);

   if (gcs_token_expires_at > 0 &&
       (now + GCS_TOKEN_SKEW) < gcs_token_expires_at &&
       pgmoneta_compare_string(gcs_cached_credentials_file, credentials_file))
   {
      *token = pgmoneta_append(NULL, gcs_cached_token);
      pthread_mutex_unlock(&gcs_token_mutex);
      return 0;
   }

   if (gcs_fetch_access_token(credentials_file, &fetched, &expires_at))
   {
      pthread_mutex_unlock(&gcs_token_mutex);
      return 1;
   }

   pgmoneta_snprintf(gcs_cached_token, sizeof(gcs_cached_token), "%s", fetched);
   pgmoneta_snprintf(gcs_cached_credentials_file, sizeof(gcs_cached_credentials_file), "%s", credentials_file);
   gcs_token_expires_at = expires_at;

   *token = fetched;
   fetched = NULL;

   pthread_mutex_unlock(&gcs_token_mutex);
   return 0;
}

static int
gcs_apply_auth_header(struct http_request* request, char* auth_token)
{
   char* bearer = NULL;
   int rc = 0;

   if (auth_token == NULL || strlen(auth_token) == 0)
   {
      return 0;
   }

   bearer = pgmoneta_append(bearer, "Bearer ");
   bearer = pgmoneta_append(bearer, auth_token);

   rc = pgmoneta_http_request_add_header(request, "Authorization", bearer);

   free(bearer);
   return rc;
}

/* ---------------------------------------------------------------------- */
/* Wire request against the GCS JSON API                                  */
/* ---------------------------------------------------------------------- */

static int
gcs_send_upload_request(char* local_root, char* gcs_root, char* relative_path, char* file_sha512, int server)
{
   char* auth_token = NULL;
   char* gcs_host = NULL;
   char* object_name = NULL;
   char* encoded_object_name = NULL;
   char* local_path = NULL;
   char* request_path = NULL;
   char content_length[32];
   struct stat file_info;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   struct gcs_upload_file_context upload_ctx = {0};

   char* effective_bucket = gcs_get_effective_bucket(server);
   int effective_port = gcs_get_effective_port(server);
   bool effective_use_tls = gcs_get_effective_use_tls(server);

   /* GCS simple media upload carries no per-request custom metadata; see gcs_upload_files(). */
   (void)file_sha512;

   local_path = pgmoneta_append(local_path, local_root);
   if (strlen(relative_path) > 0)
   {
      if (!pgmoneta_ends_with(local_root, "/"))
      {
         local_path = pgmoneta_append(local_path, "/");
      }
      local_path = pgmoneta_append(local_path, relative_path);
   }

   object_name = pgmoneta_append(object_name, gcs_root);
   if (strlen(relative_path) > 0)
   {
      if (!pgmoneta_ends_with(gcs_root, "/"))
      {
         object_name = pgmoneta_append(object_name, "/");
      }
      object_name = pgmoneta_append(object_name, relative_path);
   }

   gcs_host = gcs_get_host(server);

   if (gcs_get_access_token(server, &auth_token))
   {
      goto error;
   }

   if (stat(local_path, &file_info) != 0)
   {
      pgmoneta_log_error("GCS upload: local file stat failed for %s", local_path);
      goto error;
   }

   if (pgmoneta_vfile_create_local(local_path, "rb", &upload_ctx.file))
   {
      pgmoneta_log_error("GCS upload: failed to open local file %s", local_path);
      goto error;
   }
   upload_ctx.path = local_path;

   int gcs_port;

   if (effective_port != 0)
   {
      gcs_port = effective_port;
   }
   else
   {
      gcs_port = effective_use_tls ? 443 : 80;
   }

   bool use_tls = effective_use_tls;
   if (gcs_port == 443)
   {
      use_tls = true;
   }

   if (pgmoneta_http_create(gcs_host, gcs_port, use_tls, &connection))
   {
      goto error;
   }

   encoded_object_name = gcs_url_encode(object_name);

   request_path = pgmoneta_append(request_path, "/upload/storage/v1/b/");
   request_path = pgmoneta_append(request_path, effective_bucket);
   request_path = pgmoneta_append(request_path, "/o?uploadType=media&name=");
   request_path = pgmoneta_append(request_path, encoded_object_name);

   if (pgmoneta_http_request_create(PGMONETA_HTTP_POST, request_path, &request))
   {
      goto error;
   }

   if (gcs_apply_auth_header(request, auth_token))
   {
      goto error;
   }

   if (pgmoneta_http_request_add_header(request, "Content-Type", "application/octet-stream"))
   {
      goto error;
   }

   pgmoneta_snprintf(content_length, sizeof(content_length), "%ld", file_info.st_size);
   if (pgmoneta_http_request_add_header(request, "Content-Length", content_length))
   {
      pgmoneta_log_error("Failed to set content length");
      goto error;
   }
   request->read_cb = gcs_upload_read_cb;
   request->read_userdata = &upload_ctx;

   if (pgmoneta_http_invoke(connection, request, &response))
   {
      goto error;
   }

   if (response->status_code >= 200 && response->status_code < 300)
   {
      pgmoneta_log_info("Successfully uploaded file to URL: https://%s%s", gcs_host, request_path);
   }
   else
   {
      pgmoneta_log_error("GCS upload failed with status code: %d. Failed to upload: %s to GCS object: %s",
                         response->status_code, local_path, object_name);
      goto error;
   }

   free(gcs_host);
   free(object_name);
   free(encoded_object_name);
   free(request_path);
   free(local_path);
   free(auth_token);
   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);
   pgmoneta_vfile_destroy(upload_ctx.file);
   upload_ctx.file = NULL;

   return 0;

error:

   free(gcs_host);
   free(object_name);
   free(encoded_object_name);
   free(request_path);
   free(local_path);
   free(auth_token);

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

   pgmoneta_vfile_destroy(upload_ctx.file);
   upload_ctx.file = NULL;

   return 1;
}

static char*
gcs_get_host(int server)
{
   char* host = NULL;
   char* endpoint = NULL;
   char* effective_endpoint;

   effective_endpoint = gcs_get_effective_endpoint(server);

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

   host = pgmoneta_append(host, GCS_DEFAULT_HOST);

   return host;
}

static char*
gcs_get_basepath(int server, char* identifier)
{
   char* d = NULL;
   struct main_configuration* config;
   char* effective_base_dir;

   config = (struct main_configuration*)shmem;

   effective_base_dir = gcs_get_effective_base_dir(server);

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
   if (identifier != NULL)
   {
      d = pgmoneta_append(d, identifier);
   }
   return d;
}

static char*
gcs_url_encode(char* str)
{
   char* encoded = NULL;
   char hex[4];

   if (str == NULL)
   {
      return NULL;
   }

   for (int i = 0; str[i] != '\0'; i++)
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

int
gcs_upload(int server, char* label, int compression, int encryption)
{
   char* local_root = NULL;
   char* gcs_root = NULL;
   int rc;

   local_root = pgmoneta_get_server_backup_identifier(server, label);
   gcs_root = gcs_get_basepath(server, label);

   rc = gcs_upload_files(local_root, gcs_root, server, compression, encryption);

   free(local_root);
   free(gcs_root);
   return rc;
}
