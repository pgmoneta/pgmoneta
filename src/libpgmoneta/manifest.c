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
#include <csv.h>
#include <deque.h>
#include <json.h>
#include <logging.h>
#include <manifest.h>
#include <security.h>
#include <utils.h>

/* system */
#include <stdio.h>
#include <string.h>

static void
build_deque(struct deque* deque, struct csv_reader* reader, char** f);

static void
build_tree(struct art* tree, struct csv_reader* reader, char** f);

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
         snprintf(file_path, MAX_PATH, "%s%s", root, (char*)pgmoneta_json_get(file, "Path"));
      }
      else
      {
         snprintf(file_path, MAX_PATH, "%s/%s", root, (char*)pgmoneta_json_get(file, "Path"));
      }

      file_size = pgmoneta_get_file_size(file_path);
      file_size_manifest = (int64_t)pgmoneta_json_get(file, "Size");
      if (file_size != file_size_manifest)
      {
         pgmoneta_log_error("File size mismatch: %s, getting %lu, should be %lu", file_size, file_size_manifest);
      }

      algorithm = (char*)pgmoneta_json_get(file, "Checksum-Algorithm");
      if (pgmoneta_create_file_hash(pgmoneta_get_hash_algorithm(algorithm), file_path, &hash))
      {
         pgmoneta_log_error("Unable to generate hash for file %s with algorithm %s", file_path, algorithm);
         goto error;
      }

      checksum = (char*)pgmoneta_json_get(file, "Checksum");
      if (!pgmoneta_compare_string(hash, checksum))
      {
         pgmoneta_log_error("File checksum mismatch, path: %s. Getting %s, should be %s", file_path, hash, checksum);
      }
      free(hash);
      pgmoneta_json_destroy(file);
      file = NULL;
   }
   pgmoneta_json_reader_close(reader);
   pgmoneta_json_destroy(file);
   return 0;

error:
   pgmoneta_json_reader_close(reader);
   pgmoneta_json_destroy(file);
   return 1;
}

int
pgmoneta_compare_manifests(char* old_manifest, char* new_manifest, struct art** deleted_files, struct art** changed_files, struct art** added_files)
{
   struct csv_reader* r1 = NULL;
   char** f1 = NULL;
   struct csv_reader* r2 = NULL;
   char** f2 = NULL;
   struct art* deleted = NULL;
   struct art* changed = NULL;
   struct art* added = NULL;
   char* checksum = NULL;
   int cols = 0;
   bool manifest_changed = false;
   struct art* tree = NULL;
   struct deque* que = NULL;
   struct deque_iterator* iter = NULL;

   *deleted_files = NULL;
   *changed_files = NULL;
   *added_files = NULL;

   pgmoneta_deque_create(false, &que);

   pgmoneta_art_create(&deleted);
   pgmoneta_art_create(&added);
   pgmoneta_art_create(&changed);

   if (pgmoneta_csv_reader_init(old_manifest, &r1))
   {
      goto error;
   }

   if (pgmoneta_csv_reader_init(new_manifest, &r2))
   {
      goto error;
   }

   while (pgmoneta_csv_next_row(r1, &cols, &f1))
   {
      if (cols != MANIFEST_COLUMN_COUNT)
      {
         pgmoneta_log_error("Incorrect number of columns in manifest file");
         free(f1);
         continue;
      }
      // build left chunk into a deque
      build_deque(que, r1, f1);
      while (pgmoneta_csv_next_row(r2, &cols, &f2))
      {
         if (cols != MANIFEST_COLUMN_COUNT)
         {
            pgmoneta_log_error("Incorrect number of columns in manifest file");
            free(f2);
            continue;
         }
         // build every right chunk into an ART
         pgmoneta_art_create(&tree);
         build_tree(tree, r2, f2);
         pgmoneta_deque_iterator_create(que, &iter);
         while (pgmoneta_deque_iterator_next(iter))
         {
            checksum = (char*)pgmoneta_art_search(tree, iter->tag);
            if (checksum != NULL)
            {
               if (!strcmp((char*)pgmoneta_value_data(iter->value), checksum))
               {
                  // not changed but not deleted, remove the entry
                  pgmoneta_deque_iterator_remove(iter);
               }
               else
               {
                  // file is changed
                  manifest_changed = true;
                  pgmoneta_art_insert(changed, iter->tag, pgmoneta_value_data(iter->value), ValueString);
                  // changed but not deleted, remove the entry
                  pgmoneta_deque_iterator_remove(iter);
               }
            }
         }
         pgmoneta_art_destroy(tree);
         tree = NULL;
      }
      pgmoneta_deque_iterator_destroy(iter);
      iter = NULL;

      // traverse
      while (!pgmoneta_deque_empty(que))
      {
         char* tag = NULL;
         uintptr_t val = 0;
         manifest_changed = true;
         val = pgmoneta_deque_poll(que, &tag);
         pgmoneta_art_insert(deleted, tag, val, ValueString);
         free(tag);
         free((void*)val);
      }
      // reset right reader for the next left chunk
      if (pgmoneta_csv_reader_reset(r2))
      {
         goto error;
      }
   }
   if (pgmoneta_csv_reader_reset(r1))
   {
      goto error;
   }

   while (pgmoneta_csv_next_row(r2, &cols, &f2))
   {
      if (cols != MANIFEST_COLUMN_COUNT)
      {
         pgmoneta_log_error("Incorrect number of columns in manifest file");
         free(f2);
         continue;
      }
      build_deque(que, r2, f2);
      while (pgmoneta_csv_next_row(r1, &cols, &f1))
      {
         if (cols != MANIFEST_COLUMN_COUNT)
         {
            pgmoneta_log_error("Incorrect number of columns in manifest file");
            free(f1);
            continue;
         }
         pgmoneta_art_create(&tree);
         build_tree(tree, r1, f1);
         pgmoneta_deque_iterator_create(que, &iter);
         while (pgmoneta_deque_iterator_next(iter))
         {
            checksum = (char*)pgmoneta_art_search(tree, iter->tag);
            if (checksum != NULL)
            {
               // the entry is not new, remove it
               pgmoneta_deque_iterator_remove(iter);
            }
         }
         pgmoneta_art_destroy(tree);
         tree = NULL;
      }
      pgmoneta_deque_iterator_destroy(iter);
      iter = NULL;

      while (!pgmoneta_deque_empty(que))
      {
         char* tag = NULL;
         uintptr_t val = 0;
         manifest_changed = true;
         val = pgmoneta_deque_poll(que, &tag);
         pgmoneta_art_insert(added, tag, val, ValueString);
         free(tag);
         free((void*)val);
      }
      if (pgmoneta_csv_reader_reset(r1))
      {
         goto error;
      }
   }

   if (manifest_changed)
   {
      pgmoneta_art_insert(changed, "backup_manifest", (uintptr_t)"backup manifest", ValueString);
   }

   *deleted_files = deleted;
   *changed_files = changed;
   *added_files = added;

   pgmoneta_csv_reader_destroy(r1);
   pgmoneta_csv_reader_destroy(r2);
   pgmoneta_art_destroy(tree);
   pgmoneta_deque_destroy(que);

   return 0;
error:
   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_csv_reader_destroy(r1);
   pgmoneta_csv_reader_destroy(r2);
   pgmoneta_art_destroy(tree);
   pgmoneta_deque_destroy(que);
   return 1;
}

static void
build_deque(struct deque* deque, struct csv_reader* reader, char** f)
{
   char** entry = NULL;
   char* path = NULL;
   char* checksum = NULL;
   int cols = 0;
   if (deque == NULL)
   {
      return;
   }
   path = f[MANIFEST_PATH_INDEX];
   checksum = f[MANIFEST_CHECKSUM_INDEX];
   pgmoneta_deque_add(deque, path, (uintptr_t)checksum, ValueString);
   free(f);
   while (deque->size < MANIFEST_CHUNK_SIZE && pgmoneta_csv_next_row(reader, &cols, &entry))
   {
      if (cols != MANIFEST_COLUMN_COUNT)
      {
         pgmoneta_log_error("Incorrect number of columns in manifest file");
         free(entry);
         continue;
      }
      path = entry[MANIFEST_PATH_INDEX];
      checksum = entry[MANIFEST_CHECKSUM_INDEX];
      pgmoneta_deque_add(deque, path, (uintptr_t)checksum, ValueString);
      free(entry);
      entry = NULL;
   }
}

static void
build_tree(struct art* tree, struct csv_reader* reader, char** f)
{
   char** entry = NULL;
   char* path = NULL;
   int cols = 0;
   if (tree == NULL)
   {
      return;
   }
   path = f[MANIFEST_PATH_INDEX];
   pgmoneta_art_insert(tree, path, (uintptr_t)f[MANIFEST_CHECKSUM_INDEX], ValueString);
   free(f);
   while (tree->size < MANIFEST_CHUNK_SIZE && pgmoneta_csv_next_row(reader, &cols, &entry))
   {
      if (cols != MANIFEST_COLUMN_COUNT)
      {
         pgmoneta_log_error("Incorrect number of columns in manifest file");
         free(entry);
         continue;
      }
      path = entry[MANIFEST_PATH_INDEX];
      pgmoneta_art_insert(tree, path, (uintptr_t)entry[MANIFEST_CHECKSUM_INDEX], ValueString);
      free(entry);
   }
}

char*
pgmoneta_convert_manifest_to_pg_format(char* manifest_path)
{
   struct json_reader* reader = NULL;
   struct json* file = NULL;
   char* output = NULL;
   size_t output_size = 4096;
   char* key_path[1] = {"Files"};

   output = malloc(output_size);
   if (!output)
   {
      pgmoneta_log_error("Failed to allocate memory for converted manifest");
      return NULL;
   }
   memset(output, 0, output_size);

   if (pgmoneta_json_reader_init(manifest_path, &reader))
   {
      pgmoneta_log_error("Unable to open manifest JSON file: %s", manifest_path);
      free(output);
      return NULL;
   }

   if (pgmoneta_json_locate(reader, key_path, 1))
   {
      pgmoneta_log_error("Missing 'Files' array in manifest JSON");
      pgmoneta_json_reader_close(reader);
      free(output);
      return NULL;
   }

   while (pgmoneta_json_next_array_item(reader, &file))
   {
      char* path = (char*)pgmoneta_json_get(file, "Path");
      char* size = (char*)pgmoneta_json_get(file, "Size");
      char* checksum = (char*)pgmoneta_json_get(file, "Checksum");

      if (!path || !size || !checksum)
      {
         pgmoneta_log_error("Missing required field in manifest entry");
         pgmoneta_json_destroy(file);
         continue;
      }

      char line[512];
      snprintf(line, sizeof(line), "%s,%s,%s\n", path, size, checksum);

      if (strlen(output) + strlen(line) >= output_size - 1)
      {
         output_size += 4096; 
         char* new_output = realloc(output, output_size);
         if (!new_output)
         {
            pgmoneta_log_error("Memory reallocation failed during manifest conversion");
            pgmoneta_json_reader_close(reader);
            free(output);
            return NULL;
         }
         output = new_output;
         memset(output + strlen(output), 0, output_size - strlen(output)); 
      }

      strncat(output, line, output_size - strlen(output) - 1);
      pgmoneta_json_destroy(file);
      file = NULL;
   }

   size_t len = strlen(output);
   if (len > 0 && output[len - 1] == '\n')
   {
      output[len - 1] = '\0';
   }

   pgmoneta_json_reader_close(reader);
   return output;
}


