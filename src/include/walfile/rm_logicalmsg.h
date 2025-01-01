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

#ifndef PGMONETA_RM_LOGICALMSG_H
#define PGMONETA_RM_LOGICALMSG_H

#include <walfile/wal_reader.h>

#define XLOG_LOGICAL_MESSAGE  0x00

/**
 * @struct xl_logical_message
 * @brief Represents a logical message in WAL.
 *
 * This structure holds the information of a logical message
 * that is stored in the WAL, including the database OID,
 * whether the message is transactional, and the message content.
 */
struct xl_logical_message
{
   oid db_id;                /**< OID of the database the message was emitted from. */
   bool transactional;      /**< Indicates if the message is transactional. */
   size_t prefix_size;      /**< Length of the message prefix. */
   size_t message_size;     /**< Size of the message content. */
   char message[FLEXIBLE_ARRAY_MEMBER];  /**< The message payload, including the null-terminated prefix. */
};

/**
 * Describes a logical message from a decoded XLOG record.
 *
 * @param buf The buffer to store the description.
 * @param record The decoded XLOG record containing the logical message.
 * @return A pointer to the buffer containing the description.
 */
char*
pgmoneta_wal_logicalmsg_desc(char* buf, struct decoded_xlog_record* record);

#endif // PGMONETA_RM_LOGICALMSG_H
