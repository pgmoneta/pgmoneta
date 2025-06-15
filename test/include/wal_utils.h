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
 *
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <configuration.h>
#include <deque.h>
#include <json.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <walfile/pg_control.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>
#include <value.h>
#include <tsclient.h>
#include <walfile.h>

/* system */
#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>

#define WAL_CHECKPOINT_SHUTDOWN 0
#define WAL_CHECKPOINT_ONLINE 1
#define WAL_COMMIT_TS_TRUNCATE 2
#define WAL_HEAP2_PRUNE 3
#define WAL_END_OF_RECOVERY 4

int pgmoneta_test_walfile(struct walfile* (*generate)(void));
// int pgmoneta_wal_test(int version);

struct walfile* generate_check_point_shutdown_v17();
struct walfile* generate_check_point_online_v17();
struct walfile* generate_commit_ts_truncate_v17();
struct walfile* generate_heap_prune_v17();
struct walfile* generate_end_of_recovery_v17();

// struct wal_record
// {
//    int version;
//    int type;
//    struct walfile* (*generate)(void);
// };
