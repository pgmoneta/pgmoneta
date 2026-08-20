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
#include <compression.h>
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
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

extern void pgmoneta_wal(int srv, char** argv);

#define XLOG_SEGMENTS_PER_XLOG_ID(wal_segsz_bytes) (0x100000000UL / (wal_segsz_bytes))

struct pending_segment
{
   char name[PATH_MAX];
   uint64_t segno;
   bool compressed;
};

static int parse_segment_filename(char* name, uint32_t* tli, uint64_t* segno, uint32_t wal_seg_size);
static void prune_local_wal(char* directory, uint32_t wal_seg_size);
static void prune_downstream_wal(char* directory, uint64_t downstream, uint32_t wal_seg_size);
static int compare_pending(const void* a, const void* b);

static int
receiver_load_state(char* state_path, uint64_t* segno, uint64_t* xl_prev, uint64_t* next_lsn, uint64_t* last_up,
                    uint64_t* done_segno)
{
   FILE* f;

   *segno = 0;
   *xl_prev = 0;
   *next_lsn = 0;
   *last_up = 0;
   *done_segno = 0;

   f = fopen(state_path, "r");
   if (f == NULL)
   {
      return 0;
   }
   {
      int got5 = fscanf(f, "%lu %lu %lu %lu %lu", segno, xl_prev, next_lsn, last_up, done_segno);
      if (got5 == 4)
      {
         /* pre-done_segno state file: old 4-field layout, done_segno defaults to
          * the last fully translated segment reconstructed from last_up */
         *done_segno = *last_up > 0 ? *last_up / DEFAULT_WAL_SEGZ_BYTES : 0;
      }
      else if (got5 != 5)
      {
         pgmoneta_log_warn("walbridge: ignoring unreadable receiver state %s", state_path);
         fclose(f);
         return 1;
      }
   }
   fclose(f);
   return 0;
}

static int
receiver_persist_state(char* state_path, uint64_t segno, uint64_t xl_prev, uint64_t next_lsn, uint64_t last_up,
                       uint64_t done_segno)
{
   char tmp[PATH_MAX];
   FILE* f;

   pgmoneta_snprintf(tmp, sizeof(tmp), "%s.tmp", state_path);
   f = fopen(tmp, "w");
   if (f == NULL)
   {
      pgmoneta_log_error("walbridge: could not write receiver state %s: %m", tmp);
      return 1;
   }
   fprintf(f, "%lu %lu %lu %lu %lu\n", segno, xl_prev, next_lsn, last_up, done_segno);
   fflush(f);
   if (fsync(fileno(f)) != 0)
   {
      pgmoneta_log_error("walbridge: could not fsync receiver state %s: %m", tmp);
      fclose(f);
      unlink(tmp);
      return 1;
   }
   fclose(f);
   if (rename(tmp, state_path) != 0)
   {
      pgmoneta_log_error("walbridge: could not rename receiver state %s: %m", tmp);
      unlink(tmp);
      return 1;
   }
   if (pgmoneta_fsync_directory(state_path) != 0)
   {
      return 1;
   }
   return 0;
}

static int64_t
receiver_now_usec(void)
{
   struct timeval tv;
   gettimeofday(&tv, NULL);
   return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

int
pgmoneta_walbridge_run_receiver(int srv, char** argv, struct lsn_map* map)
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
   bool wal_level_logged = false;
   uint32_t upstream_tli = 0;
   uint64_t last_up = 0;
   uint64_t resume_segno = 0;
   uint64_t resume_xl_prev = 0;
   uint64_t resume_next_lsn = 0;
   char state_path[PATH_MAX];
   struct pending_segment* pending = NULL;
   int pending_count = 0;
   int pending_capacity = 0;
   struct walfile* wf = NULL;
   char path[PATH_MAX];
   char* wal_name = NULL;
   int64_t last_persist_ts = 0;
   uint64_t done_segno = 0;

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

   pgmoneta_snprintf(state_path, sizeof(state_path), "%s/state", downstream_dir);
   receiver_load_state(state_path, &resume_segno, &resume_xl_prev, &resume_next_lsn, &last_up, &done_segno);
   if (resume_next_lsn > 0)
   {
      pgmoneta_log_info("walbridge: resuming receiver at upstream %X/%X (downstream xl_prev %X/%X next %X/%X)",
                        LSN_FORMAT_ARGS(last_up),
                        LSN_FORMAT_ARGS(resume_xl_prev),
                        LSN_FORMAT_ARGS(resume_next_lsn));
   }

   pending = malloc(32 * sizeof(struct pending_segment));
   if (!pending)
   {
      free(d);
      return 1;
   }
   pending_capacity = 32;

   pgmoneta_log_info("walbridge: receiver forked wal client pid=%d", pid);

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
         prune_local_wal(d, config->common.servers[srv].wal_size);
         sleep(1);
         continue;
      }

      pending_count = 0;

      while ((ent = readdir(dir)) != NULL)
      {
         uint64_t segno = 0;
         bool compressed = false;
         char segment_name[PATH_MAX];

         if (ent->d_type == DT_DIR)
         {
            continue;
         }
         if (pgmoneta_ends_with(ent->d_name, ".partial"))
         {
            continue;
         }

         pgmoneta_snprintf(segment_name, sizeof(segment_name), "%s", ent->d_name);
         if (pgmoneta_ends_with(segment_name, ".zstd"))
         {
            segment_name[strlen(segment_name) - strlen(".zstd")] = '\0';
            compressed = true;
         }
         /* The filename's low part is expressed in units of the primary's
          * configured WAL segment size. */
         if (parse_segment_filename(segment_name, &upstream_tli, &segno,
                                    config->common.servers[srv].wal_size))
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
         pending[pending_count].compressed = compressed;
         pending_count++;
      }

      closedir(dir);

      /* process complete segments in ascending order */
      qsort(pending, pending_count, sizeof(struct pending_segment), compare_pending);

      for (int i = 0; i < pending_count; i++)
      {
         uint64_t resume_segno_gate = last_up / config->common.servers[srv].wal_size;
         if (pending[i].segno < resume_segno_gate || pending[i].segno <= done_segno)
         {
            continue;
         }

         pgmoneta_snprintf(path, sizeof(path), "%s/%s", d, pending[i].name);
         if (pending[i].compressed)
         {
            char decompressed[PATH_MAX];
            char decomp_dir[PATH_MAX];
            /* decompress into a scratch dir so the parsed file keeps the
             * canonical WAL segment basename (the parser rejects any other) */
            pgmoneta_snprintf(decomp_dir, sizeof(decomp_dir), "%s/walbridge-decomp", d);
            pgmoneta_mkdir(decomp_dir);
            pgmoneta_snprintf(decompressed, sizeof(decompressed), "%s/%s", decomp_dir, pending[i].name);
            decompressed[strlen(decompressed) - strlen(".zstd")] = '\0';
            if (pgmoneta_decompress_file(path, decompressed, COMPRESSION_NONE, NULL))
            {
               pgmoneta_log_error("walbridge: could not decompress %s", path);
               continue;
            }
            pgmoneta_snprintf(path, sizeof(path), "%s", decompressed);
         }

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
            if (pending[i].compressed)
               unlink(path);
            pgmoneta_log_error("walbridge: could not parse %s; skipping", path);
            pgmoneta_destroy_walfile(wf);
            wf = NULL;
            break;
         }

         if (!store_created)
         {
            uint32_t seg_size = wf->long_phd->xlp_seg_size ? wf->long_phd->xlp_seg_size : DEFAULT_WAL_SEGZ_BYTES;
            uint32_t blksz = wf->long_phd->xlp_xlog_blcksz;

            {
               char canonical_name[PATH_MAX];
               pgmoneta_snprintf(canonical_name, sizeof(canonical_name), "%s", pending[i].name);
               if (pending[i].compressed)
               {
                  canonical_name[strlen(canonical_name) - strlen(".zstd")] = '\0';
               }
               if (parse_segment_filename(canonical_name, &upstream_tli, NULL, seg_size))
               {
                  pgmoneta_log_error("walbridge: invalid upstream segment name %s", pending[i].name);
                  pgmoneta_destroy_walfile(wf);
                  free(pending);
                  free(d);
                  return 1;
               }
            }

            pgmoneta_log_info("walbridge: creating downstream store (sysid=%llu segsize=%u blksz=%u tli=%u)",
                              (unsigned long long)wf->long_phd->xlp_sysid, seg_size, blksz, upstream_tli);

            if (pgmoneta_wal_store_create(downstream_dir, map, wf->long_phd->xlp_sysid, seg_size, blksz, upstream_tli, &store))
            {
               pgmoneta_log_error("walbridge: could not create downstream store");
               pgmoneta_destroy_walfile(wf);
               wf = NULL;
               free(pending);
               free(d);
               return 1;
            }
            pgmoneta_wal_store_set_checksums(store, config->common.servers[srv].checksums);
            store_created = true;

            if (resume_next_lsn > 0)
            {
               if (pgmoneta_wal_store_resume(store, resume_xl_prev, resume_next_lsn))
               {
                  pgmoneta_log_error("walbridge: could not resume downstream store");
                  pgmoneta_destroy_walfile(wf);
                  wf = NULL;
                  free(pending);
                  free(d);
                  return 1;
               }
            }
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
                  if (rec->lsn <= last_up)
                  {
                     /* already translated before a restart */
                     continue;
                  }

                  if (!wal_level_logged)
                  {
                     int wl = -1;
                     if (pgmoneta_migration_engine_get_wal_level(rec, &wl) == 0)
                     {
                        pgmoneta_log_info("walbridge: upstream wal_level %d (%s)",
                                          wl, wl == WAL_LEVEL_LOGICAL ? "logical" : "replica");
                        wal_level_logged = true;
                     }
                  }

                  int result = pgmoneta_migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, map,
                                                                   config->common.servers[srv].checksums);
                  if (result < 0)
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
                  if (result == PGMONETA_MIGRATION_DROP)
                  {
                     /* no-op record: do not emit it downstream and do not
                       * create an LSN mapping for it */
                     last_up = rec->lsn;
                     continue;
                  }

                  if (pgmoneta_wal_store_write_record(store, rec))
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
                  if (pgmoneta_wal_store_sync_partial_page(store))
                  {
                     pgmoneta_deque_iterator_destroy(iter);
                     pgmoneta_destroy_walfile(wf);
                     wf = NULL;
                     free(pending);
                     free(d);
                     return 1;
                  }

                  last_up = rec->lsn;

                  /* persist resume state so a restart can continue in place.
                     * Throttle to at most once a second: a full fopen/fwrite/
                     * rename per record would dominate translation throughput.
                     * Ensure the store, map, and state are all durable in order
                     * before advancing the persisted state. */
                  {
                     int64_t now = receiver_now_usec();
                     if (last_persist_ts == 0 || now - last_persist_ts > 1000000)
                     {
                        if (pgmoneta_wal_store_flush(store))
                        {
                           pgmoneta_deque_iterator_destroy(iter);
                           pgmoneta_destroy_walfile(wf);
                           wf = NULL;
                           free(pending);
                           free(d);
                           return 1;
                        }
                        if (pgmoneta_lsn_map_flush(pgmoneta_wal_store_get_map(store)))
                        {
                           pgmoneta_deque_iterator_destroy(iter);
                           pgmoneta_destroy_walfile(wf);
                           wf = NULL;
                           free(pending);
                           free(d);
                           return 1;
                        }
                        uint64_t xl_prev;
                        uint64_t next_lsn;
                        pgmoneta_wal_store_get_state(store, NULL, &xl_prev, &next_lsn);
                        if (receiver_persist_state(state_path, next_lsn > 0 ? (next_lsn - 1) / config->common.servers[srv].wal_size : 0,
                                                   xl_prev, next_lsn, last_up, done_segno))
                        {
                           pgmoneta_deque_iterator_destroy(iter);
                           pgmoneta_destroy_walfile(wf);
                           wf = NULL;
                           free(pending);
                           free(d);
                           return 1;
                        }
                        last_persist_ts = now;
                     }
                  }
               }
               pgmoneta_deque_iterator_destroy(iter);
            }
         }

         /* make the downstream stream durable segment by segment */
         if (pgmoneta_wal_store_flush(store))
         {
            pgmoneta_destroy_walfile(wf);
            wf = NULL;
            free(pending);
            free(d);
            return 1;
         }

         pgmoneta_destroy_walfile(wf);
         wf = NULL;
         if (pending[i].compressed)
            unlink(path);

         wal_name = pending[i].name;
         {
            uint64_t dbg_xl_prev;
            uint64_t dbg_next_lsn;
            pgmoneta_wal_store_get_state(store, NULL, &dbg_xl_prev, &dbg_next_lsn);
            pgmoneta_log_info("walbridge: translated %s (last_up=%X/%X inmem store next=%X/%X)",
                              wal_name, LSN_FORMAT_ARGS(last_up), LSN_FORMAT_ARGS(dbg_next_lsn));
         }
         wal_name = NULL;

         /* the segment is fully translated: record its watermark so a later
          * scan skips it, and persist the final resume state so a restart does
          * not re-translate records already delivered downstream.
          * Ordering: store already fsynced above, now flush map then persist state. */
         done_segno = pending[i].segno;
         {
            if (pgmoneta_lsn_map_flush(pgmoneta_wal_store_get_map(store)))
            {
               free(pending);
               free(d);
               return 1;
            }
            uint64_t xl_prev;
            uint64_t next_lsn;
            pgmoneta_wal_store_get_state(store, NULL, &xl_prev, &next_lsn);
            if (receiver_persist_state(state_path, next_lsn > 0 ? (next_lsn - 1) / config->common.servers[srv].wal_size : 0,
                                       xl_prev, next_lsn, last_up, done_segno))
            {
               free(pending);
               free(d);
               return 1;
            }
            last_persist_ts = receiver_now_usec();
         }
      }

      /* prune retained raw upstream WAL once the replica has flushed past it.
       * The feedback watermark is only ever at or behind the translated point,
       * so this cannot remove a segment we still need. */
      prune_local_wal(d, config->common.servers[srv].wal_size);
      {
         char feedback[PATH_MAX];
         uint64_t ignored_upstream = 0;
         uint64_t downstream = 0;
         FILE* feedback_file;

         pgmoneta_snprintf(feedback, sizeof(feedback), "%s/walbridge.lsnmap.feedback", d);
         feedback_file = fopen(feedback, "r");
         if (feedback_file != NULL)
         {
            if (fscanf(feedback_file, "%lu %lu", &ignored_upstream, &downstream) == 2)
            {
               prune_downstream_wal(downstream_dir, downstream, config->common.servers[srv].wal_size);
            }
            fclose(feedback_file);
         }
      }

      sleep(1);
   }

   if (store)
   {
      int shutdown_failed = 0;

      /* final persistence: the store, the LSN map and the resume state must
       * all be durable before the process exits, otherwise a restart would
       * resume from a stale position. Report any failure so the shutdown is
       * flagged instead of silently swallowing it. */
      if (pgmoneta_wal_store_flush(store))
      {
         shutdown_failed = 1;
      }
      if (pgmoneta_lsn_map_flush(pgmoneta_wal_store_get_map(store)))
      {
         shutdown_failed = 1;
      }
      {
         uint64_t xl_prev;
         uint64_t next_lsn;
         pgmoneta_wal_store_get_state(store, NULL, &xl_prev, &next_lsn);
         if (receiver_persist_state(state_path,
                                    next_lsn > 0 ? (next_lsn - 1) / config->common.servers[srv].wal_size : 0,
                                    xl_prev, next_lsn, last_up, done_segno))
         {
            shutdown_failed = 1;
         }
      }
      if (shutdown_failed)
      {
         pgmoneta_log_error("walbridge: downstream stream, map or resume state not fully persisted at shutdown");
      }

      pgmoneta_wal_store_destroy(store);
      if (shutdown_failed)
      {
         free(pending);
         free(d);
         return 1;
      }
   }
   free(pending);
   free(d);
   return 0;
}

static void
prune_local_wal(char* directory, uint32_t wal_seg_size)
{
   char feedback[PATH_MAX];
   FILE* file;
   uint64_t upstream = 0;
   uint64_t downstream = 0;
   uint64_t keep_segno;
   DIR* dir;
   struct dirent* entry;

   pgmoneta_snprintf(feedback, sizeof(feedback), "%s/walbridge.lsnmap.feedback", directory);
   file = fopen(feedback, "r");
   if (file == NULL)
   {
      return;
   }
   if (fscanf(file, "%lu %lu", &upstream, &downstream) != 2)
   {
      fclose(file);
      return;
   }
   fclose(file);

   keep_segno = upstream / wal_seg_size;
   dir = opendir(directory);
   if (dir == NULL)
   {
      return;
   }
   while ((entry = readdir(dir)) != NULL)
   {
      char name[PATH_MAX];
      char path[PATH_MAX];
      uint64_t segno = 0;

      pgmoneta_snprintf(name, sizeof(name), "%s", entry->d_name);
      if (pgmoneta_ends_with(name, ".zstd"))
      {
         name[strlen(name) - strlen(".zstd")] = '\0';
      }
      if (parse_segment_filename(name, NULL, &segno, wal_seg_size) == 0 && segno < keep_segno)
      {
         pgmoneta_snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
         if (unlink(path) == 0)
         {
            pgmoneta_log_info("walbridge: pruned retained raw WAL %s", entry->d_name);
         }
      }
   }
   closedir(dir);
}

/* Keep the segment containing the confirmed downstream flush position: a
 * reconnect can legally begin inside it. */
static void
prune_downstream_wal(char* directory, uint64_t downstream, uint32_t wal_seg_size)
{
   DIR* dir;
   struct dirent* entry;
   uint64_t keep_segno = downstream / wal_seg_size;

   dir = opendir(directory);
   if (dir == NULL)
   {
      return;
   }
   while ((entry = readdir(dir)) != NULL)
   {
      char path[PATH_MAX];
      uint64_t segno = 0;

      if (parse_segment_filename(entry->d_name, NULL, &segno, wal_seg_size) == 0 && segno < keep_segno)
      {
         pgmoneta_snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
         if (unlink(path) == 0)
         {
            pgmoneta_log_info("walbridge: pruned translated WAL %s", entry->d_name);
         }
      }
   }
   closedir(dir);
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
