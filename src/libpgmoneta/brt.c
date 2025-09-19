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

#include <art.h>
#include <brt.h>
#include <pgmoneta.h>
#include <stddef.h>
#include <stdint.h>
#include <utils.h>
#include <wal.h>
#include <walfile/wal_reader.h>

static void generate_art_key_from_brt_key(block_ref_table_key brt_key, char** art_key);
static int brt_comparator(const void* a, const void* b);
static int brt_insert(block_ref_table* brt, block_ref_table_key key, block_ref_table_entry** brt_entry, bool* found);
static block_ref_table_entry* brt_lookup(block_ref_table* brt, block_ref_table_key key);

static void brt_set_limit_block(block_ref_table_entry* entry, block_number limit_block);
static void brt_mark_block_modified(block_ref_table_entry* entry, block_number blocknum);

static void brt_write(FILE* f, block_ref_table_buffer* buffer, void* data, int length);
static int brt_read(FILE* f, struct block_ref_table_reader* reader, void* data, int length);
static void brt_file_terminate(FILE* f, block_ref_table_buffer* buffer);
static void brt_flush(FILE* f, block_ref_table_buffer* buffer);

static bool brt_read_next_relation(FILE* f, struct block_ref_table_reader* reader, struct rel_file_locator* rlocator, enum fork_number* forknum, block_number* limit_block);
static unsigned brt_reader_get_blocks(FILE* f, struct block_ref_table_reader* reader, block_number* blocks, int nblocks);

int
pgmoneta_brt_create_empty(block_ref_table** brt)
{
   block_ref_table* brtab = (block_ref_table*)malloc(sizeof(block_ref_table));
   if (pgmoneta_art_create(&brtab->table))
   {
      goto error;
   }
   *brt = brtab;
   return 0;
error:

   pgmoneta_art_destroy(brtab->table);
   free(brtab);
   return 1;
}

int
pgmoneta_brt_set_limit_block(block_ref_table* brt, const struct rel_file_locator* rlocator,
                             enum fork_number forknum, block_number limit_block)
{
   block_ref_table_entry* brt_entry = NULL;
   block_ref_table_key key = {0};

   bool found = false;

   memcpy(&key.rlocator, rlocator, sizeof(struct rel_file_locator));
   key.forknum = forknum;

   /**
    * Find the entry in the table, if found, return its pointer
    * if not found, insert a new empty entry and return its pointer
    */
   if (brt_insert(brt, key, &brt_entry, &found))
   {
      goto error;
   }

   if (!found)
   {
      /* No existing entry, set the limit block for this relation block and initialize other fields */
      brt_entry->limit_block = limit_block;
      brt_entry->max_block_number = InvalidBlockNumber;
      brt_entry->nchunks = 0;
      brt_entry->chunk_size = NULL;
      brt_entry->chunk_usage = NULL;
      brt_entry->chunk_data = NULL;
      return 0;
   }

   brt_set_limit_block(brt_entry, limit_block);
   return 0;

error:
   return 1;
}

int
pgmoneta_brt_mark_block_modified(block_ref_table* brt, const struct rel_file_locator* rlocator, enum fork_number forknum, block_number blknum)
{
   block_ref_table_entry* brt_entry = NULL;
   block_ref_table_key key = {0};

   bool found = false;

   memcpy(&key.rlocator, rlocator, sizeof(struct rel_file_locator));
   key.forknum = forknum;

   /**
    * Find the entry in the table, if found, return its pointer
    * if not found, insert a new empty entry and return its pointer
    */
   if (brt_insert(brt, key, &brt_entry, &found))
   {
      goto error;
   }

   if (!found)
   {
      /*
       * We want to set the initial limit block value to something higher
       * than any legal block number. InvalidBlockNumber fits the bill.
       */
      brt_entry->limit_block = InvalidBlockNumber;
      brt_entry->max_block_number = InvalidBlockNumber;
      brt_entry->nchunks = 0;
      brt_entry->chunk_size = NULL;
      brt_entry->chunk_usage = NULL;
      brt_entry->chunk_data = NULL;
   }

   brt_mark_block_modified(brt_entry, blknum);
   return 0;

error:
   return 1;
}

block_ref_table_entry*
pgmoneta_brt_get_entry(block_ref_table* brtab, const struct rel_file_locator* rlocator,
                       enum fork_number forknum, block_number* limit_block)
{
   block_ref_table_key key = {0}; /* make sure any padding is zero */
   block_ref_table_entry* entry;

   memcpy(&key.rlocator, rlocator, sizeof(struct rel_file_locator));
   key.forknum = forknum;
   entry = brt_lookup(brtab, key);

   if (entry != NULL && limit_block != NULL)
   {
      *limit_block = entry->limit_block;
   }

   return entry;
}

int
pgmoneta_brt_entry_get_blocks(block_ref_table_entry* entry, block_number start_blkno,
                              block_number stop_blkno, block_number* blocks, int nblocks, int* n)
{
   // The size of blocks is nblocks
   uint32_t start_chunkno;
   uint32_t stop_chunkno;
   int nresults = 0;
   uint16_t usage;
   block_ref_table_chunk data;
   unsigned start_offset;
   unsigned stop_offset;

   start_chunkno = start_blkno / BLOCKS_PER_CHUNK;
   stop_chunkno = stop_blkno / BLOCKS_PER_CHUNK;
   /*
      This check ensures that if the stop block number is already the first element of the new chunk or
      is equal to InvalidBlockNumber, don't check the chunk in which the stop_blkno resides
    */
   if ((stop_blkno % BLOCKS_PER_CHUNK) != 0)
   {
      ++stop_chunkno;
   }
   if (stop_chunkno > entry->nchunks)
   {
      stop_chunkno = entry->nchunks;
   }

   for (uint32_t chunkno = start_chunkno; chunkno < stop_chunkno; ++chunkno)
   {
      usage = entry->chunk_usage[chunkno];
      data = entry->chunk_data[chunkno];
      start_offset = 0;
      stop_offset = BLOCKS_PER_CHUNK;
      /* Find the offset if its the start chunk or the inclusive end chunk */

      if (chunkno == start_chunkno)
      {
         start_offset = start_blkno % BLOCKS_PER_CHUNK;
      }
      if (chunkno == stop_chunkno - 1)
      {
         stop_offset = stop_blkno - (chunkno * BLOCKS_PER_CHUNK);
      }

      /*Handelling different representation */
      if (usage == MAX_ENTRIES_PER_CHUNK)
      {
         /* It's a bitmap, so test every relevant bit. */
         for (unsigned i = start_offset; i < stop_offset; ++i)
         {
            if ((data[i / BLOCKS_PER_ENTRY] & (1 << (i % BLOCKS_PER_ENTRY))) != 0)
            {
               block_number blkno = chunkno * BLOCKS_PER_CHUNK + i;
               blocks[nresults++] = blkno;

               /* Exit if blocks array get out of space */
               if (nresults == nblocks)
               {
                  return nresults;
               }
            }
         }
      }
      else
      {
         for (unsigned i = 0; i < usage; i++)
         {
            if (data[i] >= start_offset && data[i] < stop_offset)
            {
               block_number blkno = chunkno * BLOCKS_PER_CHUNK + data[i];
               blocks[nresults++] = blkno;

               /* Exit if blocks array get out of space */
               if (nresults == nblocks)
               {
                  return nresults;
               }
            }
         }
      }
   }

   *n = nresults;
   return 0;
}

int
pgmoneta_brt_destroy(block_ref_table* brt)
{
   if (!brt)
   {
      return 0;
   }

   pgmoneta_art_destroy(brt->table);
   free(brt);
   return 0;
}

int
pgmoneta_brt_write(block_ref_table* brt, char* file_path)
{
   FILE* file = NULL;
   block_ref_table_serialized_entry* sdata = NULL;
   block_ref_table_buffer* buffer = NULL;
   uint32_t magic = BLOCKREFTABLE_MAGIC;
   struct art_iterator* it = NULL;
   block_ref_table_entry* brtentry = NULL;
   block_ref_table_serialized_entry* sentry = NULL;
   unsigned i = 0, j;

   file = fopen(file_path, "w+");
   if (file == NULL)
   {
      return 1; // Error opening file
   }

   if ((buffer = (block_ref_table_buffer*)malloc(sizeof(block_ref_table_buffer))) == NULL)
   {
      goto error;
   }
   memset(buffer, 0, sizeof(block_ref_table_buffer));

   /* Write the magic number first */
   brt_write(file, buffer, &magic, sizeof(uint32_t));

   if (brt->table->size > 0)
   {
      i = 0;

      /* Extract entries into serializable format and sort them. */
      if ((sdata = malloc(brt->table->size * sizeof(block_ref_table_serialized_entry))) == NULL)
      {
         goto error;
      }

      if (pgmoneta_art_iterator_create(brt->table, &it))
      {
         goto error;
      }

      while (pgmoneta_art_iterator_next(it))
      {
         brtentry = (block_ref_table_entry*)it->value->data;
         block_ref_table_serialized_entry* sentry = &sdata[i++];

         sentry->rlocator = brtentry->key.rlocator;
         sentry->forknum = brtentry->key.forknum;
         sentry->limit_block = brtentry->limit_block;
         sentry->nchunks = brtentry->nchunks;

         /* trim trailing zero entries */
         while (sentry->nchunks > 0 &&
                brtentry->chunk_usage[sentry->nchunks - 1] == 0)
            sentry->nchunks--;
      }
      pgmoneta_art_iterator_destroy(it);
      qsort(sdata, i, sizeof(block_ref_table_serialized_entry), brt_comparator);

      /* Loop over entries in sorted order and serialize each one. */
      for (i = 0; i < brt->table->size; ++i)
      {
         sentry = &sdata[i];
         block_ref_table_key key = {0};

         /* Write the serialized entry itself. */
         brt_write(file, buffer, sentry, sizeof(block_ref_table_serialized_entry));

         /* Look up the original entry so we can access the chunks. */
         memcpy(&key.rlocator, &sentry->rlocator, sizeof(struct rel_file_locator));
         key.forknum = sentry->forknum;
         brtentry = brt_lookup(brt, key);

         /* Write the untruncated portion of the chunk length array. */
         if (sentry->nchunks != 0)
         {
            brt_write(file, buffer, brtentry->chunk_usage, sentry->nchunks * sizeof(uint16_t));
         }

         /* Write the contents of each chunk. */
         for (j = 0; j < brtentry->nchunks; ++j)
         {
            if (brtentry->chunk_usage[j] == 0)
            {
               continue;
            }
            brt_write(file, buffer, brtentry->chunk_data[j], brtentry->chunk_usage[j] * sizeof(uint16_t));
         }
      }
   }

   // /* Write out appropriate terminator and flush buffer. */
   brt_file_terminate(file, buffer);

   fclose(file);
   free(sdata);
   free(buffer);
   return 0;
error:
   if (file != NULL)
   {
      fclose(file);
   }
   free(sdata);
   free(buffer);
   return 1;
}

int
pgmoneta_brt_read(char* file_path, block_ref_table** brt)
{
   FILE* file = NULL;
   block_ref_table* b = NULL;
   struct rel_file_locator rlocator;
   enum fork_number forknum;
   block_number limit_block;
   block_number blocks[BLOCKS_PER_READ];
   unsigned nblocks;
   struct block_ref_table_reader* reader = NULL;
   uint32_t magic;

   file = fopen(file_path, "r");
   if (file == NULL)
   {
      return 1; // Error opening file
   }

   if (pgmoneta_brt_create_empty(&b))
   {
      goto error;
   }

   if ((reader = (struct block_ref_table_reader*)malloc(sizeof(struct block_ref_table_reader))) == NULL)
   {
      goto error;
   }
   memset(reader, 0, sizeof(struct block_ref_table_reader));

   /* Read the magic number */
   if (brt_read(file, reader, &magic, sizeof(uint32_t)))
   {
      goto error;
   }
   if (magic != BLOCKREFTABLE_MAGIC)
   {
      goto error;
   }

   while (brt_read_next_relation(file, reader, &rlocator, &forknum, &limit_block))
   {
      if (pgmoneta_brt_set_limit_block(b, &rlocator, forknum, limit_block))
      {
         goto error;
      }
      /* Now read the blocks for this relation fork */
      /* Read blocks in chunks */
      while (1)
      {
         nblocks = brt_reader_get_blocks(file, reader, blocks, BLOCKS_PER_READ);
         if (nblocks == 0)
         {
            break;
         }

         for (unsigned i = 0; i < nblocks; i++)
         {
            if (pgmoneta_brt_mark_block_modified(b, &rlocator, forknum, blocks[i]))
            {
               goto error;
            }
         }
      }
   }

   *brt = b;
   fclose(file);
   free(reader);
   return 0;
error:
   if (file != NULL)
   {
      fclose(file);
   }
   pgmoneta_brt_destroy(b);
   free(reader);
   return 1;
}

static void
generate_art_key_from_brt_key(block_ref_table_key brt_key, char** art_key)
{
   char* k = NULL;
   k = pgmoneta_append_int(k, brt_key.rlocator.spcOid);
   k = pgmoneta_append_char(k, '_');
   k = pgmoneta_append_int(k, brt_key.rlocator.dbOid);
   k = pgmoneta_append_char(k, '_');
   k = pgmoneta_append_int(k, brt_key.rlocator.relNumber);
   k = pgmoneta_append_char(k, '_');
   k = pgmoneta_append_int(k, brt_key.forknum);

   *art_key = k;
}

static int
brt_insert(block_ref_table* brt, block_ref_table_key key, block_ref_table_entry** brt_entry, bool* found)
{
   char* art_key = NULL;
   block_ref_table_entry* e = NULL;
   struct value_config value_config;
   value_config.destroy_data = pgmoneta_brt_entry_destroy;

   generate_art_key_from_brt_key(key, &art_key);

   if ((e = (block_ref_table_entry*)pgmoneta_art_search(brt->table, art_key)) != NULL)
   {
      *brt_entry = e;
      *found = true;
      goto done;
   }

   /* Create an empty entry and insert it into the table */
   e = (block_ref_table_entry*)malloc(sizeof(block_ref_table_entry));
   if (!e)
   {
      goto error;
   }

   e->key = key;

   if (pgmoneta_art_insert_with_config(brt->table, art_key, (uintptr_t)e, &value_config))
   {
      goto error;
   }
   *brt_entry = e;
done:
   free(art_key);
   return 0;
error:
   free(e);
   free(art_key);
   return 1;
}

static block_ref_table_entry*
brt_lookup(block_ref_table* brt, block_ref_table_key key)
{
   char* art_key = NULL;
   block_ref_table_entry* e = NULL;

   generate_art_key_from_brt_key(key, &art_key);
   e = (block_ref_table_entry*)pgmoneta_art_search(brt->table, art_key);

   free(art_key);
   return e;
}

static void
brt_set_limit_block(block_ref_table_entry* entry, block_number limit_block)
{
   unsigned chunkno;
   unsigned limit_chunkno;
   unsigned limit_chunkoffset;
   block_ref_table_chunk limit_chunk;

   /* do nothing if current limit_block is less than or equal to limit_block */
   if (entry->limit_block <= limit_block)
   {
      return;
   }

   entry->limit_block = limit_block;

   /* Now discard the chunks and blocks with block_number > limit_block */

   /* get the chunk and offset on which the limit_block resides */
   limit_chunkno = limit_block / BLOCKS_PER_CHUNK;
   limit_chunkoffset = limit_block % BLOCKS_PER_CHUNK;

   if (limit_chunkno >= entry->nchunks)
   {
      return;                                    /* Safety check */

   }
   /* Discard entire contents of any higher-numbered chunks. */
   for (chunkno = limit_chunkno + 1; chunkno < entry->nchunks; ++chunkno)
   {
      entry->chunk_usage[chunkno] = 0;
   }

   /* get actual chunk data */
   limit_chunk = entry->chunk_data[limit_chunkno];

   if (entry->chunk_usage[limit_chunkno] == MAX_ENTRIES_PER_CHUNK)  /* bitmap representation */
   {
      unsigned chunkoffset;
      for (chunkoffset = limit_chunkoffset; chunkoffset < BLOCKS_PER_CHUNK;
           ++chunkoffset)
      {
         limit_chunk[chunkoffset / BLOCKS_PER_ENTRY] &=
            ~(1 << (chunkoffset % BLOCKS_PER_ENTRY));
      }
   }
   else  /* array representation */
   {
      unsigned i,
               j = 0;

      /* It's an offset array. Filter out large offsets. */
      for (i = 0; i < entry->chunk_usage[limit_chunkno]; ++i)
      {
         if (limit_chunk[i] < limit_chunkoffset)
         {
            limit_chunk[j++] = limit_chunk[i];
         }
      }
      entry->chunk_usage[limit_chunkno] = j;
   }
}

static void
brt_mark_block_modified(block_ref_table_entry* entry, block_number blknum)
{
   unsigned chunkno;
   unsigned chunkoffset;
   unsigned i;

   if (entry->max_block_number == InvalidBlockNumber)
   {
      entry->max_block_number = blknum;
   }
   else
   {
      entry->max_block_number = MAX(entry->max_block_number, blknum);
   }
   /* get the chunk and offset on which the modified block resides */
   chunkno = blknum / BLOCKS_PER_CHUNK;
   chunkoffset = blknum % BLOCKS_PER_CHUNK;
   /*
    * If 'nchunks' isn't big enough for us to be able to represent the state
    * of this block, we need to enlarge our arrays.
    */
   if (chunkno >= entry->nchunks)
   {
      unsigned max_chunks;
      unsigned extra_chunks;

      /*
       * New array size is a power of 2, at least 16, big enough so that
       * chunkno will be a valid array index.
       */
      max_chunks = MAX((uint32_t)16, entry->nchunks);
      while (max_chunks < chunkno + 1)
         max_chunks *= 2;
      extra_chunks = max_chunks - entry->nchunks;

      if (entry->nchunks == 0)
      {
         entry->chunk_size = (uint16_t*)malloc(sizeof(uint16_t) * max_chunks);
         memset(&entry->chunk_size[entry->nchunks], 0, sizeof(uint16_t) * max_chunks);
         entry->chunk_usage = (uint16_t*)malloc(sizeof(uint16_t) * max_chunks);
         memset(&entry->chunk_usage[entry->nchunks], 0, sizeof(uint16_t) * max_chunks);
         entry->chunk_data = (block_ref_table_chunk*)malloc(sizeof(block_ref_table_chunk) * max_chunks);
         memset(&entry->chunk_data[entry->nchunks], 0, sizeof(block_ref_table_chunk) * max_chunks);
      }
      else
      {
         entry->chunk_size = (uint16_t*)realloc(entry->chunk_size, sizeof(uint16_t) * max_chunks);
         memset(&entry->chunk_size[entry->nchunks], 0, extra_chunks * sizeof(uint16_t));
         entry->chunk_usage = (uint16_t*)realloc(entry->chunk_usage, sizeof(uint16_t) * max_chunks);
         memset(&entry->chunk_usage[entry->nchunks], 0, extra_chunks * sizeof(uint16_t));
         entry->chunk_data = (block_ref_table_chunk*)realloc(entry->chunk_data, sizeof(block_ref_table_chunk) * max_chunks);
         memset(&entry->chunk_data[entry->nchunks], 0, extra_chunks * sizeof(block_ref_table_chunk));
      }
      entry->nchunks = max_chunks;
   }

   /*
    * If the chunk that covers this block number doesn't exist yet, create it
    * as an array and add the appropriate offset to it. We make it pretty
    * small initially, because there might only be 1 or a few block
    * references in this chunk and we don't want to use up too much memory.
    */
   if (entry->chunk_size[chunkno] == 0)
   {
      entry->chunk_data[chunkno] = (uint16_t*)malloc(sizeof(uint16_t) * INITIAL_ENTRIES_PER_CHUNK);
      // memset(&entry->chunk_data[chunkno], 0, sizeof(uint16_t) * INITIAL_ENTRIES_PER_CHUNK);
      entry->chunk_size[chunkno] = INITIAL_ENTRIES_PER_CHUNK;
      entry->chunk_data[chunkno][0] = chunkoffset;
      entry->chunk_usage[chunkno] = 1;
      return;
   }

   /*
    * If the number of entries in this chunk is already maximum, it must be a
    * bitmap. Just set the appropriate bit.
    */
   if (entry->chunk_usage[chunkno] == MAX_ENTRIES_PER_CHUNK)
   {
      block_ref_table_chunk chunk = entry->chunk_data[chunkno];

      chunk[chunkoffset / BLOCKS_PER_ENTRY] |=
         1 << (chunkoffset % BLOCKS_PER_ENTRY);
      return;
   }

   /*
    * There is an existing chunk and it's in array format. Let's find out
    * whether it already has an entry for this block. If so, we do not need
    * to do anything.
    */
   for (i = 0; i < entry->chunk_usage[chunkno]; ++i)
   {
      if (entry->chunk_data[chunkno][i] == chunkoffset)
      {
         return;
      }
   }

   /*
    * If the number of entries currently used is one less than the maximum,
    * it's time to convert to bitmap format.
    */
   if (entry->chunk_usage[chunkno] == MAX_ENTRIES_PER_CHUNK - 1)
   {
      block_ref_table_chunk newchunk;
      unsigned j;

      /* Allocate a new chunk. */
      newchunk = (uint16_t*)malloc(MAX_ENTRIES_PER_CHUNK * sizeof(uint16_t));
      memset(newchunk, 0, MAX_ENTRIES_PER_CHUNK * sizeof(uint16_t));

      /* Set the bit for each existing entry. */
      for (j = 0; j < entry->chunk_usage[chunkno]; ++j)
      {
         unsigned coff = entry->chunk_data[chunkno][j];

         newchunk[coff / BLOCKS_PER_ENTRY] |=
            1 << (coff % BLOCKS_PER_ENTRY);
      }

      /* Set the bit for the new entry. */
      newchunk[chunkoffset / BLOCKS_PER_ENTRY] |=
         1 << (chunkoffset % BLOCKS_PER_ENTRY);

      /* Swap the new chunk into place and update metadata. */
      free(entry->chunk_data[chunkno]);
      entry->chunk_data[chunkno] = newchunk;
      entry->chunk_size[chunkno] = MAX_ENTRIES_PER_CHUNK;
      entry->chunk_usage[chunkno] = MAX_ENTRIES_PER_CHUNK;
      return;
   }

   /*
    * OK, we currently have an array, and we don't need to convert to a
    * bitmap, but we do need to add a new element. If there's not enough
    * room, we'll have to expand the array.
    */
   if (entry->chunk_usage[chunkno] == entry->chunk_size[chunkno])
   {
      unsigned newsize = entry->chunk_size[chunkno] * 2;
      entry->chunk_data[chunkno] = (uint16_t*)realloc(entry->chunk_data[chunkno], newsize * sizeof(uint16_t));
      entry->chunk_size[chunkno] = newsize;
   }

   /* Now we can add the new entry. */
   entry->chunk_data[chunkno][entry->chunk_usage[chunkno]] =
      chunkoffset;
   entry->chunk_usage[chunkno]++;
}

static bool
brt_read_next_relation(FILE* f, struct block_ref_table_reader* reader,
                       struct rel_file_locator* rlocator,
                       enum fork_number* forknum,
                       block_number* limit_block)
{
   block_ref_table_serialized_entry sentry;
   block_ref_table_serialized_entry zentry = {0};

   /* Read serialized entry. */
   brt_read(f, reader, &sentry, sizeof(block_ref_table_serialized_entry));

   if (memcmp(&sentry, &zentry, sizeof(block_ref_table_serialized_entry)) == 0)
   {
      if (reader->chunk_size != NULL)
      {
         free(reader->chunk_size);
      }
      return false;
   }

   /* Read chunk size array. */
   if (reader->chunk_size != NULL)
   {
      free(reader->chunk_size);
   }
   reader->chunk_size = (uint16_t*)malloc(sentry.nchunks * sizeof(uint16_t));
   brt_read(f, reader, reader->chunk_size,
            sentry.nchunks * sizeof(uint16_t));

   /* Set up for chunk scan. */
   reader->total_chunks = sentry.nchunks;
   reader->consumed_chunks = 0;

   /* Return data to caller. */
   memcpy(rlocator, &sentry.rlocator, sizeof(struct rel_file_locator));
   *forknum = sentry.forknum;
   *limit_block = sentry.limit_block;
   return true;
}

static unsigned
brt_reader_get_blocks(FILE* f, struct block_ref_table_reader* reader,
                      block_number* blocks,
                      int nblocks)
{
   int blocks_found = 0;
   uint16_t next_chunk_size;
   uint32_t chunkno;
   uint16_t chunk_size;
   uint16_t chunkoffset;

   /* Loop collecting blocks to return to caller. */
   for (;;)
   {
      /*
       * If we've read at least one chunk, maybe it contains some block
       * numbers that could satisfy caller's request.
       */
      if (reader->consumed_chunks > 0)
      {
         chunkno = reader->consumed_chunks - 1;
         chunk_size = reader->chunk_size[chunkno];

         if (chunk_size == MAX_ENTRIES_PER_CHUNK)
         {
            /* Bitmap format, so search for bits that are set. */
            while (reader->chunk_position < BLOCKS_PER_CHUNK &&
                   blocks_found < nblocks)
            {
               chunkoffset = reader->chunk_position;
               if ((reader->chunk_data[chunkoffset / BLOCKS_PER_ENTRY] & (1u << (chunkoffset % BLOCKS_PER_ENTRY))) != 0)
               {
                  blocks[blocks_found++] =
                     chunkno * BLOCKS_PER_CHUNK + chunkoffset;
               }
               ++reader->chunk_position;
            }
         }
         else
         {
            /* Not in bitmap format, so each entry is a 2-byte offset. */
            while (reader->chunk_position < chunk_size && blocks_found < nblocks)
            {
               blocks[blocks_found++] = chunkno * BLOCKS_PER_CHUNK + reader->chunk_data[reader->chunk_position];
               ++reader->chunk_position;
            }
         }
      }

      /* We found enough blocks, so we're done. */
      if (blocks_found >= nblocks)
      {
         break;
      }

      /*
       * We didn't find enough blocks, so we must need the next chunk. If
       * there are none left, though, then we're done anyway.
       */
      if (reader->consumed_chunks == reader->total_chunks)
      {
         break;
      }

      /*
       * Read data for next chunk and reset scan position to beginning of
       * chunk. Note that the next chunk might be empty, in which case we
       * consume the chunk without actually consuming any bytes from the
       * underlying file.
       */
      next_chunk_size = reader->chunk_size[reader->consumed_chunks];
      if (next_chunk_size > 0)
      {
         brt_read(f, reader, reader->chunk_data, next_chunk_size * sizeof(uint16_t));
      }
      ++reader->consumed_chunks;
      reader->chunk_position = 0;
   }

   return blocks_found;
}

void
pgmoneta_brt_entry_destroy(uintptr_t entry)
{
   block_ref_table_entry* e = (block_ref_table_entry*)entry;
   if (!e)
   {
      return;
   }
   uint32_t chunknum = e->nchunks;

   for (uint32_t i = 0; i < chunknum; i++)
   {
      free(e->chunk_data[i]);
   }
   free(e->chunk_size);
   free(e->chunk_usage);
   free(e->chunk_data);
   free(e);
}

/*
 * Comparator for BlockRefTableSerializedEntry objects.
 *
 * We make the tablespace OID the first column of the sort key to match
 * the on-disk tree structure.
 */
static int
brt_comparator(const void* a, const void* b)
{
   const block_ref_table_serialized_entry* sa = a;
   const block_ref_table_serialized_entry* sb = b;

   if (sa->rlocator.spcOid > sb->rlocator.spcOid)
   {
      return 1;
   }
   if (sa->rlocator.spcOid < sb->rlocator.spcOid)
   {
      return -1;
   }

   if (sa->rlocator.dbOid > sb->rlocator.dbOid)
   {
      return 1;
   }
   if (sa->rlocator.dbOid < sb->rlocator.dbOid)
   {
      return -1;
   }

   if (sa->rlocator.relNumber > sb->rlocator.relNumber)
   {
      return 1;
   }
   if (sa->rlocator.relNumber < sb->rlocator.relNumber)
   {
      return -1;
   }

   if (sa->forknum > sb->forknum)
   {
      return 1;
   }
   if (sa->forknum < sb->forknum)
   {
      return -1;
   }

   return 0;
}

static void
brt_flush(FILE* f, block_ref_table_buffer* buffer)
{
   size_t bytes_written = 0;
   while (bytes_written < (size_t)buffer->used)
   {
      bytes_written += fwrite(buffer->data, sizeof(char), buffer->used, f);
   }
   fflush(f);

   buffer->used = 0;
}

static void
brt_write(FILE* f, block_ref_table_buffer* buffer, void* data, int length)
{
   size_t bytes_written = 0;
   size_t buffer_size = sizeof(buffer->data);

   /* If the new data can't fit into the buffer, flush the buffer. */
   if ((size_t)(buffer->used + length) > buffer_size)
   {
      brt_flush(f, buffer);
   }

   /* If the new data would fill the buffer, or more, write it directly. */
   if ((size_t)length >= buffer_size)
   {
      while (bytes_written < (size_t)length)
      {
         bytes_written += fwrite((char*)data, sizeof(char), length, f);
      }
      fflush(f);
      return;
   }

   /* Otherwise, copy the new data into the buffer. */
   memcpy(&buffer->data[buffer->used], data, length);
   buffer->used += length;
}

static int
brt_read(FILE* f, struct block_ref_table_reader* reader, void* data, int length)
{
   block_ref_table_buffer* buffer = &reader->buffer;
   size_t buffer_size = sizeof(buffer->data);
   int bytes_to_copy, bytes_read;

   while (length > 0)
   {
      if (buffer->cursor < buffer->used) /* There is data in the buffer to read */
      {
         bytes_to_copy = MIN(length, buffer->used - buffer->cursor);
         memcpy(data, &buffer->data[buffer->cursor], bytes_to_copy);
         buffer->cursor += bytes_to_copy;
         length -= bytes_to_copy;
      }
      else if ((size_t)length >= buffer_size) /* Read directly in this case */
      {
         bytes_read = fread(data, sizeof(char), length, f);
         length -= bytes_read;
         if (bytes_read == 0)
         {
            return 1;
         }
      }
      else /* Refill the buffer */
      {
         buffer->used = fread(&buffer->data[0], sizeof(char), buffer_size, f);
         buffer->cursor = 0;
         if (buffer->used == 0)
         {
            return 1;
         }
      }
   }
   return 0;
}

static void
brt_file_terminate(FILE* f, block_ref_table_buffer* buffer)
{
   block_ref_table_serialized_entry zentry = {0};
   /* Write a sentinel indicating that there are no more entries. */
   brt_write(f, buffer, &zentry, sizeof(block_ref_table_serialized_entry));
   /* Flush any leftover data out of our buffer. */
   brt_flush(f, buffer);
}
