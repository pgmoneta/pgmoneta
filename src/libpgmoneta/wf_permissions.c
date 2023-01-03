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
#include <info.h>
#include <logging.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <stdlib.h>

static int permissions_setup(int, char*, struct node*, struct node**);
static int permissions_execute_backup(int, char*, struct node*, struct node**);
static int permissions_execute_restore(int, char*, struct node*, struct node**);
static int permissions_execute_archive(int, char*, struct node*, struct node**);
static int permissions_teardown(int, char*, struct node*, struct node**);

struct workflow*
pgmoneta_workflow_create_permissions(int type)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->setup = &permissions_setup;
   switch (type)
   {
      case PERMISSION_TYPE_BACKUP:
         wf->execute = &permissions_execute_backup;
         break;
      case PERMISSION_TYPE_RESTORE:
         wf->execute = &permissions_execute_restore;
         break;
      case PERMISSION_TYPE_ARCHIVE:
         wf->execute = &permissions_execute_archive;
         break;
      default:
         pgmoneta_log_error("Invalid permission type");
   }
   wf->teardown = &permissions_teardown;
   wf->next = NULL;

   return wf;
}

static int
permissions_setup(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   return 0;
}

static int
permissions_execute_backup(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* path = NULL;

   path = pgmoneta_get_server_backup_identifier_data(server, identifier);

   pgmoneta_permission_recursive(path);

   free(path);

   return 0;
}

static int
permissions_execute_restore(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* d = NULL;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   char* id = NULL;
   char* path = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (!strcmp(identifier, "oldest"))
   {
      d = pgmoneta_get_server_backup(server);

      if (pgmoneta_get_backups(d, &number_of_backups, &backups))
      {
         goto error;
      }

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
      d = pgmoneta_get_server_backup(server);

      if (pgmoneta_get_backups(d, &number_of_backups, &backups))
      {
         goto error;
      }

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
      id = identifier;
   }

   path = pgmoneta_append(path, pgmoneta_get_node_string(i_nodes, "directory"));
   if (!pgmoneta_ends_with(path, "/"))
   {
      path = pgmoneta_append(path, "/");
   }
   path = pgmoneta_append(path, config->servers[server].name);
   path = pgmoneta_append(path, "-");
   path = pgmoneta_append(path, id);
   path = pgmoneta_append(path, "/");

   pgmoneta_permission_recursive(path);

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(d);
   free(path);

   return 0;

error:

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(d);
   free(path);

   return 1;
}

static int
permissions_execute_archive(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* d = NULL;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   char* id = NULL;
   char* path = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (!strcmp(identifier, "oldest"))
   {
      d = pgmoneta_get_server_backup(server);

      if (pgmoneta_get_backups(d, &number_of_backups, &backups))
      {
         goto error;
      }

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
      d = pgmoneta_get_server_backup(server);

      if (pgmoneta_get_backups(d, &number_of_backups, &backups))
      {
         goto error;
      }

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
      id = identifier;
   }

   path = pgmoneta_append(path, pgmoneta_get_node_string(i_nodes, "directory"));
   if (!pgmoneta_ends_with(path, "/"))
   {
      path = pgmoneta_append(path, "/");
   }
   path = pgmoneta_append(path, config->servers[server].name);
   path = pgmoneta_append(path, "-");
   path = pgmoneta_append(path, id);
   path = pgmoneta_append(path, ".tar");

   if (config->compression_type == COMPRESSION_GZIP)
   {
      path = pgmoneta_append(path, ".gz");
   }
   else if (config->compression_type == COMPRESSION_ZSTD)
   {
      path = pgmoneta_append(path, ".zstd");
   }
   else if (config->compression_type == COMPRESSION_LZ4)
   {
      path = pgmoneta_append(path, ".lz4");
   }

   pgmoneta_permission(path, 6, 0, 0);

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(d);
   free(path);

   return 0;

error:

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(d);
   free(path);

   return 1;
}

static int
permissions_teardown(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   return 0;
}
