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

/* Migration engine API */
#ifndef PGMONETA_MIGRATION_ENGINE_H
#define PGMONETA_MIGRATION_ENGINE_H

#include <stdbool.h>
#include <stdint.h>
#include <walfile.h>

struct lsn_map;

/* Return codes for pgmoneta_migration_engine_translate() */
#define PGMONETA_MIGRATION_KEEP     0 /* record passes through, unchanged */
#define PGMONETA_MIGRATION_MODIFIED 1 /* record was translated in place */
#define PGMONETA_MIGRATION_DROP     2 /* record must be discarded entirely */

/* Translate an 18.x record so it is valid as a 19.x record.
 *
 * Returns 0 (KEEP) when the record is passed through untouched, 1 (MODIFIED)
 * when it was rewritten in place, or 2 (DROP) when the record is a no-op that
 * carries no meaning on the target and must not be emitted downstream. Negative
 * values indicate a hard error.
 */
int
pgmoneta_migration_engine_translate(struct decoded_xlog_record* record, uint16_t src_magic, uint16_t tgt_magic, struct lsn_map* map, bool data_checksums);

/* Sample the upstream wal_level out of a checkpoint or checkpoint-redo record.
 * Returns 0 and sets *wal_level on success, non-zero if the record does not
 * carry a wal_level. wal_level_ok() reports whether the value is usable for
 * physical replication (replica or logical, never minimal). */
int
pgmoneta_migration_engine_get_wal_level(struct decoded_xlog_record* record, int* wal_level);

bool
pgmoneta_migration_engine_wal_level_ok(int wal_level);

#endif
