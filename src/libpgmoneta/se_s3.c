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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* s3_storage_name(void);
static int s3_storage_setup(char*, struct art*);
static int s3_storage_execute(char*, struct art*);
static int s3_storage_teardown(char*, struct art*);

static int s3_upload_files(char* local_root, char* s3_root, char* relative_path);
static int s3_send_upload_request(char* local_root, char* s3_root, char* relative_path);
static int s3_add_request_headers(struct http_request* request, char* auth_value, char* file_sha256, char* long_date, char* storage_class);

static char* s3_get_host(void);
static char* s3_get_basepath(int server, char* identifier);

struct workflow*
pgmoneta_storage_create_s3(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->name = &s3_storage_name;
   wf->setup = &s3_storage_setup;
   wf->execute = &s3_storage_execute;
   wf->teardown = &s3_storage_teardown;
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

   pgmoneta_log_debug("S3 storage engine (execute): %s/%s", config->common.servers[server].name, label);

   local_root = pgmoneta_get_server_backup_identifier(server, label);
   base_dir = pgmoneta_get_server_backup(server);
   s3_root = s3_get_basepath(server, label);

   if (s3_upload_files(local_root, s3_root, ""))
   {
      goto error;
   }

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   remote_s3_elapsed_time = pgmoneta_compute_duration(start_t, end_t);

   if (pgmoneta_load_info(base_dir, label, &temp_backup))
   {
      goto error;
   }
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
s3_upload_files(char* local_root, char* s3_root, char* relative_path)
{
   char* local_path = NULL;
   char* relative_file;
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

         if (strlen(relative_path) > 0)
         {
            snprintf(relative_dir, sizeof(relative_dir), "%s/%s", relative_path, entry->d_name);
         }
         else
         {
            snprintf(relative_dir, sizeof(relative_dir), "%s", entry->d_name);
         }

         s3_upload_files(local_root, s3_root, relative_dir);
      }
      else
      {
         relative_file = NULL;

         if (strlen(relative_path) > 0)
         {
            relative_file = pgmoneta_append(relative_file, relative_path);
            relative_file = pgmoneta_append(relative_file, "/");
         }
         relative_file = pgmoneta_append(relative_file, entry->d_name);

         if (s3_send_upload_request(local_root, s3_root, relative_file))
         {
            free(relative_file);
            goto error;
         }

         free(relative_file);
      }
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
s3_send_upload_request(char* local_root, char* s3_root, char* relative_path)
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
   struct main_configuration* config;
   bool use_storage_class = false;

   config = (struct main_configuration*)shmem;

   if (strlen(config->s3_storage_class) > 0 && strlen(config->s3_endpoint) == 0)
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

   s3_host = s3_get_host();

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
      // Add storage class to canonical request (for AWS)
      canonical_request = pgmoneta_append(canonical_request, "\nx-amz-storage-class:");
      canonical_request = pgmoneta_append(canonical_request, config->s3_storage_class);
      canonical_request = pgmoneta_append(canonical_request, "\n\nhost;x-amz-content-sha256;x-amz-date;x-amz-storage-class\n");
   }
   else
   {
      // No storage class (works for both AWS and Cloudflare)
      canonical_request = pgmoneta_append(canonical_request, "\n\nhost;x-amz-content-sha256;x-amz-date\n");
   }

   canonical_request = pgmoneta_append(canonical_request, file_sha256);

   pgmoneta_generate_string_sha256_hash(canonical_request, &canonical_request_sha256);

   string_to_sign = pgmoneta_append(string_to_sign, "AWS4-HMAC-SHA256\n");
   string_to_sign = pgmoneta_append(string_to_sign, long_date);
   string_to_sign = pgmoneta_append(string_to_sign, "\n");
   string_to_sign = pgmoneta_append(string_to_sign, short_date);
   string_to_sign = pgmoneta_append(string_to_sign, "/");
   string_to_sign = pgmoneta_append(string_to_sign, config->s3_region);
   string_to_sign = pgmoneta_append(string_to_sign, "/s3/aws4_request\n");
   string_to_sign = pgmoneta_append(string_to_sign, canonical_request_sha256);

   key = pgmoneta_append(key, "AWS4");
   key = pgmoneta_append(key, config->s3_secret_access_key);

   if (pgmoneta_generate_string_hmac_sha256_hash(key, strlen(key), short_date, SHORT_TIME_LENGTH - 1, &date_key_hmac, &hmac_length))
   {
      goto error;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash((char*)date_key_hmac, hmac_length, config->s3_region, strlen(config->s3_region), &date_region_key_hmac, &hmac_length))
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
   auth_value = pgmoneta_append(auth_value, config->s3_access_key_id);
   auth_value = pgmoneta_append(auth_value, "/");
   auth_value = pgmoneta_append(auth_value, short_date);
   auth_value = pgmoneta_append(auth_value, "/");
   auth_value = pgmoneta_append(auth_value, config->s3_region);

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

   if (config->s3_port != 0)
   {
      s3_port = config->s3_port;
   }
   else
   {
      s3_port = config->s3_use_tls? 443 : 80;
   }
   bool use_tls = config->s3_use_tls;
   if (s3_port == 443)
   {
      use_tls = true;
   }
   // we can check the validity of a flag to another(port with the correct tls or not)

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

   if (s3_add_request_headers(request, auth_value, file_sha256, long_date, config->s3_storage_class))
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
s3_get_host(void)
{
   char* host = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   char* endpoint = NULL;
   if (strlen(config->s3_endpoint) > 0)
   {
      endpoint = config->s3_endpoint;
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
   host = pgmoneta_append(host, config->s3_bucket);
   host = pgmoneta_append(host, ".s3.");
   host = pgmoneta_append(host, config->s3_region);
   host = pgmoneta_append(host, ".amazonaws.com");

   return host;
}

static char*
s3_get_basepath(int server, char* identifier)
{
   char* d = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (strlen(config->s3_endpoint) > 0)
   {
      d = pgmoneta_append(d, config->s3_bucket);
      d = pgmoneta_append(d, "/");
   }
   d = pgmoneta_append(d, config->s3_base_dir);
   if (!pgmoneta_ends_with(config->s3_base_dir, "/"))
   {
      d = pgmoneta_append(d, "/");
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
