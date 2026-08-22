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
lsn_map_create(const char* path, struct lsn_map** map)
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
   *map = m;
   return 0;
}

static int
lsn_map_persist(struct lsn_map* map)
{
   FILE* f = NULL;
   char tmp[PATH_MAX];

   pgmoneta_snprintf(tmp, sizeof(tmp), "%s.tmp", map->path);
   f = fopen(tmp, "w");
   if (!f)
   {
      pgmoneta_log_error("lsn_map: fopen %s: %m", tmp);
      return 1;
   }
   for (size_t i = 0; i < map->count; i++)
   {
      fprintf(f, "%lu %lu\n", map->pairs[i].up, map->pairs[i].down);
   }
   fflush(f);
   fsync(fileno(f));
   fclose(f);
   if (rename(tmp, map->path) != 0)
   {
      pgmoneta_log_error("lsn_map: rename %s->%s: %m", tmp, map->path);
      unlink(tmp);
      return 1;
   }
   return 0;
}

int
lsn_map_put(struct lsn_map* map, uint64_t upstream, uint64_t downstream)
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

   return lsn_map_persist(map);
}

int
lsn_map_get_downstream(struct lsn_map* map, uint64_t upstream, uint64_t* downstream)
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
lsn_map_get_downstream_at_or_before(struct lsn_map* map, uint64_t upstream, uint64_t* upstream_found, uint64_t* downstream)
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
lsn_map_get_upstream(struct lsn_map* map, uint64_t downstream, uint64_t* upstream)
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

void
lsn_map_destroy(struct lsn_map* map)
{
   if (!map)
   {
      return;
   }
   free(map->path);
   free(map->pairs);
   free(map);
}
