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

#ifndef PGMONETA_LINK_H
#define PGMONETA_LINK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <art.h>
#include <workers.h>

#include <stdlib.h>

/**
 * Create link two directories
 * @param from The from directory
 * @param to The to directory
 * @param workers The optional workers
 */
void
pgmoneta_link(char* from, char* to, struct workers* workers);

/**
 * Create link between two directories with processed manifest info
 * @param base_from The base from directory (newer)
 * @param base_to The base to directory
 * @param from The current from directory
 * @param to The current to directory
 * @param changed The changed files
 * @param added The added files
 * @param workers The optional workers
 */
void
pgmoneta_link_with_manifest(char* base_from, char* base_to, char* from, struct art* changed, struct art* added, struct workers* workers);

/**
 * Relink link two directories
 * @param from The from directory
 * @param to The to directory
 * @param workers The optional workers
 */
void
pgmoneta_relink(char* from, char* to, struct workers* workers);

/**
 * Create link between two tablespaces
 * @param from The from directory
 * @param to The to directory
 * @param workers The optional workers
 */
void
pgmoneta_link_tablespaces(char* from, char* to, struct workers* workers);

#ifdef __cplusplus
}
#endif

#endif
