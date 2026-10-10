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
#include <walfile/wal_reader.h>
#include <server.h>
#include <walbridge/lsn_map.h>

/* system */
#include <dirent.h>
#include <inttypes.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern void pgmoneta_wal(int srv, char** argv);

#define XLOG_SEGMENTS_PER_XLOG_ID(wal_segsz_bytes) (0x100000000UL / (wal_segsz_bytes))

static int parse_segment_filename(const char* name, uint32_t* tli, uint64_t* segno, uint32_t wal_seg_size);
static void prune_local_wal(const char* directory, uint32_t wal_seg_size, uint64_t keep_segno);

uint64_t
pgmoneta_wal_receiver_retention_segno(uint64_t upstream_lsn, uint32_t wal_seg_size)
{
   return wal_seg_size == 0 ? 0 : upstream_lsn / wal_seg_size;
}

int
pgmoneta_wal_receiver_prune_confirmed_raw(const char* directory, const char* feedback_path,
                                          uint32_t wal_seg_size)
{
   FILE* feedback;
   uint64_t upstream = 0;
   uint64_t downstream = 0;

   if (!directory || !feedback_path || wal_seg_size == 0)
   {
      return 1;
   }
   feedback = fopen(feedback_path, "r");
   if (feedback == NULL)
   {
      return 0; /* no confirmation: retain everything */
   }
   if (fscanf(feedback, "%" SCNu64 " %" SCNu64, &upstream, &downstream) == 2)
   {
      prune_local_wal(directory, wal_seg_size,
                      pgmoneta_wal_receiver_retention_segno(upstream, wal_seg_size));
   }
   fclose(feedback);
   return 0;
}

int
pgmoneta_walbridge_run_receiver(int srv, char** argv)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   char* wal_dir = NULL;
   pid_t pid;
   int child_status = 0;

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
      pgmoneta_wal(srv, argv);
      _exit(0);
   }

   wal_dir = pgmoneta_get_server_wal(srv);
   if (!wal_dir)
   {
      pgmoneta_log_error("walbridge: could not determine server WAL directory");
      kill(pid, SIGTERM);
      waitpid(pid, NULL, 0);
      return 1;
   }

   while (config->running)
   {
      char feedback[PATH_MAX];

      if (waitpid(pid, &child_status, WNOHANG) == pid)
      {
         pid = fork();
         if (pid == -1)
         {
            pgmoneta_log_error("walbridge: re-fork of WAL client failed: %m");
            break;
         }
         if (pid == 0)
         {
            pgmoneta_wal(srv, argv);
            _exit(0);
         }
         pgmoneta_log_warn("walbridge: WAL client exited; re-forked raw collector pid=%d", pid);
      }

      pgmoneta_snprintf(feedback, sizeof(feedback), "%s/walbridge.lsnmap.feedback", wal_dir);
      pgmoneta_wal_receiver_prune_confirmed_raw(wal_dir, feedback,
                                                config->common.servers[srv].wal_size);
      sleep(1);
   }

   if (pid > 0)
   {
      kill(pid, SIGTERM);
      waitpid(pid, NULL, 0);
   }
   free(wal_dir);
   return 0;
}

static void
prune_local_wal(const char* directory, uint32_t wal_seg_size, uint64_t keep_segno)
{
   DIR* dir;
   struct dirent* entry;

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

static int
parse_segment_filename(const char* name, uint32_t* tli, uint64_t* segno, uint32_t wal_seg_size)
{
   uint32_t log;
   uint32_t seg;
   uint32_t tl;
   int items;

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
