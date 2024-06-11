/*
 * Copyright (C) 2024 The pgmoneta community
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
#include <io.h>

/* system */
#include <stdlib.h>
#include <string.h>

static int azure_storage_setup(int, char*, struct node*, struct node**);
static int azure_storage_execute(int, char*, struct node*, struct node**);
static int azure_storage_teardown(int, char*, struct node*, struct node**);

static int azure_upload_files(char* local_root, char* azure_root, char* relative_path);
static int azure_send_upload_request(char* local_root, char* azure_root, char* relative_path);

static char* azure_get_host(void);
static char* azure_get_basepath(int server, char* identifier);

static CURL* curl = NULL;

struct workflow*
pgmoneta_storage_create_azure(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->setup = &azure_storage_setup;
   wf->execute = &azure_storage_execute;
   wf->teardown = &azure_storage_teardown;
   wf->next = NULL;

   return wf;
}

static int
azure_storage_setup(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
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
azure_storage_execute(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* local_root = NULL;
   char* azure_root = NULL;

   local_root = pgmoneta_get_server_backup_identifier(server, identifier);
   azure_root = azure_get_basepath(server, identifier);

   if (azure_upload_files(local_root, azure_root, ""))
   {
      goto error;
   }

   free(local_root);
   free(azure_root);

   return 0;

error:

   free(local_root);
   free(azure_root);

   return 1;
}

static int
azure_storage_teardown(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* root = NULL;

   root = pgmoneta_get_server_backup_identifier_data(server, identifier);

   pgmoneta_delete_directory(root);

   curl_easy_cleanup(curl);

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

      FILE* file = pgmoneta_open_file(new_file, "w");

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
   char* signature = NULL;
   char* base64_signature = NULL;
   char* local_path = NULL;
   char* azure_path = NULL;
   char* azure_host = NULL;
   char* azure_url = NULL;
   char* auth_value = NULL;
   unsigned char* signature_hmac = NULL;
   unsigned char* signature_hex = NULL;
   int hmac_length = 0;
   int signing_key_length = 0;
   FILE* file = NULL;
   struct stat file_info;
   CURLcode res = -1;
   struct curl_slist* chunk = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   local_path = pgmoneta_append(local_path, local_root);
   local_path = pgmoneta_append(local_path, relative_path);

   azure_path = pgmoneta_append(azure_path, azure_root);
   azure_path = pgmoneta_append(azure_path, relative_path);

   memset(&utc_date[0], 0, sizeof(utc_date));

   if (pgmoneta_get_timestamp_UTC_format(utc_date))
   {
      goto error;
   }

   file = pgmoneta_open_file(local_path, "rb");
   if (file == NULL)
   {
      goto error;
   }

   if (fstat(fileno(file), &file_info) != 0)
   {
      goto error;
   }

   // Construct string to sign.
   if (file_info.st_size == 0)
   {
      string_to_sign = pgmoneta_append(string_to_sign, "PUT\n\n\n\n\n\n\n\n\n\n\n\nx-ms-blob-type:BlockBlob\nx-ms-date:");
   }
   else
   {
      string_to_sign = pgmoneta_append(string_to_sign, "PUT\n\n\n");
      string_to_sign = pgmoneta_append(string_to_sign, (char*)file_info.st_size);
      string_to_sign = pgmoneta_append(string_to_sign, "\n\n\n\n\n\n\n\n\nx-ms-blob-type:BlockBlob\nx-ms-date:");
   }

   string_to_sign = pgmoneta_append(string_to_sign, utc_date);
   string_to_sign = pgmoneta_append(string_to_sign, "\nx-ms-version:2021-08-06\n/");
   string_to_sign = pgmoneta_append(string_to_sign, config->azure_storage_account);
   string_to_sign = pgmoneta_append(string_to_sign, "/");
   string_to_sign = pgmoneta_append(string_to_sign, config->azure_container);
   string_to_sign = pgmoneta_append(string_to_sign, "/");
   string_to_sign = pgmoneta_append(string_to_sign, azure_path);

   // Decode the Azure storage account shared key.
   pgmoneta_base64_decode(config->azure_shared_key, strlen(config->azure_shared_key), &signing_key, &signing_key_length);

   // Construct the signature.
   if (pgmoneta_generate_string_hmac_sha256_hash(signing_key, signing_key_length, string_to_sign, strlen(string_to_sign), &signature_hmac, &hmac_length))
   {
      goto error;
   }

   // Encode the signature.
   pgmoneta_base64_encode((char*) signature_hmac, hmac_length, &base64_signature);

   // Construct the authorization header.
   auth_value = pgmoneta_append(auth_value, "SharedKey ");
   auth_value = pgmoneta_append(auth_value, config->azure_storage_account);
   auth_value = pgmoneta_append(auth_value, ":");
   auth_value = pgmoneta_append(auth_value, base64_signature);

   chunk = pgmoneta_http_add_header(chunk, "Authorization", auth_value);

   chunk = pgmoneta_http_add_header(chunk, "x-ms-blob-type", "BlockBlob");

   chunk = pgmoneta_http_add_header(chunk, "x-ms-date", utc_date);

   chunk = pgmoneta_http_add_header(chunk, "x-ms-version", "2021-08-06");

   if (pgmoneta_http_set_header_option(curl, chunk))
   {
      goto error;
   }

   azure_host = azure_get_host();

   azure_url = pgmoneta_append(azure_url, "https://");
   azure_url = pgmoneta_append(azure_url, azure_host);
   azure_url = pgmoneta_append(azure_url, "/");
   azure_url = pgmoneta_append(azure_url, azure_path);

   pgmoneta_http_set_request_option(curl, HTTP_PUT);

   pgmoneta_http_set_url_option(curl, azure_url);

   curl_easy_setopt(curl, CURLOPT_READDATA, (void*) file);

   curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)file_info.st_size);

   res = curl_easy_perform(curl);
   if (res != CURLE_OK)
   {
      fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
      goto error;
   }

   free(local_path);
   free(azure_path);
   free(azure_url);
   free(azure_host);
   free(signature);
   free(base64_signature);
   free(signature_hmac);
   free(signature_hex);
   free(string_to_sign);
   free(auth_value);

   curl_slist_free_all(chunk);

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

   if (azure_url != NULL)
   {
      free(azure_url);
   }

   if (azure_host != NULL)
   {
      free(azure_host);
   }

   if (signature != NULL)
   {
      free(signature);
   }

   if (base64_signature != NULL)
   {
      free(base64_signature);
   }

   if (signature_hmac != NULL)
   {
      free(signature_hmac);
   }

   if (signature_hex != NULL)
   {
      free(signature_hex);
   }

   if (string_to_sign != NULL)
   {
      free(string_to_sign);
   }

   if (auth_value != NULL)
   {
      free(auth_value);
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
azure_get_host()
{
   char* host = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   host = pgmoneta_append(host, config->azure_storage_account);
   host = pgmoneta_append(host, ".blob.core.windows.net/");
   host = pgmoneta_append(host, config->azure_container);

   return host;
}

static char*
azure_get_basepath(int server, char* identifier)
{
   char* d = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   d = pgmoneta_append(d, config->azure_base_dir);
   if (!pgmoneta_ends_with(config->azure_base_dir, "/"))
   {
      d = pgmoneta_append(d, "/");
   }
   d = pgmoneta_append(d, config->servers[server].name);
   d = pgmoneta_append(d, "/backup/");
   d = pgmoneta_append(d, identifier);

   return d;
}