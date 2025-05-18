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
#include <logging.h>
#include <security.h>
#include <string.h>
#include <utils.h>
#include <verify.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>

static char* sha512_name(void);
static int sha512_execute(char*, struct art*);

static int write_backup_sha512(char* root, char* relative_path);

static FILE* sha512_file = NULL;

struct workflow*
pgmoneta_create_sha512(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->name = &sha512_name;
   wf->setup = &pgmoneta_common_setup;
   wf->execute = &sha512_execute;
   wf->teardown = &pgmoneta_common_teardown;
   wf->next = NULL;

   return wf;
}

static char*
sha512_name(void)
{
   return "SHA512";
}

static int
sha512_execute(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   char* root = NULL;
   char* d = NULL;
   char* sha512_path = NULL;
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

   pgmoneta_log_debug("SHA512 (execute): %s/%s", config->common.servers[server].name, label);

   root = pgmoneta_get_server_backup_identifier(server, label);

   sha512_path = pgmoneta_append(sha512_path, root);
   sha512_path = pgmoneta_append(sha512_path, "backup.sha512");

   sha512_file = fopen(sha512_path, "w");
   if (sha512_file == NULL)
   {
      goto error;
   }

   d = pgmoneta_get_server_backup_identifier_data(server, label);

   if (write_backup_sha512(root, ""))
   {
      goto error;
   }

   pgmoneta_permission(sha512_path, 6, 0, 0);

   fclose(sha512_file);

   free(sha512_path);
   free(root);
   free(d);

   return 0;

error:

   if (sha512_file != NULL)
   {
      fclose(sha512_file);
   }

   free(sha512_path);
   free(root);
   free(d);

   return 1;
}

static int
write_backup_sha512(char* root, char* relative_path)
{
   char* dir_path = NULL;
   char* relative_file_path;
   char* absolute_file_path;
   char* buffer;
   char* sha512;
   DIR* dir;
   struct dirent* entry;

   dir_path = pgmoneta_append(dir_path, root);
   dir_path = pgmoneta_append(dir_path, relative_path);

   if (!(dir = opendir(dir_path)))
   {
      goto error;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      char relative_dir[1024];

      if (entry->d_type == DT_DIR)
      {
         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }

         snprintf(relative_dir, sizeof(relative_dir), "%s/%s", relative_path, entry->d_name);

         write_backup_sha512(root, relative_dir);
      }
      else if (strcmp(entry->d_name, "backup.sha512"))
      {
         relative_file_path = NULL;
         absolute_file_path = NULL;
         sha512 = NULL;
         buffer = NULL;

         relative_file_path = pgmoneta_append(relative_file_path, relative_path);
         relative_file_path = pgmoneta_append(relative_file_path, "/");
         relative_file_path = pgmoneta_append(relative_file_path, entry->d_name);

         absolute_file_path = pgmoneta_append(absolute_file_path, root);
         absolute_file_path = pgmoneta_append(absolute_file_path, "/");
         absolute_file_path = pgmoneta_append(absolute_file_path, relative_file_path);

         pgmoneta_create_sha512_file(absolute_file_path, &sha512);

         buffer = pgmoneta_append(buffer, sha512);
         buffer = pgmoneta_append(buffer, " *.");
         buffer = pgmoneta_append(buffer, relative_file_path);
         buffer = pgmoneta_append(buffer, "\n");

         fputs(buffer, sha512_file);

         free(buffer);
         free(sha512);
         free(relative_file_path);
         free(absolute_file_path);
      }
   }

   closedir(dir);

   free(dir_path);

   return 0;

error:

   if (dir != NULL)
   {
      closedir(dir);
   }

   free(dir_path);

   return 1;
}

int
pgmoneta_update_sha512(char* root_dir, char* filename)
{
   char buffer[4096];
   char line[4096];
   bool found = false;
   char* sha512_path = NULL;
   char* sha512_tmp_path = NULL;
   FILE* source_file = NULL;
   FILE* dest_file = NULL;
   char* absolute_file_path = NULL;
   char* new_sha512 = NULL;
   char* new_line = NULL;

   sha512_path = pgmoneta_append(sha512_path, root_dir);
   sha512_path = pgmoneta_append(sha512_path, "/backup.sha512");

   sha512_tmp_path = pgmoneta_append(sha512_tmp_path, root_dir);
   sha512_tmp_path = pgmoneta_append(sha512_tmp_path, "/backup.sha512.tmp");

   absolute_file_path = pgmoneta_append(absolute_file_path, root_dir);
   absolute_file_path = pgmoneta_append(absolute_file_path, "/");
   absolute_file_path = pgmoneta_append(absolute_file_path, filename);

   if (pgmoneta_create_sha512_file(absolute_file_path, &new_sha512))
   {
      pgmoneta_log_error("Could not create SHA512 hash for %s", absolute_file_path);
      goto error;
   }

   source_file = fopen(sha512_path, "r");
   if (source_file == NULL)
   {
      pgmoneta_log_error("Could not open file %s due to %s", sha512_path, strerror(errno));
      errno = 0;
      goto error;
   }

   dest_file = fopen(sha512_tmp_path, "w");
   if (dest_file == NULL)
   {
      pgmoneta_log_error("Could not open file %s due to %s", sha512_tmp_path, strerror(errno));
      errno = 0;
      goto error;
   }

   while ((fgets(&buffer[0], sizeof(buffer), source_file)) != NULL)
   {
      char* file_entry;

      memset(&line[0], 0, sizeof(line));
      memcpy(&line[0], &buffer[0], strlen(&buffer[0]));

      file_entry = strstr(&buffer[0], filename);

      if (file_entry != NULL)
      {
         new_line = pgmoneta_append(new_line, new_sha512);
         new_line = pgmoneta_append(new_line, " *./");
         new_line = pgmoneta_append(new_line, filename);
         new_line = pgmoneta_append(new_line, "\n");

         fputs(new_line, dest_file);
         found = true;
         free(new_line);
         new_line = NULL;
      }
      else
      {
         fputs(&line[0], dest_file);
      }
   }

   if (!found)
   {
      new_line = pgmoneta_append(new_line, new_sha512);
      new_line = pgmoneta_append(new_line, " *.");
      new_line = pgmoneta_append(new_line, filename);
      new_line = pgmoneta_append(new_line, "\n");

      fputs(new_line, dest_file);
      pgmoneta_log_trace("Added new SHA512 entry for %s", filename);
      free(new_line);
      new_line = NULL;
   }

   if (source_file != NULL)
   {
      fsync(fileno(source_file));
      fclose(source_file);
   }

   if (dest_file != NULL)
   {
      fsync(fileno(dest_file));
      fclose(dest_file);
   }

   pgmoneta_move_file(sha512_tmp_path, sha512_path);
   pgmoneta_permission(sha512_path, 6, 0, 0);

   pgmoneta_log_trace("Updated SHA512 hash for %s", filename);

   free(sha512_path);
   free(sha512_tmp_path);
   free(absolute_file_path);
   free(new_sha512);

   return 0;

error:
   if (source_file != NULL)
   {
      fclose(source_file);
   }

   if (dest_file != NULL)
   {
      fclose(dest_file);
   }

   free(sha512_path);
   free(sha512_tmp_path);
   free(absolute_file_path);
   free(new_sha512);
   free(new_line);

   return 1;
}
