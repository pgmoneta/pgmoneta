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
#include <assert.h>
#include <pgmoneta.h>
#include <backup.h>
#include <info.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <stddef.h>
#include <stdint.h>
#include <utils.h>
#include <security.h>

/* system */
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NAME "info"

/**
 * Create a backup information file
 * @param directory The backup directory
 * @param label The label
 * @param status The status
 */
static void __attribute__((unused))
create_info(char* directory, char* label, int status);

static int
file_final_name(char* file, int encryption, int compression, char** finalname);

/**
 * Best effort to split a file path into a relative path and a bare file name
 * a wrapper around `dirname()`
 * @note The function will not modify the input path as we are dealing with a copy
 * @param path The file path
 * @param relative_path [out] The relative path
 * @param bare_file_name [out] The bare file name
 * @return 0 on success, 1 if otherwise
 */
static int
split_file_path(char* path, char** relative_path, char** bare_file_name);

static void
write_info(FILE* sfile, const char* fmt, ...);

static void
create_info(char* directory, char* label, int status);

int
pgmoneta_update_info_annotate(int server, struct backup* backup, char* action, char* key, char* comment)
{
   char* d = NULL;
   char* dir = NULL;
   char* old_comments = NULL;
   char* new_comments = NULL;
   bool found = false;
   bool fail = false;
   struct backup* temp_backup = NULL;

   old_comments = pgmoneta_append(old_comments, backup->comments);

   if (!strcmp("add", action))
   {
      if (old_comments == NULL || strlen(old_comments) == 0)
      {
         new_comments = pgmoneta_append(new_comments, key);
         new_comments = pgmoneta_append(new_comments, "|");
         new_comments = pgmoneta_append(new_comments, comment);
      }
      else
      {
         char* tokens = NULL;
         char* ptr = NULL;

         tokens = pgmoneta_append(tokens, old_comments);
         ptr = strtok(tokens, ",");

         while (ptr != NULL && !fail)
         {
            char tk[INFO_BUFFER_SIZE];
            char tv[INFO_BUFFER_SIZE];
            char* equal = NULL;

            memset(&tk[0], 0, sizeof(tk));
            memset(&tv[0], 0, sizeof(tv));

            equal = strchr(ptr, '|');

            memcpy(&tk[0], ptr, strlen(ptr) - strlen(equal));
            memcpy(&tv[0], equal + 1, strlen(equal) - 1);

            if (strcmp(key, tk))
            {
               new_comments = pgmoneta_append(new_comments, tk);
               new_comments = pgmoneta_append(new_comments, "|");
               new_comments = pgmoneta_append(new_comments, tv);
               found = true;
            }
            else
            {
               fail = true;
            }

            ptr = strtok(NULL, ",");
            if (ptr != NULL)
            {
               new_comments = pgmoneta_append(new_comments, ",");
            }
         }

         if (!fail)
         {
            new_comments = pgmoneta_append(new_comments, ",");
            new_comments = pgmoneta_append(new_comments, key);
            new_comments = pgmoneta_append(new_comments, "|");
            new_comments = pgmoneta_append(new_comments, comment);
            found = true;
         }

         free(tokens);
      }
   }
   else if (!strcmp("update", action))
   {
      if (old_comments == NULL || strlen(old_comments) == 0)
      {
         fail = true;
      }
      else
      {
         char tokens[INFO_BUFFER_SIZE];
         char* ptr = NULL;

         memset(&tokens[0], 0, sizeof(tokens));
         memcpy(&tokens[0], old_comments, strlen(old_comments));

         ptr = strtok(&tokens[0], ",");

         while (ptr != NULL)
         {
            char tk[INFO_BUFFER_SIZE];
            char tv[INFO_BUFFER_SIZE];
            char* equal = NULL;

            memset(&tk[0], 0, sizeof(tk));
            memset(&tv[0], 0, sizeof(tv));

            equal = strchr(ptr, '|');

            memcpy(&tk[0], ptr, strlen(ptr) - strlen(equal));
            memcpy(&tv[0], equal + 1, strlen(equal) - 1);

            if (strcmp(key, tk))
            {
               new_comments = pgmoneta_append(new_comments, tk);
               new_comments = pgmoneta_append(new_comments, "|");
               new_comments = pgmoneta_append(new_comments, tv);
            }
            else
            {
               new_comments = pgmoneta_append(new_comments, ",");
               new_comments = pgmoneta_append(new_comments, key);
               new_comments = pgmoneta_append(new_comments, "|");
               new_comments = pgmoneta_append(new_comments, comment);
               found = true;
            }

            ptr = strtok(NULL, ",");
            if (ptr != NULL)
            {
               new_comments = pgmoneta_append(new_comments, ",");
            }
         }

         if (!found)
         {
            fail = true;
         }
      }
   }
   else if (!strcmp("remove", action))
   {
      if (old_comments == NULL || strlen(old_comments) == 0)
      {
         fail = true;
      }
      else
      {
         char tokens[INFO_BUFFER_SIZE];
         char* ptr = NULL;

         memset(&tokens[0], 0, sizeof(tokens));
         memcpy(&tokens[0], old_comments, strlen(old_comments));

         ptr = strtok(&tokens[0], ",");

         while (ptr != NULL)
         {
            char tk[INFO_BUFFER_SIZE];
            char tv[INFO_BUFFER_SIZE];
            char* equal = NULL;

            memset(&tk[0], 0, sizeof(tk));
            memset(&tv[0], 0, sizeof(tv));

            equal = strchr(ptr, '|');

            memcpy(&tk[0], ptr, strlen(ptr) - strlen(equal));
            memcpy(&tv[0], equal + 1, strlen(equal) - 1);

            if (strcmp(key, tk))
            {
               new_comments = pgmoneta_append(new_comments, tk);
               new_comments = pgmoneta_append(new_comments, "|");
               new_comments = pgmoneta_append(new_comments, tv);
            }
            else
            {
               found = true;
            }

            ptr = strtok(NULL, ",");
            if (ptr != NULL)
            {
               new_comments = pgmoneta_append(new_comments, ",");
            }
         }

         if (!found)
         {
            fail = true;
         }
      }
   }
   else
   {
      fail = true;
   }

   if (new_comments != NULL)
   {
      if (!strcmp(new_comments, ",") || pgmoneta_starts_with(new_comments, ","))
      {
         new_comments = pgmoneta_remove_first(new_comments);

         if (new_comments == NULL)
         {
            fail = true;
         }
      }

      if (pgmoneta_ends_with(new_comments, ","))
      {
         new_comments = pgmoneta_remove_last(new_comments);

         if (new_comments == NULL)
         {
            fail = true;
         }
      }
   }
   else
   {
      new_comments = pgmoneta_append(new_comments, "");
   }

   if (fail)
   {
      free(new_comments);
      new_comments = NULL;

      new_comments = pgmoneta_append(new_comments, backup->comments);
   }

   d = pgmoneta_get_server(server);
   d = pgmoneta_append(d, "backup/");

   dir = pgmoneta_append(dir, d);
   if (!pgmoneta_ends_with(dir, "/"))
   {
      dir = pgmoneta_append(dir, "/");
   }

   if (pgmoneta_load_info(dir, backup->label, &temp_backup))
   {
      pgmoneta_log_error("Unable to find backup in directory %s", dir);
      goto error;
   }

   snprintf(temp_backup->comments, sizeof(temp_backup->comments), "%s", new_comments);
   if (pgmoneta_save_info(dir, temp_backup))
   {
      pgmoneta_log_error("Unable to save backup info for directory %s", dir);
      goto error;
   }

   memset(backup->comments, 0, sizeof(backup->comments));
   memcpy(backup->comments, new_comments, strlen(new_comments));
   free(temp_backup);
   free(d);
   free(dir);
   free(old_comments);
   free(new_comments);

   return 0;
error:
   free(temp_backup);
   free(d);
   free(dir);
   free(old_comments);
   free(new_comments);
   return 1;
}

int
pgmoneta_load_infos(char* directory, int* number_of_backups, struct backup*** backups)
{
   char* d = NULL;
   struct backup** bcks = NULL;
   int number_of_bcks = 0;
   char** dirs = NULL;

#ifdef DEBUG
   assert(directory != NULL);
   assert(strlen(directory) > 0);
#endif

   *number_of_backups = 0;
   *backups = NULL;

   pgmoneta_get_directories(directory, &number_of_bcks, &dirs);

   if (number_of_bcks > 0)
   {
      bcks = (struct backup**)malloc(number_of_bcks * sizeof(struct backup*));

      if (bcks == NULL)
      {
         goto error;
      }

      memset(bcks, 0, number_of_bcks * sizeof(struct backup*));

      for (int i = 0; i < number_of_bcks; i++)
      {
         d = pgmoneta_append(d, directory);

         if (pgmoneta_load_info(d, dirs[i], &bcks[i]))
         {
            pgmoneta_log_error("pgmoneta_load_infos: Unable to load backup for %s", d);
            goto error;
         }

         free(d);
         d = NULL;
      }

      for (int i = 0; i < number_of_bcks; i++)
      {
         free(dirs[i]);
      }
      free(dirs);
   }

   *number_of_backups = number_of_bcks;
   *backups = bcks;

   return 0;

error:

   free(d);

   if (dirs != NULL)
   {
      for (int i = 0; i < number_of_bcks; i++)
      {
         free(dirs[i]);
      }
      free(dirs);
   }

   if (bcks != NULL)
   {
      for (int i = 0; i < number_of_bcks; i++)
      {
         free(bcks[i]);
      }
      free(bcks);
   }

   return 1;
}

int
pgmoneta_load_info(char* directory, char* identifier, struct backup** backup)
{
   char* label = NULL;
   char* fn = NULL;
   char buffer[INFO_BUFFER_SIZE];
   FILE* file = NULL;
   int tbl_idx = 0;
   struct backup* bck = NULL;
   int number_of_backups = 0;
   struct backup** backups = NULL;

   *backup = NULL;

#ifdef DEBUG
   assert(directory != NULL);
   assert(strlen(directory) > 0);
   assert(identifier != NULL);
   assert(strlen(identifier) > 0);
#endif

   if (!strcmp(identifier, "oldest") || !strcmp(identifier, "newest") || !strcmp(identifier, "latest"))
   {
      int number_of_backups = 0;
      struct backup** backups = NULL;

      if (pgmoneta_load_infos(directory, &number_of_backups, &backups))
      {
         goto error;
      }

      if (number_of_backups == 0)
      {
         goto error;
      }

      if (!strcmp(identifier, "oldest"))
      {
         label = pgmoneta_append(label, backups[0]->label);
      }
      else if (!strcmp(identifier, "latest") || !strcmp(identifier, "newest"))
      {
         label = pgmoneta_append(label, backups[number_of_backups - 1]->label);
      }

      for (int i = 0; i < number_of_backups; i++)
      {
         free(backups[i]);
      }
      free(backups);
      backups = NULL;
   }
   else
   {
      label = identifier;
   }

   fn = NULL;
   fn = pgmoneta_append(fn, directory);
   fn = pgmoneta_append(fn, label);
   fn = pgmoneta_append(fn, "/backup.info");

   if (pgmoneta_exists(fn))
   {
      file = fopen(fn, "r");
      if (file == NULL)
      {
         pgmoneta_log_error("Could not open file %s due to %s", fn, strerror(errno));
         errno = 0;
         goto error;
      }
   }

   bck = (struct backup*)malloc(sizeof(struct backup));

   if (bck == NULL)
   {
      goto error;
   }

   memset(bck, 0, sizeof(struct backup));

   bck->valid = VALID_UNKNOWN;

   if (file != NULL)
   {
      while ((fgets(&buffer[0], sizeof(buffer), file)) != NULL)
      {
         char key[INFO_BUFFER_SIZE];
         char value[INFO_BUFFER_SIZE];
         char* ptr = NULL;

         memset(&key[0], 0, sizeof(key));
         memset(&value[0], 0, sizeof(value));

         ptr = strtok(&buffer[0], "=");

         if (ptr == NULL)
         {
            goto error;
         }

         memcpy(&key[0], ptr, strlen(ptr));

         ptr = strtok(NULL, "=");

         if (ptr == NULL)
         {
            goto error;
         }

         memcpy(&value[0], ptr, strlen(ptr) - 1);

         if (!strcmp(INFO_PGMONETA_VERSION, &key[0]))
         {
            memcpy(&bck->version[0], &value[0], strlen(&value[0]));
         }
         else if (!strcmp(INFO_STATUS, &key[0]))
         {
            if (!strcmp("1", &value[0]))
            {
               bck->valid = VALID_TRUE;
            }
            else
            {
               bck->valid = VALID_FALSE;
            }
         }
         else if (!strcmp(INFO_LABEL, &key[0]))
         {
            memcpy(&bck->label[0], &value[0], strlen(&value[0]));
         }
         else if (!strcmp(INFO_WAL, &key[0]))
         {
            memcpy(&bck->wal[0], &value[0], strlen(&value[0]));
         }
         else if (!strcmp(INFO_BACKUP, &key[0]))
         {
            bck->backup_size = strtoul(&value[0], &ptr, 10);
         }
         else if (!strcmp(INFO_RESTORE, &key[0]))
         {
            bck->restore_size = strtoul(&value[0], &ptr, 10);
         }
         else if (!strcmp(INFO_BIGGEST_FILE, &key[0]))
         {
            bck->biggest_file_size = strtoul(&value[0], &ptr, 10);
         }
         else if (!strcmp(INFO_ELAPSED, &key[0]))
         {
            bck->total_elapsed_time = atof(&value[0]);
         }
         else if (!strcmp(INFO_BASEBACKUP_ELAPSED, &key[0]))
         {
            bck->basebackup_elapsed_time = atof(&value[0]);
         }
         else if (!strcmp(INFO_MANIFEST_ELAPSED, &key[0]))
         {
            bck->manifest_elapsed_time = atof(&value[0]);
         }
         else if (!strcmp(INFO_COMPRESSION_ZSTD_ELAPSED, &key[0]))
         {
            bck->compression_zstd_elapsed_time = atof(&value[0]);
         }
         else if (!strcmp(INFO_COMPRESSION_BZIP2_ELAPSED, &key[0]))
         {
            bck->compression_bzip2_elapsed_time = atof(&value[0]);
         }
         else if (!strcmp(INFO_COMPRESSION_GZIP_ELAPSED, &key[0]))
         {
            bck->compression_gzip_elapsed_time = atof(&value[0]);
         }
         else if (!strcmp(INFO_COMPRESSION_LZ4_ELAPSED, &key[0]))
         {
            bck->compression_lz4_elapsed_time = atof(&value[0]);
         }
         else if (!strcmp(INFO_ENCRYPTION_ELAPSED, &key[0]))
         {
            bck->encryption_elapsed_time = atof(&value[0]);
         }
         else if (!strcmp(INFO_LINKING_ELAPSED, &key[0]))
         {
            bck->linking_elapsed_time = atof(&value[0]);
         }
         else if (!strcmp(INFO_REMOTE_SSH_ELAPSED, &key[0]))
         {
            bck->remote_ssh_elapsed_time = atof(&value[0]);
         }
         else if (!strcmp(INFO_REMOTE_AZURE_ELAPSED, &key[0]))
         {
            bck->remote_azure_elapsed_time = atof(&value[0]);
         }
         else if (!strcmp(INFO_REMOTE_S3_ELAPSED, &key[0]))
         {
            bck->remote_s3_elapsed_time = atof(&value[0]);
         }
         else if (!strcmp(INFO_MAJOR_VERSION, &key[0]))
         {
            bck->major_version = atoi(&value[0]);
         }
         else if (!strcmp(INFO_MINOR_VERSION, &key[0]))
         {
            bck->minor_version = atoi(&value[0]);
         }
         else if (!strcmp(INFO_KEEP, &key[0]))
         {
            bck->keep = atoi(&value[0]) == 1 ? true : false;
         }
         else if (!strcmp(INFO_TABLESPACES, &key[0]))
         {
            bck->number_of_tablespaces = strtoul(&value[0], &ptr, 10);
         }
         else if (pgmoneta_starts_with(&key[0], "TABLESPACE_OID"))
         {
            memcpy(&bck->tablespaces_oids[tbl_idx], &value[0], strlen(&value[0]));
         }
         else if (pgmoneta_starts_with(&key[0], "TABLESPACE_PATH"))
         {
            memcpy(&bck->tablespaces_paths[tbl_idx], &value[0], strlen(&value[0]));
            /* This one is last */
            tbl_idx++;
         }
         else if (pgmoneta_starts_with(&key[0], "TABLESPACE"))
         {
            memcpy(&bck->tablespaces[tbl_idx], &value[0], strlen(&value[0]));
         }
         else if (pgmoneta_starts_with(&key[0], INFO_START_WALPOS))
         {
            sscanf(&value[0], "%X/%X", &bck->start_lsn_hi32, &bck->start_lsn_lo32);
         }
         else if (pgmoneta_starts_with(&key[0], INFO_END_WALPOS))
         {
            sscanf(&value[0], "%X/%X", &bck->end_lsn_hi32, &bck->end_lsn_lo32);
         }
         else if (pgmoneta_starts_with(&key[0], INFO_CHKPT_WALPOS))
         {
            sscanf(&value[0], "%X/%X", &bck->checkpoint_lsn_hi32, &bck->checkpoint_lsn_lo32);
         }
         else if (pgmoneta_starts_with(&key[0], INFO_START_TIMELINE))
         {
            bck->start_timeline = atoi(&value[0]);
         }
         else if (pgmoneta_starts_with(&key[0], INFO_END_TIMELINE))
         {
            bck->end_timeline = atoi(&value[0]);
         }
         else if (pgmoneta_starts_with(&key[0], INFO_COMMENTS))
         {
            memcpy(&bck->comments[0], &value[0], strlen(&value[0]));
         }
         else if (pgmoneta_starts_with(&key[0], INFO_EXTRA))
         {
            memcpy(&bck->comments[0], &value[0], strlen(&value[0]));
         }
         else if (pgmoneta_starts_with(&key[0], INFO_COMPRESSION))
         {
            bck->compression = atoi(&value[0]);
         }
         else if (pgmoneta_starts_with(&key[0], INFO_ENCRYPTION))
         {
            bck->encryption = atoi(&value[0]);
         }
         else if (pgmoneta_starts_with(&key[0], INFO_TYPE))
         {
            bck->type = atoi(&value[0]);
         }
         else if (pgmoneta_starts_with(&key[0], INFO_PARENT))
         {
            memcpy(&bck->parent_label[0], &value[0], strlen(&value[0]));
         }
      }
   }

   *backup = bck;

   if (file != NULL)
   {
      fsync(fileno(file));
      fclose(file);
   }

   free(fn);

   if (backups != NULL)
   {
      for (int i = 0; i < number_of_backups; i++)
      {
         free(backups[i]);
      }
      free(backups);
   }

   if (identifier != label)
   {
      free(label);
   }

   return 0;

error:

   if (backups != NULL)
   {
      for (int i = 0; i < number_of_backups; i++)
      {
         free(backups[i]);
      }
      free(backups);
   }

   free(bck);

   if (file != NULL)
   {
      fsync(fileno(file));
      fclose(file);
   }

   free(fn);

   if (identifier != label)
   {
      free(label);
   }

   return 1;
}

int
pgmoneta_get_number_of_valid_backups(int server)
{
   char* server_path = NULL;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   int result = 0;

   server_path = pgmoneta_get_server_backup(server);
   if (server_path == NULL)
   {
      goto error;
   }

   if (pgmoneta_load_infos(server_path, &number_of_backups, &backups))
   {
      goto error;
   }

   for (int i = 0; i < number_of_backups; i++)
   {
      if (pgmoneta_is_backup_struct_valid(server, backups[i]))
      {
         result++;
      }
   }

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(server_path);

   return result;

error:

   return 0;
}

int
pgmoneta_get_backup_parent(int server, struct backup* backup, struct backup** parent)
{
   char* d = NULL;
   struct backup* p = NULL;

   *parent = NULL;

   if (backup == NULL)
   {
      goto error;
   }

   if (backup->type == TYPE_FULL || strlen(backup->parent_label) == 0)
   {
      goto error;
   }

   d = pgmoneta_get_server_backup(server);

   if (pgmoneta_load_info(d, backup->parent_label, &p))
   {
      goto error;
   }

   if (p == NULL)
   {
      goto error;
   }

   *parent = p;

   free(d);

   return 0;

error:

   free(d);

   return 1;
}

int
pgmoneta_get_backup_root(int server, struct backup* backup, struct backup** root)
{
   struct backup* p = NULL;
   struct backup* bck = NULL;

   *root = NULL;

   if (backup == NULL)
   {
      goto error;
   }

   if (backup->type == TYPE_FULL || strlen(backup->parent_label) == 0)
   {
      goto error;
   }

   if (pgmoneta_get_backup_parent(server, backup, &p))
   {
      goto error;
   }

   while (p->type != TYPE_FULL)
   {
      if (pgmoneta_get_backup_parent(server, p, &bck))
      {
         goto error;
      }
      free(p);
      p = bck;
   }

   *root = p;

   return 0;

error:
   free(p);

   return 1;
}

int
pgmoneta_get_backup_child(int server, struct backup* backup, struct backup** child)
{
   char* d = NULL;
   char* c_identifier = NULL;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   struct backup* c = NULL;

   *child = NULL;

   if (backup == NULL)
   {
      goto error;
   }

   d = pgmoneta_get_server_backup(server);

   if (pgmoneta_load_infos(d, &number_of_backups, &backups))
   {
      goto error;
   }

   for (int j = 0; c_identifier == NULL && j < number_of_backups; j++)
   {
      if (!strcmp(backup->label, backups[j]->parent_label))
      {
         c_identifier = pgmoneta_append(c_identifier, backups[j]->label);
      }
   }

   if (c_identifier != NULL)
   {
      if (pgmoneta_load_info(d, c_identifier, &c))
      {
         goto error;
      }

      *child = c;
   }

   free(d);
   free(c_identifier);

   for (int j = 0; j < number_of_backups; j++)
   {
      free(backups[j]);
   }
   free(backups);

   return 0;

error:

   free(d);
   free(c_identifier);

   for (int j = 0; j < number_of_backups; j++)
   {
      free(backups[j]);
   }
   free(backups);

   return 1;
}

void
pgmoneta_info_request(SSL* ssl, int client_fd, int server,
                      uint8_t compression, uint8_t encryption,
                      struct json* payload)
{
   char* identifier = NULL;
   char* d = NULL;
   char* elapsed = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   char* en = NULL;
   int ec = -1;
   int32_t number_of_backups = 0;
   struct backup** backups = NULL;
   struct backup* bck = NULL;
   struct json* tablespaces = NULL;
   struct json* req = NULL;
   struct json* response = NULL;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   d = pgmoneta_get_server_backup(server);

   number_of_backups = 0;
   backups = NULL;

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   identifier = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_BACKUP);

   pgmoneta_load_infos(d, &number_of_backups, &backups);

   if (number_of_backups == 0)
   {
      ec = MANAGEMENT_ERROR_INFO_NOBACKUP;
      pgmoneta_log_warn("Info: No backups");
      goto error;
   }

   if (!strcmp("oldest", identifier))
   {
      bck = backups[0];
   }
   else if (!strcmp("newest", identifier) || !strcmp("latest", identifier))
   {
      bck = backups[number_of_backups - 1];
   }
   else
   {
      for (int i = 0; bck == NULL && i < number_of_backups; i++)
      {
         if (!strcmp(backups[i]->label, identifier))
         {
            bck = backups[i];
         }
      }
   }

   if (bck == NULL)
   {
      ec = MANAGEMENT_ERROR_INFO_NOBACKUP;
      pgmoneta_log_warn("Info: No identifier for %s/%s", config->common.servers[server].name, identifier);
      goto error;
   }

   if (pgmoneta_management_create_response(payload, server, &response))
   {
      ec = MANAGEMENT_ERROR_ALLOCATION;
      pgmoneta_log_error("Info: Allocation error");
      goto error;
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)bck->label, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_WAL, (uintptr_t)bck->wal, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP_SIZE, (uintptr_t)bck->backup_size, ValueUInt64);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_RESTORE_SIZE, (uintptr_t)bck->restore_size, ValueUInt64);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BIGGEST_FILE_SIZE, (uintptr_t)bck->biggest_file_size, ValueUInt64);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_ELAPSED, (uintptr_t)bck->total_elapsed_time, ValueFloat);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_MAJOR_VERSION, (uintptr_t)bck->major_version, ValueInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_MINOR_VERSION, (uintptr_t)bck->minor_version, ValueInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_KEEP, (uintptr_t)bck->keep, ValueBool);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_VALID, (uintptr_t)bck->valid, ValueInt8);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_NUMBER_OF_TABLESPACES, (uintptr_t)bck->number_of_tablespaces, ValueUInt64);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_COMPRESSION, (uintptr_t)bck->compression, ValueInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_ENCRYPTION, (uintptr_t)bck->encryption, ValueInt32);

   if (pgmoneta_json_create(&tablespaces))
   {
      ec = MANAGEMENT_ERROR_ALLOCATION;
      pgmoneta_log_error("Info: Allocation error");
      goto error;
   }

   for (uint64_t i = 0; i < bck->number_of_tablespaces; i++)
   {
      struct json* tbl = NULL;

      if (pgmoneta_json_create(&tbl))
      {
         ec = MANAGEMENT_ERROR_ALLOCATION;
         pgmoneta_log_error("Info: Allocation error");
         goto error;
      }

      pgmoneta_json_put(tbl, MANAGEMENT_ARGUMENT_TABLESPACE_NAME, (uintptr_t)bck->tablespaces[i], ValueString);

      pgmoneta_json_append(tablespaces, (uintptr_t)tbl, ValueJSON);
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_TABLESPACES, (uintptr_t)tablespaces, ValueJSON);

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_START_HILSN, (uintptr_t)bck->start_lsn_hi32, ValueUInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_START_LOLSN, (uintptr_t)bck->start_lsn_lo32, ValueUInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_END_HILSN, (uintptr_t)bck->end_lsn_hi32, ValueUInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_END_LOLSN, (uintptr_t)bck->end_lsn_lo32, ValueUInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_CHECKPOINT_HILSN, (uintptr_t)bck->checkpoint_lsn_hi32, ValueUInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_CHECKPOINT_LOLSN, (uintptr_t)bck->checkpoint_lsn_lo32, ValueUInt32);

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_START_TIMELINE, (uintptr_t)bck->start_timeline, ValueUInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_END_TIMELINE, (uintptr_t)bck->end_timeline, ValueUInt32);

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_COMMENTS, (uintptr_t)bck->comments, ValueString);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(ssl, client_fd, start_t, end_t, compression, encryption, payload))
   {
      ec = MANAGEMENT_ERROR_INFO_NETWORK;
      pgmoneta_log_error("Info: Error sending response");
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("Info: %s/%s (Elapsed: %s)", config->common.servers[server].name, bck->label, elapsed);

   pgmoneta_json_destroy(payload);

   for (int j = 0; j < number_of_backups; j++)
   {
      free(backups[j]);
   }
   free(backups);

   free(d);
   free(elapsed);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(0);

error:

   pgmoneta_management_response_error(ssl, client_fd, config->common.servers[server].name,
                                      ec != -1 ? ec : MANAGEMENT_ERROR_INFO_ERROR, en != NULL ? en : NAME,
                                      compression, encryption, payload);

   pgmoneta_json_destroy(payload);

   for (int j = 0; j < number_of_backups; j++)
   {
      free(backups[j]);
   }
   free(backups);

   free(d);

   free(elapsed);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(1);
}

void
pgmoneta_annotate_request(SSL* ssl, int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* backup = NULL;
   char* action = NULL;
   char* key = NULL;
   char* comment = NULL;
   char* d = NULL;
   char* elapsed = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   char* en = NULL;
   int ec = -1;
   int32_t number_of_backups = 0;
   struct backup** backups = NULL;
   struct backup* bck = NULL;
   struct json* tablespaces = NULL;
   struct json* req = NULL;
   struct json* response = NULL;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   d = pgmoneta_get_server_backup(server);

   number_of_backups = 0;
   backups = NULL;

   pgmoneta_load_infos(d, &number_of_backups, &backups);

   if (number_of_backups == 0)
   {
      ec = MANAGEMENT_ERROR_ANNOTATE_NOBACKUP;
      pgmoneta_log_warn("Annotate: No backups");
      goto error;
   }

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   backup = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_BACKUP);
   action = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_ACTION);
   key = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_KEY);
   comment = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_COMMENT);

   if (!strcmp("oldest", backup))
   {
      bck = backups[0];
   }
   else if (!strcmp("newest", backup) || !strcmp("latest", backup))
   {
      bck = backups[number_of_backups - 1];
   }
   else
   {
      for (int i = 0; bck == NULL && i < number_of_backups; i++)
      {
         if (!strcmp(backups[i]->label, backup))
         {
            bck = backups[i];
         }
      }
   }

   if (bck == NULL)
   {
      ec = MANAGEMENT_ERROR_ANNOTATE_NOBACKUP;
      pgmoneta_log_warn("Annotate: No backup (%s)", backup);
      goto error;
   }

   if (!pgmoneta_compare_string("add", action) &&
       !pgmoneta_compare_string("update", action) &&
       !pgmoneta_compare_string("remove", action))
   {
      ec = MANAGEMENT_ERROR_ANNOTATE_UNKNOWN_ACTION;
      pgmoneta_log_error("Annotate: unknown action (%s)", action);
      goto error;
   }

   if (pgmoneta_update_info_annotate(server, bck, action, key, comment))
   {
      ec = MANAGEMENT_ERROR_ANNOTATE_FAILED;
      pgmoneta_log_error("Annotate: Failed annotate (%s)", backup);
      goto error;
   }

   if (pgmoneta_management_create_response(payload, server, &response))
   {
      ec = MANAGEMENT_ERROR_ALLOCATION;
      pgmoneta_log_error("Annotate: Allocation error");
      goto error;
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)bck->label, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_WAL, (uintptr_t)bck->wal, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP_SIZE, (uintptr_t)bck->backup_size, ValueUInt64);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_RESTORE_SIZE, (uintptr_t)bck->restore_size, ValueUInt64);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BIGGEST_FILE_SIZE, (uintptr_t)bck->biggest_file_size, ValueUInt64);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_ELAPSED, (uintptr_t)bck->total_elapsed_time, ValueFloat);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_MAJOR_VERSION, (uintptr_t)bck->major_version, ValueInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_MINOR_VERSION, (uintptr_t)bck->minor_version, ValueInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_KEEP, (uintptr_t)bck->keep, ValueBool);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_VALID, (uintptr_t)bck->valid, ValueInt8);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_NUMBER_OF_TABLESPACES, (uintptr_t)bck->number_of_tablespaces, ValueUInt64);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_COMPRESSION, (uintptr_t)bck->compression, ValueInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_ENCRYPTION, (uintptr_t)bck->encryption, ValueInt32);

   if (pgmoneta_json_create(&tablespaces))
   {
      ec = MANAGEMENT_ERROR_ALLOCATION;
      pgmoneta_log_error("Annotate: Allocation error");
      goto error;
   }

   for (uint64_t i = 0; i < bck->number_of_tablespaces; i++)
   {
      struct json* tbl = NULL;

      if (pgmoneta_json_create(&tbl))
      {
         ec = MANAGEMENT_ERROR_ALLOCATION;
         pgmoneta_log_error("Annotate: Allocation error");
         goto error;
      }

      pgmoneta_json_put(tbl, MANAGEMENT_ARGUMENT_TABLESPACE_NAME, (uintptr_t)bck->tablespaces[i], ValueString);

      pgmoneta_json_append(tablespaces, (uintptr_t)tbl, ValueJSON);
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_TABLESPACES, (uintptr_t)tablespaces, ValueJSON);

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_START_HILSN, (uintptr_t)bck->start_lsn_hi32, ValueUInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_START_LOLSN, (uintptr_t)bck->start_lsn_lo32, ValueUInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_END_HILSN, (uintptr_t)bck->end_lsn_hi32, ValueUInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_END_LOLSN, (uintptr_t)bck->end_lsn_lo32, ValueUInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_CHECKPOINT_HILSN, (uintptr_t)bck->checkpoint_lsn_hi32, ValueUInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_CHECKPOINT_LOLSN, (uintptr_t)bck->checkpoint_lsn_lo32, ValueUInt32);

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_START_TIMELINE, (uintptr_t)bck->start_timeline, ValueUInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_END_TIMELINE, (uintptr_t)bck->end_timeline, ValueUInt32);

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_COMMENTS, (uintptr_t)bck->comments, ValueString);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(ssl, client_fd, start_t, end_t, compression, encryption, payload))
   {
      ec = MANAGEMENT_ERROR_ANNOTATE_NETWORK;
      pgmoneta_log_error("Annotate: Error sending response");
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("Annotate: %s/%s (Elapsed: %s)", config->common.servers[server].name, bck->label, elapsed);

   pgmoneta_json_destroy(payload);

   for (int j = 0; j < number_of_backups; j++)
   {
      free(backups[j]);
   }
   free(backups);

   free(d);
   free(elapsed);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(0);

error:

   pgmoneta_management_response_error(ssl, client_fd, config->common.servers[server].name,
                                      ec != -1 ? ec : MANAGEMENT_ERROR_ANNOTATE_ERROR, en != NULL ? en : NAME,
                                      compression, encryption, payload);

   pgmoneta_json_destroy(payload);

   for (int j = 0; j < number_of_backups; j++)
   {
      free(backups[j]);
   }
   free(backups);

   free(d);

   free(elapsed);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(1);
}

int
pgmoneta_save_info(char* directory, struct backup* backup)
{
   char buffer[INFO_BUFFER_SIZE];
   char* bck_root_dir = NULL;
   char* bck_info_file = NULL;
   FILE* sfile = NULL;

#ifdef DEBUG
   assert(directory != NULL);
   assert(strlen(directory) > 0);
   assert(backup != NULL);
#endif

   bck_info_file = pgmoneta_append(bck_info_file, directory);
   bck_info_file = pgmoneta_append(bck_info_file, backup->label);
   bck_info_file = pgmoneta_append(bck_info_file, "/backup.info");

   sfile = fopen(bck_info_file, "w");
   if (sfile == NULL)
   {
      pgmoneta_log_error("Could not open file %s due to %s", bck_info_file, strerror(errno));
      errno = 0;
      goto error;
   }

   write_info(sfile, "%s=%s\n", INFO_PGMONETA_VERSION, VERSION);
   write_info(sfile, "%s=%d\n", INFO_STATUS, backup->valid == VALID_TRUE ? 1 : 0);
   write_info(sfile, "%s=%s\n", INFO_LABEL, backup->label);
   write_info(sfile, "%s=%s\n", INFO_WAL, backup->wal);
   write_info(sfile, "%s=%lu\n", INFO_BACKUP, backup->backup_size);
   write_info(sfile, "%s=%lu\n", INFO_RESTORE, backup->restore_size);
   write_info(sfile, "%s=%lu\n", INFO_BIGGEST_FILE, backup->biggest_file_size);
   write_info(sfile, "%s=%.4f\n", INFO_ELAPSED, backup->total_elapsed_time);
   write_info(sfile, "%s=%.4f\n", INFO_BASEBACKUP_ELAPSED, backup->basebackup_elapsed_time);
   write_info(sfile, "%s=%.4f\n", INFO_COMPRESSION_ZSTD_ELAPSED, backup->compression_zstd_elapsed_time);
   write_info(sfile, "%s=%.4f\n", INFO_COMPRESSION_GZIP_ELAPSED, backup->compression_gzip_elapsed_time);
   write_info(sfile, "%s=%.4f\n", INFO_COMPRESSION_BZIP2_ELAPSED, backup->compression_bzip2_elapsed_time);
   write_info(sfile, "%s=%.4f\n", INFO_COMPRESSION_LZ4_ELAPSED, backup->compression_lz4_elapsed_time);
   write_info(sfile, "%s=%.4f\n", INFO_ENCRYPTION_ELAPSED, backup->encryption_elapsed_time);
   write_info(sfile, "%s=%.4f\n", INFO_LINKING_ELAPSED, backup->linking_elapsed_time);
   write_info(sfile, "%s=%.4f\n", INFO_MANIFEST_ELAPSED, backup->manifest_elapsed_time);
   write_info(sfile, "%s=%.4f\n", INFO_REMOTE_SSH_ELAPSED, backup->remote_ssh_elapsed_time);
   write_info(sfile, "%s=%.4f\n", INFO_REMOTE_S3_ELAPSED, backup->remote_s3_elapsed_time);
   write_info(sfile, "%s=%.4f\n", INFO_REMOTE_AZURE_ELAPSED, backup->remote_azure_elapsed_time);
   write_info(sfile, "%s=%d\n", INFO_MAJOR_VERSION, backup->major_version);
   write_info(sfile, "%s=%d\n", INFO_MINOR_VERSION, backup->minor_version);
   write_info(sfile, "%s=%d\n", INFO_KEEP, backup->keep ? 1 : 0);
   write_info(sfile, "%s=%lu\n", INFO_TABLESPACES, backup->number_of_tablespaces);
   write_info(sfile, "%s=%d\n", INFO_COMPRESSION, backup->compression);
   write_info(sfile, "%s=%d\n", INFO_ENCRYPTION, backup->encryption);

   for (uint64_t i = 0; i < backup->number_of_tablespaces; i++)
   {
      write_info(sfile, "TABLESPACE%lu=%s\n", i + 1, backup->tablespaces[i]);
      write_info(sfile, "TABLESPACE_OID%lu=%s\n", i + 1, backup->tablespaces_oids[i]);
      write_info(sfile, "TABLESPACE_PATH%lu=%s\n", i + 1, backup->tablespaces_paths[i]);
   }

   write_info(sfile, "%s=%X/%X\n", INFO_START_WALPOS, backup->start_lsn_hi32, backup->start_lsn_lo32);
   write_info(sfile, "%s=%X/%X\n", INFO_END_WALPOS, backup->end_lsn_hi32, backup->end_lsn_lo32);
   write_info(sfile, "%s=%X/%X\n", INFO_CHKPT_WALPOS, backup->checkpoint_lsn_hi32, backup->checkpoint_lsn_lo32);

   write_info(sfile, "%s=%u\n", INFO_START_TIMELINE, backup->start_timeline);
   write_info(sfile, "%s=%u\n", INFO_END_TIMELINE, backup->end_timeline);
   write_info(sfile, "%s=%d\n", INFO_TYPE, backup->type);
   write_info(sfile, "%s=%s\n", INFO_PARENT, backup->parent_label);
   write_info(sfile, "%s=%s\n", INFO_COMMENTS, backup->comments);

   memset(&buffer[0], 0, sizeof(buffer));
   snprintf(&buffer[0], sizeof(buffer), "%s=%.1024s\n", INFO_EXTRA, backup->extra);
   fputs(&buffer[0], sfile);

   pgmoneta_permission(bck_info_file, 6, 0, 0);

   fsync(fileno(sfile));
   fclose(sfile);

   bck_root_dir = pgmoneta_append(bck_root_dir, directory);
   bck_root_dir = pgmoneta_append(bck_root_dir, backup->label);
   pgmoneta_log_trace("Updating SHA512 for %s", bck_root_dir);
   pgmoneta_update_sha512(bck_root_dir, "backup.info");

   free(bck_root_dir);
   free(bck_info_file);
   return 0;

error:

   if (sfile != NULL)
   {
      fclose(sfile);
   }

   free(bck_root_dir);
   free(bck_info_file);
   return 1;
}

int
pgmoneta_rfile_create(int server, char* label, char* relative_dir, char* base_file_name, int encryption, int compression, struct rfile** rfile)
{
   struct rfile* rf = NULL;
   char* extracted_file_path = NULL;
   char* final_relative_path = NULL;
   char base_relative_path[MAX_PATH];
   FILE* fp = NULL;

   memset(base_relative_path, 0, MAX_PATH);
   if (pgmoneta_ends_with(relative_dir, "/"))
   {
      snprintf(base_relative_path, MAX_PATH, "%s%s", relative_dir, base_file_name);
   }
   else
   {
      snprintf(base_relative_path, MAX_PATH, "%s/%s", relative_dir, base_file_name);
   }

   // try both base and final relative path
   if (pgmoneta_extract_backup_file(server, label, base_relative_path, NULL, &extracted_file_path))
   {
      free(extracted_file_path);
      extracted_file_path = NULL;
      file_final_name(base_relative_path, encryption, compression, &final_relative_path);
      if (pgmoneta_extract_backup_file(server, label, final_relative_path, NULL, &extracted_file_path))
      {
         goto error;
      }
   }
   fp = fopen(extracted_file_path, "r");

   if (fp == NULL)
   {
      goto error;
   }
   rf = (struct rfile*) malloc(sizeof(struct rfile));
   memset(rf, 0, sizeof(struct rfile));

   rf->fp = fp;
   rf->filepath = extracted_file_path;
   *rfile = rf;

   free(final_relative_path);
   return 0;

error:
   free(extracted_file_path);
   free(final_relative_path);
   pgmoneta_rfile_destroy(rf);
   return 1;
}

void
pgmoneta_rfile_destroy(struct rfile* rf)
{
   if (rf == NULL)
   {
      return;
   }
   if (rf->fp != NULL)
   {
      fclose(rf->fp);
   }
   if (rf->filepath != NULL)
   {
      // this is the extracted file, we should delete it
      pgmoneta_delete_file(rf->filepath, NULL);
   }

   free(rf->filepath);
   free(rf->relative_block_numbers);
   free(rf);
}

int
pgmoneta_incremental_rfile_initialize(int server, char* label, char* relative_dir, char* base_file_name, int encryption, int compression, struct rfile** rfile)
{
   uint32_t magic = 0;
   uint32_t nread = 0;
   struct rfile* rf = NULL;
   struct main_configuration* config;
   size_t relsegsz = 0;
   size_t blocksz = 0;

   config = (struct main_configuration*)shmem;

   relsegsz = config->common.servers[server].relseg_size;
   blocksz = config->common.servers[server].block_size;

   /*
    * Header structure:
    * magic number(uint32)
    * num blocks (number of changed blocks, uint32)
    * truncation block length (uint32)
    * relative_block_numbers (uint32 * (num blocks))
    */

   // create rfile after file is opened successfully
   if (pgmoneta_rfile_create(server, label, relative_dir, base_file_name, encryption, compression, &rf))
   {
      pgmoneta_log_error("rfile initialize: failed to open incremental backup (label %s) file at %s/%s", label, relative_dir, base_file_name);
      goto error;
   }

   // read magic number from header
   nread = fread(&magic, 1, sizeof(uint32_t), rf->fp);
   if (nread != sizeof(uint32_t))
   {
      pgmoneta_log_error("rfile initialize: incomplete file header at %s, cannot read magic number", rf->filepath);
      goto error;
   }

   if (magic != INCREMENTAL_MAGIC)
   {
      pgmoneta_log_error("rfile initialize: incorrect magic number, getting %X, expecting %X", magic, INCREMENTAL_MAGIC);
      goto error;
   }

   // read number of blocks
   nread = fread(&rf->num_blocks, 1, sizeof(uint32_t), rf->fp);
   if (nread != sizeof(uint32_t))
   {
      pgmoneta_log_error("rfile initialize: incomplete file header at %s%s, cannot read block count", relative_dir, base_file_name);
      goto error;
   }
   if (rf->num_blocks > relsegsz)
   {
      pgmoneta_log_error("rfile initialize: file has %d blocks which is more than server's segment size", rf->num_blocks);
      goto error;
   }

   // read truncation block length
   nread = fread(&rf->truncation_block_length, 1, sizeof(uint32_t), rf->fp);
   if (nread != sizeof(uint32_t))
   {
      pgmoneta_log_error("rfile initialize: incomplete file header at %s%s, cannot read truncation block length", relative_dir, base_file_name);
      goto error;
   }
   if (rf->truncation_block_length > relsegsz)
   {
      pgmoneta_log_error("rfile initialize: file has truncation block length of %d which is more than server's segment size", rf->truncation_block_length);
      goto error;
   }

   if (rf->num_blocks > 0)
   {
      rf->relative_block_numbers = malloc(sizeof(uint32_t) * rf->num_blocks);
      nread = fread(rf->relative_block_numbers, sizeof(uint32_t), rf->num_blocks, rf->fp);
      if (nread != rf->num_blocks)
      {
         pgmoneta_log_error("rfile initialize: incomplete file header at %s, cannot read relative block numbers", rf->filepath);
         goto error;
      }
   }

   // magic + block num + truncation block length + relative block numbers
   rf->header_length = sizeof(uint32_t) * (1 + 1 + 1 + rf->num_blocks);
   // round header length to multiple of block size, since the actual file data are aligned
   // only needed when the file actually has data
   if (rf->num_blocks > 0 && rf->header_length % blocksz != 0)
   {
      rf->header_length += (blocksz - (rf->header_length % blocksz));
   }

   *rfile = rf;
   return 0;
error:
   // contains fp closing logic
   pgmoneta_rfile_destroy(rf);
   return 1;
}

int
pgmoneta_extract_backup_file(int server, char* label, char* relative_file_path, char* target_directory, char** target_file)
{
   char* from = NULL;
   char* to = NULL;

   *target_file = NULL;

   from = pgmoneta_get_server_backup_identifier_data(server, label);

   if (!pgmoneta_ends_with(from, "/"))
   {
      from = pgmoneta_append_char(from, '/');
   }
   from = pgmoneta_append(from, relative_file_path);

   if (!pgmoneta_exists(from))
   {
      goto error;
   }

   if (target_directory == NULL || strlen(target_directory) == 0)
   {
      to = pgmoneta_get_server_workspace(server);
      to = pgmoneta_append(to, label);
      to = pgmoneta_append(to, "/");
   }
   else
   {
      to = pgmoneta_append(to, target_directory);
   }

   if (!pgmoneta_ends_with(to, "/"))
   {
      to = pgmoneta_append_char(to, '/');
   }
   to = pgmoneta_append(to, relative_file_path);

   if (pgmoneta_copy_and_extract_file(from, &to))
   {
      goto error;
   }

   pgmoneta_log_trace("Extract: %s -> %s", from, to);

   *target_file = to;

   free(from);

   return 0;

error:

   free(from);
   free(to);

   return 1;
}

int
pgmoneta_backup_size(int server, char* label, unsigned long* size, uint64_t* biggest_file_size)
{
   struct json* manifest_read = NULL;
   struct json* files = NULL;
   struct main_configuration* config = NULL;
   struct json_iterator* iter = NULL;
   char* manifest_path = NULL;
   unsigned long sz = 0;
   uint64_t biggest_file_sz = 0;

   config = (struct main_configuration*)shmem;

   // read and traverse the manifest of the incremental backup
   manifest_path = pgmoneta_get_server_backup_identifier_data(server, label);
   manifest_path = pgmoneta_append(manifest_path, "backup_manifest");
   if (pgmoneta_json_read_file(manifest_path, &manifest_read))
   {
      pgmoneta_log_error("Unable to read manifest %s", manifest_path);
      goto error;
   }

   files = (struct json*)pgmoneta_json_get(manifest_read, MANIFEST_FILES);

   if (files == NULL)
   {
      goto error;
   }

   pgmoneta_json_iterator_create(files, &iter);
   while (pgmoneta_json_iterator_next(iter))
   {
      struct json* file = NULL;
      char* file_path = NULL;
      uint64_t file_size = 0;

      file = (struct json*)pgmoneta_value_data(iter->value);
      file_path = (char*)pgmoneta_json_get(file, "Path");
      /* for incremental files get the `truncated_block_length` */
      if (pgmoneta_is_incremental_path(file_path))
      {
         struct rfile* rf = NULL;
         uint32_t block_length = 0;
         char* relative_path = NULL;
         char* bare_file_name = NULL;

         if (split_file_path(file_path, &relative_path, &bare_file_name))
         {
            pgmoneta_log_error("Unable to split file path %s", file_path);
            goto error;
         }

         if (pgmoneta_incremental_rfile_initialize(server, label, relative_path, bare_file_name, ENCRYPTION_NONE, COMPRESSION_NONE, &rf))
         {
            pgmoneta_log_error("Unable to create rfile %s", bare_file_name);
            goto error;
         }
         block_length = rf->truncation_block_length;
         for (uint32_t i = 0; i < rf->num_blocks; i++)
         {
            if (rf->relative_block_numbers[i] >= block_length)
            {
               block_length = rf->relative_block_numbers[i] + 1;
            }
         }

         if (block_length == 0)
         {
            pgmoneta_log_error("Unable to find block length for %s", bare_file_name);
            goto error;
         }
         file_size = block_length * config->common.servers[server].block_size;
         pgmoneta_rfile_destroy(rf);
         free(relative_path);
         free(bare_file_name);
      }
      /* for non-incremental files get the file size from manifest itself */
      else
      {
         file_size = (uint64_t)pgmoneta_json_get(file, "Size");
      }

      if (file_size > biggest_file_sz)
      {
         biggest_file_sz = file_size;
      }
      sz += file_size;
   }
   pgmoneta_json_iterator_destroy(iter);

   *size = sz;
   *biggest_file_size = biggest_file_sz;

   pgmoneta_json_destroy(manifest_read);
   free(manifest_path);
   return 0;

error:
   pgmoneta_json_destroy(manifest_read);
   free(manifest_path);
   return 1;
}

int
pgmoneta_sort_backups(struct backup** backups, int number_of_backups, bool desc)
{
   int comp_result;

   if (backups == NULL)
   {
      goto error;
   }

   for (int i = 0; i < number_of_backups - 1; i++)
   {
      for (int j = i + 1; j < number_of_backups; j++)
      {
         comp_result = strcmp(backups[i]->label, backups[j]->label);

         // Swap if needed based on sort order
         if ((desc && comp_result < 0) || (!desc && comp_result > 0))
         {
            struct backup* temp = backups[i];
            backups[i] = backups[j];
            backups[j] = temp;
         }
      }
   }
   return 0;

error:
   return 1;
}

static int
file_final_name(char* file, int encryption, int compression, char** finalname)
{
   char* final = NULL;

   *finalname = NULL;
   if (file == NULL)
   {
      goto error;
   }

   final = pgmoneta_append(final, file);
   if (compression == COMPRESSION_CLIENT_GZIP || compression == COMPRESSION_SERVER_GZIP)
   {
      final = pgmoneta_append(final, ".gz");
   }
   else if (compression == COMPRESSION_CLIENT_ZSTD || compression == COMPRESSION_SERVER_ZSTD)
   {
      final = pgmoneta_append(final, ".zstd");
   }
   else if (compression == COMPRESSION_CLIENT_LZ4 || compression == COMPRESSION_SERVER_LZ4)
   {
      final = pgmoneta_append(final, ".lz4");
   }
   else if (compression == COMPRESSION_CLIENT_BZIP2)
   {
      final = pgmoneta_append(final, ".bz2");
   }

   if (encryption != ENCRYPTION_NONE)
   {
      final = pgmoneta_append(final, ".aes");
   }

   *finalname = final;
   return 0;

error:
   free(final);
   return 1;
}

static int
split_file_path(char* path, char** relative_path, char** bare_file_name)
{
   int relative_path_len = 0;

   char* path_copy = NULL;
   char* rel_path = NULL;
   char* file_name = NULL;

   path_copy = pgmoneta_append(path_copy, path);

   if (path_copy == NULL || !strcmp(path_copy, ".") || !strcmp(path_copy, ".."))
   {
      goto error;
   }

   rel_path = dirname(path_copy);

   /* don't use path_copy from this point onwards */
   relative_path_len = strlen(rel_path);

   /* path is only the filename (doesn't contain any '/') */
   if (!strcmp(rel_path, "."))
   {
      file_name = pgmoneta_append(file_name, path);
   }

   /* path is the root directory */
   if (!strcmp(rel_path, "/"))
   {
      file_name = pgmoneta_append(file_name, path + relative_path_len);
   }

   if (file_name == NULL)
   {
      file_name = pgmoneta_append(file_name, path + relative_path_len + 1);
   }
   *relative_path = rel_path;
   *bare_file_name = file_name;

   return 0;
error:
   free(path_copy);
   return 1;
}

static void
write_info(FILE* sfile, const char* fmt, ...)
{
   char buffer[INFO_BUFFER_SIZE];
   va_list args;

   memset(buffer, 0, sizeof(buffer));

   va_start(args, fmt);
   vsnprintf(buffer, sizeof(buffer), fmt, args);
   va_end(args);

   if (sfile != NULL)
   {
      fputs(buffer, sfile);
   }
}

static void
create_info(char* directory, char* label, int status)
{
   char buffer[INFO_BUFFER_SIZE];
   char* s = NULL;
   FILE* sfile = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   s = pgmoneta_append(s, directory);
   s = pgmoneta_append(s, "/backup.info");

   sfile = fopen(s, "w");
   if (sfile == NULL)
   {
      pgmoneta_log_error("Could not open file %s due to %s", s, strerror(errno));
      errno = 0;
      goto error;
   }

   memset(&buffer[0], 0, sizeof(buffer));
   snprintf(&buffer[0], sizeof(buffer), "%s=%d\n", INFO_STATUS, status);
   fputs(&buffer[0], sfile);
   pgmoneta_log_trace("%s=%d", INFO_STATUS, status);

   memset(&buffer[0], 0, sizeof(buffer));
   snprintf(&buffer[0], sizeof(buffer), "%s=%s\n", INFO_LABEL, label);
   fputs(&buffer[0], sfile);
   pgmoneta_log_trace("%s=%s", INFO_LABEL, label);

   memset(&buffer[0], 0, sizeof(buffer));
   snprintf(&buffer[0], sizeof(buffer), "%s=0\n", INFO_TABLESPACES);
   fputs(&buffer[0], sfile);
   pgmoneta_log_trace("%s=0", INFO_TABLESPACES);

   memset(&buffer[0], 0, sizeof(buffer));
   snprintf(&buffer[0], sizeof(buffer), "%s=%s\n", INFO_PGMONETA_VERSION, VERSION);
   fputs(&buffer[0], sfile);
   pgmoneta_log_trace("%s=%s", INFO_PGMONETA_VERSION, VERSION);

   memset(&buffer[0], 0, sizeof(buffer));
   snprintf(&buffer[0], sizeof(buffer), "%s=\n", INFO_COMMENTS);
   fputs(&buffer[0], sfile);
   pgmoneta_log_trace("%s=", INFO_COMMENTS);

   memset(&buffer[0], 0, sizeof(buffer));
   snprintf(&buffer[0], sizeof(buffer), "%s=%d\n", INFO_COMPRESSION, config->compression_type);
   fputs(&buffer[0], sfile);
   pgmoneta_log_trace("%s=%d", INFO_COMPRESSION, config->compression_type);

   memset(&buffer[0], 0, sizeof(buffer));
   snprintf(&buffer[0], sizeof(buffer), "%s=%d\n", INFO_ENCRYPTION, config->encryption);
   fputs(&buffer[0], sfile);
   pgmoneta_log_trace("%s=%d", INFO_ENCRYPTION, config->encryption);

   pgmoneta_permission(s, 6, 0, 0);

   if (sfile != NULL)
   {
      fsync(fileno(sfile));
      fclose(sfile);
   }

   free(s);

   return;

error:

   free(s);
}
