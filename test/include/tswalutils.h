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
#include <deque.h>
#include <walfile/pg_control.h>
#include <utils.h>
#include <value.h>
#include <tsclient.h>

/* system */
#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>

/* WAL record types */
#define WAL_CHECKPOINT_SHUTDOWN 0
#define WAL_CHECKPOINT_ONLINE   1
#define WAL_COMMIT_TS_TRUNCATE  2
#define WAL_HEAP2_PRUNE         3
#define WAL_END_OF_RECOVERY     4

/* Long Header random values */
#define RANDOM_PAGEADDR    0x0000000017000000
#define RANDOM_MAGIC       0xD116
#define RANDOM_INFO        0x0007
#define RANDOM_TLI         1
#define RANDOM_SEG_SIZE    16777216
#define RANDOM_XLOG_BLCKSZ 8192
#define RANDOM_REMLEN      0

/* Random values for CheckPoint Shutdown v17 */
#define RANDOM_REDO                 0x123456789ABCDEF0ULL
#define RANDOM_THIS_TLI             1
#define RANDOM_PREV_TLI             1
#define RANDOM_FULL_PAGE_WRITES     true
#define RANDOM_WAL_LEVEL            2
#define RANDOM_NEXT_XID             42
#define RANDOM_NEXT_OID             100
#define RANDOM_NEXT_MULTI           200
#define RANDOM_NEXT_MULTI_OFFSET    300
#define RANDOM_OLDEST_XID           400
#define RANDOM_OLDEST_XID_DB        500
#define RANDOM_OLDEST_MULTI         600
#define RANDOM_OLDEST_MULTI_DB      700
#define RANDOM_TIME                 800
#define RANDOM_OLDEST_COMMIT_TS_XID 900
#define RANDOM_NEWEST_COMMIT_TS_XID 1000
#define RANDOM_OLDEST_ACTIVE_XID    1100

/* Random values for decoded_xlog_record */
#define RANDOM_MAIN_DATA_LEN sizeof(struct check_point_v17)
#define RANDOM_MAX_BLOCK_ID  -1
#define RANDOM_OVERSIZED     false
#define RANDOM_RECORD_ORIGIN INVALID_REP_ORIGIN_ID
#define RANDOM_TOPLEVEL_XID  INVALID_TRANSACTION_ID
#define RANDOM_PARTIAL       false

/* Random values for usage inside tests */
#define RANDOM_WALFILE_NAME "/00000001000000000000001D"

struct walfile*
pgmoneta_test_generate_check_point_shutdown_v17();
