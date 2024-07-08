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

#ifndef PGMONETA_WAL_H
#define PGMONETA_WAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ev.h>
#include <stdint.h>
#include <stdlib.h>

/** @struct timeline_history
 * Defines a timeline history
 */
struct timeline_history
{
   uint32_t parent_tli;           /**< the previous timeline current timeline switched off from */
   uint32_t switchpos_hi;         /**< the high 32 bit in decimal of xlog pos where the switch happened */
   uint32_t switchpos_lo;         /**< the low 32 bit in decimal of xlog pos where the switch happened */
   struct timeline_history* next; /**< the next history entry */
};

/**
 * Receive WAL
 * @param srv The server index
 * @param argv The argv
 */
void
pgmoneta_wal(int srv, char** argv);

/**
 * Find and extract the history info from .history file of given server and timeline
 * @param srv The server index
 * @param tli The timeline
 * @param history [out] the history info
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_get_timeline_history(int srv, uint32_t tli, struct timeline_history** history);

/**
 * Free the history
 * @param history The history
 */
void
pgmoneta_free_timeline_history(struct timeline_history* history);

#ifdef __cplusplus
}
#endif

#endif
