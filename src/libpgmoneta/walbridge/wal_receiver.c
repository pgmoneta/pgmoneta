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
#include <logging.h>
#include <utils.h>
#include <wal.h>
#include <walfile.h>
#include <walfile/wal_reader.h>
#include <server.h>
#include <walbridge/lsn_map.h>
#include <walbridge/migration_engine.h>
#include <walbridge/wal_store.h>

/* system */
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern void pgmoneta_wal(int srv, char** argv);

#define XLOG_SEGMENTS_PER_XLOG_ID(wal_segsz_bytes) (0x100000000UL / (wal_segsz_bytes))

struct pending_segment
{
   char name[PATH_MAX];
   uint64_t segno;
};

static int parse_segment_filename(char* name, uint32_t* tli, uint64_t* segno, uint32_t wal_seg_size);
static int compare_pending(const void* a, const void* b);

int
walbridge_run_receiver(int srv, char** argv, struct lsn_map* map)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   char* d = NULL;
   char downstream_dir[MAX_PATH];
   DIR* dir = NULL;
   struct dirent* ent = NULL;
   pid_t pid;
   int child_status = 0;
   struct wal_store* store = NULL;
   bool store_created = false;
   uint32_t upstream_tli = 0;
   uint64_t last_segno = 0;
   struct pending_segment* pending = NULL;
   int pending_count = 0;
   int pending_capacity = 0;
   struct walfile* wf = NULL;
   char path[PATH_MAX];
   char* wal_name = NULL;

   pgmoneta_server_set_online(srv, true);

   if (config->common.servers[srv].wal_size == 0)
   {
      config->common.servers[srv].wal_size = DEFAULT_WAL_SEGZ_BYTES;
   }

   pid = fork();
   if (pid == -1)
   {
      pgmoneta_log_error("walbridge: fork failed: %m");
      return 1;
   }
   if (pid == 0)
   {
      /* child: run the existing WAL client which writes raw V18 WAL to disk */
      pgmoneta_wal(srv, argv);
      /* pgmoneta_wal exits, but be defensive */
      _exit(0);
   }

   /* parent: monitor WAL directory and translate completed segments */
   d = pgmoneta_get_server_wal(srv);
   if (!d)
   {
      pgmoneta_log_error("walbridge: could not determine server WAL directory");
      return 1;
   }

   /* downstream store lives in a dedicated sub-directory of the WAL dir */
   pgmoneta_snprintf(downstream_dir, sizeof(downstream_dir), "%s/walbridge", d);
   pgmoneta_mkdir(downstream_dir);

   pending = malloc(32 * sizeof(struct pending_segment));
   if (!pending)
   {
      free(d);
      return 1;
   }
   pending_capacity = 32;

   while (config->running && pgmoneta_server_is_online(srv))
   {
      /* if the WAL client died, stop processing (a restart re-syncs) */
      if (waitpid(pid, &child_status, WNOHANG) == pid)
      {
         if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0)
         {
            pgmoneta_log_error("walbridge: WAL client exited abnormally; stopping receiver");
            break;
         }
      }

      dir = opendir(d);
      if (!dir)
      {
         pgmoneta_log_error("walbridge: opendir(%s): %m", d);
         sleep(1);
         continue;
      }

      pending_count = 0;

      while ((ent = readdir(dir)) != NULL)
      {
         uint64_t segno = 0;

         if (ent->d_type == DT_DIR)
         {
            continue;
         }
         if (pgmoneta_ends_with(ent->d_name, ".partial"))
         {
            continue;
         }

         if (parse_segment_filename(ent->d_name, &upstream_tli, &segno, DEFAULT_WAL_SEGZ_BYTES))
         {
            continue;
         }

         if (pending_count == pending_capacity)
         {
            pending_capacity *= 2;
            struct pending_segment* tmp = realloc(pending, pending_capacity * sizeof(struct pending_segment));
            if (!tmp)
            {
               free(pending);
               closedir(dir);
               free(d);
               return 1;
            }
            pending = tmp;
         }

         pgmoneta_snprintf(pending[pending_count].name, sizeof(pending[pending_count].name), "%s", ent->d_name);
         pending[pending_count].segno = segno;
         pending_count++;
      }

      closedir(dir);

      /* process complete segments in ascending order */
      qsort(pending, pending_count, sizeof(struct pending_segment), compare_pending);

      for (int i = 0; i < pending_count; i++)
      {
         if (pending[i].segno <= last_segno)
         {
            continue;
         }

         pgmoneta_snprintf(path, sizeof(path), "%s/%s", d, pending[i].name);

         wf = calloc(1, sizeof(*wf));
         if (!wf)
         {
            pgmoneta_log_error("walbridge: memory allocation failed");
            break;
         }

         if (pgmoneta_deque_create(false, &wf->records) || pgmoneta_deque_create(false, &wf->page_headers))
         {
            pgmoneta_log_error("walbridge: failed to initialize WAL deques");
            pgmoneta_destroy_walfile(wf);
            wf = NULL;
            break;
         }

         if (pgmoneta_wal_parse_wal_file(path, srv, wf) != 0)
         {
            pgmoneta_log_error("walbridge: could not parse %s; skipping", path);
            pgmoneta_destroy_walfile(wf);
            wf = NULL;
            break;
         }

         if (!store_created)
         {
            uint32_t seg_size = wf->long_phd->xlp_seg_size ? wf->long_phd->xlp_seg_size : DEFAULT_WAL_SEGZ_BYTES;
            uint32_t blksz = wf->long_phd->xlp_xlog_blcksz;

            parse_segment_filename(pending[i].name, &upstream_tli, NULL, seg_size);

            pgmoneta_log_info("walbridge: creating downstream store (sysid=%llu segsize=%u blksz=%u tli=%u)",
                              (unsigned long long)wf->long_phd->xlp_sysid, seg_size, blksz, upstream_tli);

            if (wal_store_create(downstream_dir, map, wf->long_phd->xlp_sysid, seg_size, blksz, upstream_tli, &store))
            {
               pgmoneta_log_error("walbridge: could not create downstream store");
               pgmoneta_destroy_walfile(wf);
               wf = NULL;
               free(pending);
               free(d);
               return 1;
            }
            store_created = true;
         }

         {
            struct deque_iterator* iter = NULL;
            if (pgmoneta_deque_iterator_create(wf->records, &iter) == 0)
            {
               while (pgmoneta_deque_iterator_next(iter))
               {
                  struct decoded_xlog_record* rec = (struct decoded_xlog_record*)iter->value->data;
                  if (!rec || rec->partial)
                  {
                     continue;
                  }

                  if (migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, map))
                  {
                     pgmoneta_log_error("walbridge: translation failed for record at %X/%X in %s",
                                        LSN_FORMAT_ARGS(rec->lsn), path);
                     pgmoneta_deque_iterator_destroy(iter);
                     pgmoneta_destroy_walfile(wf);
                     wf = NULL;
                     free(pending);
                     free(d);
                     return 1;
                  }

                  if (wal_store_write_record(store, rec))
                  {
                     pgmoneta_log_error("walbridge: store failed for record at %X/%X in %s",
                                        LSN_FORMAT_ARGS(rec->lsn), path);
                     pgmoneta_deque_iterator_destroy(iter);
                     pgmoneta_destroy_walfile(wf);
                     wf = NULL;
                     free(pending);
                     free(d);
                     return 1;
                  }

                  /* expose the record to the downstream sender immediately */
                  if (wal_store_sync_partial_page(store))
                  {
                     pgmoneta_deque_iterator_destroy(iter);
                     pgmoneta_destroy_walfile(wf);
                     wf = NULL;
                     free(pending);
                     free(d);
                     return 1;
                  }
               }
               pgmoneta_deque_iterator_destroy(iter);
            }
         }

         last_segno = pending[i].segno;

         /* make the downstream stream durable segment by segment */
         wal_store_flush(store);

         pgmoneta_destroy_walfile(wf);
         wf = NULL;

         wal_name = pending[i].name;
         pgmoneta_log_info("walbridge: translated %s", wal_name);
         wal_name = NULL;
      }

      sleep(1);
   }

   if (store)
   {
      wal_store_destroy(store);
   }
   free(pending);
   free(d);
   return 0;
}

static int
parse_segment_filename(char* name, uint32_t* tli, uint64_t* segno, uint32_t wal_seg_size)
{
   uint32_t log;
   uint32_t seg;
   uint32_t tl;
   int items;

   /*
    * Only accept bare segment names (e.g. 000000010000000000000000).
    * Reject anything with a suffix (e.g. .zstd, .partial) so compressed
    * or incomplete files are never mistaken for raw WAL segments.
    */
   if (strlen(name) != 24)
   {
      return 1;
   }

   items = sscanf(name, "%08X%08X%08X", &tl, &log, &seg);
   if (items != 3)
   {
      return 1;
   }

   if (tli)
   {
      *tli = tl;
   }
   if (segno)
   {
      *segno = (uint64_t)log * XLOG_SEGMENTS_PER_XLOG_ID(wal_seg_size) + seg;
   }

   return 0;
}

static int
compare_pending(const void* a, const void* b)
{
   const struct pending_segment* pa = (const struct pending_segment*)a;
   const struct pending_segment* pb = (const struct pending_segment*)b;

   if (pa->segno < pb->segno)
   {
      return -1;
   }
   if (pa->segno > pb->segno)
   {
      return 1;
   }
   return 0;
}
