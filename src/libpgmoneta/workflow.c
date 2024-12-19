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
#include <hot_standby.h>
#include <info.h>
#include <logging.h>
#include <storage.h>
#include <workflow.h>
#include <workflow_funcs.h>
#include <utils.h>

/* system */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static struct workflow* wf_backup(struct backup* backup);
static struct workflow* wf_incremental_backup(void);
static struct workflow* wf_restore(struct backup* backup);
static struct workflow* wf_restore_incremental(int server, struct backup* backup);
static struct workflow* wf_verify(struct backup* backup);
static struct workflow* wf_archive(struct backup* backup);
static struct workflow* wf_delete_backup(struct backup* backup);
static struct workflow* wf_retention(struct backup* backup);

struct workflow*
pgmoneta_workflow_create(int workflow_type, int server, struct backup* backup)
{
   switch (workflow_type)
   {
      case WORKFLOW_TYPE_BACKUP:
         return wf_backup(backup);
         break;
      case WORKFLOW_TYPE_RESTORE:
         return wf_restore(backup);
         break;
      case WORKFLOW_TYPE_RESTORE_INCREMENTAL:
         return wf_restore_incremental(server, backup);
         break;
      case WORKFLOW_TYPE_VERIFY:
         return wf_verify(backup);
         break;
      case WORKFLOW_TYPE_ARCHIVE:
         return wf_archive(backup);
         break;
      case WORKFLOW_TYPE_DELETE_BACKUP:
         return wf_delete_backup(backup);
         break;
      case WORKFLOW_TYPE_RETENTION:
         return wf_retention(backup);
         break;
      case WORKFLOW_TYPE_INCREMENTAL_BACKUP:
         return wf_incremental_backup();
         break;
      default:
         break;
   }

   return NULL;
}

int
pgmoneta_workflow_nodes(int server, char* identifier, struct deque* nodes, struct backup** backup)
{
   char* server_base = NULL;
   char* server_backup = NULL;
   char* backup_base = NULL;
   char* backup_data = NULL;
   struct backup* bck = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   *backup = NULL;

   if (!pgmoneta_deque_exists(nodes, NODE_IDENTIFIER))
   {
      if (pgmoneta_deque_add(nodes, NODE_IDENTIFIER, (uintptr_t)identifier, ValueString))
      {
         goto error;
      }
   }

   if (!pgmoneta_deque_exists(nodes, NODE_SERVER_BASE))
   {
      server_base = pgmoneta_append(server_base, config->base_dir);
      if (!pgmoneta_ends_with(server_base, "/"))
      {
         server_base = pgmoneta_append(server_base, "/");
      }
      server_base = pgmoneta_append(server_base, config->servers[server].name);
      server_base = pgmoneta_append(server_base, "/");

      if (pgmoneta_deque_add(nodes, NODE_SERVER_BASE, (uintptr_t)server_base, ValueString))
      {
         free(server_base);
         goto error;
      }

      free(server_base);
      server_base = NULL;
   }

   if (!pgmoneta_deque_exists(nodes, NODE_SERVER_BACKUP))
   {
      server_backup = pgmoneta_append(server_backup, (char*)pgmoneta_deque_get(nodes, NODE_SERVER_BASE));
      server_backup = pgmoneta_append(server_backup, "backup/");

      if (pgmoneta_deque_add(nodes, NODE_SERVER_BACKUP, (uintptr_t)server_backup, ValueString))
      {
         free(server_backup);
         goto error;
      }

      free(server_backup);
      server_backup = NULL;
   }

   if (identifier != NULL)
   {
      if (pgmoneta_get_backup_server(server, identifier, &bck))
      {
         goto error;
      }

      if (!pgmoneta_deque_exists(nodes, NODE_LABEL))
      {
         if (pgmoneta_deque_add(nodes, NODE_LABEL, (uintptr_t)bck->label, ValueString))
         {
            goto error;
         }
      }

      if (!pgmoneta_deque_exists(nodes, NODE_BACKUP))
      {
         if (pgmoneta_deque_add(nodes, NODE_BACKUP, (uintptr_t)bck, ValueRef))
         {
            goto error;
         }
      }

      backup_base = pgmoneta_append(backup_base, (char*)pgmoneta_deque_get(nodes, NODE_SERVER_BACKUP));
      backup_base = pgmoneta_append(backup_base, bck->label);
      backup_base = pgmoneta_append(backup_base, "/");

      if (!pgmoneta_deque_exists(nodes, NODE_BACKUP_BASE))
      {
         if (pgmoneta_deque_add(nodes, NODE_BACKUP_BASE, (uintptr_t)backup_base, ValueString))
         {
            goto error;
         }
      }

      backup_data = pgmoneta_append(backup_data, backup_base);
      backup_data = pgmoneta_append(backup_data, "data/");

      if (!pgmoneta_deque_exists(nodes, NODE_BACKUP_DATA))
      {
         if (pgmoneta_deque_add(nodes, NODE_BACKUP_DATA, (uintptr_t)backup_data, ValueString))
         {
            goto error;
         }
      }

      free(backup_base);
      free(backup_data);

      backup_base = NULL;
      backup_data = NULL;
   }
   else
   {
      bck = (struct backup*)pgmoneta_deque_get(nodes, NODE_BACKUP);
   }
   *backup = bck;

   return 0;

error:

   return 1;
}

int
pgmoneta_workflow_destroy(struct workflow* workflow)
{
   struct workflow* wf = NULL;
   struct workflow* nxt = NULL;

   if (workflow != NULL)
   {
      wf = workflow;

      while (wf != NULL)
      {
         nxt = wf->next;

         free(wf);

         wf = nxt;
      }
   }

   return 0;
}

static struct workflow*
wf_backup(struct backup* backup)
{
   struct workflow* head = NULL;
   struct workflow* current = NULL;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;

   head = pgmoneta_create_basebackup();
   current = head;

   current->next = pgmoneta_create_manifest();
   current = current->next;

   current->next = pgmoneta_create_extra();
   current = current->next;

   current->next = pgmoneta_storage_create_local();
   current = current->next;

   current->next = pgmoneta_create_hot_standby();
   current = current->next;

   if (config->compression_type == COMPRESSION_CLIENT_GZIP || config->compression_type == COMPRESSION_SERVER_GZIP)
   {
      current->next = pgmoneta_create_gzip(true);
      current = current->next;
   }
   else if (config->compression_type == COMPRESSION_CLIENT_ZSTD || config->compression_type == COMPRESSION_SERVER_ZSTD)
   {
      current->next = pgmoneta_create_zstd(true);
      current = current->next;
   }
   else if (config->compression_type == COMPRESSION_CLIENT_LZ4 || config->compression_type == COMPRESSION_SERVER_LZ4)
   {
      current->next = pgmoneta_create_lz4(true);
      current = current->next;
   }
   else if (config->compression_type == COMPRESSION_CLIENT_BZIP2)
   {
      current->next = pgmoneta_create_bzip2(true);
      current = current->next;
   }

   if (config->encryption != ENCRYPTION_NONE)
   {
      current->next = pgmoneta_encryption(true);
      current = current->next;
   }

#ifdef DEBUG
   if (config->link)
   {
      current->next = pgmoneta_create_link();
      current = current->next;
   }
#else
   current->next = pgmoneta_create_link();
   current = current->next;
#endif

   current->next = pgmoneta_create_permissions(PERMISSION_TYPE_BACKUP);
   current = current->next;

   if (config->storage_engine & STORAGE_ENGINE_SSH)
   {
      current->next = pgmoneta_create_sha256();
      current = current->next;

      current->next = pgmoneta_storage_create_ssh(WORKFLOW_TYPE_BACKUP);
      current = current->next;
   }

   if (config->storage_engine & STORAGE_ENGINE_S3)
   {
      current->next = pgmoneta_storage_create_s3();
      current = current->next;
   }

   if (config->storage_engine & STORAGE_ENGINE_AZURE)
   {
      current->next = pgmoneta_storage_create_azure();
      current = current->next;
   }

   return head;
}

static struct workflow*
wf_restore(struct backup* backup)
{
   struct workflow* head = NULL;
   struct workflow* current = NULL;

   head = pgmoneta_create_restore();
   current = head;

   if (backup->encryption != ENCRYPTION_NONE)
   {
      current->next = pgmoneta_encryption(false);
      current = current->next;
   }

   if (backup->compression == COMPRESSION_CLIENT_GZIP || backup->compression == COMPRESSION_SERVER_GZIP)
   {
      current->next = pgmoneta_create_gzip(false);
      current = current->next;
   }
   else if (backup->compression == COMPRESSION_CLIENT_ZSTD || backup->compression == COMPRESSION_SERVER_ZSTD)
   {
      current->next = pgmoneta_create_zstd(false);
      current = current->next;
   }
   else if (backup->compression == COMPRESSION_CLIENT_LZ4 || backup->compression == COMPRESSION_SERVER_LZ4)
   {
      current->next = pgmoneta_create_lz4(false);
      current = current->next;
   }
   else if (backup->compression == COMPRESSION_CLIENT_BZIP2)
   {
      current->next = pgmoneta_create_bzip2(false);
      current = current->next;
   }

   current->next = pgmoneta_create_recovery_info();
   current = current->next;

   current->next = pgmoneta_restore_excluded_files();
   current = current->next;

   current->next = pgmoneta_create_permissions(PERMISSION_TYPE_RESTORE);
   current = current->next;

   current->next = pgmoneta_create_cleanup(CLEANUP_TYPE_RESTORE);
   current = current->next;

   return head;
}

static struct workflow*
wf_restore_incremental(int server, struct backup* backup)
{
   struct workflow* head = NULL;
   struct workflow* current = NULL;
   struct backup* bck = NULL;
   char* server_dir = pgmoneta_get_server_backup(server);
   char label[sizeof(backup->parent_label)];

   // initialize label to be the parent label of current backup
   memset(label, 0, sizeof(backup->parent_label));
   memcpy(label, backup->parent_label, sizeof(backup->parent_label));

   head = wf_restore(backup);
   current = head;

   while (current->next != NULL)
   {
      current = current->next;
   }
   current->next = pgmoneta_create_batch_restore_relay();
   current = current->next;

   while (bck == NULL || bck->type != TYPE_FULL)
   {
      free(bck);
      pgmoneta_get_backup(server_dir, label, &bck);
      current->next = wf_restore(bck);

      while (current->next != NULL)
      {
         current = current->next;
      }
      current->next = pgmoneta_create_batch_restore_relay();
      current = current->next;

      // get a copy of current backup's parent before we free it in the next round
      memcpy(label, bck->parent_label, sizeof(bck->parent_label));
   }

#ifdef DEBUG
   assert(bck != NULL && bck->type == TYPE_FULL);
   assert(strlen(label) == 0);
#endif

   current->next = pgmoneta_create_combine_incremental();
   current = current->next;

   current->next = pgmoneta_create_permissions(PERMISSION_TYPE_RESTORE_INCREMENTAL);
   current = current->next;

   free(server_dir);
   free(bck);

   return head;
}

static struct workflow*
wf_incremental_backup(void)
{
   struct workflow* head = NULL;
   struct workflow* current = NULL;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;

   head = pgmoneta_create_basebackup();
   current = head;

   current->next = pgmoneta_create_manifest();
   current = current->next;

   current->next = pgmoneta_create_extra();
   current = current->next;

   current->next = pgmoneta_storage_create_local();
   current = current->next;

   // TODO: use a new pgmoneta_create_hot_standby_incremental instead since we need to combine backup first
   // current->next = pgmoneta_create_hot_standby();
   // current = current->next;

   if (config->compression_type == COMPRESSION_CLIENT_GZIP || config->compression_type == COMPRESSION_SERVER_GZIP)
   {
      current->next = pgmoneta_create_gzip(true);
      current = current->next;
   }
   else if (config->compression_type == COMPRESSION_CLIENT_ZSTD || config->compression_type == COMPRESSION_SERVER_ZSTD)
   {
      current->next = pgmoneta_create_zstd(true);
      current = current->next;
   }
   else if (config->compression_type == COMPRESSION_CLIENT_LZ4 || config->compression_type == COMPRESSION_SERVER_LZ4)
   {
      current->next = pgmoneta_create_lz4(true);
      current = current->next;
   }
   else if (config->compression_type == COMPRESSION_CLIENT_BZIP2)
   {
      current->next = pgmoneta_create_bzip2(true);
      current = current->next;
   }

   if (config->encryption != ENCRYPTION_NONE)
   {
      current->next = pgmoneta_encryption(true);
      current = current->next;
   }

   current->next = pgmoneta_create_link();
   current = current->next;

   current->next = pgmoneta_create_permissions(PERMISSION_TYPE_BACKUP);
   current = current->next;

   if (config->storage_engine & STORAGE_ENGINE_SSH)
   {
      current->next = pgmoneta_create_sha256();
      current = current->next;

      current->next = pgmoneta_storage_create_ssh(WORKFLOW_TYPE_BACKUP);
      current = current->next;
   }

   if (config->storage_engine & STORAGE_ENGINE_S3)
   {
      current->next = pgmoneta_storage_create_s3();
      current = current->next;
   }

   if (config->storage_engine & STORAGE_ENGINE_AZURE)
   {
      current->next = pgmoneta_storage_create_azure();
      current = current->next;
   }

   return head;
}

static struct workflow*
wf_verify(struct backup* backup)
{
   struct workflow* head = NULL;
   struct workflow* current = NULL;

   head = pgmoneta_create_restore();
   current = head;

   if (backup->encryption != ENCRYPTION_NONE)
   {
      current->next = pgmoneta_encryption(false);
      current = current->next;
   }

   if (backup->compression == COMPRESSION_CLIENT_GZIP || backup->compression == COMPRESSION_SERVER_GZIP)
   {
      current->next = pgmoneta_create_gzip(false);
      current = current->next;
   }
   else if (backup->compression == COMPRESSION_CLIENT_ZSTD || backup->compression == COMPRESSION_SERVER_ZSTD)
   {
      current->next = pgmoneta_create_zstd(false);
      current = current->next;
   }
   else if (backup->compression == COMPRESSION_CLIENT_LZ4 || backup->compression == COMPRESSION_SERVER_LZ4)
   {
      current->next = pgmoneta_create_lz4(false);
      current = current->next;
   }
   else if (backup->compression == COMPRESSION_CLIENT_BZIP2)
   {
      current->next = pgmoneta_create_bzip2(false);
      current = current->next;
   }

   current->next = pgmoneta_restore_excluded_files();
   current = current->next;

   current->next = pgmoneta_create_permissions(PERMISSION_TYPE_RESTORE);
   current = current->next;

   current->next = pgmoneta_create_verify();
   current = current->next;

   return head;
}

static struct workflow*
wf_archive(struct backup* backup)
{
   struct workflow* head = NULL;
   struct workflow* current = NULL;

   head = pgmoneta_create_archive();
   current = head;

   if (backup->compression == COMPRESSION_CLIENT_GZIP || backup->compression == COMPRESSION_SERVER_GZIP)
   {
      current->next = pgmoneta_create_gzip(true);
      current = current->next;
   }
   else if (backup->compression == COMPRESSION_CLIENT_ZSTD || backup->compression == COMPRESSION_SERVER_ZSTD)
   {
      current->next = pgmoneta_create_zstd(true);
      current = current->next;
   }
   else if (backup->compression == COMPRESSION_CLIENT_LZ4 || backup->compression == COMPRESSION_SERVER_LZ4)
   {
      current->next = pgmoneta_create_lz4(true);

      current = current->next;
   }
   else if (backup->compression == COMPRESSION_CLIENT_BZIP2)
   {
      current->next = pgmoneta_create_bzip2(true);

      current = current->next;
   }

   if (backup->encryption != ENCRYPTION_NONE)
   {
      current->next = pgmoneta_encryption(true);
      current = current->next;
   }

   current->next = pgmoneta_create_permissions(PERMISSION_TYPE_ARCHIVE);
   current = current->next;

   return head;
}

static struct workflow*
wf_retention(struct backup* backup)
{
   struct workflow* head = NULL;

   head = pgmoneta_create_retention();

   return head;
}

static struct workflow*
wf_delete_backup(struct backup* backup)
{
   struct workflow* head = NULL;

   head = pgmoneta_create_delete_backup();

   return head;
}
