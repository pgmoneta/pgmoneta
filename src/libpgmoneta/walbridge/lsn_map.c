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
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * In memory the map is an array of (upstream, downstream) pairs kept sorted
 * by upstream LSN with no two pairs sharing an upstream LSN, so every lookup
 * is a binary search. On disk the map is an append-only text file, one
 * "<upstream> <downstream>\n" pair per line, in the order the pairs were
 * persisted. The file may contain legacy duplicates, malformed lines or a
 * partial trailing line (a crash mid-write); loading and tail reads
 * normalize it into the sorted, unique in-memory form.
 *
 * Pairs added with pgmoneta_lsn_map_put() are also queued in `pending`, in
 * insertion order, until a persist appends them to the file.
 */

struct lsn_pair
{
   uint64_t up;
   uint64_t down;
};

struct lsn_map
{
   char* path;
   struct lsn_pair* pairs; /* sorted by up, unique up */
   size_t count;
   size_t capacity;
   struct lsn_pair* pending; /* not yet persisted, insertion order */
   size_t pending_count;
   size_t pending_capacity;
   bool down_monotonic;    /* pairs[].down is non-decreasing too */
   uint64_t durable_bytes; /* byte offset in the file that this map reflects */
   int64_t last_persist_us;
};

static int
lsn_map_reserve(struct lsn_pair** array, size_t* capacity, size_t needed)
{
   size_t nc;
   struct lsn_pair* np;

   if (needed <= *capacity)
   {
      return 0;
   }
   nc = *capacity == 0 ? 128 : *capacity;
   while (nc < needed)
   {
      nc *= 2;
   }
   np = realloc(*array, nc * sizeof(*np));
   if (np == NULL)
   {
      return 1;
   }
   *array = np;
   *capacity = nc;
   return 0;
}

/* First index in [0, count) whose up is >= up. */
static size_t
lower_bound_up(struct lsn_map* map, uint64_t up)
{
   size_t lo = 0;
   size_t hi = map->count;

   while (lo < hi)
   {
      size_t mid = lo + (hi - lo) / 2;
      if (map->pairs[mid].up < up)
      {
         lo = mid + 1;
      }
      else
      {
         hi = mid;
      }
   }
   return lo;
}

/* First index in [0, count) whose up is > up. */
static size_t
upper_bound_up(struct lsn_map* map, uint64_t up)
{
   size_t lo = 0;
   size_t hi = map->count;

   while (lo < hi)
   {
      size_t mid = lo + (hi - lo) / 2;
      if (map->pairs[mid].up <= up)
      {
         lo = mid + 1;
      }
      else
      {
         hi = mid;
      }
   }
   return lo;
}

/* First index in [0, count) whose down is >= down. Requires down_monotonic. */
static size_t
lower_bound_down(struct lsn_map* map, uint64_t down)
{
   size_t lo = 0;
   size_t hi = map->count;

   while (lo < hi)
   {
      size_t mid = lo + (hi - lo) / 2;
      if (map->pairs[mid].down < down)
      {
         lo = mid + 1;
      }
      else
      {
         hi = mid;
      }
   }
   return lo;
}

/* First index in [0, count) whose down is > down. Requires down_monotonic. */
static size_t
upper_bound_down(struct lsn_map* map, uint64_t down)
{
   size_t lo = 0;
   size_t hi = map->count;

   while (lo < hi)
   {
      size_t mid = lo + (hi - lo) / 2;
      if (map->pairs[mid].down <= down)
      {
         lo = mid + 1;
      }
      else
      {
         hi = mid;
      }
   }
   return lo;
}

/* Insert a pair keeping pairs[] sorted and unique by up.
 * Returns 0 when inserted or when the identical pair is already present,
 * 1 on a conflicting pair (same up, different down), -1 on allocation failure.
 * With persist, a newly inserted pair is also queued for the next persist. */
static int
lsn_map_insert(struct lsn_map* map, uint64_t up, uint64_t down, bool persist)
{
   size_t idx;

   if (map->count > 0 && map->pairs[map->count - 1].up < up)
   {
      /* the normal case: records arrive in upstream order */
      idx = map->count;
   }
   else
   {
      idx = lower_bound_up(map, up);
      if (idx < map->count && map->pairs[idx].up == up)
      {
         if (map->pairs[idx].down == down)
         {
            return 0;
         }
         pgmoneta_log_error("lsn_map: conflicting mapping for upstream %" PRIu64 ": %" PRIu64 " != %" PRIu64,
                            up, map->pairs[idx].down, down);
         return 1;
      }
   }

   if (lsn_map_reserve(&map->pairs, &map->capacity, map->count + 1))
   {
      return -1;
   }
   if (persist && lsn_map_reserve(&map->pending, &map->pending_capacity, map->pending_count + 1))
   {
      return -1;
   }

   if (idx < map->count)
   {
      memmove(&map->pairs[idx + 1], &map->pairs[idx], (map->count - idx) * sizeof(struct lsn_pair));
   }
   map->pairs[idx].up = up;
   map->pairs[idx].down = down;
   map->count++;

   if (map->down_monotonic)
   {
      if ((idx > 0 && map->pairs[idx - 1].down > down) ||
          (idx + 1 < map->count && map->pairs[idx + 1].down < down))
      {
         map->down_monotonic = false;
      }
   }

   if (persist)
   {
      map->pending[map->pending_count].up = up;
      map->pending[map->pending_count].down = down;
      map->pending_count++;
   }

   return 0;
}

/* Parse one whole line "<up> <down>\n". Returns 0 on success. */
static int
lsn_map_parse_line(const char* line, uint64_t* up, uint64_t* down)
{
   char term = 0;

   if (sscanf(line, "%" SCNu64 " %" SCNu64 "%c", up, down, &term) != 3 || term != '\n')
   {
      return 1;
   }
   return 0;
}

struct lsn_load_entry
{
   uint64_t up;
   uint64_t down;
   size_t order;
};

static int
lsn_load_entry_compare(const void* a, const void* b)
{
   const struct lsn_load_entry* x = a;
   const struct lsn_load_entry* y = b;

   if (x->up != y->up)
   {
      return x->up < y->up ? -1 : 1;
   }
   if (x->order != y->order)
   {
      return x->order < y->order ? -1 : 1;
   }
   return 0;
}

static int
lsn_map_load(struct lsn_map* map)
{
   FILE* f = NULL;
   char* line = NULL;
   size_t cap = 0;
   ssize_t n = 0;
   uint64_t pos = 0;
   struct lsn_load_entry* raw = NULL;
   size_t raw_count = 0;
   size_t raw_capacity = 0;
   size_t duplicates = 0;
   size_t conflicts = 0;
   size_t malformed = 0;

   map->down_monotonic = true;

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

   while ((n = getline(&line, &cap, f)) != -1)
   {
      uint64_t up = 0;
      uint64_t down = 0;

      if (n == 0)
      {
         continue;
      }
      if (line[n - 1] != '\n')
      {
         /* partial trailing line (crash mid-write): not durable */
         break;
      }
      pos += (uint64_t)n;
      if (lsn_map_parse_line(line, &up, &down))
      {
         malformed++;
         continue;
      }
      if (raw_count + 1 > raw_capacity)
      {
         size_t nc = raw_capacity == 0 ? 128 : raw_capacity * 2;
         struct lsn_load_entry* np = realloc(raw, nc * sizeof(*np));
         if (!np)
         {
            goto error;
         }
         raw = np;
         raw_capacity = nc;
      }
      raw[raw_count].up = up;
      raw[raw_count].down = down;
      raw[raw_count].order = raw_count;
      raw_count++;
   }

   /* stable sort by upstream, then keep the first entry per upstream LSN */
   if (raw_count > 1)
   {
      qsort(raw, raw_count, sizeof(*raw), lsn_load_entry_compare);
   }
   if (lsn_map_reserve(&map->pairs, &map->capacity, raw_count))
   {
      goto error;
   }
   for (size_t i = 0; i < raw_count; i++)
   {
      if (map->count > 0 && map->pairs[map->count - 1].up == raw[i].up)
      {
         if (map->pairs[map->count - 1].down == raw[i].down)
         {
            duplicates++;
         }
         else
         {
            conflicts++;
         }
         continue;
      }
      if (map->count > 0 && map->pairs[map->count - 1].down > raw[i].down)
      {
         map->down_monotonic = false;
      }
      map->pairs[map->count].up = raw[i].up;
      map->pairs[map->count].down = raw[i].down;
      map->count++;
   }

   if (duplicates > 0 || conflicts > 0 || malformed > 0)
   {
      pgmoneta_log_warn("lsn_map: %s: normalized on load (%zu duplicate, %zu conflicting, %zu malformed lines)",
                        map->path, duplicates, conflicts, malformed);
   }

   map->durable_bytes = pos;

   free(raw);
   free(line);
   fclose(f);
   return 0;

error:
   free(raw);
   free(line);
   fclose(f);
   return 1;
}

int
pgmoneta_lsn_map_create(const char* path, struct lsn_map** map)
{
   struct lsn_map* m = NULL;

   if (path == NULL || map == NULL)
   {
      goto error;
   }
   *map = NULL;

   m = calloc(1, sizeof(struct lsn_map));
   if (m == NULL)
   {
      goto error;
   }
   m->down_monotonic = true;
   m->path = strdup(path);
   if (m->path == NULL)
   {
      goto error;
   }
   if (lsn_map_load(m))
   {
      goto error;
   }
   *map = m;
   return 0;

error:
   pgmoneta_lsn_map_destroy(m);
   return 1;
}

static int
lsn_map_write_all(int fd, const char* buf, size_t len)
{
   size_t done = 0;

   while (done < len)
   {
      ssize_t w = write(fd, buf + done, len - done);
      if (w < 0)
      {
         if (errno == EINTR)
         {
            continue;
         }
         return 1;
      }
      if (w == 0)
      {
         errno = EIO;
         return 1;
      }
      done += (size_t)w;
   }
   return 0;
}

static int
lsn_map_persist(struct lsn_map* map)
{
   int fd = -1;
   struct stat st;
   off_t pre = 0;
   char* buf = NULL;
   size_t len = 0;
   size_t buf_cap = 0;
   char last = '\n';

   /* Append-only persistence: only the pairs added since the last persist
    * are written, as one batch, so the per-record cost stays O(1). On any
    * failure the file is truncated back to its size before the batch, and
    * the batch stays pending, so a retry never leaves duplicates or a torn
    * line behind. Should the truncate itself fail, loading and tail reads
    * skip duplicates and partial lines anyway. */
   if (map->pending_count > 0)
   {
      fd = open(map->path, O_WRONLY | O_APPEND | O_CREAT, 0600);
      if (fd < 0)
      {
         pgmoneta_log_error("lsn_map: open %s: %m", map->path);
         return 1;
      }
      if (fstat(fd, &st) != 0)
      {
         pgmoneta_log_error("lsn_map: fstat %s: %m", map->path);
         goto error_close;
      }
      pre = st.st_size;

      /* A torn line left by a crash would swallow the first pair of this
       * batch. Terminate it with a non-digit so it reads back as one skipped
       * malformed line: a bare newline could turn e.g. "200 8" (torn from
       * "200 80") into a valid but wrong pair. */
      if (pre > 0)
      {
         int rfd = open(map->path, O_RDONLY);
         if (rfd >= 0)
         {
            if (pread(rfd, &last, 1, pre - 1) != 1)
            {
               last = '\n';
            }
            close(rfd);
         }
      }

      /* each line is at most 2 * 20 digits + ' ' + '\n' */
      buf_cap = map->pending_count * 42 + 2;
      buf = malloc(buf_cap);
      if (buf == NULL)
      {
         goto error_close;
      }
      if (last != '\n')
      {
         buf[len++] = '!';
         buf[len++] = '\n';
      }
      for (size_t i = 0; i < map->pending_count; i++)
      {
         int w = snprintf(buf + len, buf_cap - len, "%" PRIu64 " %" PRIu64 "\n",
                          map->pending[i].up, map->pending[i].down);
         if (w < 0 || (size_t)w >= buf_cap - len)
         {
            goto error_close;
         }
         len += (size_t)w;
      }

      if (lsn_map_write_all(fd, buf, len))
      {
         pgmoneta_log_error("lsn_map: write %s: %m", map->path);
         goto error_truncate;
      }
      if (fsync(fd) != 0)
      {
         pgmoneta_log_error("lsn_map: fsync %s: %m", map->path);
         goto error_truncate;
      }
      close(fd);
      fd = -1;
      free(buf);
      buf = NULL;

      /* Advance the offset this map reflects only when nothing foreign sits
       * between it and the batch; otherwise leave it so a tail read still
       * picks up the other bytes (our own pairs are then skipped as known). */
      if (map->durable_bytes == (uint64_t)pre)
      {
         map->durable_bytes = (uint64_t)pre + len;
      }
      map->pending_count = 0;
   }
   if (pgmoneta_fsync_directory(map->path) != 0)
   {
      return 1;
   }
   return 0;

error_truncate:
   if (ftruncate(fd, pre) != 0)
   {
      pgmoneta_log_error("lsn_map: could not truncate %s back to %" PRIu64 ": %m",
                         map->path, (uint64_t)pre);
   }
error_close:
   free(buf);
   if (fd >= 0)
   {
      close(fd);
   }
   return 1;
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
   if (lsn_map_insert(map, upstream, downstream, true) != 0)
   {
      return 1;
   }

   /* Persist throttled: an fsync on every record would dominate translation
    * throughput. Persist at most once a second; the in-memory map stays
    * authoritative for lookups and callers flush before exit. */
   if (map->pending_count > 0)
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
   size_t idx;

   if (!map)
   {
      return 1;
   }
   idx = lower_bound_up(map, upstream);
   if (idx < map->count && map->pairs[idx].up == upstream)
   {
      *downstream = map->pairs[idx].down;
      return 0;
   }
   return 1;
}

int
pgmoneta_lsn_map_get_downstream_at_or_before(struct lsn_map* map, uint64_t upstream, uint64_t* upstream_found, uint64_t* downstream)
{
   size_t idx;

   if (!map)
   {
      return 1;
   }
   idx = upper_bound_up(map, upstream);
   if (idx == 0)
   {
      return 1;
   }
   idx--;
   if (upstream_found)
   {
      *upstream_found = map->pairs[idx].up;
   }
   if (downstream)
   {
      *downstream = map->pairs[idx].down;
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
   if (map->down_monotonic)
   {
      /* first entry of the run of equal downstream values */
      size_t idx = lower_bound_down(map, downstream);
      if (idx < map->count && map->pairs[idx].down == downstream)
      {
         *upstream = map->pairs[idx].up;
         return 0;
      }
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
   if (map->down_monotonic)
   {
      size_t idx = upper_bound_down(map, downstream);
      if (idx == 0)
      {
         return 1;
      }
      /* largest down <= downstream, then the first entry of its run */
      idx = lower_bound_down(map, map->pairs[idx - 1].down);
      *upstream = map->pairs[idx].up;
      return 0;
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

/* Return the pair with the smallest downstream LSN (the smallest upstream LSN
 * on a tie), which anchors the origin of the translated stream. Returns 1 if
 * the map has no entries. */
int
pgmoneta_lsn_map_get_first(struct lsn_map* map, uint64_t* upstream, uint64_t* downstream)
{
   uint64_t first_up = 0;
   uint64_t first_down = 0;
   bool found = false;

   if (!map || !upstream)
   {
      return 1;
   }
   if (map->down_monotonic)
   {
      if (map->count == 0)
      {
         return 1;
      }
      first_up = map->pairs[0].up;
      first_down = map->pairs[0].down;
      found = true;
   }
   else
   {
      for (size_t i = 0; i < map->count; i++)
      {
         if (!found || map->pairs[i].down < first_down)
         {
            found = true;
            first_down = map->pairs[i].down;
            first_up = map->pairs[i].up;
         }
      }
   }
   if (!found)
   {
      return 1;
   }
   *upstream = first_up;
   if (downstream)
   {
      *downstream = first_down;
   }
   return 0;
}

int
pgmoneta_lsn_map_append_tail(struct lsn_map* map, uint64_t offset, uint64_t* next_offset)
{
   FILE* f = NULL;
   char* line = NULL;
   size_t cap = 0;
   ssize_t n = 0;
   uint64_t pos = offset;

   if (!map || !map->path || !next_offset)
   {
      return 1;
   }

   f = fopen(map->path, "r");
   if (!f)
   {
      return 1;
   }
   if (fseeko(f, (off_t)offset, SEEK_SET) != 0)
   {
      fclose(f);
      return 1;
   }

   while ((n = getline(&line, &cap, f)) != -1)
   {
      uint64_t up = 0;
      uint64_t down = 0;

      if (n == 0)
      {
         continue;
      }
      if (line[n - 1] != '\n')
      {
         /* Partial trailing line: the writer may still be mid-persist, so the
          * pair is not durable yet. Leave it for the next refresh by not
          * advancing the resume offset. */
         break;
      }
      pos += (uint64_t)n;
      if (lsn_map_parse_line(line, &up, &down))
      {
         /* a whole malformed line (e.g. a terminated torn write): skip it */
         continue;
      }
      /* The pair is already durable: never queue it for persist again.
       * A pair the map already holds is skipped; a conflicting one is
       * reported by lsn_map_insert and skipped, keeping the first mapping. */
      if (lsn_map_insert(map, up, down, false) < 0)
      {
         free(line);
         fclose(f);
         return 1;
      }
   }

   free(line);
   fclose(f);
   map->durable_bytes = pos;
   *next_offset = pos;
   return 0;
}

uint64_t
pgmoneta_lsn_map_durable_bytes(struct lsn_map* map)
{
   if (!map)
   {
      return 0;
   }
   return map->durable_bytes;
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
   free(map->pending);
   free(map);
}
