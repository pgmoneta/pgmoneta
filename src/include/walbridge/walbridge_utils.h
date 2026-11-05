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

#ifndef PGMONETA_WALBRIDGE_UTILS_H
#define PGMONETA_WALBRIDGE_UTILS_H

/**
 * Parse a PostgreSQL WAL segment size string.
 *
 * Converts a positive numeric WAL segment size specified in
 * megabytes (MB) or gigabytes (GB) into bytes.
 *
 * The input must contain a positive base-10 integer followed
 * by either the uppercase MB or GB unit. Spaces and tabs
 * between the numeric value and unit are supported.
 *
 * The parser validates the input and checks that the
 * resulting size fits within the range of an int.
 *
 * Examples of valid inputs:
 * - "16MB"
 * - "16 MB"
 * - "1GB"
 * - "1 GB"
 *
 * @param value Input string containing the WAL segment size and unit.
 * @param size  Output parameter receiving the parsed size in bytes.
 *
 * @return 0 if parsing succeeds, 1 if the input is invalid or
 *         the resulting size exceeds the supported integer range.
 */

int
pgmoneta_walbridge_parse_wal_segment_size(const char* value, int* size);

#endif
