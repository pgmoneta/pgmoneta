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

#ifndef PGMONETA_RM_SEQ_H
#define PGMONETA_RM_SEQ_H

#ifdef __cplusplus
extern "C" {
#endif

#include <walfile/wal_reader.h>

#define XLOG_SEQ_LOG 0x00 /**< XLOG opcode for sequence log operation. */

/**
 * @struct xl_seq_rec
 * @brief Represents a sequence record in XLOG.
 *
 * Contains the relation file node for the sequence.
 * The sequence tuple data follows this structure.
 */
struct xl_seq_rec
{
   struct rel_file_node node; /**< Relation file node for the sequence. */
   /* SEQUENCE TUPLE DATA FOLLOWS AT THE END */
};

/**
 * Describes a sequence operation from a decoded XLOG record.
 *
 * @param buf The buffer to store the description.
 * @param record The decoded XLOG record containing the sequence operation.
 * @return A pointer to the buffer containing the description.
 */
char*
pgmoneta_wal_seq_desc(char* buf, struct decoded_xlog_record* record);

#ifdef __cplusplus
}
#endif

#endif // PGMONETA_RM_SEQ_H
