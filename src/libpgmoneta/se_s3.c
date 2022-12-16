/*
 * Copyright (C) 2022 Red Hat
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
#include <dirent.h>
#include <http.h>
#include <info.h>
#include <logging.h>
#include <security.h>
#include <stdio.h>
#include <storage.h>
#include <utils.h>

/* system */
#include <stdlib.h>
#include <string.h>

static int s3_storage_setup(int, char*, struct node*, struct node**);
static int s3_storage_execute(int, char*, struct node*, struct node**);
static int s3_storage_teardown(int, char*, struct node*, struct node**);

static int s3_upload_files(char* local_root, char* s3_root, char* relative_path);
static int s3_send_upload_request(char* local_root, char* s3_root, char* relative_path);

static char* s3_get_host(void);
static char* s3_get_basepath(int server, char* identifier);

static CURL* curl = NULL;

struct workflow*
pgmoneta_storage_create_s3(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->setup = &s3_storage_setup;
   wf->execute = &s3_storage_execute;
   wf->teardown = &s3_storage_teardown;
   wf->next = NULL;

   return wf;
}

static int
s3_storage_setup(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   curl = curl_easy_init();
   if (curl == NULL)
   {
      goto error;
   }

   return 0;

error:
   return 1;
}

static int
s3_storage_execute(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* local_root = NULL;
   char* s3_root = NULL;

   local_root = pgmoneta_get_server_backup_identifier(server, identifier);
   s3_root = s3_get_basepath(server, identifier);

   if (s3_upload_files(local_root, s3_root, ""))
   {
      goto error;
   }

   free(local_root);
   free(s3_root);

   return 0;

error:

   free(local_root);
   free(s3_root);

   return 1;
}

static int
s3_storage_teardown(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* root = NULL;

   root = pgmoneta_get_server_backup_identifier_data(server, identifier);

   pgmoneta_delete_directory(root);

   curl_easy_cleanup(curl);

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

         snprintf(relative_dir, sizeof(relative_dir), "%s/%s", relative_path, entry->d_name);

         s3_upload_files(local_root, s3_root, relative_dir);
      }
      else
      {
         relative_file = NULL;

         relative_file = pgmoneta_append(relative_file, relative_path);
         relative_file = pgmoneta_append(relative_file, "/");
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
   char short_date[SHORT_TIME_LENGHT];
   char long_date[LONG_TIME_LENGHT];
   char canonical_request[4096];
   char auth_value[4096];
   char string_to_sign[1024];
   char* s3_host = NULL;
   char* s3_url = NULL;
   char* file_sha256 = NULL;
   char* canonical_request_sha256 = NULL;
   char* key = NULL;
   char* local_path = NULL;
   char* s3_path = NULL;
   unsigned char* date_key_hmac = NULL;
   unsigned char* date_region_key_hmac = NULL;
   unsigned char* date_region_service_key_hmac = NULL;
   unsigned char* signing_key_hmac = NULL;
   unsigned char* signature_hmac = NULL;
   unsigned char* signature_hex = NULL;
   size_t string_to_sign_length = 0;
   size_t key_length = 0;
   int hmac_length = 0;
   FILE* file = NULL;
   struct stat file_info;
   CURLcode res = -1;
   struct curl_slist* chunk = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   local_path = pgmoneta_append(local_path, local_root);
   local_path = pgmoneta_append(local_path, relative_path);

   s3_path = pgmoneta_append(s3_path, s3_root);
   s3_path = pgmoneta_append(s3_path, relative_path);

   memset(&short_date[0], 0, sizeof(short_date));
   memset(&long_date[0], 0, sizeof(long_date));

   if (pgmoneta_get_timestamp_ISO8601_format(short_date, long_date))
   {
      goto error;
   }

   pgmoneta_generate_file_sha256_hash(local_path, &file_sha256);

   // Construct canonical request.
   memset(canonical_request, 0, sizeof(canonical_request));
   sprintf(canonical_request, "PUT\n/%s\n\nhost:%s.s3.%s.amazonaws.com\nx-amz-content-sha256:%s\nx-amz-date:%s\nx-amz-storage-class:REDUCED_REDUNDANCY\n\nhost;x-amz-content-sha256;x-amz-date;x-amz-storage-class\n%s", s3_path, config->s3_bucket, config->s3_aws_region, file_sha256, long_date, file_sha256);

   pgmoneta_generate_string_sha256_hash(canonical_request, &canonical_request_sha256);

   // Construct string to sign.
   memset(string_to_sign, 0, sizeof(string_to_sign));
   string_to_sign_length = sprintf(string_to_sign, "AWS4-HMAC-SHA256\n%s\n%s/%s/s3/aws4_request\n%s", long_date, short_date, config->s3_aws_region, canonical_request_sha256);

   key = malloc(4 + 40 + 1);
   key_length = sprintf(key, "AWS4%s", config->s3_secret_access_key);

   if (pgmoneta_generate_string_hmac_sha256_hash(key, key_length, short_date, SHORT_TIME_LENGHT - 1, &date_key_hmac, &hmac_length))
   {
      goto error;
   }

   if (pgmoneta_generate_string_hmac_sha256_hash((char*)date_key_hmac, hmac_length, config->s3_aws_region, strlen(config->s3_aws_region), &date_region_key_hmac, &hmac_length))
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

   if (pgmoneta_generate_string_hmac_sha256_hash((char*)signing_key_hmac, hmac_length, string_to_sign, string_to_sign_length, &signature_hmac, &hmac_length))
   {
      goto error;
   }

   pgmoneta_convert_base32_to_hex(signature_hmac, hmac_length, &signature_hex);

   memset(&auth_value[0], 0, sizeof(auth_value));
   sprintf(auth_value, "AWS4-HMAC-SHA256 Credential=%s/%s/%s/s3/aws4_request,SignedHeaders=host;x-amz-content-sha256;x-amz-date;x-amz-storage-class,Signature=%s", config->s3_access_key_id, short_date, config->s3_aws_region, signature_hex);

   chunk = pgmoneta_http_add_header(chunk, "Authorization", auth_value);

   s3_host = s3_get_host();

   chunk = pgmoneta_http_add_header(chunk, "Host", s3_host);

   chunk = pgmoneta_http_add_header(chunk, "x-amz-content-sha256", file_sha256);

   chunk = pgmoneta_http_add_header(chunk, "x-amz-date", long_date);

   chunk = pgmoneta_http_add_header(chunk, "x-amz-storage-class", "REDUCED_REDUNDANCY");

   if (pgmoneta_http_set_header_option(curl, chunk))
   {
      goto error;
   }

   s3_url = pgmoneta_append(s3_url, "https://");
   s3_url = pgmoneta_append(s3_url, s3_host);
   s3_url = pgmoneta_append(s3_url, "/");
   s3_url = pgmoneta_append(s3_url, s3_path);

   file = fopen(local_path, "rb");
   if (file == NULL)
   {
      goto error;
   }

   if (fstat(fileno(file), &file_info) != 0)
   {
      goto error;
   }

   pgmoneta_http_set_request_option(curl, HTTP_PUT);

   pgmoneta_http_set_url_option(curl, s3_url);

   curl_easy_setopt(curl, CURLOPT_READDATA, (void*) file);

   curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)file_info.st_size);

   curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

   res = curl_easy_perform(curl);
   if (res != CURLE_OK)
   {
      fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
      goto error;
   }

   free(s3_url);
   free(s3_host);
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

   curl_slist_free_all(chunk);

   fclose(file);
   return 0;

error:

   if (s3_url != NULL)
   {
      free(s3_url);
   }

   if (s3_host != NULL)
   {
      free(s3_host);
   }

   if (signature_hex != NULL)
   {
      free(signature_hex);
   }

   if (signature_hmac != NULL)
   {
      free(signature_hmac);
   }

   if (signing_key_hmac != NULL)
   {
      free(signing_key_hmac);
   }

   if (date_region_service_key_hmac != NULL)
   {
      free(date_region_service_key_hmac);
   }

   if (date_region_key_hmac != NULL)
   {
      free(date_region_key_hmac);
   }

   if (date_key_hmac != NULL)
   {
      free(date_region_key_hmac);
   }

   if (key != NULL)
   {
      free(key);
   }

   if (local_path != NULL)
   {
      free(local_path);
   }

   if (s3_path != NULL)
   {
      free(s3_path);
   }

   if (canonical_request_sha256 != NULL)
   {
      free(canonical_request_sha256);
   }

   if (file_sha256 != NULL)
   {
      free(file_sha256);
   }

   if (chunk != NULL)
   {
      curl_slist_free_all(chunk);
   }

   if (file != NULL)
   {
      fclose(file);
   }

   return 1;
}

static char*
s3_get_host()
{
   char* host = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   host = pgmoneta_append(host, config->s3_bucket);
   host = pgmoneta_append(host, ".s3.");
   host = pgmoneta_append(host, config->s3_aws_region);
   host = pgmoneta_append(host, ".amazonaws.com");

   return host;
}

static char*
s3_get_basepath(int server, char* identifier)
{
   char* d = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   d = pgmoneta_append(d, config->s3_base_dir);
   if (!pgmoneta_ends_with(config->s3_base_dir, "/"))
   {
      d = pgmoneta_append(d, "/");
   }
   d = pgmoneta_append(d, config->servers[server].name);
   d = pgmoneta_append(d, "/backup/");
   d = pgmoneta_append(d, identifier);

   return d;
}