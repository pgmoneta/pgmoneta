/*
 * Copyright (C) 2023 Red Hat
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

#ifndef PGMONETA_TABLESPACE_H
#define PGMONETA_TABLESPACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdbool.h>

struct tablespace
{
   char* name;
   char* path;
   struct tablespace* next;
};

/**
 * Create a tablespace string
 * @param name The name
 * @param path The path
 * @param result The result
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_tablespace(char* name, char* path, struct tablespace** result);

/**
 * Append a tablespace tablespace to the chain.
 * @param chain The tablespace chain
 * @param tablespace The tablespace tablespace
 */
void
pgmoneta_append_tablespace(struct tablespace** chain, struct tablespace* tablespace);

/**
 * List the tablespaces
 * @param chain The tablespace chain
 */
void
pgmoneta_list_tablespaces(struct tablespace* chain);

/**
 * Delete the tablespace
 * @param in The tablespace
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_free_tablespaces(struct tablespace* in);

#ifdef __cplusplus
}
#endif

#endif
