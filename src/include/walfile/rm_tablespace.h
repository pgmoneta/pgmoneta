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

#ifndef PGMONETA_RM_TABLESPACE_H
#define PGMONETA_RM_TABLESPACE_H

#include <walfile/wal_reader.h>

/* XLOG stuff */
#define XLOG_TBLSPC_CREATE    0x00       /**< XLOG opcode for creating a tablespace. */
#define XLOG_TBLSPC_DROP      0x10       /**< XLOG opcode for dropping a tablespace. */

/**
 * @struct xl_tblspc_create_rec
 * @brief Represents a tablespace creation record in XLOG.
 *
 * Contains the tablespace ID and the path to the tablespace.
 */
struct xl_tblspc_create_rec
{
   oid ts_id;                             /**< ID of the tablespace. */
   char ts_path[FLEXIBLE_ARRAY_MEMBER];   /**< Null-terminated string containing the path to the tablespace. */
};

/**
 * @struct xl_tblspc_drop_rec
 * @brief Represents a tablespace drop record in XLOG.
 *
 * Contains the ID of the tablespace being dropped.
 */
struct xl_tblspc_drop_rec
{
   oid ts_id;                             /**< ID of the tablespace being dropped. */
};

/**
 * Describes a tablespace operation from a decoded XLOG record.
 *
 * @param buf The buffer to store the description.
 * @param record The decoded XLOG record containing the tablespace operation.
 * @return A pointer to the buffer containing the description.
 */
char*
pgmoneta_wal_tablespace_desc(char* buf, struct decoded_xlog_record* record);

#endif /* PGMONETA_RM_TABLESPACE_H */
