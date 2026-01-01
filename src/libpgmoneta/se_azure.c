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
static int azure_add_request_headers(struct http_request* request, char* auth_value, char* utc_date);

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
   if (pgmoneta_load_info(base_dir, label, &temp_backup))
   {
      pgmoneta_log_error("Unable to get backup for directory %s", base_dir);
      goto error;
   }
   temp_backup->remote_azure_elapsed_time = remote_azure_elapsed_time;
   if (pgmoneta_save_info(base_dir, temp_backup))
   {
      pgmoneta_log_error("Unable to save backup info for directory %s", base_dir);
      goto error;
   }

   free(temp_backup);
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
   pgmoneta_dump_art(nodes);

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
   if (strlen(relative_path) > 0)
   {
      if (!pgmoneta_ends_with(local_root, "/"))
      {
         local_path = pgmoneta_append(local_path, "/");
      }
      local_path = pgmoneta_append(local_path, relative_path);
   }

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

         if (strlen(relative_path) > 0)
         {
            snprintf(relative_dir, sizeof(relative_dir), "%s/%s", relative_path, entry->d_name);
         }
         else
         {
            snprintf(relative_dir, sizeof(relative_dir), "%s", entry->d_name);
         }

         azure_upload_files(local_root, azure_root, relative_dir);
      }
      else
      {
         copied_files = true;

         relative_file = NULL;

         if (strlen(relative_path) > 0)
         {
            relative_file = pgmoneta_append(relative_file, relative_path);
            relative_file = pgmoneta_append(relative_file, "/");
         }
         relative_file = pgmoneta_append(relative_file, entry->d_name);

         if (azure_send_upload_request(local_root, azure_root, relative_file))
         {
            free(relative_file);
            goto error;
         }

         free(relative_file);
      }
   }

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

   pgmoneta_base64_decode(config->azure_shared_key, strlen(config->azure_shared_key), (void**)&signing_key, &signing_key_length);

   if (pgmoneta_generate_string_hmac_sha256_hash(signing_key, signing_key_length, string_to_sign, strlen(string_to_sign), &signature_hmac, &hmac_length))
   {
      goto error;
   }

   pgmoneta_base64_encode((char*)signature_hmac, hmac_length, &base64_signature, &base64_signature_length);

   auth_value = pgmoneta_append(auth_value, "SharedKey ");
   auth_value = pgmoneta_append(auth_value, config->azure_storage_account);
   auth_value = pgmoneta_append(auth_value, ":");
   auth_value = pgmoneta_append(auth_value, base64_signature);

   azure_host = azure_get_host();

   if (pgmoneta_http_create(azure_host, 443, true, &connection))
   {
      pgmoneta_log_error("Failed to connect to Azure host: %s", azure_host);
      goto error;
   }

   snprintf(azure_put_path, sizeof(azure_put_path), "/%s/%s", config->azure_container, azure_path);

   if (pgmoneta_http_request_create(PGMONETA_HTTP_PUT, azure_put_path, &request))
   {
      goto error;
   }

   if (azure_add_request_headers(request, auth_value, utc_date))
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