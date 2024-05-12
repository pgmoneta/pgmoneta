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
#include <json.h>
#include <logging.h>
#include <manifest.h>
#include <security.h>
#include <utils.h>

/* system */
#include <stdio.h>
#include <string.h>

static int manifest_file_hash(char* algorithm, char* file_path, char** hash);

int
pgmoneta_manifest_checksum_verify(char* root)
{
   char manifest_path[MAX_PATH];
   char* key_path[1] = {"Files"};
   struct json_reader* reader = NULL;
   struct json* file = NULL;

   memset(manifest_path, 0, MAX_PATH);
   if (pgmoneta_ends_with(root, "/"))
   {
      snprintf(manifest_path, MAX_PATH, "%s%s", root, "backup_manifest");
   }
   else
   {
      snprintf(manifest_path, MAX_PATH, "%s/%s", root, "backup_manifest");
   }
   if (pgmoneta_json_reader_init(manifest_path, &reader))
   {
      goto error;
   }
   if (pgmoneta_json_locate(reader, key_path, 1))
   {
      pgmoneta_log_error("cannot locate files array in manifest %s", manifest_path);
      goto error;
   }
   while (pgmoneta_json_next_array_item(reader, &file))
   {
      char file_path[MAX_PATH];
      size_t file_size = 0;
      size_t file_size_manifest = 0;
      char* hash = NULL;
      char* algorithm = NULL;
      char* checksum = NULL;

      memset(file_path, 0, MAX_PATH);
      if (pgmoneta_ends_with(root, "/"))
      {
         snprintf(file_path, MAX_PATH, "%s%s", root, pgmoneta_json_get_string_value(file, "Path"));
      }
      else
      {
         snprintf(file_path, MAX_PATH, "%s/%s", root, pgmoneta_json_get_string_value(file, "Path"));
      }

      file_size = pgmoneta_get_file_size(file_path);
      file_size_manifest = pgmoneta_json_get_int64_value(file, "Size");
      if (file_size != file_size_manifest)
      {
         pgmoneta_log_error("File size mismatch: %s, getting %lu, should be %lu", file_size, file_size_manifest);
      }

      algorithm = pgmoneta_json_get_string_value(file, "Checksum-Algorithm");
      if (manifest_file_hash(algorithm, file_path, &hash))
      {
         pgmoneta_log_error("Unable to generate hash for file %s with algorithm %s", file_path, algorithm);
         goto error;
      }

      checksum = pgmoneta_json_get_string_value(file, "Checksum");
      if (!pgmoneta_compare_string(hash, checksum))
      {
         pgmoneta_log_error("File checksum mismatch, path: %s. Getting %s, should be %s", file_path, hash, checksum);
      }
      free(hash);
      pgmoneta_json_free(file);
      file = NULL;
   }
   pgmoneta_json_close_reader(reader);
   pgmoneta_json_free(file);
   return 0;

error:
   pgmoneta_json_close_reader(reader);
   pgmoneta_json_free(file);
   return 1;
}

int
pgmoneta_compare_manifests(char* old_manifest, char* new_manifest, struct node** deleted_files, struct node** changed_files, struct node** new_files)
{
   char* key_path[1] = {"Files"};
   struct json_reader* r1 = NULL;
   struct json* f1 = NULL;
   struct json_reader* r2 = NULL;
   struct json* f2 = NULL;
   struct node* deleted_files_head = NULL;
   struct node* changed_files_head = NULL;
   struct node* new_files_head = NULL;
   struct node* n = NULL;
   char* checksum1 = NULL;
   char* checksum2 = NULL;
   char* path = NULL;
   bool manifest_changed = false;

   *deleted_files = NULL;
   *changed_files = NULL;
   *new_files = NULL;

   if (pgmoneta_json_reader_init(old_manifest, &r1) || pgmoneta_json_locate(r1, key_path, 1))
   {
      goto error;
   }

   if (pgmoneta_json_reader_init(new_manifest, &r2) || pgmoneta_json_locate(r2, key_path, 1))
   {
      goto error;
   }
   while (pgmoneta_json_next_array_item(r1, &f1))
   {
      bool deleted = true;
      path = pgmoneta_json_get_string_value(f1, "Path");
      checksum1 = pgmoneta_json_get_string_value(f1, "Checksum");
      while (pgmoneta_json_next_array_item(r2, &f2) && deleted)
      {
         if (pgmoneta_compare_string(path, pgmoneta_json_get_string_value(f2, "Path")))
         {
            deleted = false;
            checksum2 = pgmoneta_json_get_string_value(f2, "Checksum");
            if (!pgmoneta_compare_string(checksum1, checksum2))
            {
               manifest_changed = true;
               pgmoneta_log_trace("%s: %s <-> %s", path, checksum1, checksum2);

               if (pgmoneta_create_node_string(path, checksum1, &n))
               {
                  goto error;
               }

               pgmoneta_append_node(&changed_files_head, n);
               n = NULL;

               break;
            }
         }

         pgmoneta_json_free(f2);
         f2 = NULL;
      }

      if (deleted)
      {
         manifest_changed = true;
         if (pgmoneta_create_node_string(path, checksum1, &n))
         {
            goto error;
         }

         pgmoneta_append_node(&deleted_files_head, n);
         n = NULL;
      }

      pgmoneta_json_free(f1);
      f1 = NULL;
      if (pgmoneta_json_reader_reset(r2) || pgmoneta_json_locate(r2, key_path, 1))
      {
         goto error;
      }
   }

   while (pgmoneta_json_next_array_item(r2, &f2))
   {
      bool new = true;

      path = pgmoneta_json_get_string_value(f2, "Path");
      checksum2 = pgmoneta_json_get_string_value(f2, "Checksum");
      while (pgmoneta_json_next_array_item(r1, &f1) && new)
      {
         if (pgmoneta_compare_string(pgmoneta_json_get_string_value(f1, "Path"), path))
         {
            new = false;
         }

         pgmoneta_json_free(f1);
         f1 = NULL;
      }

      if (new)
      {
         if (pgmoneta_create_node_string(path, checksum2, &n))
         {
            goto error;
         }
         manifest_changed = true;

         pgmoneta_append_node(&new_files_head, n);
         n = NULL;
      }

      pgmoneta_json_free(f2);
      f2 = NULL;
      if (pgmoneta_json_reader_reset(r1) || pgmoneta_json_locate(r1, key_path, 1))
      {
         goto error;
      }
   }
   if (manifest_changed)
   {
      if (pgmoneta_create_node_string("backup_manifest", "manifest", &n))
      {
         goto error;
      }
      pgmoneta_append_node(&changed_files_head, n);
   }

   *deleted_files = deleted_files_head;
   *changed_files = changed_files_head;
   *new_files = new_files_head;

   pgmoneta_json_close_reader(r1);
   pgmoneta_json_close_reader(r2);
   pgmoneta_json_free(f1);
   pgmoneta_json_free(f2);

   return 0;

error:
   pgmoneta_json_close_reader(r1);
   pgmoneta_json_close_reader(r2);
   pgmoneta_json_free(f1);
   pgmoneta_json_free(f2);

   return 1;
}

static int
manifest_file_hash(char* algorithm, char* file_path, char** hash)
{
   int stat = 0;
   if (pgmoneta_compare_string(algorithm, "SHA256"))
   {
      stat = pgmoneta_create_sha256_file(file_path, hash);
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
