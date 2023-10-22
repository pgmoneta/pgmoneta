/*
 * Copyright (C) 2023 Red Hat
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
#include <logging.h>
#include <manifest.h>
#include <security.h>
#include <utils.h>

/* system */
#include <cjson/cJSON.h>
#include <stdio.h>
#include <string.h>

static void manifest_init(struct manifest** manifest);
static void manifest_file_init(struct manifest_file** file, char* path, char* checksum, char* algorithm, size_t size);
static void manifest_file_free(struct manifest_file* file);
static int manifest_file_hash(char* algorithm, char* file_path, char** hash);

int
pgmoneta_manifest_checksum_verify(char* root)
{
   char manifest_path[MAX_PATH];
   char* manifest_sha256 = NULL;
   struct manifest* manifest = NULL;
   struct manifest_file* file = NULL;

   memset(manifest_path, 0, MAX_PATH);
   if (pgmoneta_ends_with(root, "/"))
   {
      snprintf(manifest_path, MAX_PATH, "%s%s", root, "backup_manifest");
   }
   else
   {
      snprintf(manifest_path, MAX_PATH, "%s/%s", root, "backup_manifest");
   }

   if (pgmoneta_parse_manifest(manifest_path, &manifest))
   {
      goto error;
   }
   // first check manifest checksum, it's always a SHA256 hash
   pgmoneta_generate_string_sha256_hash(manifest->content, &manifest_sha256);
   if (!pgmoneta_compare_string(manifest_sha256, manifest->checksum))
   {
      pgmoneta_log_error("Manifest checksum mismatch. Getting %s, should be %s", manifest_sha256, manifest->checksum);
      goto error;
   }
   file = manifest->files;
   while (file != NULL)
   {
      char file_path[MAX_PATH];
      size_t file_size = 0;
      char* hash = NULL;

      memset(file_path, 0, MAX_PATH);
      if (pgmoneta_ends_with(root, "/"))
      {
         snprintf(file_path, MAX_PATH, "%s%s", root, file->path);
      }
      else
      {
         snprintf(file_path, MAX_PATH, "%s/%s", root, file->path);
      }
      file_size = pgmoneta_get_file_size(file_path);
      if (file_size != file->size)
      {
         pgmoneta_log_error("File size mismatch: %s, getting %lu, should be %lu", file_size, file->size);
      }

      if (manifest_file_hash(file->algorithm, file_path, &hash))
      {
         pgmoneta_log_error("Unable to generate hash for file %s with algorithm %s", file_path, file->algorithm);
         goto error;
      }
      if (!pgmoneta_compare_string(hash, file->checksum))
      {
         pgmoneta_log_error("File checksum mismatch, path: %s. Getting %s, should be %s", file_path, manifest_sha256, manifest->checksum);
      }
      free(hash);
      file = file->next;
   }
   pgmoneta_manifest_free(manifest);
   free(manifest_sha256);
   return 0;

error:
   pgmoneta_manifest_free(manifest);
   free(manifest_sha256);
   return 1;
}

int
pgmoneta_parse_manifest(char* manifest_path, struct manifest** manifest)
{
   struct manifest* m = NULL;
   FILE* f = NULL;
   size_t checksum_size = 0;
   size_t content_size = 0;
   size_t size = 0;
   char* json = NULL;
   char* ptr = NULL;
   struct cJSON* manifest_json = NULL;
   struct cJSON* manifest_checksum = NULL;
   struct cJSON* files = NULL;
   struct cJSON* file = NULL;

   *manifest = NULL;
   manifest_init(&m);

   if (!pgmoneta_exists(manifest_path))
   {
      pgmoneta_log_error("Could not find backup manifest: %s", manifest_path);
      goto error;
   }
   size = pgmoneta_get_file_size(manifest_path);
   json = (char*) malloc(size + 1);
   memset(json, 0, size + 1);

   f = fopen(manifest_path, "r");
   if (f == NULL)
   {
      pgmoneta_log_error("Could not open backup manifest: %s", manifest_path);
      goto error;
   }

   size = fread(json, 1, size, f);

   manifest_json = cJSON_Parse(json);
   if (manifest_json == NULL)
   {
      const char* error_ptr = cJSON_GetErrorPtr();
      if (error_ptr != NULL)
      {
         pgmoneta_log_error("Unable to parse manifest: %s", error_ptr);
      }
      goto error;
   }

   files = cJSON_GetObjectItemCaseSensitive(manifest_json, "Files");
   manifest_checksum = cJSON_GetObjectItemCaseSensitive(manifest_json, "Manifest-Checksum");
   if (!cJSON_IsString(manifest_checksum))
   {
      pgmoneta_log_error("Unable to parse manifest %s", manifest_path);
      goto error;
   }
   checksum_size = strlen(manifest_checksum->valuestring);
   m->checksum = (char*)malloc(checksum_size + 1);
   memset(m->checksum, 0, checksum_size + 1);
   memcpy(m->checksum, manifest_checksum->valuestring, checksum_size);

   struct manifest_file* tmp = NULL;
   cJSON_ArrayForEach(file, files)
   {
      struct manifest_file* manifest_file = NULL;
      struct cJSON* file_path = cJSON_GetObjectItemCaseSensitive(file, "Path");
      struct cJSON* file_size = cJSON_GetObjectItemCaseSensitive(file, "Size");
      struct cJSON* file_checksum = cJSON_GetObjectItemCaseSensitive(file, "Checksum");
      struct cJSON* file_algorithm = cJSON_GetObjectItemCaseSensitive(file, "Checksum-Algorithm");

      if (!cJSON_IsNumber(file_size) || !cJSON_IsString(file_path) || !cJSON_IsString(file_checksum) || !cJSON_IsString(file_algorithm))
      {
         pgmoneta_log_error("Unable to parse manifest %s", manifest_path);
         goto error;
      }
      manifest_file_init(&manifest_file, file_path->valuestring, file_checksum->valuestring, file_algorithm->valuestring, file_size->valuedouble);
      if (manifest_file == NULL)
      {
         pgmoneta_log_error("Unable to allocated space for manifest file info");
         goto error;
      }
      if (tmp == NULL)
      {
         tmp = manifest_file;
         m->files = tmp;
      }
      else
      {
         tmp->next = manifest_file;
         tmp = tmp->next;
      }
   }

   ptr = strstr(json, "\"Manifest-Checksum\"");

   if (ptr == NULL)
   {
      pgmoneta_log_error("Incomplete manifest, missing manifest checksum");
      goto error;
   }

   // save preceding lines before the manifest checksum field,
   // in order to verify the checksum of the manifest itself
   content_size = ptr - json;
   m->content = (char*)malloc(content_size + 1);
   memset(m->content, 0, content_size + 1);
   memcpy(m->content, json, content_size);

   if (manifest_json != NULL)
   {
      cJSON_Delete(manifest_json);
   }
   if (f != NULL)
   {
      fclose(f);
   }
   free(json);
   *manifest = m;
   return 0;
error:
   if (manifest_json != NULL)
   {
      cJSON_Delete(manifest_json);
   }
   if (m != NULL)
   {
      pgmoneta_manifest_free(m);
   }
   if (f != NULL)
   {
      fclose(f);
   }
   free(json);
   return 1;
}

void
pgmoneta_manifest_free(struct manifest* manifest)
{
   if (manifest == NULL)
   {
      return;
   }
   struct manifest_file* file = manifest->files;
   while (file != NULL)
   {
      struct manifest_file* f = file;
      file = file->next;
      manifest_file_free(f);
   }
   free(manifest->checksum);
   free(manifest->content);
   free(manifest);
}

static void
manifest_init(struct manifest** manifest)
{
   struct manifest* m = NULL;
   m = (struct manifest*) malloc(sizeof(struct manifest));
   m->checksum = NULL;
   m->content = NULL;
   m->files = NULL;
   *manifest = m;
}

static void
manifest_file_init(struct manifest_file** file, char* path, char* checksum, char* algorithm, size_t size)
{
   struct manifest_file* f = NULL;
   int checksum_len = strlen(checksum);
   int path_len = strlen(path);
   int algorithm_len = strlen(algorithm);

   f = (struct manifest_file*) malloc(sizeof(struct manifest_file));
   f->next = NULL;
   f->checksum = (char*)malloc(checksum_len + 1);
   f->path = (char*)malloc(path_len + 1);
   f->algorithm = (char*)malloc(algorithm_len + 1);
   f->size = size;

   memset(f->checksum, 0, checksum_len + 1);
   memset(f->path, 0, path_len + 1);
   memset(f->algorithm, 0, algorithm_len + 1);

   memcpy(f->checksum, checksum, checksum_len);
   memcpy(f->path, path, path_len);
   memcpy(f->algorithm, algorithm, algorithm_len);
   *file = f;
}

static void
manifest_file_free(struct manifest_file* file)
{
   if (file == NULL)
   {
      return;
   }
   free(file->path);
   free(file->checksum);
   free(file->algorithm);
   free(file);
}

static int
manifest_file_hash(char* algorithm, char* file_path, char** hash)
{
   int stat = 0;
   if (pgmoneta_compare_string(algorithm, "SHA256"))
   {
      stat = pgmoneta_generate_file_sha256_hash(file_path, hash);
   }
   else if (pgmoneta_compare_string(algorithm, "CRC32C") || pgmoneta_compare_string(algorithm, "SHA224") ||
            pgmoneta_compare_string(algorithm, "SHA384") || pgmoneta_compare_string(algorithm, "SHA512"))
   {
      pgmoneta_log_error("Unsupported hash algorithm: %s", algorithm);
      stat = 1;
   }
   else
   {
      pgmoneta_log_error("Unrecognized hash algorithm: %s", algorithm);
      stat = 1;
   }
   return stat;
}
