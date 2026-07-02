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
 *
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <http.h>
#include <info.h>
#include <logging.h>
#include <mctf_container.h>
#include <mctf_se.h>
#include <security.h>
#include <shmem.h>
#include <tscommon.h>
#include <utils.h>

/* system */
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MANAGED_DAEMON_RETRIES 15

/*
 * The backend registry.
 *
 * Each driver is defined in its own file under backends/ and referenced
 * here. Adding a backend means: implement backends/<name>.c, declare its
 * driver below, and add one row to the table. Nothing else changes.
 */
extern const struct mctf_se_driver mctf_garage_driver;
extern const struct mctf_se_driver mctf_azurite_driver;
/* extern const struct mctf_se_driver mctf_ssh_driver; */

static const struct mctf_se_driver* registry[] = {
   [MCTF_BACKEND_GARAGE]  = &mctf_garage_driver,
   [MCTF_BACKEND_AZURITE] = &mctf_azurite_driver,
   /* [MCTF_BACKEND_SSH] = &mctf_ssh_driver, */
};

/* Managed-instance state (single active backend). */
static struct mctf_se storage;
static bool storage_active = false;
static bool atexit_registered = false;

static char run_dir[MAX_PATH];
static char sock_dir[MAX_PATH];
static char conf_path[MAX_PATH];
static char cli_conf_path[MAX_PATH];
static char user_conf[MAX_PATH];

static char cli_bin[MAX_PATH];
static char daemon_bin[MAX_PATH];

static int write_confs(void);
static int daemon_up(void);
static void dump_managed_log(void);
static void daemon_down(void);
static void cleanup_atexit(void);
static void signal_cleanup(int sig);

static int
write_confs(void)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   struct server* primary;
   FILE* f = NULL;

   if (config == NULL || config->common.number_of_servers <= 0)
   {
      pgmoneta_log_error("mctf_se: no primary server in configuration");
      return MCTF_FAIL;
   }
   primary = &config->common.servers[PRIMARY_SERVER];

   f = fopen(conf_path, "w");
   if (f == NULL)
   {
      pgmoneta_log_error("mctf_se: cannot write %s", conf_path);
      return MCTF_FAIL;
   }
   fprintf(f, "[pgmoneta]\n");
   fprintf(f, "host = localhost\n");
   fprintf(f, "base_dir = %s/backup\n", run_dir);
   fprintf(f, "compression = zstd\n");
   fprintf(f, "encryption = none\n");
   fprintf(f, "storage_engine = %s\n", storage.driver->storage_engine);
   fprintf(f, "log_type = file\n");
   fprintf(f, "log_level = debug5\n");
   fprintf(f, "log_path = %s/pgmoneta.log\n", run_dir);
   fprintf(f, "unix_socket_dir = %s/\n", sock_dir);
   fprintf(f, "pidfile = %s/pgmoneta.pid\n", run_dir);
   fprintf(f, "workspace = %s/workspace\n", run_dir);
   if (storage.driver->write_global_conf != NULL)
   {
      if (storage.driver->write_global_conf(&storage, f) != MCTF_OK)
      {
         fclose(f);
         return MCTF_FAIL;
      }
   }
   fprintf(f, "\n");
   fprintf(f, "[primary]\n");
   fprintf(f, "host = %s\n", primary->host);
   fprintf(f, "port = %d\n", primary->port);
   fprintf(f, "user = %s\n", primary->username);
   fprintf(f, "wal_slot = mctf_se\n");
   fprintf(f, "create_slot = yes\n");
   if (storage.driver->write_server_conf != NULL)
   {
      if (storage.driver->write_server_conf(&storage, f) != MCTF_OK)
      {
         fclose(f);
         return MCTF_FAIL;
      }
   }
   fclose(f);

   f = fopen(cli_conf_path, "w");
   if (f == NULL)
   {
      pgmoneta_log_error("mctf_se: cannot write %s", cli_conf_path);
      return MCTF_FAIL;
   }
   fprintf(f, "unix_socket_dir = %s/\n", sock_dir);
   fprintf(f, "log_type = file\n");
   fprintf(f, "log_level = info\n");
   fprintf(f, "log_path = %s/pgmoneta-cli.log\n", run_dir);
   fclose(f);

   return MCTF_OK;
}

static int
daemon_up(void)
{
   int i;

   if (mctf_sh(NULL, "setsid %s -c %s -u %s -d >/dev/null 2>&1",
               daemon_bin, conf_path, user_conf) != 0)
   {
      pgmoneta_log_error("mctf_se: failed to start managed pgmoneta");
      return MCTF_FAIL;
   }

   for (i = 0; i < MANAGED_DAEMON_RETRIES; i++)
   {
      if (mctf_sh(NULL, "%s -c %s status", cli_bin, cli_conf_path) == 0)
      {
         fprintf(stderr, "    - managed pgmoneta started\n");
         fflush(stderr);
         return MCTF_OK;
      }
      sleep(1);
   }

   pgmoneta_log_error("mctf_se: managed pgmoneta did not become ready");
   return MCTF_FAIL;
}

/* Print the tail of the managed pgmoneta log, indented, to stderr (failures). */
static void
dump_managed_log(void)
{
   fprintf(stderr, "  managed pgmoneta did not start - last log:\n");
   fflush(stderr);
   mctf_sh(NULL, "tail -20 %s/pgmoneta.log 2>/dev/null | sed 's/^/    /' 1>&2", run_dir);
}

static void
daemon_down(void)
{
   if (cli_bin[0] != '\0' && cli_conf_path[0] != '\0')
   {
      mctf_sh(NULL, "%s -c %s shutdown", cli_bin, cli_conf_path);
   }
}

static void
cleanup_atexit(void)
{
   mctf_se_down();
}

static void
signal_cleanup(int sig)
{
   mctf_se_down();
   signal(sig, SIG_DFL);
   raise(sig);
}

int
mctf_se_up(enum mctf_backend backend)
{
   const struct mctf_se_driver* driver;
   const char* uc;
   time_t start;
   int rc;

   if (storage_active)
   {
      pgmoneta_log_error("mctf_se: a backend is already active");
      return MCTF_FAIL;
   }

   if ((size_t)backend >= sizeof(registry) / sizeof(registry[0]) ||
       registry[backend] == NULL)
   {
      pgmoneta_log_error("mctf_se: unknown backend %d", (int)backend);
      return MCTF_FAIL;
   }
   driver = registry[backend];

   memset(&storage, 0, sizeof(storage));
   storage.driver = driver;

   /* Requires the full test environment (check.sh); skip otherwise. */
   uc = getenv("PGMONETA_TEST_USER_CONF");
   if (uc == NULL || TEST_BASE_DIR[0] == '\0')
   {
      pgmoneta_log_info("mctf_se: test environment not initialised; skipping");
      return MCTF_SKIPPED;
   }
   snprintf(user_conf, sizeof(user_conf), "%s", uc);

   if (pgmoneta_test_resolve_binary_path("pgmoneta", daemon_bin) != 0 ||
       pgmoneta_test_resolve_binary_path("pgmoneta-cli", cli_bin) != 0)
   {
      pgmoneta_log_error("mctf_se: cannot resolve pgmoneta binaries");
      return MCTF_FAIL;
   }

   snprintf(run_dir, sizeof(run_dir), "%s/se-%s", TEST_BASE_DIR, driver->name);
   snprintf(sock_dir, sizeof(sock_dir), "%s/sock", run_dir);
   snprintf(conf_path, sizeof(conf_path), "%s/pgmoneta.conf", run_dir);
   snprintf(cli_conf_path, sizeof(cli_conf_path), "%s/pgmoneta_cli.conf", run_dir);

   mkdir(run_dir, 0700);
   mkdir(sock_dir, 0700);
   mctf_sh(NULL, "mkdir -p %s/backup %s/workspace", run_dir, run_dir);

   if (!atexit_registered)
   {
      atexit(cleanup_atexit);
      signal(SIGINT, signal_cleanup);
      signal(SIGTERM, signal_cleanup);
      atexit_registered = true;
   }

   storage_active = true; /* set early so teardown runs if a later step fails */

   start = time(NULL);
   fprintf(stderr, "  ~ starting %s backend\n", driver->name);
   fflush(stderr);

   rc = driver->start(&storage);
   if (rc == MCTF_SKIPPED)
   {
      fprintf(stderr, "  ~ skipped (no container engine)\n");
      fflush(stderr);
      storage_active = false;
      return MCTF_SKIPPED;
   }
   if (rc != MCTF_OK || !storage.container.running || write_confs() != MCTF_OK)
   {
      fprintf(stderr, "  ~ %s backend FAILED\n", driver->name);
      fflush(stderr);
      mctf_se_down();
      return MCTF_FAIL;
   }
   if (daemon_up() != MCTF_OK)
   {
      fprintf(stderr, "  ~ %s backend FAILED\n", driver->name);
      fflush(stderr);
      dump_managed_log();
      mctf_se_down();
      return MCTF_FAIL;
   }

   fprintf(stderr, "  ~ %s backend ready (%ds)\n", driver->name, (int)(time(NULL) - start));
   fflush(stderr);

   return MCTF_OK;
}

void
mctf_se_down(void)
{
   if (!storage_active)
   {
      return;
   }
   storage_active = false;

   daemon_down();

   if (storage.driver != NULL && storage.driver->stop != NULL)
   {
      storage.driver->stop(&storage);
   }
}

int
mctf_se_cli(const char* args, char** output)
{
   if (!storage_active || cli_bin[0] == '\0')
   {
      return MCTF_FAIL;
   }

   return mctf_sh(output, "%s -c %s %s", cli_bin, cli_conf_path, args != NULL ? args : "");
}

int
mctf_se_backup(const char* server)
{
   char args[256];

   snprintf(args, sizeof(args), "backup %s", server != NULL ? server : "primary");
   return mctf_se_cli(args, NULL);
}

int
mctf_se_restore(const char* server, const char* backup_id, const char* directory)
{
   char args[2 * MAX_PATH];

   snprintf(args, sizeof(args), "restore %s %s %s",
            server != NULL ? server : "primary",
            backup_id != NULL ? backup_id : "newest",
            directory != NULL ? directory : TEST_RESTORE_DIR);
   return mctf_se_cli(args, NULL);
}

int
mctf_se_list_backup(const char* server, char** output)
{
   char args[256];

   snprintf(args, sizeof(args), "-F json list-backup %s", server != NULL ? server : "primary");
   return mctf_se_cli(args, output);
}

int
mctf_se_s3_ls(const char* server, const char* label, char** output)
{
   char args[512];

   snprintf(args, sizeof(args), "-F json s3 ls %s %s",
            server != NULL ? server : "primary",
            label != NULL ? label : "");
   return mctf_se_cli(args, output);
}

int
mctf_se_delete(const char* server, const char* label)
{
   char args[512];

   snprintf(args, sizeof(args), "delete %s %s",
            server != NULL ? server : "primary",
            label != NULL ? label : "newest");
   return mctf_se_cli(args, NULL);
}

bool
mctf_se_has_local_metadata(const char* server)
{
   char path[MAX_PATH];
   struct backup** backups = NULL;
   int number_of_backups = 0;

   if (!storage_active)
   {
      return false;
   }

   snprintf(path, sizeof(path), "%s/backup/%s/backup", run_dir, server != NULL ? server : "primary");
   pgmoneta_load_infos(path, &number_of_backups, &backups);

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   return number_of_backups > 0;
}

const char*
mctf_se_run_dir(void)
{
   return storage_active ? run_dir : "";
}

const struct mctf_se*
mctf_se_context(void)
{
   return storage_active ? &storage : NULL;
}

int
mctf_se_azure_blob_count(void)
{
   const struct mctf_se* ctx = mctf_se_context();
   char utc_date[UTC_TIME_LENGTH];
   char* string_to_sign = NULL;
   char* signing_key = NULL;
   char* base64_signature = NULL;
   size_t base64_signature_length = 0;
   char* auth_value = NULL;
   unsigned char* signature_hmac = NULL;
   int hmac_length = 0;
   size_t signing_key_length = 0;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   char request_path[512];
   char* body = NULL;
   char* p = NULL;
   int count = -1;

   if (ctx == NULL || ctx->driver == NULL)
   {
      goto done;
   }

   memset(utc_date, 0, sizeof(utc_date));
   if (pgmoneta_get_timestamp_UTC_format(utc_date))
   {
      goto done;
   }

   /*
    * Canonical string for Azurite list-blobs (GET).
    * Content-Length is empty (not "0") for GET requests.
    * Canonical resource uses the account name twice for path-style URLs:
    *   /{account}/{account}/{container}\ncomp:list\nrestype:container
    */
   string_to_sign = pgmoneta_append(string_to_sign, "GET\n\n\n\n\n\n\n\n\n\n\n\n");
   string_to_sign = pgmoneta_append(string_to_sign, "x-ms-date:");
   string_to_sign = pgmoneta_append(string_to_sign, utc_date);
   string_to_sign = pgmoneta_append(string_to_sign, "\nx-ms-version:2020-08-04\n/");
   string_to_sign = pgmoneta_append(string_to_sign, ctx->access_key);
   string_to_sign = pgmoneta_append(string_to_sign, "/");
   string_to_sign = pgmoneta_append(string_to_sign, ctx->access_key);
   string_to_sign = pgmoneta_append(string_to_sign, "/");
   string_to_sign = pgmoneta_append(string_to_sign, ctx->bucket);
   string_to_sign = pgmoneta_append(string_to_sign, "\ncomp:list\nrestype:container");

   if (pgmoneta_base64_decode((char*)ctx->secret_key, strlen(ctx->secret_key), (void**)&signing_key, &signing_key_length))
   {
      goto done;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash(signing_key, (int)signing_key_length,
                                                  string_to_sign, strlen(string_to_sign),
                                                  &signature_hmac, &hmac_length))
   {
      goto done;
   }

   if (pgmoneta_base64_encode((char*)signature_hmac, hmac_length, &base64_signature, &base64_signature_length))
   {
      goto done;
   }

   auth_value = pgmoneta_append(auth_value, "SharedKey ");
   auth_value = pgmoneta_append(auth_value, ctx->access_key);
   auth_value = pgmoneta_append(auth_value, ":");
   auth_value = pgmoneta_append(auth_value, base64_signature);

   if (pgmoneta_http_create((char*)ctx->endpoint, ctx->port, ctx->use_tls, &connection))
   {
      goto done;
   }

   pgmoneta_snprintf(request_path, sizeof(request_path),
                     "/%s/%s?restype=container&comp=list",
                     ctx->access_key, ctx->bucket);

   if (pgmoneta_http_request_create(PGMONETA_HTTP_GET, request_path, &request))
   {
      goto done;
   }

   pgmoneta_http_request_add_header(request, "x-ms-date", utc_date);
   pgmoneta_http_request_add_header(request, "x-ms-version", "2020-08-04");
   pgmoneta_http_request_add_header(request, "Authorization", auth_value);

   if (pgmoneta_http_invoke(connection, request, &response))
   {
      goto done;
   }

   if (response == NULL || response->status_code != 200 || response->payload.data_size == 0)
   {
      goto done;
   }

   /* Count <Blob> tags in the XML response to determine how many blobs exist. */
   body = malloc(response->payload.data_size + 1);
   if (body == NULL)
   {
      goto done;
   }
   memcpy(body, response->payload.data, response->payload.data_size);
   body[response->payload.data_size] = '\0';

   count = 0;
   p = body;
   while ((p = strstr(p, "<Blob>")) != NULL)
   {
      count++;
      p += 6;
   }

done:
   free(body);
   free(string_to_sign);
   free(signing_key);
   free(base64_signature);
   free(auth_value);
   free(signature_hmac);
   pgmoneta_http_request_destroy(request);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_destroy(connection);

   return count;
}
