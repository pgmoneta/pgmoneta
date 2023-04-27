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

#ifndef PGMONETA_MEMORY_H
#define PGMONETA_MEMORY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>

#include <stdlib.h>

/**
 * Initialize a memory segment for the process local message structure
 */
void
pgmoneta_memory_init(void);

/**
 * Set the size of the local message structure
 * @param size The size
 */
void
pgmoneta_memory_size(size_t size);

/**
 * Get the message structure
 * @return The structure
 */
struct message*
pgmoneta_memory_message(void);

/**
 * Free the memory segment
 */
void
pgmoneta_memory_free(void);

/**
 * Destroy the memory segment
 */
void
pgmoneta_memory_destroy(void);

/**
 * Create a dynamic memory segment
 * @param size The new size
 * @return The segment
 */
void*
pgmoneta_memory_dynamic_create(size_t* size);

/**
 * Destroy a dynamic memory segment
 * @param data The segment
 */
void
pgmoneta_memory_dynamic_destroy(void* data);

/**
 * Append a dynamic memory segment
 * @param orig The original memory segment
 * @param orig_size The original size
 * @param append The append memory segment
 * @param append_size The append size
 * @param new_size The new size
 * @return The new segment
 */
void*
pgmoneta_memory_dynamic_append(void* orig, size_t orig_size, void* append, size_t append_size, size_t* new_size);

#ifdef __cplusplus
}
#endif

#endif
