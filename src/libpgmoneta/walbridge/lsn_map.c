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
#include <walbridge/lsn_map.h>

/* system */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

struct lsn_pair
{
   uint64_t up;
   uint64_t down;
};

struct lsn_map
{
   char* path;
   struct lsn_pair* pairs;
   size_t count;
   size_t capacity;
   size_t durable_count;
   int64_t last_persist_us;
};

static int
lsn_map_load(struct lsn_map* map)
{
   FILE* f = NULL;
   uint64_t up, down;

   f = fopen(map->path, "r");
   if (!f)
   {
      if (errno == ENOENT)
      {
         return 0;
      }
      pgmoneta_log_error("lsn_map: open %s: %m", map->path);
      return 1;
   }

   while (fscanf(f, "%lu %lu\n", &up, &down) == 2)
   {
      if (map->count + 1 > map->capacity)
      {
         size_t nc = map->capacity == 0 ? 128 : map->capacity * 2;
         struct lsn_pair* np = realloc(map->pairs, nc * sizeof(*np));
         if (!np)
         {
            fclose(f);
            return 1;
         }
         map->pairs = np;
         map->capacity = nc;
      }
      map->pairs[map->count].up = up;
      map->pairs[map->count].down = down;
      map->count++;
   }

   fclose(f);
   return 0;
}

int
pgmoneta_lsn_map_create(const char* path, struct lsn_map** map)
{
   struct lsn_map* m = calloc(1, sizeof(*m));
   if (!m)
   {
      return 1;
   }
   m->path = strdup(path);
   if (lsn_map_load(m))
   {
      free(m->path);
      free(m);
      return 1;
   }
   m->durable_count = m->count;
   *map = m;
   return 0;
}

static int
lsn_map_persist(struct lsn_map* map)
{
   FILE* f = NULL;

   /* Append-only persistence: rewriting the whole map on every flush is
    * O(n^2) once the map grows large (millions of entries) and stalls the
    * translation loop. Only the pairs added since the last persist are written,
    * which keeps per-record cost O(1). The file is loaded in full on startup
    * (lsn_map_load), so appending preserves the on-disk format. */
   if (map->durable_count < map->count)
   {
      f = fopen(map->path, "a");
      if (!f)
      {
         pgmoneta_log_error("lsn_map: fopen %s: %m", map->path);
         return 1;
      }
      for (size_t i = map->durable_count; i < map->count; i++)
      {
         fprintf(f, "%lu %lu\n", map->pairs[i].up, map->pairs[i].down);
      }
      fflush(f);
      if (fsync(fileno(f)) != 0)
      {
         pgmoneta_log_error("lsn_map: fsync %s: %m", map->path);
         fclose(f);
         return 1;
      }
      fclose(f);
      map->durable_count = map->count;
   }
   if (pgmoneta_fsync_directory(map->path) != 0)
   {
      return 1;
   }
   return 0;
}

int
pgmoneta_lsn_map_flush(struct lsn_map* map)
{
   if (!map)
   {
      return 1;
   }
   if (lsn_map_persist(map))
   {
      return 1;
   }
   map->last_persist_us = 0;
   return 0;
}

int
pgmoneta_lsn_map_put(struct lsn_map* map, uint64_t upstream, uint64_t downstream)
{
   if (!map)
   {
      return 1;
   }
   if (map->count + 1 > map->capacity)
   {
      size_t nc = map->capacity == 0 ? 128 : map->capacity * 2;
      struct lsn_pair* np = realloc(map->pairs, nc * sizeof(*np));
      if (!np)
      {
         return 1;
      }
      map->pairs = np;
      map->capacity = nc;
   }
   map->pairs[map->count].up = upstream;
   map->pairs[map->count].down = downstream;
   map->count++;

   /* Persist throttled: rewriting the whole file with an fsync on every record
    * is O(N^2) and would dominate translation throughput. Persist at most once
    * a second; the in-memory map stays authoritative for lookups and a final
    * persist happens on destroy. */
   {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      int64_t now = (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
      if (map->last_persist_us == 0 || now - map->last_persist_us > 1000000)
      {
         if (lsn_map_persist(map))
         {
            return 1;
         }
         map->last_persist_us = now;
      }
   }

   return 0;
}

int
pgmoneta_lsn_map_get_downstream(struct lsn_map* map, uint64_t upstream, uint64_t* downstream)
{
   if (!map)
   {
      return 1;
   }
   for (size_t i = 0; i < map->count; i++)
   {
      if (map->pairs[i].up == upstream)
      {
         *downstream = map->pairs[i].down;
         return 0;
      }
   }
   return 1;
}

int
pgmoneta_lsn_map_get_downstream_at_or_before(struct lsn_map* map, uint64_t upstream, uint64_t* upstream_found, uint64_t* downstream)
{
   bool found = false;
   uint64_t best_up = 0;
   uint64_t best_down = 0;

   if (!map)
   {
      return 1;
   }
   for (size_t i = 0; i < map->count; i++)
   {
      if (map->pairs[i].up <= upstream &&
          (!found || map->pairs[i].up > best_up))
      {
         found = true;
         best_up = map->pairs[i].up;
         best_down = map->pairs[i].down;
      }
   }
   if (!found)
   {
      return 1;
   }
   if (upstream_found)
   {
      *upstream_found = best_up;
   }
   if (downstream)
   {
      *downstream = best_down;
   }
   return 0;
}

int
pgmoneta_lsn_map_get_upstream(struct lsn_map* map, uint64_t downstream, uint64_t* upstream)
{
   if (!map)
   {
      return 1;
   }
   for (size_t i = 0; i < map->count; i++)
   {
      if (map->pairs[i].down == downstream)
      {
         *upstream = map->pairs[i].up;
         return 0;
      }
   }
   return 1;
}

int
pgmoneta_lsn_map_get_upstream_at_or_before(struct lsn_map* map, uint64_t downstream, uint64_t* upstream)
{
   bool found = false;
   uint64_t best_down = 0;
   uint64_t best_up = 0;

   if (!map || !upstream)
   {
      return 1;
   }
   for (size_t i = 0; i < map->count; i++)
   {
      if (map->pairs[i].down <= downstream && (!found || map->pairs[i].down > best_down))
      {
         found = true;
         best_down = map->pairs[i].down;
         best_up = map->pairs[i].up;
      }
   }
   if (!found)
   {
      return 1;
   }
   *upstream = best_up;
   return 0;
}

void
pgmoneta_lsn_map_destroy(struct lsn_map* map)
{
   if (!map)
   {
      return;
   }
   free(map->path);
   free(map->pairs);
   free(map);
}
