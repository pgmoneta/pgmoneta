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

#ifndef PGMONETA_HASHMAP_H
#define PGMONETA_HASHMAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdlib.h>

/**
 * Define an element of the hash map.
 */
struct hashmap_element
{
   char* key;
   unsigned int key_len;
   int in_use;
   void* data;
};

/**
 * A simple hash map implementation where the client owns
 * both the key and value pointers.
 */
struct hashmap
{
   unsigned int table_size;
   unsigned int size;
   struct hashmap_element* data;
};

/**
 *  Create a hashmap.
 *  @param initial_size The initial size of the hashmap. Must be a power of two.
 *  @param new_hashmap The new hashmap.
 *  @return On success 0 is returned.
 *
 *  Note that the initial size of the hashmap must be a power of two, and
 *  creation of the hashmap will fail if this is not the case.
 */
int
pgmoneta_hashmap_create(unsigned int initial_size, struct hashmap** new_hashmap);

/**
 *  Put an element into the hashmap.
 *  @param hashmap The hashmap to insert into.
 *  @param key The string key to use.
 *  @param value The value to insert.
 *  @return On success 0 is returned.
 *
 *  The key string slice is not copied when creating the hashmap entry, and thus
 *  must remain a valid pointer until the hashmap entry is removed or the
 *  hashmap is destroyed.
 */
int
pgmoneta_hashmap_put(struct hashmap* hashmap, char* key, void* value);

/**
 *  Get an element from the hashmap.
 *  @param hashmap The hashmap to get from.
 *  @param key The string key to use.
 *  @return The previously set element, or NULL if none exists.
 */
void*
pgmoneta_hashmap_get(struct hashmap* hashmap, char* key);

/**
 *  Remove an element from the hashmap.
 *  @param hashmap The hashmap to remove from.
 *  @param key The string key to use.
 *  @return On success 0 is returned.
 */
int
pgmoneta_hashmap_remove(struct hashmap* hashmap, char* key);

/**
 *  Does the hash map contains the key.
 *  @param hashmap The hashmap to iterate over.
 *  @param key The key.
 *  @return True if the key exist, otherwise false.
 */
bool
pgmoneta_hashmap_contains_key(struct hashmap* hashmap, char* key);

/**
 *  Get the key set of the hashmap.
 *  @param hashmap The hashmap to iterate over.
 *  @param keys The key set (must be free'd after use).
 *  @return On success 0 is returned.
 */
int
pgmoneta_hashmap_key_set(struct hashmap* hashmap, char*** keys);

/**
 *  Get the size of the hashmap.
 *  @param hashmap The hashmap to get the size of.
 *  @return The size of the hashmap.
 */
unsigned int
pgmoneta_hashmap_size(struct hashmap* hashmap);

/**
 *  Destroy the hashmap.
 *  @param hashmap The hashmap to destroy.
 */
void
pgmoneta_hashmap_destroy(struct hashmap* hashmap);

#ifdef __cplusplus
}
#endif

#endif
