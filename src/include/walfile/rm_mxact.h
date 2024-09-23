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

#ifndef PGMONETA_RM_MXACT_H
#define PGMONETA_RM_MXACT_H

#include <walfile/wal_reader.h>

#define XLOG_MULTIXACT_ZERO_OFF_PAGE  0x00
#define XLOG_MULTIXACT_ZERO_MEM_PAGE  0x10
#define XLOG_MULTIXACT_CREATE_ID      0x20
#define XLOG_MULTIXACT_TRUNCATE_ID    0x30

/**
 * @enum MULTI_XACT_STATUS
 * @brief Enumeration of possible multixact lock modes.
 *
 * These modes represent the lock statuses that can be assigned to a tuple
 * within a multixact transaction.
 */
enum MULTI_XACT_STATUS
{
    MULTI_XACT_STATUS_FOR_KEY_SHARE    = 0x00,  /**< FOR KEY SHARE lock mode. */
    MULTI_XACT_STATUS_FOR_SHARE        = 0x01,  /**< FOR SHARE lock mode. */
    MULTI_XACT_STATUS_FOR_NO_KEY_UPDATE = 0x02, /**< FOR NO KEY UPDATE lock mode. */
    MULTI_XACT_STATUS_FOR_UPDATE       = 0x03,  /**< FOR UPDATE lock mode. */
    MULTI_XACT_STATUS_NO_KEY_UPDATE    = 0x04,  /**< Update that doesn't touch "key" columns. */
    MULTI_XACT_STATUS_UPDATE           = 0x05   /**< Other updates and delete operations. */
};

/**
 * @struct multi_xact_member
 * @brief Represents a member of a multixact.
 *
 * This structure holds a transaction ID and the corresponding lock status.
 */
struct multi_xact_member
{
    transaction_id xid;                /**< Transaction ID of the member. */
    enum MULTI_XACT_STATUS status;     /**< Lock status of the member. */
};

/**
 * @struct xl_multixact_create
 * @brief Represents the creation of a multixact in XLOG.
 *
 * This structure holds the information necessary to create a new multixact,
 * including the multixact ID, starting offset, and member XIDs.
 */
struct xl_multixact_create
{
    multi_xact_id mid;                                        /**< New MultiXact's ID. */
    multi_xact_offset moff;                                   /**< Starting offset in the members file. */
    int32_t nmembers;                                         /**< Number of member XIDs. */
    struct multi_xact_member members[FLEXIBLE_ARRAY_MEMBER];  /**< Array of multixact members. */
};

/**
 * @struct xl_multixact_truncate
 * @brief Represents a multixact truncation in XLOG.
 *
 * This structure holds the information required to truncate multixacts,
 * including the oldest database OID and the range of offsets and members to be truncated.
 */
struct xl_multixact_truncate
{
    oid oldest_multi_db;                       /**< OID of the oldest database with active multixacts. */
    multi_xact_id start_trunc_off;             /**< Starting offset for truncation (for completeness). */
    multi_xact_id end_trunc_off;               /**< Ending offset for truncation. */
    multi_xact_offset start_trunc_memb;        /**< Starting member offset for truncation. */
    multi_xact_offset end_trunc_memb;          /**< Ending member offset for truncation. */
};

/**
 * Describes a multixact operation from a decoded XLOG record.
 *
 * @param buf The buffer to store the description.
 * @param record The decoded XLOG record containing the multixact operation.
 * @return A pointer to the buffer containing the description.
 */
char*
pgmoneta_wal_multixact_desc(char* buf, struct decoded_xlog_record* record);

#endif // PGMONETA_RM_MXACT_H
