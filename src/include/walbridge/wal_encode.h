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

#ifndef PGMONETA_WAL_ENCODE_H
#define PGMONETA_WAL_ENCODE_H

#include <walfile/wal_reader.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct wal_encoder;

/**
 * Create a fresh stream encoder. The downstream stream starts at LSN 0 on
 * segment 0, so the first emitted bytes are segment 0's long page header.
 *
 * @param sysid The system identifier embedded in long page headers.
 * @param wal_seg_size The downstream WAL segment size in bytes.
 * @param xlog_blksz The downstream XLOG block size in bytes.
 * @param tli The downstream timeline id.
 * @param checksums Whether full-page-image checksums are recomputed on output.
 * @param encoder Output parameter for the created encoder.
 * @return 0 on success, otherwise 1.
 */
int
pgmoneta_wal_encoder_create(uint64_t sysid, uint32_t wal_seg_size, uint32_t xlog_blksz,
                            uint32_t tli, bool checksums, struct wal_encoder** encoder);

/**
 * Resume an encoder in the middle of an existing downstream stream (a peer
 * reconnecting at its flushed position). The encoder emits bytes starting at
 * next_lsn; bytes below it are assumed to already be in the peer's possession.
 *
 * @param encoder The encoder to resume.
 * @param xl_prev Downstream LSN of the last record the peer already has.
 * @param next_lsn Downstream LSN where the next record will start.
 * @return 0 on success, otherwise 1.
 */
int
pgmoneta_wal_encoder_resume(struct wal_encoder* encoder, uint64_t xl_prev, uint64_t next_lsn);

/**
 * Encode one translated record into the downstream stream. The record is
 * placed with the exact page/segment layout produced by the wal_store
 * (same downstream LSN space and identical bytes), and the full-page-image
 * checksums are recomputed when the encoder was created with checksums.
 *
 * The emitted bytes (record payload plus alignment and any flushed pages) are
 * appended to the encoder's internal buffer and retrieved with
 * pgmoneta_wal_encoder_take().
 *
 * @param encoder The encoder.
 * @param record The translated record to encode.
 * @param record_lsn Optional output: downstream LSN where the record starts.
 * @return 0 on success, otherwise 1.
 */
int
pgmoneta_wal_encoder_write_record(struct wal_encoder* encoder, struct decoded_xlog_record* record, uint64_t* record_lsn);

/**
 * Take all buffered emitted bytes since the previous take. Ownership of the
 * returned buffer is transferred to the caller (free(3) it). When there is
 * nothing new, *out is set to NULL and *len to 0.
 *
 * @param encoder The encoder.
 * @param out Output: newly available downstream bytes.
 * @param len Output: byte count.
 * @return 0 on success, otherwise 1.
 */
int
pgmoneta_wal_encoder_take(struct wal_encoder* encoder, char** out, size_t* len);

/**
 * Return the downstream LSN where the next record will be placed.
 */
uint64_t
pgmoneta_wal_encoder_next_lsn(struct wal_encoder* encoder);

/**
 * Return the downstream LSN of the last encoded record.
 */
uint64_t
pgmoneta_wal_encoder_xl_prev(struct wal_encoder* encoder);

/**
 * Return the current downstream segment number.
 */
uint64_t
pgmoneta_wal_encoder_segno(struct wal_encoder* encoder);

/**
 * Destroy the encoder and release all resources.
 */
void
pgmoneta_wal_encoder_destroy(struct wal_encoder* encoder);

#endif /* PGMONETA_WAL_ENCODE_H */