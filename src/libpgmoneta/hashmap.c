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
static unsigned int hashmap_crc32_helper(char* s, unsigned int len);
static unsigned int hashmap_hash_helper_int_helper(struct hashmap* m, char* keystring, unsigned int len);
static int hashmap_match_helper(struct hashmap_element* element, char* key, unsigned int len);
static int hashmap_hash_helper(struct hashmap* m, char* key, unsigned int len, unsigned* out_index);
static int hashmap_rehash_iterator(void* new_hash, struct hashmap_element* e);
static int hashmap_rehash_helper(struct hashmap* m);

__attribute__((used))
int
pgmoneta_hashmap_create(unsigned int initial_size, struct hashmap** new_hashmap)
{
   struct hashmap* m = NULL;

   if (0 == initial_size || 0 != (initial_size & (initial_size - 1)))
   {
      return 1;
   }

   m = HASHMAP_CAST(struct hashmap*, calloc(1, sizeof(struct hashmap)));

   if (!m)
   {
      return 1;
   }

   m->table_size = initial_size;
   m->size = 0;

   m->data = HASHMAP_CAST(struct hashmap_element*,
                          calloc(initial_size, sizeof(struct hashmap_element)));
   if (!m->data)
   {
      free(m);
      return 1;
   }

   *new_hashmap = m;

   return 0;
}

__attribute__((used))
int
pgmoneta_hashmap_put(struct hashmap* hashmap, char* key, void* value)
{
   unsigned int index = 0;
   unsigned int len = strlen(key);

   if (hashmap == NULL || key == NULL)
   {
      return 1;
   }

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
hashmap_crc32_helper(char* s, unsigned int len)
{
   unsigned int crc32val = 0;

#if defined(HASHMAP_SSE42)
   for (unsigned int i = 0; i < len; i++)
   {
      crc32val = _mm_crc32_u8(crc32val, HASHMAP_CAST(unsigned char, s[i]));
   }

   return crc32val;
#else
   // Using polynomial 0x11EDC6F41 to match SSE 4.2's crc function.
   static const unsigned int crc32_tab[] = {
      0x00000000U, 0xF26B8303U, 0xE13B70F7U, 0x1350F3F4U, 0xC79A971FU,
      0x35F1141CU, 0x26A1E7E8U, 0xD4CA64EBU, 0x8AD958CFU, 0x78B2DBCCU,
      0x6BE22838U, 0x9989AB3BU, 0x4D43CFD0U, 0xBF284CD3U, 0xAC78BF27U,
      0x5E133C24U, 0x105EC76FU, 0xE235446CU, 0xF165B798U, 0x030E349BU,
      0xD7C45070U, 0x25AFD373U, 0x36FF2087U, 0xC494A384U, 0x9A879FA0U,
      0x68EC1CA3U, 0x7BBCEF57U, 0x89D76C54U, 0x5D1D08BFU, 0xAF768BBCU,
      0xBC267848U, 0x4E4DFB4BU, 0x20BD8EDEU, 0xD2D60DDDU, 0xC186FE29U,
      0x33ED7D2AU, 0xE72719C1U, 0x154C9AC2U, 0x061C6936U, 0xF477EA35U,
      0xAA64D611U, 0x580F5512U, 0x4B5FA6E6U, 0xB93425E5U, 0x6DFE410EU,
      0x9F95C20DU, 0x8CC531F9U, 0x7EAEB2FAU, 0x30E349B1U, 0xC288CAB2U,
      0xD1D83946U, 0x23B3BA45U, 0xF779DEAEU, 0x05125DADU, 0x1642AE59U,
      0xE4292D5AU, 0xBA3A117EU, 0x4851927DU, 0x5B016189U, 0xA96AE28AU,
      0x7DA08661U, 0x8FCB0562U, 0x9C9BF696U, 0x6EF07595U, 0x417B1DBCU,
      0xB3109EBFU, 0xA0406D4BU, 0x522BEE48U, 0x86E18AA3U, 0x748A09A0U,
      0x67DAFA54U, 0x95B17957U, 0xCBA24573U, 0x39C9C670U, 0x2A993584U,
      0xD8F2B687U, 0x0C38D26CU, 0xFE53516FU, 0xED03A29BU, 0x1F682198U,
      0x5125DAD3U, 0xA34E59D0U, 0xB01EAA24U, 0x42752927U, 0x96BF4DCCU,
      0x64D4CECFU, 0x77843D3BU, 0x85EFBE38U, 0xDBFC821CU, 0x2997011FU,
      0x3AC7F2EBU, 0xC8AC71E8U, 0x1C661503U, 0xEE0D9600U, 0xFD5D65F4U,
      0x0F36E6F7U, 0x61C69362U, 0x93AD1061U, 0x80FDE395U, 0x72966096U,
      0xA65C047DU, 0x5437877EU, 0x4767748AU, 0xB50CF789U, 0xEB1FCBADU,
      0x197448AEU, 0x0A24BB5AU, 0xF84F3859U, 0x2C855CB2U, 0xDEEEDFB1U,
      0xCDBE2C45U, 0x3FD5AF46U, 0x7198540DU, 0x83F3D70EU, 0x90A324FAU,
      0x62C8A7F9U, 0xB602C312U, 0x44694011U, 0x5739B3E5U, 0xA55230E6U,
      0xFB410CC2U, 0x092A8FC1U, 0x1A7A7C35U, 0xE811FF36U, 0x3CDB9BDDU,
      0xCEB018DEU, 0xDDE0EB2AU, 0x2F8B6829U, 0x82F63B78U, 0x709DB87BU,
      0x63CD4B8FU, 0x91A6C88CU, 0x456CAC67U, 0xB7072F64U, 0xA457DC90U,
      0x563C5F93U, 0x082F63B7U, 0xFA44E0B4U, 0xE9141340U, 0x1B7F9043U,
      0xCFB5F4A8U, 0x3DDE77ABU, 0x2E8E845FU, 0xDCE5075CU, 0x92A8FC17U,
      0x60C37F14U, 0x73938CE0U, 0x81F80FE3U, 0x55326B08U, 0xA759E80BU,
      0xB4091BFFU, 0x466298FCU, 0x1871A4D8U, 0xEA1A27DBU, 0xF94AD42FU,
      0x0B21572CU, 0xDFEB33C7U, 0x2D80B0C4U, 0x3ED04330U, 0xCCBBC033U,
      0xA24BB5A6U, 0x502036A5U, 0x4370C551U, 0xB11B4652U, 0x65D122B9U,
      0x97BAA1BAU, 0x84EA524EU, 0x7681D14DU, 0x2892ED69U, 0xDAF96E6AU,
      0xC9A99D9EU, 0x3BC21E9DU, 0xEF087A76U, 0x1D63F975U, 0x0E330A81U,
      0xFC588982U, 0xB21572C9U, 0x407EF1CAU, 0x532E023EU, 0xA145813DU,
      0x758FE5D6U, 0x87E466D5U, 0x94B49521U, 0x66DF1622U, 0x38CC2A06U,
      0xCAA7A905U, 0xD9F75AF1U, 0x2B9CD9F2U, 0xFF56BD19U, 0x0D3D3E1AU,
      0x1E6DCDEEU, 0xEC064EEDU, 0xC38D26C4U, 0x31E6A5C7U, 0x22B65633U,
      0xD0DDD530U, 0x0417B1DBU, 0xF67C32D8U, 0xE52CC12CU, 0x1747422FU,
      0x49547E0BU, 0xBB3FFD08U, 0xA86F0EFCU, 0x5A048DFFU, 0x8ECEE914U,
      0x7CA56A17U, 0x6FF599E3U, 0x9D9E1AE0U, 0xD3D3E1ABU, 0x21B862A8U,
      0x32E8915CU, 0xC083125FU, 0x144976B4U, 0xE622F5B7U, 0xF5720643U,
      0x07198540U, 0x590AB964U, 0xAB613A67U, 0xB831C993U, 0x4A5A4A90U,
      0x9E902E7BU, 0x6CFBAD78U, 0x7FAB5E8CU, 0x8DC0DD8FU, 0xE330A81AU,
      0x115B2B19U, 0x020BD8EDU, 0xF0605BEEU, 0x24AA3F05U, 0xD6C1BC06U,
      0xC5914FF2U, 0x37FACCF1U, 0x69E9F0D5U, 0x9B8273D6U, 0x88D28022U,
      0x7AB90321U, 0xAE7367CAU, 0x5C18E4C9U, 0x4F48173DU, 0xBD23943EU,
      0xF36E6F75U, 0x0105EC76U, 0x12551F82U, 0xE03E9C81U, 0x34F4F86AU,
      0xC69F7B69U, 0xD5CF889DU, 0x27A40B9EU, 0x79B737BAU, 0x8BDCB4B9U,
      0x988C474DU, 0x6AE7C44EU, 0xBE2DA0A5U, 0x4C4623A6U, 0x5F16D052U,
      0xAD7D5351U
   };

   for (unsigned int i = 0; i < len; i++)
   {
      crc32val = crc32_tab[(HASHMAP_CAST(unsigned char, crc32val) ^
                            HASHMAP_CAST(unsigned char, s[i]))] ^
                 (crc32val >> 8);
   }
   return crc32val;
#endif
}

static unsigned int
hashmap_hash_helper_int_helper(struct hashmap* m, char* keystring, unsigned int len)
{
   unsigned int key = hashmap_crc32_helper(keystring, len);

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
   /* If this multiplication overflows hashmap_create will fail. */
   unsigned int new_size = 2 * m->table_size;
   struct hashmap* new_hash = NULL;

   int flag = pgmoneta_hashmap_create(new_size, &new_hash);

   if (0 != flag)
   {
      return flag;
   }

   /* copy the old elements to the new table */
   flag = hashmap_iterate(m, hashmap_rehash_iterator,
                          HASHMAP_PTR_CAST(void*, new_hash));

   if (0 != flag)
   {
      return flag;
   }

   pgmoneta_hashmap_destroy(m);
   memcpy(m, new_hash, sizeof(struct hashmap));

   pgmoneta_hashmap_destroy(new_hash);
   free(new_hash);

   return 0;
}
