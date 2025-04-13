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
#include <info.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <utils.h>

/* system */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NAME "info"
#define INFO_BUFFER_SIZE 8192

void
pgmoneta_create_info(char* directory, char* label, int status)
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

void
pgmoneta_update_info_unsigned_long(char* directory, char* key, unsigned long value)
{
   char buffer[INFO_BUFFER_SIZE];
   char line[INFO_BUFFER_SIZE];
   bool found = false;
   char* s = NULL;
   FILE* sfile = NULL;
   char* d = NULL;
   FILE* dfile = NULL;

   s = pgmoneta_append(s, directory);
   s = pgmoneta_append(s, "/backup.info");

   d = pgmoneta_append(d, directory);
   d = pgmoneta_append(d, "/backup.info.tmp");

   sfile = fopen(s, "r");
   if (sfile == NULL)
   {
      pgmoneta_log_error("Could not open file %s due to %s", s, strerror(errno));
      errno = 0;
      goto error;
   }
   dfile = fopen(d, "w");
   if (dfile == NULL)
   {
      pgmoneta_log_error("Could not open file %s due to %s", d, strerror(errno));
      errno = 0;
      goto error;
   }

   while ((fgets(&buffer[0], sizeof(buffer), sfile)) != NULL)
   {
      char k[INFO_BUFFER_SIZE];
      char v[INFO_BUFFER_SIZE];
      char* ptr = NULL;

      memset(&k[0], 0, sizeof(k));
      memset(&v[0], 0, sizeof(v));

      memset(&line[0], 0, sizeof(line));
      memcpy(&line[0], &buffer[0], strlen(&buffer[0]));

      ptr = strtok(&buffer[0], "=");
      memcpy(&k[0], ptr, strlen(ptr));

      ptr = strtok(NULL, "=");
      memcpy(&v[0], ptr, strlen(ptr) - 1);

      if (!strcmp(key, &k[0]))
      {
         memset(&line[0], 0, sizeof(line));
         snprintf(&line[0], sizeof(line), "%s=%lu\n", key, value);
         fputs(&line[0], dfile);
         found = true;
      }
      else
      {
         fputs(&line[0], dfile);
      }
   }

   if (!found)
   {
      memset(&line[0], 0, sizeof(line));
      pgmoneta_log_trace("%s=%lu", key, value);
      snprintf(&line[0], sizeof(line), "%s=%lu\n", key, value);
      fputs(&line[0], dfile);
   }

   if (sfile != NULL)
   {
      fsync(fileno(sfile));
      fclose(sfile);
   }

   if (dfile != NULL)
   {
      fsync(fileno(dfile));
      fclose(dfile);
   }

   pgmoneta_move_file(d, s);
   pgmoneta_permission(s, 6, 0, 0);

   free(s);
   free(d);

   return;

error:

   free(s);
   free(d);
}

void
pgmoneta_update_info_double(char* directory, char* key, double value)
{
   char buffer[INFO_BUFFER_SIZE];
   char line[INFO_BUFFER_SIZE];
   bool found = false;
   char* s = NULL;
   FILE* sfile = NULL;
   char* d = NULL;
   FILE* dfile = NULL;

   s = pgmoneta_append(s, directory);
   s = pgmoneta_append(s, "/backup.info");

   d = pgmoneta_append(d, directory);
   d = pgmoneta_append(d, "/backup.info.tmp");

   sfile = fopen(s, "r");
   if (sfile == NULL)
   {
      pgmoneta_log_error("Could not open file %s due to %s", s, strerror(errno));
      errno = 0;
      goto error;
   }
   dfile = fopen(d, "w");
   if (dfile == NULL)
   {
      pgmoneta_log_error("Could not open file %s due to %s", d, strerror(errno));
      errno = 0;
      goto error;
   }

   while ((fgets(&buffer[0], sizeof(buffer), sfile)) != NULL)
   {
      char k[INFO_BUFFER_SIZE];
      char v[INFO_BUFFER_SIZE];
      char* ptr = NULL;

      memset(&k[0], 0, sizeof(k));
      memset(&v[0], 0, sizeof(v));

      memset(&line[0], 0, sizeof(line));
      memcpy(&line[0], &buffer[0], strlen(&buffer[0]));

      ptr = strtok(&buffer[0], "=");
      memcpy(&k[0], ptr, strlen(ptr));

      ptr = strtok(NULL, "=");
      memcpy(&v[0], ptr, strlen(ptr) - 1);

      if (!strcmp(key, &k[0]))
      {
         memset(&line[0], 0, sizeof(line));
         snprintf(&line[0], sizeof(line), "%s=%.4f\n", key, value);
         fputs(&line[0], dfile);
         found = true;
      }
      else
      {
         fputs(&line[0], dfile);
      }
   }

   if (!found)
   {
      memset(&line[0], 0, sizeof(line));
      pgmoneta_log_trace("%s=%.4f", key, value);
      snprintf(&line[0], sizeof(line), "%s=%.4f\n", key, value);
      fputs(&line[0], dfile);
   }

   if (sfile != NULL)
   {
      fsync(fileno(sfile));
      fclose(sfile);
   }

   if (dfile != NULL)
   {
      fsync(fileno(dfile));
      fclose(dfile);
   }

   pgmoneta_move_file(d, s);
   pgmoneta_permission(s, 6, 0, 0);

   free(s);
   free(d);

   return;

error:

   free(s);
   free(d);
}

void
pgmoneta_update_info_string(char* directory, char* key, char* value)
{
   char buffer[INFO_BUFFER_SIZE];
   char line[INFO_BUFFER_SIZE];
   bool found = false;
   char* s = NULL;
   FILE* sfile = NULL;
   char* d = NULL;
   FILE* dfile = NULL;

   s = pgmoneta_append(s, directory);
   s = pgmoneta_append(s, "/backup.info");

   d = pgmoneta_append(d, directory);
   d = pgmoneta_append(d, "/backup.info.tmp");

   sfile = fopen(s, "r");
   if (sfile == NULL)
   {
      pgmoneta_log_error("Could not open file %s due to %s", s, strerror(errno));
      errno = 0;
      goto error;
   }
   dfile = fopen(d, "w");
   if (dfile == NULL)
   {
      pgmoneta_log_error("Could not open file %s due to %s", d, strerror(errno));
      errno = 0;
      goto error;
   }

   while ((fgets(&buffer[0], sizeof(buffer), sfile)) != NULL)
   {
      char k[INFO_BUFFER_SIZE];
      char v[INFO_BUFFER_SIZE];
      char* ptr = NULL;

      memset(&k[0], 0, sizeof(k));
      memset(&v[0], 0, sizeof(v));

      memset(&line[0], 0, sizeof(line));
      memcpy(&line[0], &buffer[0], strlen(&buffer[0]));

      ptr = strtok(&buffer[0], "=");
      memcpy(&k[0], ptr, strlen(ptr));

      ptr = strtok(NULL, "=");
      memcpy(&v[0], ptr, strlen(ptr) - 1);

      if (!strcmp(key, &k[0]))
      {
         memset(&line[0], 0, sizeof(line));
         if (value == NULL)
         {
            snprintf(&line[0], sizeof(line), "%s=\n", key);
         }
         else
         {
            snprintf(&line[0], sizeof(line), "%s=%s\n", key, value);
         }
         fputs(&line[0], dfile);
         found = true;
      }
      else
      {
         fputs(&line[0], dfile);
      }
   }

   if (!found)
   {
      memset(&line[0], 0, sizeof(line));
      pgmoneta_log_trace("%s=%s", key, value);
      if (value == NULL)
      {
         snprintf(&line[0], sizeof(line), "%s=\n", key);
      }
      else
      {
         snprintf(&line[0], sizeof(line), "%s=%s\n", key, value);
      }
      fputs(&line[0], dfile);
   }

   if (sfile != NULL)
   {
      fsync(fileno(sfile));
      fclose(sfile);
   }

   if (dfile != NULL)
   {
      fsync(fileno(dfile));
      fclose(dfile);
   }

   pgmoneta_move_file(d, s);
   pgmoneta_permission(s, 6, 0, 0);

   free(s);
   free(d);

   return;

error:

   free(s);
   free(d);
}

void
pgmoneta_update_info_bool(char* directory, char* key, bool value)
{
   pgmoneta_log_trace("%s=%d", key, value ? 1 : 0);
   pgmoneta_update_info_unsigned_long(directory, key, value ? 1 : 0);
}

int
pgmoneta_update_info_annotate(int server, struct backup* backup, char* action, char* key, char* comment)
{
   char* d = NULL;
   char* dir = NULL;
   char* old_comments = NULL;
   char* new_comments = NULL;
   bool found = false;
   bool fail = false;

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
   dir = pgmoneta_append(dir, backup->label);
   dir = pgmoneta_append(dir, "/");

   pgmoneta_update_info_string(dir, INFO_COMMENTS, new_comments);

   memset(backup->comments, 0, sizeof(backup->comments));
   memcpy(backup->comments, new_comments, strlen(new_comments));

   free(d);
   free(dir);
   free(old_comments);
   free(new_comments);

   return 0;
}

int
pgmoneta_get_info_string(struct backup* backup, char* key, char** value)
{
   char* result = NULL;

   if (!strcmp(INFO_LABEL, key))
   {
      result = pgmoneta_append(result, backup->label);
   }
   else if (!strcmp(INFO_WAL, key))
   {
      result = pgmoneta_append(result, backup->wal);
   }
   else if (pgmoneta_starts_with(key, "TABLESPACE_OID"))
   {
      unsigned long number = strtoul(key + 14, NULL, 10);

      result = pgmoneta_append(result, backup->tablespaces_oids[number - 1]);
   }
   else if (pgmoneta_starts_with(key, "TABLESPACE_PATH"))
   {
      unsigned long number = strtoul(key + 15, NULL, 10);

      result = pgmoneta_append(result, backup->tablespaces_paths[number - 1]);
   }
   else if (pgmoneta_starts_with(key, "TABLESPACE"))
   {
      unsigned long number = strtoul(key + 10, NULL, 10);

      result = pgmoneta_append(result, backup->tablespaces[number - 1]);
   }
   else if (!strcmp(INFO_COMMENTS, key))
   {
      result = pgmoneta_append(result, backup->comments);
   }
   else
   {
      goto error;
   }

   *value = result;

   return 0;

error:

   return 1;
}

int
pgmoneta_get_backups(char* directory, int* number_of_backups, struct backup*** backups)
{
   char* d = NULL;
   struct backup** bcks = NULL;
   int number_of_directories;
   char** dirs;

   *number_of_backups = 0;
   *backups = NULL;

   number_of_directories = 0;
   dirs = NULL;

   pgmoneta_get_directories(directory, &number_of_directories, &dirs);

   bcks = (struct backup**)malloc(number_of_directories * sizeof(struct backup*));

   if (bcks == NULL)
   {
      goto error;
   }

   memset(bcks, 0, number_of_directories * sizeof(struct backup*));

   for (int i = 0; i < number_of_directories; i++)
   {
      d = pgmoneta_append(d, directory);

      if (pgmoneta_get_backup(d, dirs[i], &bcks[i]))
      {
         goto error;
      }

      free(d);
      d = NULL;
   }

   for (int i = 0; i < number_of_directories; i++)
   {
      free(dirs[i]);
   }
   free(dirs);

   *number_of_backups = number_of_directories;
   *backups = bcks;

   return 0;

error:

   free(d);

   if (dirs != NULL)
   {
      for (int i = 0; i < number_of_directories; i++)
      {
         free(dirs[i]);
      }
      free(dirs);
   }

   return 1;
}

int
pgmoneta_get_backup(char* directory, char* label, struct backup** backup)
{
   char* fn = NULL;
   int ret = 0;

   *backup = NULL;

   fn = NULL;
   fn = pgmoneta_append(fn, directory);
   fn = pgmoneta_append(fn, "/");
   fn = pgmoneta_append(fn, label);
   fn = pgmoneta_append(fn, "/backup.info");

   ret = pgmoneta_get_backup_file(fn, backup);

   free(fn);

   return ret;
}

int
pgmoneta_get_backup_server(int server, char* identifier, struct backup** backup)
{
   char* d = NULL;
   char* id = NULL;
   char* root = NULL;
   char* base = NULL;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   struct backup* bck = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   *backup = NULL;

   d = pgmoneta_get_server_backup(server);

   if (pgmoneta_get_backups(d, &number_of_backups, &backups))
   {
      goto error;
   }

   if (!strcmp(identifier, "oldest"))
   {
      for (int i = 0; id == NULL && i < number_of_backups; i++)
      {
         if (backups[i]->valid == VALID_TRUE)
         {
            id = backups[i]->label;
         }
      }
   }
   else if (!strcmp(identifier, "latest") || !strcmp(identifier, "newest"))
   {
      for (int i = number_of_backups - 1; id == NULL && i >= 0; i--)
      {
         if (backups[i]->valid == VALID_TRUE)
         {
            id = backups[i]->label;
         }
      }
   }
   else
   {
      /* Explicit search */
      for (int i = 0; id == NULL && i < number_of_backups; i++)
      {
         if (!strcmp(backups[i]->label, identifier) && backups[i]->valid == VALID_TRUE)
         {
            id = backups[i]->label;
         }
      }

      if (id == NULL)
      {
         /* Prefix search */
         for (int i = 0; id == NULL && i < number_of_backups; i++)
         {
            if (pgmoneta_starts_with(backups[i]->label, identifier) && backups[i]->valid == VALID_TRUE)
            {
               id = backups[i]->label;
            }
         }
      }
   }

   if (id == NULL)
   {
      pgmoneta_log_warn("No identifier for %s/%s", config->common.servers[server].name, identifier);
      goto error;
   }

   root = pgmoneta_get_server_backup(server);
   base = pgmoneta_get_server_backup_identifier(server, id);

   if (pgmoneta_get_backup(root, id, &bck))
   {
      pgmoneta_log_error("Unable to get backup for %s/%s", config->common.servers[server].name, id);
      goto error;
   }

   *backup = bck;

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(root);
   free(base);
   free(d);

   return 0;

error:

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(root);
   free(base);
   free(d);

   return 1;
}

int
pgmoneta_get_backup_file(char* fn, struct backup** backup)
{
   char buffer[INFO_BUFFER_SIZE];
   FILE* file = NULL;
   int tbl_idx = 0;
   struct backup* bck = NULL;

   *backup = NULL;

   file = fopen(fn, "r");
   if (file == NULL)
   {
      pgmoneta_log_error("Could not open file %s due to %s", fn, strerror(errno));
      errno = 0;
      goto error;
   }

   bck = (struct backup*)malloc(sizeof(struct backup));

   if (bck == NULL)
   {
      goto error;
   }

   memset(bck, 0, sizeof(struct backup));
   bck->valid = VALID_UNKNOWN;
   bck->basebackup_elapsed_time = 0;
   bck->manifest_elapsed_time = 0;
   bck->compression_zstd_elapsed_time = 0;
   bck->compression_bzip2_elapsed_time = 0;
   bck->compression_lz4_elapsed_time = 0;
   bck->compression_gzip_elapsed_time = 0;
   bck->encryption_elapsed_time = 0;
   bck->linking_elapsed_time = 0;
   bck->remote_ssh_elapsed_time = 0;
   bck->remote_azure_elapsed_time = 0;
   bck->remote_s3_elapsed_time = 0;

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

         if (!strcmp(INFO_STATUS, &key[0]))
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
         else if (pgmoneta_starts_with(&key[0], INFO_HASH_ALGORITHM))
         {
            bck->hash_algorithm = atoi(&value[0]);
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

   return 0;

error:

   free(bck);

   if (file != NULL)
   {
      fsync(fileno(file));
      fclose(file);
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

   if (pgmoneta_get_backups(server_path, &number_of_backups, &backups))
   {
      goto error;
   }

   for (int i = 0; i < number_of_backups; i++)
   {
      if (backups[i] != NULL && backups[i]->valid)
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

   if (pgmoneta_get_backup(d, backup->parent_label, &p))
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

   if (pgmoneta_get_backups(d, &number_of_backups, &backups))
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
      if (pgmoneta_get_backup_server(server, c_identifier, &c))
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
pgmoneta_info_request(SSL* ssl __attribute__((unused)), int client_fd, int server,
                      uint8_t compression, uint8_t encryption,
                      struct json* payload)
{
   char* identifier = NULL;
   char* d = NULL;
   char* elapsed = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
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

   pgmoneta_get_backups(d, &number_of_backups, &backups);

   if (number_of_backups == 0)
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_INFO_NOBACKUP, NAME, compression, encryption, payload);
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
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_INFO_NOBACKUP, NAME, compression, encryption, payload);
      pgmoneta_log_warn("Info: No identifier for %s/%s", config->common.servers[server].name, identifier);
      goto error;
   }

   if (pgmoneta_management_create_response(payload, server, &response))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ALLOCATION, NAME, compression, encryption, payload);
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
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ALLOCATION, NAME, compression, encryption, payload);
      pgmoneta_log_error("Info: Allocation error");

      goto error;
   }

   for (uint64_t i = 0; i < bck->number_of_tablespaces; i++)
   {
      struct json* tbl = NULL;

      if (pgmoneta_json_create(&tablespaces))
      {
         pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ALLOCATION, NAME, compression, encryption, payload);
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

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_INFO_NETWORK, NAME, compression, encryption, payload);
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
pgmoneta_annotate_request(SSL* ssl __attribute__((unused)), int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* payload)
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

   pgmoneta_get_backups(d, &number_of_backups, &backups);

   if (number_of_backups == 0)
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ANNOTATE_NOBACKUP, NAME, compression, encryption, payload);
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
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ANNOTATE_NOBACKUP, NAME, compression, encryption, payload);
      pgmoneta_log_warn("Annotate: No backup (%s)", backup);

      goto error;
   }

   if (pgmoneta_update_info_annotate(server, bck, action, key, comment))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ANNOTATE_FAILED, NAME, compression, encryption, payload);
      pgmoneta_log_error("Annotate: Failed annotate (%s)", backup);

      goto error;
   }

   if (pgmoneta_management_create_response(payload, server, &response))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ALLOCATION, NAME, compression, encryption, payload);
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
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ALLOCATION, NAME, compression, encryption, payload);
      pgmoneta_log_error("Annotate: Allocation error");

      goto error;
   }

   for (uint64_t i = 0; i < bck->number_of_tablespaces; i++)
   {
      struct json* tbl = NULL;

      if (pgmoneta_json_create(&tablespaces))
      {
         pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ALLOCATION, NAME, compression, encryption, payload);
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

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_ANNOTATE_NETWORK, NAME, compression, encryption, payload);
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
