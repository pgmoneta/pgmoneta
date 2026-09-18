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

/*
 * The checksum PostgreSQL stores in each page, reimplemented so a backup can be
 * checked without a server. The algorithm is in src/include/storage/checksum_impl.h
 * and src/include/storage/checksum_block_internal.h in the PostgreSQL tree: a
 * 32-way FNV-1a over the page, the block number mixed in, then folded to 16 bits.
 *
 * The block number is part of the checksum, so a page moved to a different block
 * fails. For a relation large enough to be split into segments, the number wanted
 * is the one within the relation rather than within the segment file, which is what
 * pg_checksums computes as blockno + segmentno * RELSEG_SIZE.
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <info.h>
#include <pagechecksum.h>

/* system */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Parallel FNV-1a streams, from the PostgreSQL implementation. */
#define N_SUMS    32
#define FNV_PRIME 16777619

/* pd_checksum sits after pd_lsn, which is 8 bytes. */
#define PD_CHECKSUM_OFFSET 8

static const uint32_t base_offsets[N_SUMS] = {
   0x5B1F36E9, 0xB8525960, 0x02AB50AA, 0x1DE66D2A,
   0x79FF467A, 0x9BB9F8A3, 0x217E7CD2, 0x83E13D2C,
   0xF8D4474F, 0xE39EB970, 0x42C6AE16, 0x993216FA,
   0x7B093B5D, 0x98DAFF3C, 0xF718902A, 0x0B1C9CDB,
   0xE58F764B, 0x187636BC, 0x5D7B3BB1, 0xE73DE7DE,
   0x92BEC979, 0xCCA6C0B2, 0x304A0979, 0x85AA43D4,
   0x783125BB, 0x6CA8EAA2, 0xE407EAC6, 0x4B5CFC3E,
   0x9FBF8C76, 0x15CA20BE, 0xF2CA9FD3, 0x959BD756};

#define CHECKSUM_COMP(checksum, value)            \
   do                                             \
   {                                              \
      uint32_t tmp = (checksum) ^ (value);        \
      (checksum) = tmp * FNV_PRIME ^ (tmp >> 17); \
   }                                              \
   while (0)

static uint32_t
checksum_block(void* page, size_t block_size)
{
   uint32_t sums[N_SUMS];
   uint32_t result = 0;
   size_t rows = block_size / (sizeof(uint32_t) * N_SUMS);
   uint32_t value = 0;
   char* p = (char*)page;

   memcpy(sums, base_offsets, sizeof(base_offsets));

   for (size_t i = 0; i < rows; i++)
   {
      for (size_t j = 0; j < N_SUMS; j++)
      {
         /* Copied out rather than cast, so the page does not have to be
          * aligned and the read stays defined under strict aliasing. */
         memcpy(&value, p + (i * N_SUMS + j) * sizeof(uint32_t), sizeof(uint32_t));
         CHECKSUM_COMP(sums[j], value);
      }
   }

   for (size_t i = 0; i < 2; i++)
   {
      for (size_t j = 0; j < N_SUMS; j++)
      {
         CHECKSUM_COMP(sums[j], 0);
      }
   }

   for (size_t i = 0; i < N_SUMS; i++)
   {
      result ^= sums[i];
   }

   return result;
}

uint16_t
pgmoneta_page_checksum(void* page, size_t block_size, uint32_t blockno)
{
   uint16_t saved = 0;
   uint16_t zero = 0;
   uint32_t checksum = 0;
   char* p = (char*)page;

   /* The stored checksum is not part of what is summed. */
   memcpy(&saved, p + PD_CHECKSUM_OFFSET, sizeof(saved));
   memcpy(p + PD_CHECKSUM_OFFSET, &zero, sizeof(zero));

   checksum = checksum_block(page, block_size);

   memcpy(p + PD_CHECKSUM_OFFSET, &saved, sizeof(saved));

   checksum ^= blockno;

   /* Folded so that a valid checksum is never zero. */
   return (uint16_t)((checksum % 65535) + 1);
}

uint16_t
pgmoneta_page_stored_checksum(void* page)
{
   uint16_t stored = 0;

   memcpy(&stored, (char*)page + PD_CHECKSUM_OFFSET, sizeof(stored));

   return stored;
}

bool
pgmoneta_page_is_new(void* page, size_t block_size)
{
   char* p = (char*)page;

   for (size_t i = 0; i < block_size; i++)
   {
      if (p[i] != 0)
      {
         return false;
      }
   }

   return true;
}

bool
pgmoneta_page_verify(void* page, size_t block_size, uint32_t blockno,
                     uint16_t* computed, uint16_t* stored)
{
   uint16_t c = 0;
   uint16_t s = 0;

   if (pgmoneta_page_is_new(page, block_size))
   {
      return true;
   }

   s = pgmoneta_page_stored_checksum(page);
   c = pgmoneta_page_checksum(page, block_size, blockno);

   if (computed != NULL)
   {
      *computed = c;
   }

   if (stored != NULL)
   {
      *stored = s;
   }

   return c == s;
}

/* The files PostgreSQL leaves out of its own checksumming, plus the ones
 * pgmoneta writes itself. Kept the same as pg_checksums so that what is
 * checked here matches what it checks. */
static bool
excluded(char* name)
{
   if (!strcmp(name, "pg_control") ||
       !strcmp(name, "pg_filenode.map") ||
       !strcmp(name, "PG_VERSION") ||
       !strncmp(name, "pg_internal.init", strlen("pg_internal.init")))
   {
      return true;
   }

   /* Temporary relations and the directories holding them. */
   if (!strncmp(name, "pgsql_tmp", strlen("pgsql_tmp")))
   {
      return true;
   }

   if (!strcmp(name, ".DS_Store"))
   {
      return true;
   }

   /* An incremental backup stores only the blocks that changed, behind a
    * header, so the file is not a run of pages at their own block numbers and
    * cannot be walked like one. Reconstructing the block numbers from the
    * incremental header is worth doing, but it is not this. */
   if (!strncmp(name, INCREMENTAL_PREFIX, INCREMENTAL_PREFIX_LENGTH))
   {
      return true;
   }

   return false;
}

/* Does any component of the path name a directory holding relations. */
static bool
under_relation_directory(char* path)
{
   char* p = path;

   while (p != NULL && *p != '\0')
   {
      if (!strncmp(p, "base/", 5) ||
          !strncmp(p, "global/", 7) ||
          !strncmp(p, "pg_tblspc/", 10))
      {
         return true;
      }

      p = strchr(p, '/');
      if (p != NULL)
      {
         p++;
      }
   }

   return false;
}

bool
pgmoneta_page_checksummable(char* path, uint32_t* segno)
{
   char* name = NULL;
   char* dot = NULL;

   if (segno != NULL)
   {
      *segno = 0;
   }

   if (path == NULL)
   {
      return false;
   }

   if (!under_relation_directory(path))
   {
      return false;
   }

   name = strrchr(path, '/');
   name = name != NULL ? name + 1 : path;

   if (excluded(name))
   {
      return false;
   }

   /* A relation is split at 1GB, and the segments after the first carry the
    * segment number as a suffix: 16384, 16384.1, 16384.2. The number matters
    * because it is part of the block number the checksum is taken over. */
   dot = strchr(name, '.');
   if (dot != NULL)
   {
      char* end = NULL;
      long long value = 0;

      errno = 0;
      value = strtoll(dot + 1, &end, 10);

      /* Anything else after a dot is not a segment, so not a relation. A
       * number too large to be one is not either, and must not be truncated
       * into a plausible looking segment: the segment is part of every block
       * number, so a wrong one fails every page of a file that is fine. */
      if (end == dot + 1 || *end != '\0' || errno == ERANGE ||
          value < 0 || value > UINT32_MAX)
      {
         return false;
      }

      if (segno != NULL)
      {
         *segno = (uint32_t)value;
      }
   }

   return true;
}

int
pgmoneta_page_verify_file(char* path, size_t block_size, uint32_t relseg_size,
                          uint32_t segno, uint32_t* blockno,
                          uint16_t* computed, uint16_t* stored,
                          int* number_of_bad)
{
   FILE* file = NULL;
   char* page = NULL;
   uint32_t block = 0;
   int bad = 0;

   if (number_of_bad != NULL)
   {
      *number_of_bad = 0;
   }

   page = (char*)malloc(block_size);
   if (page == NULL)
   {
      goto error;
   }

   file = fopen(path, "rb");
   if (file == NULL)
   {
      goto error;
   }

   while (true)
   {
      uint16_t c = 0;
      uint16_t s = 0;
      size_t r = fread(page, 1, block_size, file);

      if (r == 0)
      {
         /* End of the file on a block boundary, which is how it should end. */
         if (feof(file))
         {
            break;
         }

         goto error;
      }

      /* A relation is whole blocks. Anything else is a truncated file, which
       * is a problem in its own right rather than something to skip over. */
      if (r != block_size)
      {
         goto error;
      }

      if (!pgmoneta_page_verify(page, block_size, block + segno * relseg_size, &c, &s))
      {
         if (bad == 0)
         {
            /* Report the first failure; the count says how many there were. */
            if (blockno != NULL)
            {
               *blockno = block;
            }
            if (computed != NULL)
            {
               *computed = c;
            }
            if (stored != NULL)
            {
               *stored = s;
            }
         }

         bad++;
      }

      block++;
   }

   fclose(file);
   free(page);

   if (number_of_bad != NULL)
   {
      *number_of_bad = bad;
   }

   return bad > 0 ? 1 : 0;

error:

   if (file != NULL)
   {
      fclose(file);
   }

   free(page);

   return 1;
}
