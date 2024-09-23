/*
 * Copyright (C) 2024 The pgmoneta community
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

#ifndef PGMONETA_RM_REPLORIGIN_H
#define PGMONETA_RM_REPLORIGIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <walfile/wal_reader.h>

#define INVALID_REP_ORIGIN_ID 0            /**< Invalid replication origin ID. */
#define XLOG_REPLORIGIN_SET   0x00         /**< XLOG opcode for setting a replication origin. */
#define XLOG_REPLORIGIN_DROP  0x10         /**< XLOG opcode for dropping a replication origin. */

/**
 * @struct xl_replorigin_set
 * @brief Represents a replication origin set operation in XLOG.
 *
 * Contains the remote LSN, replication origin ID, and a flag indicating
 * whether the operation should be forced.
 */
struct xl_replorigin_set {
    xlog_rec_ptr remote_lsn;  /**< Remote LSN associated with the replication origin. */
    rep_origin_id node_id;    /**< Replication origin ID. */
    bool force;               /**< Indicates if the operation should be forced. */
};

/**
 * @struct xl_replorigin_drop
 * @brief Represents a replication origin drop operation in XLOG.
 *
 * Contains the replication origin ID of the node being dropped.
 */
struct xl_replorigin_drop {
    rep_origin_id node_id;  /**< Replication origin ID of the node being dropped. */
};

/**
 * Describes a replication origin operation from a decoded XLOG record.
 *
 * @param buf The buffer to store the description.
 * @param record The decoded XLOG record containing the replication origin operation.
 * @return A pointer to the buffer containing the description.
 */
char*
pgmoneta_wal_replorigin_desc(char* buf, struct decoded_xlog_record* record);

#ifdef __cplusplus
}
#endif

#endif // PGMONETA_RM_REPLORIGIN_H
