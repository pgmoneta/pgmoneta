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
#include "logging.h"
#include "security.h"
#include <pgmoneta.h>
#include <node.h>
#include <utils.h>
#include <workflow.h>
#include <io.h>

/* system */
#include <dirent.h>

static int sha256_setup(int, char*, struct node*, struct node**);
static int sha256_execute(int, char*, struct node*, struct node**);
static int sha256_teardown(int, char*, struct node*, struct node**);

static int write_backup_sha256(char* root, char* relative_path);

static FILE* sha256_file = NULL;

struct workflow*
pgmoneta_workflow_create_sha256(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->setup = &sha256_setup;
   wf->execute = &sha256_execute;
   wf->teardown = &sha256_teardown;
   wf->next = NULL;

   return wf;
}

static int
sha256_setup(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   return 0;
}

static int
sha256_execute(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* root = NULL;
   char* d = NULL;
   char* sha256_path = NULL;

   root = pgmoneta_get_server_backup_identifier(server, identifier);

   sha256_path = pgmoneta_append(sha256_path, root);
   sha256_path = pgmoneta_append(sha256_path, "backup.sha256");

   sha256_file = pgmoneta_open_file(sha256_path, "w");
   if (sha256_file == NULL)
   {
      goto error;
   }

   d = pgmoneta_get_server_backup_identifier_data(server, identifier);

   if (write_backup_sha256(d, ""))
   {
      goto error;
   }

   pgmoneta_permission(sha256_path, 6, 0, 0);

   fclose(sha256_file);

   free(sha256_path);
   free(root);
   free(d);

   return 0;

error:

   if (sha256_file != NULL)
   {
      fclose(sha256_file);
   }

   free(sha256_path);
   free(root);
   free(d);

   return 1;
}

static int
sha256_teardown(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   return 0;
}

static int
write_backup_sha256(char* root, char* relative_path)
{
   char* dir_path = NULL;
   char* relative_file_path;
   char* absolute_file_path;
   char* buffer;
   char* sha256;
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

         write_backup_sha256(root, relative_dir);
      }
      else
      {
         relative_file_path = NULL;
         absolute_file_path = NULL;
         sha256 = NULL;
         buffer = NULL;

         relative_file_path = pgmoneta_append(relative_file_path, relative_path);
         relative_file_path = pgmoneta_append(relative_file_path, "/");
         relative_file_path = pgmoneta_append(relative_file_path, entry->d_name);

         absolute_file_path = pgmoneta_append(absolute_file_path, root);
         absolute_file_path = pgmoneta_append(absolute_file_path, "/");
         absolute_file_path = pgmoneta_append(absolute_file_path, relative_file_path);

         pgmoneta_create_sha256_file(absolute_file_path, &sha256);

         buffer = pgmoneta_append(buffer, relative_file_path);
         buffer = pgmoneta_append(buffer, ":");
         buffer = pgmoneta_append(buffer, sha256);
         buffer = pgmoneta_append(buffer, "\n");

         fputs(buffer, sha256_file);

         free(buffer);
         free(sha256);
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
