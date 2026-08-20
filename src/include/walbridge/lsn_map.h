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

#ifndef PGMONETA_LSN_MAP_H
#define PGMONETA_LSN_MAP_H

#include <stdint.h>

struct lsn_map;

int
pgmoneta_lsn_map_create(const char* path, struct lsn_map** map);

int
pgmoneta_lsn_map_put(struct lsn_map* map, uint64_t upstream, uint64_t downstream);

int
pgmoneta_lsn_map_get_downstream(struct lsn_map* map, uint64_t upstream, uint64_t* downstream);

int
pgmoneta_lsn_map_get_downstream_at_or_before(struct lsn_map* map, uint64_t upstream, uint64_t* upstream_found, uint64_t* downstream);

int
pgmoneta_lsn_map_get_upstream(struct lsn_map* map, uint64_t downstream, uint64_t* upstream);

/* Find the newest upstream LSN whose downstream translation is at or before downstream. */
int
pgmoneta_lsn_map_get_upstream_at_or_before(struct lsn_map* map, uint64_t downstream, uint64_t* upstream);

/* Return the oldest (first written) upstream/downstream pair, an anchor for
 * the origin of the translated stream. Returns 1 if the map has no entries. */
int
pgmoneta_lsn_map_get_first(struct lsn_map* map, uint64_t* upstream, uint64_t* downstream);

/**
 * Force a durable persist of the map. The file is written append-only: only
 * the pairs added since the last persist are appended and fsynced, so the
 * per-record persistence cost is O(1) regardless of map size.
 * Returns 0 on success, nonzero on failure.
 */
int
pgmoneta_lsn_map_flush(struct lsn_map* map);

/* Incrementally extend an in-memory map with the tail of the persisted file
 * starting at `offset`, without re-parsing the pairs already resident. Only
 * whole lines are committed; a partial trailing line (the writer mid-persist)
 * is left for the next call with *next_offset still pointing at it. Returns 0
 * on success; returns 1 on I/O failure, in which case the caller should fall
 * back to a fresh load. */
int
pgmoneta_lsn_map_append_tail(struct lsn_map* map, uint64_t offset, uint64_t* next_offset);

/* The byte offset in the persisted file that the map already reflects. An
 * incremental tail reader resumes from here. */
uint64_t
pgmoneta_lsn_map_durable_bytes(struct lsn_map* map);

void
pgmoneta_lsn_map_destroy(struct lsn_map* map);

#endif
