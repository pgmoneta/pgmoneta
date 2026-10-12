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

#ifndef PGMONETA_WALBRIDGE_WAL_STORE_H
#define PGMONETA_WALBRIDGE_WAL_STORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <walfile/wal_reader.h>
#include <walbridge/lsn_map.h>

struct wal_store;

int
pgmoneta_wal_store_create(const char* downstream_dir,
                          struct lsn_map* map,
                          uint64_t sysid,
                          uint32_t wal_seg_size,
                          uint32_t xlog_blksz,
                          uint32_t tli,
                          struct wal_store** store);

int
pgmoneta_wal_store_write_record(struct wal_store* store, struct decoded_xlog_record* record);

/* Resume an existing downstream stream after a restart. xl_prev is the
 * downstream LSN of the last record written and next_lsn the LSN where the
 * next record will start (both persist across runs). A next_lsn of 0 keeps the
 * store in its freshly created state. */
int
pgmoneta_wal_store_resume(struct wal_store* store, uint64_t xl_prev, uint64_t next_lsn);

/* Read the store's current downstream position for persistence across runs. */
void
pgmoneta_wal_store_get_state(struct wal_store* store, uint64_t* segno, uint64_t* xl_prev, uint64_t* next_lsn);

int
pgmoneta_wal_store_flush(struct wal_store* store);

struct lsn_map*
pgmoneta_wal_store_get_map(struct wal_store* store);

int
pgmoneta_wal_store_sync_partial_page(struct wal_store* store);

void
pgmoneta_wal_store_destroy(struct wal_store* store);

void
pgmoneta_wal_store_set_checksums(struct wal_store* store, bool checksums);

void
pgmoneta_wal_store_recompute_page_checksums(struct wal_store* store, struct decoded_xlog_record* record);

/* Recompute the data-page checksum of every full-page image in a record
 * without any store context. Used by the on-demand stream encoder, which
 * shares wal_store's byte-exact downstream layout but has no files. */
void
pgmoneta_wal_store_recompute_record_page_checksums(struct decoded_xlog_record* record);

uint32_t
pgmoneta_wal_store_compute_crc(const char* buffer, uint32_t total_len);

/* Remap a PostgreSQL 18 page-header flag field (xlp_info) to the flag space
 * that is valid on PostgreSQL 19. PG18 accepted XLP_BKP_REMOVABLE (0x0008),
 * which was removed in PG19 where XLP_ALL_FLAGS is 0x0007; the XLP_* flag bits
 * otherwise did not change between the two versions. Returns the flags to write
 * into a 19.x WAL page header. */
uint16_t
pgmoneta_wal_store_remap_page_flags(uint16_t flags);

#endif
