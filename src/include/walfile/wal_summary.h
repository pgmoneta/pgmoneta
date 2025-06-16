/*
 * Copyright (C) 2025 The pgmoneta community
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

#ifndef PGMONETA_WAL_SUMMARY_H
#define PGMONETA_WAL_SUMMARY_H

#include <pgmoneta.h>

/**
 * Summarize the WAL records in the range [start_lsn, end_lsn) for a timeline, assuming that
 * both start and end LSNs belongs to the same timeline.
 * @param srv The server
 * @param wal_dir The directory to the wal segments (Optional and is used for testing)
 * @param start_lsn The start lsn is the point at which we should start summarizing. If this
 * value comes from the end LSN of the previous record as returned by the xlogreader machinery,
 * 'exact' should be true; otherwise, 'exact' should be false, and this function will search
 * forward for the start of a valid WAL record.
 * @param end_lsn identifies the point beyond which we can't count on being able to read any
 * more WAL.
 * @return 0 is success, otherwise failure
 */
int
pgmoneta_summarize_wal(int srv, char* wal_dir, uint64_t start_lsn, uint64_t end_lsn);

#endif