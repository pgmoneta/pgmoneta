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

#ifndef PGMONETA_ART_H
#define PGMONETA_ART_H

#ifdef __cplusplus
extern "C" {
#endif

#include <deque.h>

#include <stdint.h>

#define MAX_PREFIX_LEN 10

typedef int (*art_callback)(void* data, const unsigned char* key, uint32_t key_len, void* value);

typedef void (*value_destroy_callback)(void* value);

/** @struct art
 * The ART tree
 */
struct art
{
   struct art_node* root;                 /**< The root node of ART */
   uint64_t size;                         /**< The size of the ART */
   value_destroy_callback val_destroy_cb; /**< The callback for the value destroy */
};

/** @struct art_iterator
 * Defines an art_iterator
 */
struct art_iterator
{
   struct deque* que;  /**< The deque */
   struct art* tree;   /**< The ART */
   uint32_t count;     /**< The count of the iterator */
   unsigned char* key; /**< The key */
   void* value;        /**< The value */
};

/**
 * Initializes an adaptive radix tree
 * @param tree [out] The tree
 * @param val_destroy_cb The callback to destroy the val in leaf,
 * simple free() is used as default if input is NULL.
 * See pgmoneta_art_destroy_value_noop() if you don't need value to be freed.
 * @return 0 on success, 1 if otherwise
 */
int
pgmoneta_art_init(struct art** tree, value_destroy_callback val_destroy_cb);

/**
 * Destroys an ART tree
 * @return 0 on success, 1 if otherwise
 */
int
pgmoneta_art_destroy(struct art* tree);

/**
 * inserts a new value into the art tree, note that the key is copied while the value is not
 * @param t the tree
 * @param key the key
 * @param key_len the length of the key
 * @param value opaque value
 * @return null if the item was newly inserted, otherwise
 * the old value pointer is returned
 */
void*
pgmoneta_art_insert(struct art* t, unsigned char* key, uint32_t key_len, void* value);

/**
 * Deletes a value from the ART tree
 * @param t The tree
 * @param key The key
 * @param key_len The length of the key
 * @return NULL if the item was not found, otherwise
 * the value pointer is returned
 */
void*
pgmoneta_art_delete(struct art* t, unsigned char* key, uint32_t key_len);

/**
 * Searches for a value in the ART tree
 * @param t The tree
 * @param key The key
 * @param key_len The length of the key
 * @return NULL if the item was not found, otherwise
 * the value pointer is returned
 */
void*
pgmoneta_art_search(struct art* t, unsigned char* key, uint32_t key_len);

/**
 * Iterates through the entries pairs in the map,
 * invoking a callback for each. The call back gets a
 * key, value for each and returns an integer stop value
 * If the callback returns non-zero, then the iteration stops
 * @param t The tree to iterate over
 * @param cb The callback function to invoke
 * @param data Opaque handle passed to the callback
 * @return 0 on success, or the return of the callback
 */
int
pgmoneta_art_iterate(struct art* t, art_callback cb, void* data);

/**
 * Create an art iterator
 * @param t The tree
 * @param iter [out] The iterator
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_art_iterator_init(struct art* t, struct art_iterator** iter);

/**
 * Destroy the iterator
 * @param iter The iterator
 */
void
pgmoneta_art_iterator_destroy(struct art_iterator* iter);

/**
 * Get the next key value pair into iterator
 * @param iter The iterator
 * @return true if iterator has next, otherwise false
 */
bool
pgmoneta_art_iterator_next(struct art_iterator* iter);

/**
 * Check if iterator has next
 * @param iter The iterator
 * @return true if the iterator has the next leaf, otherwise false
 */
bool
pgmoneta_art_iterator_has_next(struct art_iterator* iter);

/**
 * The noop callback function for destroying value when destroying ART
 * @param val The value
 */
void
pgmoneta_art_destroy_value_noop(void* val);

/**
 * The default callback function for destroying value when destroying ART
 * @param val The value
 */
void
pgmoneta_art_destroy_value_default(void* val);

#ifdef __cplusplus
}
#endif

#endif
