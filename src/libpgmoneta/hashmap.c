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

/* Based on https://github.com/sheredom/hashmap.h */

#include <hashmap.h>
#include <security.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#if defined(__AVX__) || defined(__SSE4_2__)
#define HASHMAP_SSE42
#endif

#if defined(HASHMAP_SSE42)
#include <nmmintrin.h>
#endif

#define HASHMAP_CAST(type, x) ((type)x)
#define HASHMAP_PTR_CAST(type, x) ((type)x)
#define HASHMAP_MAX_CHAIN_LENGTH 8

static int hashmap_iterate(struct hashmap* hashmap, int (* f)(void*, struct hashmap_element*), void* context);
static unsigned int hashmap_hash_helper_int_helper(struct hashmap* m, char* keystring, unsigned int len);
static int hashmap_match_helper(struct hashmap_element* element, char* key, unsigned int len);
static int hashmap_hash_helper(struct hashmap* m, char* key, unsigned int len, unsigned* out_index);
static int hashmap_rehash_iterator(void* new_hash, struct hashmap_element* e);
static int hashmap_rehash_helper(struct hashmap* m);

__attribute__((used))
int
pgmoneta_hashmap_create(unsigned int initial_size, struct hashmap** new_hashmap)
{
   void* m = NULL;
   struct hashmap* hm = NULL;

   if (0 == initial_size || 0 != (initial_size & (initial_size - 1)))
   {
      goto error;
   }

   m = calloc(1, sizeof(struct hashmap));

   if (m == NULL)
   {
      goto error;
   }

   hm = HASHMAP_CAST(struct hashmap*, m);

   hm->table_size = initial_size;
   hm->size = 0;

   m = calloc(initial_size, sizeof(struct hashmap_element));

   if (m == NULL)
   {
      goto error;
   }

   hm->data = HASHMAP_CAST(struct hashmap_element*, m);
   if (!hm->data)
   {
      goto error;
   }

   *new_hashmap = hm;

   return 0;

error:

   free(m);

   if (hm != NULL)
   {
      free(hm->data);
      free(hm);
   }

   return 1;
}

__attribute__((used))
int
pgmoneta_hashmap_put(struct hashmap* hashmap, char* key, void* value)
{
   unsigned int index;
   unsigned int len;

   if (hashmap == NULL || key == NULL)
   {
      return 1;
   }

   index = 0;
   len = strlen(key);

   /* Find a place to put our value. */
   while (!hashmap_hash_helper(hashmap, key, len, &index))
   {
      if (hashmap_rehash_helper(hashmap))
      {
         return 1;
      }
   }

   /* Set the data. */
   hashmap->data[index].data = value;
   hashmap->data[index].key = key;
   hashmap->data[index].key_len = len;

   /* If the hashmap element was not already in use, set that it is being used
    * and bump our size. */
   if (0 == hashmap->data[index].in_use)
   {
      hashmap->data[index].in_use = 1;
      hashmap->size++;
   }

   return 0;
}

__attribute__((used))
void*
pgmoneta_hashmap_get(struct hashmap* hashmap, char* key)
{
   unsigned int curr = 0;
   unsigned int len = strlen(key);

   if (hashmap == NULL || key == NULL)
   {
      return NULL;
   }

   /* Find data location */
   curr = hashmap_hash_helper_int_helper(hashmap, key, len);

   /* Linear probing, if necessary */
   for (int i = 0; i < HASHMAP_MAX_CHAIN_LENGTH; i++)
   {
      if (hashmap->data[curr].in_use)
      {
         if (hashmap_match_helper(&hashmap->data[curr], key, len))
         {
            return hashmap->data[curr].data;
         }
      }

      curr = (curr + 1) % hashmap->table_size;
   }

   /* Not found */
   return NULL;
}

__attribute__((used))
int
pgmoneta_hashmap_remove(struct hashmap* hashmap, char* key)
{
   unsigned int curr;
   unsigned int len = strlen(key);

   if (hashmap == NULL || key == NULL)
   {
      return 1;
   }

   /* Find key */
   curr = hashmap_hash_helper_int_helper(hashmap, key, len);

   /* Linear probing, if necessary */
   for (int i = 0; i < HASHMAP_MAX_CHAIN_LENGTH; i++)
   {
      if (hashmap->data[curr].in_use)
      {
         if (hashmap_match_helper(&hashmap->data[curr], key, len))
         {
            /* Blank out the fields including in_use */
            memset(&hashmap->data[curr], 0, sizeof(struct hashmap_element));

            /* Reduce the size */
            hashmap->size--;

            return 0;
         }
      }

      curr = (curr + 1) % hashmap->table_size;
   }

   return 1;
}

__attribute__((used))
bool
pgmoneta_hashmap_contains_key(struct hashmap* hashmap, char* key)
{
   struct hashmap_element* p;

   if (hashmap == NULL || key == NULL)
   {
      return 1;
   }

   for (unsigned int i = 0; i < hashmap->table_size; i++)
   {
      p = &hashmap->data[i];
      if (p->in_use)
      {
         if (!strcmp(hashmap->data[i].key, key))
         {
            return true;
         }
      }
   }

   return false;
}

__attribute__((used))
int
pgmoneta_hashmap_key_set(struct hashmap* hashmap, char*** keys)
{
   struct hashmap_element* p;
   unsigned int k_i = 0;
   char** k = NULL;

   if (hashmap == NULL)
   {
      return 1;
   }

   k = (char**)malloc(sizeof(char*) * pgmoneta_hashmap_size(hashmap));

   if (!k)
   {
      return 1;
   }

   for (unsigned int i = 0; i < hashmap->table_size; i++)
   {
      p = &hashmap->data[i];
      if (p->in_use)
      {
         k[k_i] = hashmap->data[i].key;
         k_i++;
      }
   }

   *keys = k;

   return 0;
}

__attribute__((used))
unsigned int
pgmoneta_hashmap_size(struct hashmap* hashmap)
{
   if (hashmap == NULL)
   {
      return 0;
   }

   return hashmap->size;
}

__attribute__((used))
void
pgmoneta_hashmap_destroy(struct hashmap* hashmap)
{
   if (hashmap != NULL)
   {
      free(hashmap->data);
      memset(hashmap, 0, sizeof(struct hashmap));
   }
}

static int
hashmap_iterate(struct hashmap* hashmap,
                int (* f)(void*, struct hashmap_element*),
                void* context)
{
   struct hashmap_element* p;
   int r;

   /* Linear probing */
   for (unsigned int i = 0; i < hashmap->table_size; i++)
   {
      p = &hashmap->data[i];
      if (p->in_use)
      {
         r = f(context, p);
         switch (r)
         {
            case -1: /* remove item */
               memset(p, 0, sizeof(struct hashmap_element));
               hashmap->size--;
               break;
            case 0: /* continue iterating */
               break;
            default: /* early exit */
               return 1;
         }
      }
   }

   return 0;
}

static unsigned int
hashmap_hash_helper_int_helper(struct hashmap* m, char* keystring, unsigned int len)
{
   unsigned int key;
   pgmoneta_create_crc32c_buffer(keystring, len, &key);

   /* Robert Jenkins' 32 bit Mix Function */
   key += (key << 12);
   key ^= (key >> 22);
   key += (key << 4);
   key ^= (key >> 9);
   key += (key << 10);
   key ^= (key >> 2);
   key += (key << 7);
   key ^= (key >> 12);

   /* Knuth's Multiplicative Method */
   key = (key >> 3) * 2654435761;

   return key % m->table_size;
}

static int
hashmap_match_helper(struct hashmap_element* element,
                     char* key, unsigned int len)
{
   return (element->key_len == len) && (0 == memcmp(element->key, key, len));
}

static int
hashmap_hash_helper(struct hashmap* m, char* key,
                    unsigned int len, unsigned* out_index)
{
   unsigned int start, curr;
   int total_in_use;

   /* If full, return immediately */
   if (m->size >= m->table_size)
   {
      return 0;
   }

   /* Find the best index */
   curr = start = hashmap_hash_helper_int_helper(m, key, len);

   /* First linear probe to check if we've already insert the element */
   total_in_use = 0;

   for (unsigned int i = 0; i < HASHMAP_MAX_CHAIN_LENGTH; i++)
   {
      int in_use = m->data[curr].in_use;

      total_in_use += in_use;

      if (in_use && hashmap_match_helper(&m->data[curr], key, len))
      {
         *out_index = curr;
         return 1;
      }

      curr = (curr + 1) % m->table_size;
   }

   curr = start;

   /* Second linear probe to actually insert our element (only if there was at
    * least one empty entry) */
   if (HASHMAP_MAX_CHAIN_LENGTH > total_in_use)
   {
      for (unsigned int i = 0; i < HASHMAP_MAX_CHAIN_LENGTH; i++)
      {
         if (!m->data[curr].in_use)
         {
            *out_index = curr;
            return 1;
         }

         curr = (curr + 1) % m->table_size;
      }
   }

   return 0;
}

static int
hashmap_rehash_iterator(void* new_hash, struct hashmap_element* e)
{
   int temp = pgmoneta_hashmap_put(HASHMAP_PTR_CAST(struct hashmap*, new_hash), e->key, e->data);
   if (0 < temp)
   {
      return 1;
   }

   /* clear old value to avoid stale pointers */
   return -1;
}

static int
hashmap_rehash_helper(struct hashmap* m)
{
   int flag;
   unsigned int new_size = 2 * m->table_size;
   struct hashmap* new_hash = NULL;

   flag = pgmoneta_hashmap_create(new_size, &new_hash);

   if (flag != 0)
   {
      goto error;
   }

   /* copy the old elements to the new table */
   flag = hashmap_iterate(m, hashmap_rehash_iterator,
                          HASHMAP_PTR_CAST(void*, new_hash));

   if (flag != 0)
   {
      goto error;
   }

   pgmoneta_hashmap_destroy(m);
   memcpy(m, new_hash, sizeof(struct hashmap));

   pgmoneta_hashmap_destroy(new_hash);
   free(new_hash);

   return 0;

error:

   if (new_hash != NULL)
   {
      free(new_hash->data);
      free(new_hash);
   }

   return 1;
}
