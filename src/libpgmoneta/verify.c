/*
 * Copyright (C) 2026 The pgmoneta community
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
#include <art.h>
#include <backup.h>
#include <deque.h>
#include <info.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <pagechecksum.h>
#include <security.h>
#include <storage.h>
#include <utils.h>
#include <restore.h>
#include <verify.h>
#include <workflow.h>
#include <walfile/pg_control.h>

/* system */
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#define NAME            "verify"

#define MAX_CHAIN_DEPTH 1000000

/*
 * Check whether the incremental backup chain is restorable from its metadata.
 * Returns 1 and sets *problem (caller frees) on the first condition that makes
 * the chain unrestorable: a missing or unreadable parent, a dangling
 * incremental, a backup in the chain marked invalid, or a chain that does not
 * terminate at a full backup within a sane depth. Returns 0 for a full backup
 * or a sound chain. Reads backup.info only; no reconstruction.
 */
static int
verify_chain_broken(int server, struct backup* backup, char** problem)
{
   struct backup* current = NULL;
   struct backup* owned = NULL;
   int depth = 0;
   int broken = 0;

   *problem = NULL;

   if (backup == NULL || backup->type != TYPE_INCREMENTAL)
   {
      return 0;
   }

   current = backup;

   while (current->type == TYPE_INCREMENTAL)
   {
      struct backup* parent = NULL;

      if (depth++ > MAX_CHAIN_DEPTH)
      {
         *problem = pgmoneta_append(NULL, "incremental chain exceeds the maximum depth or is cyclic");
         broken = 1;
         break;
      }

      if (strlen(current->parent_label) == 0)
      {
         *problem = pgmoneta_append(NULL, "incremental backup has no parent (dangling chain)");
         broken = 1;
         break;
      }

      if (pgmoneta_get_backup_parent(server, current, &parent) || parent == NULL || strlen(parent->label) == 0)
      {
         /* load_info returns an empty struct (no label) when backup.info is
          * absent, so an empty label means the parent is gone, not full. */
         *problem = pgmoneta_format_and_append(NULL, "parent backup %s is missing or unreadable (broken chain)", current->parent_label);
         broken = 1;
         free(parent);
         break;
      }

      if (parent->valid == VALID_FALSE)
      {
         *problem = pgmoneta_format_and_append(NULL, "backup %s in the chain is marked invalid", parent->label);
         broken = 1;
         free(parent);
         break;
      }

      if (owned != NULL)
      {
         free(owned);
      }
      owned = parent;
      current = parent;
   }

   free(owned);

   return broken;
}

/*
 * Verify the pages of a reconstructed incremental backup.
 *
 * A reconstructed chain that combines without error can still hold damaged
 * pages: the normal verify path checks page checksums only on a backup's own
 * stored files, and an incremental stores deltas, not whole relations, so its
 * actual restored pages are first seen here -- after the combine. Read the
 * reconstructed pg_control for the block size and whether checksums are on,
 * then verify every page of every checksummable file under the root.
 *
 * Returns 1 and sets *problem (caller frees) on the first page that fails.
 * Returns 0 when every page verifies, or when the cluster has no page checksums
 * or no readable pg_control (there is then nothing that can be checked, exactly
 * as the normal verify path treats such a backup).
 */
static int
verify_deep_pages(int server, char* recon_base, char** problem)
{
   struct control_file_data* controldata = NULL;
   struct deque* files = NULL;
   struct deque_iterator* iter = NULL;
   uint32_t blcksz = 0;
   uint32_t relseg_size = 0;
   uint32_t checksum_version = 0;
   size_t prefix;
   int broken = 0;

   *problem = NULL;

   if (pgmoneta_read_control_data(server, recon_base, &controldata) || controldata == NULL)
   {
      pgmoneta_log_warn("Verify (deep): could not read the reconstructed pg_control, skipping page checks");
      return 0;
   }

   pgmoneta_control_data_page_layout(controldata, &blcksz, &relseg_size, &checksum_version);
   free(controldata);

   if (!(checksum_version != 0 && blcksz > 0 && relseg_size > 0))
   {
      /* The cluster was built without page checksums: nothing to check. */
      return 0;
   }

   if (pgmoneta_get_files(PGMONETA_FILE_TYPE_ALL, recon_base, true, &files))
   {
      *problem = pgmoneta_append(NULL, "the reconstructed files could not be listed");
      return 1;
   }

   prefix = strlen(recon_base);
   if (prefix > 0 && recon_base[prefix - 1] == '/')
   {
      prefix--;
   }

   pgmoneta_deque_iterator_create(files, &iter);
   while (!broken && pgmoneta_deque_iterator_next(iter))
   {
      char* full = (char*)pgmoneta_value_data(iter->value);
      char* relative = full + prefix;
      uint32_t segno = 0;

      while (*relative == '/')
      {
         relative++;
      }

      if (pgmoneta_page_checksummable(relative, &segno))
      {
         uint32_t blockno = 0;
         uint16_t computed = 0;
         uint16_t stored = 0;
         int number_of_bad = 0;

         if (pgmoneta_page_verify_file(full, (size_t)blcksz, relseg_size, segno,
                                       &blockno, &computed, &stored, &number_of_bad))
         {
            if (number_of_bad > 0)
            {
               *problem = pgmoneta_format_and_append(NULL,
                                                     "reconstructed %s fails its page checksum at block %u (calculated %04X but block contains %04X)",
                                                     relative, blockno, computed, stored);
            }
            else
            {
               *problem = pgmoneta_format_and_append(NULL,
                                                     "reconstructed %s is not a whole number of blocks", relative);
            }
            broken = 1;
         }
      }
   }

   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_deque_destroy(files);

   return broken;
}

/* The visibility map stores two bits per heap block after a standard page
 * header, so one VM page covers (block_size - header) * 4 heap blocks. */
#define VM_PAGE_HEADER     24
#define VM_BITS_PER_BLOCK  2
#define VM_BLOCKS_PER_BYTE 4

/* Count the heap blocks of a relation: the size of its main-fork segments
 * (relfilenode, relfilenode.1, ...) in block_size units. */
static uint64_t
heap_block_count(char* dir, char* relfilenode, size_t block_size)
{
   uint64_t blocks = 0;
   uint32_t seg = 0;
   char path[MAX_PATH];

   while (true)
   {
      off_t size;

      memset(path, 0, sizeof(path));
      if (seg == 0)
      {
         snprintf(path, sizeof(path), "%s/%s", dir, relfilenode);
      }
      else
      {
         snprintf(path, sizeof(path), "%s/%s.%u", dir, relfilenode, seg);
      }

      if (!pgmoneta_exists(path))
      {
         break;
      }

      size = pgmoneta_get_file_size(path);
      if (size > 0)
      {
         blocks += (uint64_t)size / block_size;
      }
      seg++;
   }

   return blocks;
}

/* Scan one visibility-map fork file for a bit set on a heap block at or past
 * heap_blocks -- a stale all-visible/all-frozen marking left over a truncation.
 * Returns 1 and sets *stale_block when one is found. */
static int
vm_fork_stale_bit(char* path, size_t block_size, uint32_t relseg_size, uint32_t vm_segno,
                  uint64_t heap_blocks, uint64_t* stale_block)
{
   FILE* file = NULL;
   unsigned char* page = NULL;
   uint64_t heapblocks_per_vm_page;
   uint64_t page_in_file = 0;
   int found = 0;

   heapblocks_per_vm_page = (uint64_t)(block_size - VM_PAGE_HEADER) * VM_BLOCKS_PER_BYTE;

   page = (unsigned char*)malloc(block_size);
   file = fopen(path, "rb");
   if (page == NULL || file == NULL)
   {
      goto done;
   }

   while (!found)
   {
      size_t r = fread(page, 1, block_size, file);
      uint64_t vm_page;
      uint64_t heap_base;
      size_t b;

      if (r == 0)
      {
         break;
      }
      if (r != block_size)
      {
         /* A short trailing read is not whole blocks; the page check reports
          * that, so there is nothing to add here. */
         break;
      }

      vm_page = (uint64_t)vm_segno * relseg_size + page_in_file;
      heap_base = vm_page * heapblocks_per_vm_page;
      page_in_file++;

      /* Every heap block this page can describe is within the relation: no bit
       * here can be past the end. */
      if (heap_base + heapblocks_per_vm_page <= heap_blocks)
      {
         continue;
      }

      for (b = VM_PAGE_HEADER; b < block_size && !found; b++)
      {
         int k;

         if (page[b] == 0)
         {
            continue;
         }

         for (k = 0; k < VM_BLOCKS_PER_BYTE; k++)
         {
            uint64_t hb = heap_base + (uint64_t)(b - VM_PAGE_HEADER) * VM_BLOCKS_PER_BYTE + k;

            if (hb < heap_blocks)
            {
               continue;
            }
            if ((page[b] >> (k * VM_BITS_PER_BLOCK)) & 0x3)
            {
               *stale_block = hb;
               found = 1;
               break;
            }
         }
      }
   }

done:
   if (file != NULL)
   {
      fclose(file);
   }
   free(page);

   return found;
}

/*
 * Deep verification step 3: the reconstructed pages are intact, but is the
 * visibility map consistent with the heap?
 *
 * A heap truncation shortens the main fork and the VM fork together, but the
 * VM pages that remain can keep all-visible/all-frozen bits for blocks that no
 * longer exist. Such a page passes its own checksum, so the page check does not
 * see it, yet it makes an index-only scan return tuples from truncated-away
 * blocks. Over the reconstructed data, for every VM fork confirm that no bit is
 * set at or past the end of its relation's heap.
 *
 * Returns 1 and sets *problem (caller frees) on the first stale bit. Returns 0
 * when every VM fork agrees with its heap, or when there is no readable
 * pg_control to take the block and segment size from.
 */
static int
verify_deep_vm_consistency(int server, char* recon_base, char** problem)
{
   struct control_file_data* controldata = NULL;
   struct deque* files = NULL;
   struct deque_iterator* iter = NULL;
   uint32_t blcksz = 0;
   uint32_t relseg_size = 0;
   uint32_t checksum_version = 0;
   size_t prefix;
   int broken = 0;

   *problem = NULL;

   if (pgmoneta_read_control_data(server, recon_base, &controldata) || controldata == NULL)
   {
      return 0;
   }

   pgmoneta_control_data_page_layout(controldata, &blcksz, &relseg_size, &checksum_version);
   free(controldata);

   if (blcksz <= VM_PAGE_HEADER || relseg_size == 0)
   {
      return 0;
   }

   if (pgmoneta_get_files(PGMONETA_FILE_TYPE_ALL, recon_base, true, &files))
   {
      *problem = pgmoneta_append(NULL, "the reconstructed files could not be listed");
      return 1;
   }

   prefix = strlen(recon_base);
   if (prefix > 0 && recon_base[prefix - 1] == '/')
   {
      prefix--;
   }

   pgmoneta_deque_iterator_create(files, &iter);
   while (!broken && pgmoneta_deque_iterator_next(iter))
   {
      char* full = (char*)pgmoneta_value_data(iter->value);
      char* slash = strrchr(full, '/');
      char* name = slash != NULL ? slash + 1 : full;
      char* vm = strstr(name, "_vm");
      char relfilenode[NAME_MAX];
      char dir[MAX_PATH];
      uint32_t vm_segno = 0;
      uint64_t heap_blocks;
      uint64_t stale_block = 0;

      /* Only the visibility-map fork: <relfilenode>_vm or <relfilenode>_vm.<seg>,
       * with a purely numeric relfilenode. */
      if (vm == NULL || vm == name || (vm[3] != '\0' && vm[3] != '.'))
      {
         continue;
      }
      if (strspn(name, "0123456789") != (size_t)(vm - name))
      {
         continue;
      }
      if (vm[3] == '.')
      {
         char* end = NULL;
         long s = strtol(vm + 4, &end, 10);

         if (end == vm + 4 || *end != '\0' || s < 0 || s > UINT32_MAX)
         {
            continue;
         }
         vm_segno = (uint32_t)s;
      }

      memset(relfilenode, 0, sizeof(relfilenode));
      memcpy(relfilenode, name, (size_t)(vm - name));

      memset(dir, 0, sizeof(dir));
      if (slash != NULL)
      {
         memcpy(dir, full, (size_t)(slash - full));
      }
      else
      {
         dir[0] = '.';
      }

      heap_blocks = heap_block_count(dir, relfilenode, (size_t)blcksz);

      if (vm_fork_stale_bit(full, (size_t)blcksz, relseg_size, vm_segno, heap_blocks, &stale_block))
      {
         *problem = pgmoneta_format_and_append(NULL,
                                               "reconstructed %s marks heap block %llu visible past the end of a %llu block relation (stale visibility map)",
                                               full + prefix + (full[prefix] == '/' ? 1 : 0),
                                               (unsigned long long)stale_block, (unsigned long long)heap_blocks);
         broken = 1;
      }
   }

   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_deque_destroy(files);

   return broken;
}

/*
 * Deep verification step 4 (the restorability guarantee): a reconstructed chain
 * is only restorable if every backup it is built on is itself intact.
 *
 * The page and visibility-map checks above look at the combined result, and a
 * cluster without data checksums has no page checksum to compare, so a single
 * changed byte in a parent backup can flow through the combine unseen. Verify
 * every ancestor backup against its own manifest -- which is taken over the
 * decompressed, decrypted file contents -- by running the same verify workflow
 * the target backup gets. A changed byte anywhere in the chain then fails the
 * deep verify, with or without page checksums.
 *
 * Returns 1 and sets *problem (caller frees) on the first ancestor that does
 * not verify. Returns 0 when every ancestor verifies.
 */
static int
verify_deep_chain(int server, struct backup* backup, char** problem)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   struct backup* current = backup;
   struct backup* owned = NULL;
   char* workspace = NULL;
   int broken = 0;

   *problem = NULL;

   workspace = pgmoneta_get_server_workspace(server);
   workspace = pgmoneta_append(workspace, "verify_chain");
   pgmoneta_delete_directory(workspace);
   pgmoneta_mkdir(workspace);

   while (!broken && current != NULL && current->type == TYPE_INCREMENTAL)
   {
      struct backup* parent = NULL;
      struct art* nodes = NULL;
      struct workflow* workflow = NULL;
      struct workflow* step = NULL;
      struct backup* anc = NULL;
      struct deque* failed = NULL;
      char* base = NULL;

      if (pgmoneta_get_backup_parent(server, current, &parent) || parent == NULL || strlen(parent->label) == 0)
      {
         /* The chain precondition already reports a missing parent; nothing to
          * add here. */
         free(parent);
         break;
      }

      if (pgmoneta_art_create(&nodes))
      {
         *problem = pgmoneta_append(NULL, "out of memory while verifying the chain");
         broken = 1;
         free(parent);
         break;
      }

      base = pgmoneta_append(base, workspace);
      base = pgmoneta_append_char(base, '/');
      base = pgmoneta_append(base, config->common.servers[server].name);
      base = pgmoneta_append_char(base, '-');
      base = pgmoneta_append(base, parent->label);
      if (pgmoneta_exists(base))
      {
         pgmoneta_delete_directory(base);
      }
      pgmoneta_mkdir(base);

      pgmoneta_art_insert(nodes, USER_POSITION, (uintptr_t)"", ValueString);
      pgmoneta_art_insert(nodes, USER_DIRECTORY, (uintptr_t)workspace, ValueString);
      pgmoneta_art_insert(nodes, USER_FILES, (uintptr_t)MANAGEMENT_ARGUMENT_FAILED, ValueString);
      pgmoneta_art_insert(nodes, NODE_TARGET_ROOT, (uintptr_t)workspace, ValueString);
      pgmoneta_art_insert(nodes, NODE_TARGET_BASE, (uintptr_t)base, ValueString);

      if (pgmoneta_workflow_nodes(server, parent->label, nodes, &anc))
      {
         *problem = pgmoneta_format_and_append(NULL, "could not prepare verification of backup %s", parent->label);
         broken = 1;
         pgmoneta_art_destroy(nodes);
         free(base);
         free(parent);
         break;
      }

      workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_VERIFY, anc);

      step = workflow;
      while (step != NULL && !broken)
      {
         if (step->setup(step->name(), nodes) ||
             step->execute(step->name(), nodes) ||
             step->teardown(step->name(), nodes))
         {
            *problem = pgmoneta_format_and_append(NULL, "verification of backup %s could not be run", parent->label);
            broken = 1;
         }
         step = step->next;
      }

      if (!broken)
      {
         failed = (struct deque*)pgmoneta_art_search(nodes, NODE_FAILED);
         if (failed != NULL && pgmoneta_deque_size(failed) > 0)
         {
            *problem = pgmoneta_format_and_append(NULL,
                                                  "backup %s in the chain has %d file(s) that do not match its manifest",
                                                  parent->label, (int)pgmoneta_deque_size(failed));
            broken = 1;
         }
      }

      pgmoneta_workflow_destroy(workflow);
      pgmoneta_delete_directory(base);
      pgmoneta_art_destroy(nodes);
      free(base);

      if (owned != NULL)
      {
         free(owned);
      }
      owned = parent;
      current = parent;
   }

   free(owned);
   pgmoneta_delete_directory(workspace);
   free(workspace);

   return broken;
}

void
pgmoneta_verify(SSL* ssl, int client_fd, int server, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* identifier = NULL;
   char* directory = NULL;
   char* real_directory = NULL;
   char* files = NULL;
   char* elapsed = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   int number_of_failed = 0;
   char* label = NULL;
   struct backup* backup = NULL;
   struct workflow* workflow = NULL;
   struct workflow* current = NULL;
   struct art* nodes = NULL;
   struct deque* f = NULL;
   struct deque* a = NULL;
   struct deque_iterator* fiter = NULL;
   struct deque_iterator* aiter = NULL;
   struct json* failed = NULL;
   struct json* all = NULL;
   struct json* req = NULL;
   struct json* response = NULL;
   struct json* filesj = NULL;
   struct main_configuration* config;

   pgmoneta_start_logging();

   config = (struct main_configuration*)shmem;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   identifier = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_BACKUP);
   directory = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_DIRECTORY);
   files = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_FILES);
   bool deep = (bool)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_DEEP);

   if (pgmoneta_art_create(&nodes))
   {
      goto error;
   }

   if (pgmoneta_art_insert(nodes, USER_POSITION, (uintptr_t)"", ValueString))
   {
      goto error;
   }

   if (pgmoneta_art_insert(nodes, USER_DIRECTORY, (uintptr_t)directory, ValueString))
   {
      goto error;
   }

   if (files == NULL || strlen(files) == 0)
   {
      files = MANAGEMENT_ARGUMENT_FAILED;
   }

   if (pgmoneta_art_insert(nodes, USER_FILES, (uintptr_t)files, ValueString))
   {
      goto error;
   }

   if (pgmoneta_art_insert(nodes, NODE_TARGET_ROOT, (uintptr_t)directory, ValueString))
   {
      goto error;
   }

   if (pgmoneta_workflow_nodes(server, identifier, nodes, &backup))
   {
      goto error;
   }

   /* An incremental backup is only restorable if its whole parent chain is
    * present and valid. Verify otherwise reconstructs and hashes files, which
    * for a broken chain produces a flood of "missing file" errors instead of
    * the real cause. Check the chain first and fail fast with a clear reason. */
   if (backup != NULL && backup->type == TYPE_INCREMENTAL)
   {
      char* chain_problem = NULL;

      if (verify_chain_broken(server, backup, &chain_problem))
      {
         struct json* outcome = NULL;

         pgmoneta_log_error("Verify: %s/%s: %s", config->common.servers[server].name, identifier, chain_problem);
         free(chain_problem);

         if (pgmoneta_management_create_outcome_failure(payload, MANAGEMENT_ERROR_VERIFY_ERROR, NAME, &outcome) ||
             pgmoneta_management_write_json(ssl, client_fd, compression, encryption, payload))
         {
            pgmoneta_log_error("Verify: Error sending response for %s/%s", config->common.servers[server].name, identifier);
         }

         goto error;
      }
   }

   /*
    * Deep (opt-in) verification: actually reconstruct the incremental chain into
    * a workspace scratch directory and confirm it combines without error. This
    * reads and rebuilds the whole backup, so it is gated behind --deep and never
    * runs on the default path (backups can be very large).
    */
   if (deep && backup != NULL && backup->type == TYPE_INCREMENTAL)
   {
      char* recon_root = NULL;
      char* recon_base = NULL;
      char* deep_problem = NULL;

      if (pgmoneta_extract_incremental_backup(server, backup->label, &recon_root, &recon_base))
      {
         struct json* outcome = NULL;

         pgmoneta_log_error("Verify (deep): %s/%s could not be reconstructed from its chain",
                            config->common.servers[server].name, identifier);

         if (pgmoneta_management_create_outcome_failure(payload, MANAGEMENT_ERROR_VERIFY_ERROR, NAME, &outcome) ||
             pgmoneta_management_write_json(ssl, client_fd, compression, encryption, payload))
         {
            pgmoneta_log_error("Verify: Error sending response for %s/%s", config->common.servers[server].name, identifier);
         }

         goto error;
      }

      /*
       * The chain combined; now confirm the reconstructed data is sound: every
       * page's checksum, and that the visibility map does not mark heap blocks
       * past the end of their relation. A stale all-visible VM page survives a
       * heap truncation and passes its own checksum, but makes index-only scans
       * return dead tuples, so it is not caught by the page check above.
       */
      if (verify_deep_chain(server, backup, &deep_problem) ||
          verify_deep_pages(server, recon_base, &deep_problem) ||
          verify_deep_vm_consistency(server, recon_base, &deep_problem))
      {
         struct json* outcome = NULL;

         pgmoneta_log_error("Verify (deep): %s/%s: %s", config->common.servers[server].name, identifier,
                            deep_problem != NULL ? deep_problem : "the reconstructed data failed verification");
         free(deep_problem);
         deep_problem = NULL;

         pgmoneta_delete_directory(recon_root);
         free(recon_root);
         free(recon_base);

         if (pgmoneta_management_create_outcome_failure(payload, MANAGEMENT_ERROR_VERIFY_ERROR, NAME, &outcome) ||
             pgmoneta_management_write_json(ssl, client_fd, compression, encryption, payload))
         {
            pgmoneta_log_error("Verify: Error sending response for %s/%s", config->common.servers[server].name, identifier);
         }

         goto error;
      }

      pgmoneta_log_info("Verify (deep): %s/%s reconstructs from its chain and its pages and visibility map verify",
                        config->common.servers[server].name, identifier);

      pgmoneta_delete_directory(recon_root);
      free(recon_root);
      free(recon_base);
   }

   real_directory = pgmoneta_append(real_directory, directory);
   if (!pgmoneta_ends_with(real_directory, "/"))
   {
      real_directory = pgmoneta_append_char(real_directory, '/');
   }
   real_directory = pgmoneta_append(real_directory, config->common.servers[server].name);
   real_directory = pgmoneta_append_char(real_directory, '-');
   real_directory = pgmoneta_append(real_directory, backup->label);

   if (pgmoneta_exists(real_directory))
   {
      pgmoneta_delete_directory(real_directory);
   }

   pgmoneta_mkdir(real_directory);

   if (pgmoneta_art_insert(nodes, NODE_TARGET_BASE, (uintptr_t)real_directory, ValueString))
   {
      goto error;
   }

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_VERIFY, backup);

   current = workflow;
   while (current != NULL)
   {
      if (current->setup(current->name(), nodes))
      {
         goto error;
      }
      current = current->next;
   }

   current = workflow;
   while (current != NULL)
   {
      if (current->execute(current->name(), nodes))
      {
         goto error;
      }
      current = current->next;
   }

   current = workflow;
   while (current != NULL)
   {
      if (current->teardown(current->name(), nodes))
      {
         goto error;
      }
      current = current->next;
   }

   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   f = (struct deque*)pgmoneta_art_search(nodes, NODE_FAILED);
   a = (struct deque*)pgmoneta_art_search(nodes, NODE_ALL);

   /* an incremental is restored with the backups it is built on, so check those too */
   if (backup->type == TYPE_INCREMENTAL && f != NULL)
   {
      pgmoneta_verify_sha512_chain(server, backup->label, false, false, f);
   }

   if (pgmoneta_json_create(&failed))
   {
      goto error;
   }

   if (pgmoneta_deque_iterator_create(f, &fiter))
   {
      goto error;
   }

   while (pgmoneta_deque_iterator_next(fiter))
   {
      struct json* j = NULL;

      if (pgmoneta_json_clone((struct json*)pgmoneta_value_data(fiter->value), &j))
      {
         goto error;
      }

      pgmoneta_json_append(failed, (uintptr_t)j, ValueJSON);
      number_of_failed++;
   }

   if (files != NULL && !strcasecmp(files, "all"))
   {
      pgmoneta_json_create(&all);

      if (pgmoneta_deque_iterator_create(a, &aiter))
      {
         goto error;
      }

      while (pgmoneta_deque_iterator_next(aiter))
      {
         struct json* j = NULL;

         if (pgmoneta_json_clone((struct json*)pgmoneta_value_data(aiter->value), &j))
         {
            goto error;
         }

         pgmoneta_json_append(all, (uintptr_t)j, ValueJSON);
      }
   }

   if (pgmoneta_management_create_response(payload, server, &response))
   {
      pgmoneta_management_response_error_with_nodes(ssl, client_fd, config->common.servers[server].name, MANAGEMENT_ERROR_ALLOCATION, NAME, compression, encryption, payload, nodes);

      goto error;
   }

   if (pgmoneta_json_create(&filesj))
   {
      pgmoneta_management_response_error_with_nodes(ssl, client_fd, config->common.servers[server].name, MANAGEMENT_ERROR_ALLOCATION, NAME, compression, encryption, payload, nodes);

      goto error;
   }

   pgmoneta_json_put(filesj, MANAGEMENT_ARGUMENT_FAILED, (uintptr_t)failed, ValueJSON);
   failed = NULL;
   pgmoneta_json_put(filesj, MANAGEMENT_ARGUMENT_ALL, (uintptr_t)all, ValueJSON);
   all = NULL;

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)label, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->common.servers[server].name, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_FILES, (uintptr_t)filesj, ValueJSON);
   filesj = NULL;

   pgmoneta_delete_directory((char*)pgmoneta_art_search(nodes, NODE_TARGET_BASE));

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   /* A backup that did not verify is a failed verification, not a successful
    * run that happens to list the files which did not match. */
   if (number_of_failed > 0)
   {
      struct json* outcome = NULL;

      pgmoneta_log_error("Verify: %s/%s failed for %d file%s",
                         config->common.servers[server].name, label,
                         number_of_failed, number_of_failed == 1 ? "" : "s");

      /* Send the failure with the response that lists the files */
      if (pgmoneta_management_create_outcome_failure(payload, MANAGEMENT_ERROR_VERIFY_ERROR, NAME, &outcome) ||
          pgmoneta_management_write_json(ssl, client_fd, compression, encryption, payload))
      {
         pgmoneta_log_error("Verify: Error sending response for %s/%s", config->common.servers[server].name, identifier);
      }
      goto error;
   }

   if (pgmoneta_management_response_ok(ssl, client_fd, start_t, end_t, compression, encryption, payload))
   {
      pgmoneta_management_response_error_with_nodes(ssl, client_fd, config->common.servers[server].name, MANAGEMENT_ERROR_VERIFY_NETWORK, NAME, compression, encryption, payload, nodes);
      pgmoneta_log_error("Verify: Error sending response for %s/%s", config->common.servers[server].name, identifier);

      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("Verify: %s/%s (Elapsed: %s)", config->common.servers[server].name, label, elapsed);

   pgmoneta_deque_iterator_destroy(fiter);
   pgmoneta_deque_iterator_destroy(aiter);

   pgmoneta_art_destroy(nodes);

   pgmoneta_json_destroy(payload);

   pgmoneta_workflow_destroy(workflow);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   free(real_directory);
   free(elapsed);

   exit(0);

error:

   pgmoneta_delete_directory((char*)pgmoneta_art_search(nodes, NODE_TARGET_BASE));

   pgmoneta_deque_iterator_destroy(fiter);
   pgmoneta_deque_iterator_destroy(aiter);

   pgmoneta_art_destroy(nodes);

   pgmoneta_json_destroy(filesj);
   pgmoneta_json_destroy(failed);
   pgmoneta_json_destroy(all);

   pgmoneta_json_destroy(payload);

   pgmoneta_workflow_destroy(workflow);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   free(real_directory);
   free(elapsed);

   exit(1);
}

void
pgmoneta_sha512_verification(char** argv)
{
   int server = 0;
   struct main_configuration* config;
   char* backup_dir = NULL;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   struct deque* labels = NULL;
   struct art* s3_labels = NULL;
   bool active = false;
   bool locked = false;
   int err = 0;
   char* elapsed = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;

   pgmoneta_start_logging();

   config = (struct main_configuration*)shmem;

   pgmoneta_set_proc_title(1, argv, "verification", NULL);

   for (server = 0; server < config->common.number_of_servers; server++)
   {
      if (!config->common.servers[server].online)
      {
         pgmoneta_log_debug("Verification: Server %s is offline", config->common.servers[server].name);
         continue;
      }

      active = false;
      if (!atomic_compare_exchange_strong(&config->common.servers[server].repository, &active, true))
      {
         pgmoneta_log_info("Verification: Server %s is already active, skipping verification", config->common.servers[server].name);
         continue;
      }

      pgmoneta_log_debug("Verification: Starting for server %s", config->common.servers[server].name);

      locked = true;
      config->common.servers[server].repository_pid = getpid();

      backup_dir = pgmoneta_get_server_backup(server);

      if (pgmoneta_load_infos(backup_dir, &number_of_backups, &backups))
      {
         pgmoneta_log_error("Verification: %s: Unable to get backups", config->common.servers[server].name);
         err = 1;
         goto server_cleanup;
      }
      if (pgmoneta_storage_list_backup_labels(server, &labels))
      {
         goto server_cleanup;
      }

      if (pgmoneta_deque_size(labels) > 0)
      {
         struct deque_iterator* liter = NULL;

         if (pgmoneta_art_create(&s3_labels))
         {
            goto server_cleanup;
         }
         if (pgmoneta_deque_iterator_create(labels, &liter))
         {
            goto server_cleanup;
         }
         while (pgmoneta_deque_iterator_next(liter))
         {
            char* label = (char*)pgmoneta_value_data(liter->value);

            pgmoneta_art_insert(s3_labels, label, (uintptr_t)true, ValueBool);
         }
         pgmoneta_deque_iterator_destroy(liter);
      }

      for (int i = 0; i < number_of_backups; i++)
      {
         bool success = true;
         bool remote_verify = false;
         bool azure_verify = false;

#ifdef HAVE_FREEBSD
         clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
         clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

         if (!pgmoneta_is_backup_struct_valid(server, backups[i]))
         {
            err = 1;
            success = false;
            continue;
         }
         if ((config->storage_engine & STORAGE_ENGINE_S3) &&
             s3_labels != NULL && pgmoneta_art_contains_key(s3_labels, backups[i]->label))
         {
            remote_verify = true;
         }
         else if (config->storage_engine & STORAGE_ENGINE_AZURE)
         {
            remote_verify = true;
            azure_verify = true;
         }

         if (remote_verify)
         {
            int verify_rc = azure_verify ? azure_verify_backup(server, backups[i]) : pgmoneta_storage_verify_backup(server, backups[i]);

            if (verify_rc)
            {
               pgmoneta_log_error("Verification: %s/%s remote verification failed",
                                  config->common.servers[server].name, backups[i]->label);
               err = 1;
               success = false;
            }
         }
         else
         {
            int r = pgmoneta_verify_sha512(server, backups[i]->label, false, NULL);

            if (r == 2)
            {
               pgmoneta_log_error("Verification: %s/%s has no backup.sha512",
                                  config->common.servers[server].name, backups[i]->label);
            }
            if (r != 0)
            {
               err = 1;
               success = false;
            }
         }

#ifdef HAVE_FREEBSD
         clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
         clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

         elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);
         if (success)
         {
            pgmoneta_log_info("Verification: %s/%s (Elapsed: %s)", config->common.servers[server].name, backups[i]->label, elapsed);
         }
         else
         {
            char* bck_dir = NULL;

            pgmoneta_log_info("Update .info");

            backups[i]->valid = VALID_FALSE;

            bck_dir = pgmoneta_get_server_backup(server);

            pgmoneta_save_info(bck_dir, backups[i]);

            free(bck_dir);
         }
         free(elapsed);
      }

server_cleanup:
      for (int i = 0; i < number_of_backups; i++)
      {
         if (backups[i])
         {
            free(backups[i]);
         }
      }
      free(backups);
      backups = NULL;

      free(backup_dir);
      backup_dir = NULL;

      if (locked)
      {
         config->common.servers[server].repository_pid = 0;
         atomic_store(&config->common.servers[server].repository, false);
         locked = false;
      }

      pgmoneta_deque_destroy(labels);
      labels = NULL;

      pgmoneta_art_destroy(s3_labels);
      s3_labels = NULL;
   }

   pgmoneta_stop_logging();
   exit(err);
}

int
pgmoneta_verify_sha512(int server, char* label, bool missing_ok, struct deque* failed)
{
   char* root = NULL;
   char* sha512_path = NULL;
   FILE* sha512_file = NULL;
   char buffer[4096];
   int line = 0;
   int ret = 0;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   root = pgmoneta_get_server_backup_identifier(server, label);

   sha512_path = pgmoneta_append(sha512_path, root);
   if (!pgmoneta_ends_with(sha512_path, "/"))
   {
      sha512_path = pgmoneta_append_char(sha512_path, '/');
   }
   sha512_path = pgmoneta_append(sha512_path, "backup.sha512");

   if (!pgmoneta_exists(sha512_path))
   {
      ret = 2;
      goto done;
   }

   sha512_file = fopen(sha512_path, "r");
   if (sha512_file == NULL)
   {
      pgmoneta_log_error("Verify: %s/%s: could not open %s: %s",
                         config->common.servers[server].name, label, sha512_path, strerror(errno));
      ret = 1;
      goto done;
   }

   while (fgets(&buffer[0], sizeof(buffer), sha512_file) != NULL)
   {
      char* hash = NULL;
      char* filename = NULL;
      char* path = NULL;
      char* calculated = NULL;
      char* status = NULL;

      line++;

      hash = strtok(&buffer[0], " ");
      filename = strtok(NULL, "\n");
      if (hash == NULL || filename == NULL || strlen(filename) < 3)
      {
         pgmoneta_log_error("Verify: %s/%s: %s formatting error at line %d",
                            config->common.servers[server].name, label, sha512_path, line);
         ret = 1;
         continue;
      }

      /* skip the "*./" in front of the path */
      filename += 3;

      /* backup.info is rewritten after the backup (retain, annotate, verification) */
      if (pgmoneta_compare_string(filename, "backup.info"))
      {
         continue;
      }

      path = pgmoneta_append(path, root);
      if (!pgmoneta_ends_with(path, "/"))
      {
         path = pgmoneta_append(path, "/");
      }
      path = pgmoneta_append(path, filename);

      if (!pgmoneta_exists(path))
      {
         if (missing_ok)
         {
            /* a remote restore only stages the files it needs */
            free(path);
            continue;
         }
         status = "Missing";
      }
      else if (pgmoneta_create_sha512_file(path, &calculated))
      {
         status = "Could not be read";
      }
      else if (strcmp(hash, calculated) != 0)
      {
         status = "Hash mismatch";
      }

      if (status != NULL)
      {
         pgmoneta_log_error("Verify: %s/%s: %s: %s", config->common.servers[server].name, label, filename, status);
         ret = 1;

         if (failed != NULL)
         {
            struct json* j = NULL;

            if (!pgmoneta_json_create(&j))
            {
               pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_DIRECTORY, (uintptr_t)root, ValueString);
               pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_FILENAME, (uintptr_t)filename, ValueString);
               pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_ORIGINAL, (uintptr_t)hash, ValueString);
               pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_CALCULATED, (uintptr_t)(calculated != NULL ? calculated : "Unknown"), ValueString);
               pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_HASH_ALGORITHM, (uintptr_t)"SHA512", ValueString);
               pgmoneta_json_put(j, MANAGEMENT_ARGUMENT_STATUS, (uintptr_t)status, ValueString);
               pgmoneta_deque_add(failed, path, (uintptr_t)j, ValueJSON);
            }
         }
      }

      free(path);
      free(calculated);
   }

done:
   if (sha512_file != NULL)
   {
      fclose(sha512_file);
   }
   free(sha512_path);
   free(root);

   return ret;
}

int
pgmoneta_verify_sha512_chain(int server, char* label, bool self, bool missing_ok, struct deque* failed)
{
   char current[MISC_LENGTH];
   char* backup_dir = NULL;
   struct backup* bck = NULL;
   int ret = 0;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   backup_dir = pgmoneta_get_server_backup(server);
   pgmoneta_snprintf(current, sizeof(current), "%s", label);

   while (true)
   {
      if (pgmoneta_load_info(backup_dir, current, &bck) || bck == NULL)
      {
         pgmoneta_log_error("Verify: %s: unable to find backup %s", config->common.servers[server].name, current);
         ret = 1;
         break;
      }

      if (self || !pgmoneta_compare_string(current, label))
      {
         int r = pgmoneta_verify_sha512(server, current, missing_ok, failed);

         if (r == 2)
         {
            pgmoneta_log_warn("Verify: %s/%s has no backup.sha512, not checked", config->common.servers[server].name, current);
         }
         else if (r != 0)
         {
            ret = 1;
         }
      }

      if (bck->type != TYPE_INCREMENTAL)
      {
         break;
      }

      pgmoneta_snprintf(current, sizeof(current), "%s", bck->parent_label);
      free(bck);
      bck = NULL;
   }

   free(bck);
   free(backup_dir);

   return ret;
}
