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
#include <utils.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <stdlib.h>

static char* permissions_name(void);
static int permissions_execute_backup(char*, struct art*);
static int permissions_execute_restore(char*, struct art*);
static int permissions_execute_archive(char*, struct art*);

struct workflow*
pgmoneta_create_permissions(int type)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->name = &permissions_name;
   wf->setup = &pgmoneta_common_setup;
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
   wf->teardown = &pgmoneta_common_teardown;
   wf->next = NULL;

   return wf;
}

static char*
permissions_name(void)
{
   return "Permissions";
}

static int
permissions_execute_backup(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   char* path = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   pgmoneta_dump_art(nodes);

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("Permissions (backup): %s/%s", config->common.servers[server].name, label);

   path = pgmoneta_get_server_backup_identifier_data(server, label);

   pgmoneta_permission_recursive(path);

   free(path);

   return 0;
}

static int
permissions_execute_restore(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   char* path = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   pgmoneta_dump_art(nodes);

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);
   path = pgmoneta_append(path, (char*)pgmoneta_art_search(nodes, NODE_TARGET_ROOT));

   if (!pgmoneta_ends_with(path, "/"))
   {
      path = pgmoneta_append(path, "/");
   }
   path = pgmoneta_append(path, config->common.servers[server].name);
   path = pgmoneta_append(path, "-");
   path = pgmoneta_append(path, label);
   path = pgmoneta_append(path, "/");

   pgmoneta_log_debug("Permissions (restore): %s/%s at %s", config->common.servers[server].name, label, path);

   pgmoneta_permission_recursive(path);

   free(path);

   return 0;
}

static int
permissions_execute_archive(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   char* d = NULL;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   /* char* id = NULL; */
   char* path = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   pgmoneta_dump_art(nodes);

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("Permissions (archive): %s/%s", config->common.servers[server].name, label);

   path = pgmoneta_append(path, (char*)pgmoneta_art_search(nodes, NODE_TARGET_ROOT));
   if (!pgmoneta_ends_with(path, "/"))
   {
      path = pgmoneta_append(path, "/");
   }
   path = pgmoneta_append(path, config->common.servers[server].name);
   path = pgmoneta_append(path, "-");
   path = pgmoneta_append(path, label);
   path = pgmoneta_append(path, ".tar");

   if (config->compression_type == COMPRESSION_CLIENT_GZIP || config->compression_type == COMPRESSION_SERVER_GZIP)
   {
      path = pgmoneta_append(path, ".gz");
   }
   else if (config->compression_type == COMPRESSION_CLIENT_ZSTD || config->compression_type == COMPRESSION_SERVER_ZSTD)
   {
      path = pgmoneta_append(path, ".zstd");
   }
   else if (config->compression_type == COMPRESSION_CLIENT_LZ4 || config->compression_type == COMPRESSION_SERVER_LZ4)
   {
      path = pgmoneta_append(path, ".lz4");
   }
   else if (config->compression_type == COMPRESSION_CLIENT_BZIP2)
   {
      path = pgmoneta_append(path, ".bz2");
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
}
