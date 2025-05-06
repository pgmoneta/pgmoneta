/*
 * Copyright (C) 2025 The pgmoneta community
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
#include <http.h>
#include <logging.h>
#include <security.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>

static char* azure_storage_name(void);
static int azure_storage_setup(char* name, struct art*);
static int azure_storage_execute(char* name, struct art*);
static int azure_storage_teardown(char* name, struct art*);

static int azure_upload_files(char* local_root, char* azure_root, char* relative_path);
static int azure_send_upload_request(char* local_root, char* azure_root, char* relative_path);

static char* azure_get_host(void);
static char* azure_get_basepath(int server, char* identifier);

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
   if (pgmoneta_log_is_enabled(PGMONETA_LOGGING_LEVEL_DEBUG1))
   {
      char* a = pgmoneta_art_to_string(nodes, FORMAT_TEXT, NULL, 0);
      pgmoneta_log_debug("(Tree)\n%s", a);
      free(a);
   }
   assert(nodes != NULL);
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
   char* azure_root = NULL;
   struct main_configuration* config;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   if (pgmoneta_log_is_enabled(PGMONETA_LOGGING_LEVEL_DEBUG1))
   {
      char* a = NULL;
      a = pgmoneta_art_to_string(nodes, FORMAT_TEXT, NULL, 0);
      pgmoneta_log_debug("(Tree)\n%s", a);
      free(a);
   }
   assert(nodes != NULL);
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("Azure storage engine (execute): %s/%s", config->common.servers[server].name, label);

   local_root = pgmoneta_get_server_backup_identifier(server, label);
   azure_root = azure_get_basepath(server, label);

   if (azure_upload_files(local_root, azure_root, ""))
   {
      goto error;
   }

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   remote_azure_elapsed_time = pgmoneta_compute_duration(start_t, end_t);

   pgmoneta_update_info_double(local_root, INFO_REMOTE_AZURE_ELAPSED, remote_azure_elapsed_time);

   free(local_root);
   free(azure_root);

   return 0;

error:

   free(local_root);
   free(azure_root);

   return 1;
}

static int
azure_storage_teardown(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   char* root = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   if (pgmoneta_log_is_enabled(PGMONETA_LOGGING_LEVEL_DEBUG1))
   {
      char* a = NULL;
      a = pgmoneta_art_to_string(nodes, FORMAT_TEXT, NULL, 0);
      pgmoneta_log_debug("(Tree)\n%s", a);
      free(a);
   }
   assert(nodes != NULL);
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   root = pgmoneta_get_server_backup_identifier_data(server, label);

   pgmoneta_delete_directory(root);

   pgmoneta_log_debug("Azure storage engine (teardown): %s/%s", config->common.servers[server].name, label);

   free(root);

   return 0;
}

static int
azure_upload_files(char* local_root, char* azure_root, char* relative_path)
{
   char* local_path = NULL;
   char* relative_file;
   char* new_file;
   bool copied_files = false;
   DIR* dir;
   struct dirent* entry;

   local_path = pgmoneta_append(local_path, local_root);
   local_path = pgmoneta_append(local_path, relative_path);

   if (!(dir = opendir(local_path)))
   {
      goto error;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_DIR)
      {
         char relative_dir[1024];

         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }

         snprintf(relative_dir, sizeof(relative_dir), "%s/%s", relative_path, entry->d_name);

         azure_upload_files(local_root, azure_root, relative_dir);
      }
      else
      {
         copied_files = true;

         relative_file = NULL;

         relative_file = pgmoneta_append(relative_file, relative_path);
         relative_file = pgmoneta_append(relative_file, "/");
         relative_file = pgmoneta_append(relative_file, entry->d_name);

         if (azure_send_upload_request(local_root, azure_root, relative_file))
         {
            free(relative_file);
            goto error;
         }

         free(relative_file);
      }
   }

   // In case no files are copied, then the directory is empty.
   // Create a .pgmoneta file for uploading.
   if (!copied_files)
   {
      relative_file = NULL;

      relative_file = pgmoneta_append(relative_file, relative_path);
      relative_file = pgmoneta_append(relative_file, "/.pgmoneta");

      new_file = NULL;

      new_file = pgmoneta_append(new_file, local_root);
      new_file = pgmoneta_append(new_file, relative_file);

      FILE* file = fopen(new_file, "w");

      pgmoneta_permission(new_file, 6, 4, 4);

      azure_send_upload_request(local_root, azure_root, relative_file);

      fclose(file);

      remove(new_file);

      free(new_file);
      free(relative_file);
   }

   closedir(dir);

   free(local_path);

   return 0;

error:

   closedir(dir);

   free(local_path);

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
   char azure_put_path[MAX_PATH];
   char* azure_host = NULL;
   char* auth_value = NULL;
   unsigned char* signature_hmac = NULL;
   int hmac_length = 0;
   size_t signing_key_length = 0;
   FILE* file = NULL;
   struct stat file_info;
   int res = -1;
   struct http* http = NULL;
   struct main_configuration* config;
   char size_str[64];

   config = (struct main_configuration*)shmem;

   // Check for storage account name contains spaces, which will cause issues
   if (strchr(config->azure_storage_account, ' ') != NULL)
   {
      pgmoneta_log_error("Azure storage account name contains spaces: '%s'. This is not allowed.", config->azure_storage_account);
      goto error;
   }

   local_path = pgmoneta_append(local_path, local_root);
   local_path = pgmoneta_append(local_path, relative_path);

   azure_path = pgmoneta_append(azure_path, azure_root);
   azure_path = pgmoneta_append(azure_path, relative_path);

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

   // Since we specifiy octet-stream in header, we include it to our string_to_sign
   if (file_info.st_size == 0)
   {
      string_to_sign = pgmoneta_append(string_to_sign, "PUT\n\n\n\n\napplication/octet-stream\n\n\n\n\n\n\nx-ms-blob-type:BlockBlob\nx-ms-date:");
   }
   else
   {
      string_to_sign = pgmoneta_append(string_to_sign, "PUT\n\n\n");
      snprintf(size_str, sizeof(size_str), "%ld", (long)file_info.st_size);
      string_to_sign = pgmoneta_append(string_to_sign, size_str);
      string_to_sign = pgmoneta_append(string_to_sign, "\n\napplication/octet-stream\n\n\n\n\n\n\nx-ms-blob-type:BlockBlob\nx-ms-date:");
   }

   string_to_sign = pgmoneta_append(string_to_sign, utc_date);
   string_to_sign = pgmoneta_append(string_to_sign, "\nx-ms-version:2021-08-06\n/");
   string_to_sign = pgmoneta_append(string_to_sign, config->azure_storage_account);
   string_to_sign = pgmoneta_append(string_to_sign, "/");
   string_to_sign = pgmoneta_append(string_to_sign, config->azure_container);
   string_to_sign = pgmoneta_append(string_to_sign, "/");
   string_to_sign = pgmoneta_append(string_to_sign, azure_path);

   // Decode the Azure storage account shared key.
   pgmoneta_base64_decode(config->azure_shared_key, strlen(config->azure_shared_key), (void**)&signing_key, &signing_key_length);

   // Construct the signature.
   if (pgmoneta_generate_string_hmac_sha256_hash(signing_key, signing_key_length, string_to_sign, strlen(string_to_sign), &signature_hmac, &hmac_length))
   {
      goto error;
   }

   // Encode the signature.
   pgmoneta_base64_encode((char*) signature_hmac, hmac_length, &base64_signature, &base64_signature_length);

   // Construct the authorization header.
   auth_value = pgmoneta_append(auth_value, "SharedKey ");
   auth_value = pgmoneta_append(auth_value, config->azure_storage_account);
   auth_value = pgmoneta_append(auth_value, ":");
   auth_value = pgmoneta_append(auth_value, base64_signature);

   azure_host = azure_get_host();

   // Create HTTP connection
   if (pgmoneta_http_connect(azure_host, 443, true, &http))
   {
      pgmoneta_log_error("Failed to connect to Azure host: %s", azure_host);
      goto error;
   }

   // Add headers
   pgmoneta_http_add_header(http, "Authorization", auth_value);
   pgmoneta_http_add_header(http, "x-ms-blob-type", "BlockBlob");
   pgmoneta_http_add_header(http, "x-ms-date", utc_date);
   pgmoneta_http_add_header(http, "x-ms-version", "2021-08-06");

   // Construct PUT path
   snprintf(azure_put_path, sizeof(azure_put_path), "/%s/%s", config->azure_container, azure_path);

   // Send PUT request with file
   res = pgmoneta_http_put_file(http, azure_host, azure_put_path, file, file_info.st_size, "application/octet-stream");
   if (res != 0)
   {
      pgmoneta_log_error("Azure upload failed for %s", local_path);
      pgmoneta_log_error("Failed to execute HTTP PUT request");
      goto error;
   }
   else
   {
      int status_code = 0;
      if (http->headers && sscanf(http->headers, "HTTP/1.1 %d", &status_code) == 1)
      {
         pgmoneta_log_info("Azure HTTP status code: %d", status_code);
         if (status_code >= 200 && status_code < 300)
         {
            pgmoneta_log_info("Successfully uploaded file to Azure: %s", azure_path);

            char* azure_url = NULL;
            azure_url = pgmoneta_append(azure_url, "https://");
            azure_url = pgmoneta_append(azure_url, azure_host);
            azure_url = pgmoneta_append(azure_url, "/");
            azure_url = pgmoneta_append(azure_url, config->azure_container);
            azure_url = pgmoneta_append(azure_url, "/");
            azure_url = pgmoneta_append(azure_url, azure_path);

            pgmoneta_log_info("Blob URL: %s", azure_url);
            free(azure_url);
         }
         else
         {
            pgmoneta_log_error("Azure upload failed with status code: %d. Failed to upload: %s to Azure path: %s. Azure container: %s, host: %s. Response headers: %s. Response body: %s",
                               status_code, local_path, azure_path,
                               config->azure_container, azure_host,
                               http->headers ? http->headers : "None",
                               http->body ? http->body : "None");
            goto error;
         }
      }
      else
      {
         pgmoneta_log_error("Azure upload failed - could not parse HTTP status code. Failed to upload: %s to Azure path: %s. Response headers: %s",
                            local_path, azure_path,
                            http->headers ? http->headers : "None");
         goto error;
      }
   }

   free(local_path);
   free(azure_path);
   free(azure_host);
   free(base64_signature);
   free(signature_hmac);
   free(string_to_sign);
   free(auth_value);
   free(signing_key);

   pgmoneta_http_disconnect(http);
   free(http);

   fclose(file);
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

   if (http != NULL)
   {
      pgmoneta_http_disconnect(http);
      free(http);
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

   host = pgmoneta_append(host, config->azure_storage_account);
   host = pgmoneta_append(host, ".blob.core.windows.net");

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
