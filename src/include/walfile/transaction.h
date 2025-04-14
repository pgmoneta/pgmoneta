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

#ifndef PGMONETA_TRANSACTION_H
#define PGMONETA_TRANSACTION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Typedefs
typedef uint32_t transaction_id;
typedef transaction_id multi_xact_id;
typedef uint32_t multi_xact_offset;

// #define variables
#define INVALID_TRANSACTION_ID              ((transaction_id) 0)

// #define macros
#define EPOCH_FROM_FULL_TRANSACTION_ID(x)   ((uint32_t) ((x).value >> 32))
#define XID_FROM_FULL_TRANSACTION_ID(x)     ((uint32_t) (x).value)
#define TRANSACTION_ID_IS_VALID(xid)        ((xid) != INVALID_TRANSACTION_ID)

// Structs
/**
 * @struct full_transaction_id
 * @brief Represents a full transaction identifier.
 *
 * Fields:
 * - value: The full 64-bit transaction ID value.
 */
struct full_transaction_id
{
   uint64_t value;   /**< The full 64-bit transaction ID value */
};

#ifdef __cplusplus
}
#endif

#endif // PGMONETA_TRANSACTION_H
