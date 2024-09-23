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

#include <utils.h>
#include <walfile/rm.h>
#include <walfile/rm_gin.h>
#include <walfile/wal_reader.h>

static char*
desc_recompress_leaf(char* buf, struct gin_xlog_recompress_data_leaf* insertData)
{
   int i;
   char* walbuf = ((char*) insertData) + sizeof(struct gin_xlog_recompress_data_leaf);

   buf = pgmoneta_format_and_append(buf, " %d segments:", (int) insertData->nactions);

   for (i = 0; i < insertData->nactions; i++)
   {
      uint8_t a_segno = *((uint8_t*) (walbuf++));
      uint8_t a_action = *((uint8_t*) (walbuf++));
      uint16_t nitems = 0;
      int newsegsize = 0;

      if (a_action == GIN_SEGMENT_INSERT ||
          a_action == GIN_SEGMENT_REPLACE)
      {
         newsegsize = SIZE_OF_GIN_POSTING_LIST((struct gin_posting_list*) walbuf);
         walbuf += SHORTALIGN(newsegsize);
      }

      if (a_action == GIN_SEGMENT_ADDITEMS)
      {
         memcpy(&nitems, walbuf, sizeof(uint16_t));
         walbuf += sizeof(uint16_t);
         walbuf += nitems * sizeof(struct item_pointer_data);
      }

      switch (a_action)
      {
         case GIN_SEGMENT_ADDITEMS:
            buf = pgmoneta_format_and_append(buf, " %d (add %d items)", a_segno, nitems);
            break;
         case GIN_SEGMENT_DELETE:
            buf = pgmoneta_format_and_append(buf, " %d (delete)", a_segno);
            break;
         case GIN_SEGMENT_INSERT:
            buf = pgmoneta_format_and_append(buf, " %d (insert)", a_segno);
            break;
         case GIN_SEGMENT_REPLACE:
            buf = pgmoneta_format_and_append(buf, " %d (replace)", a_segno);
            break;
         default:
            buf = pgmoneta_format_and_append(buf, " %d unknown action %d ???", a_segno, a_action);
            /* cannot decode unrecognized actions further */
            break;
      }
   }
   return buf;
}

char*
pgmoneta_wal_gin_desc(char* buf, struct decoded_xlog_record* record)
{
   char* rec = record->main_data;
   uint8_t info = record->header.xl_info & ~XLR_INFO_MASK;

   switch (info)
   {
      case XLOG_GIN_CREATE_PTREE:
         /* no further information */
         break;
      case XLOG_GIN_INSERT: {
         struct gin_xlog_insert* xlrec = (struct gin_xlog_insert*) rec;

         buf = pgmoneta_format_and_append(buf, "isdata: %c isleaf: %c",
                                          (xlrec->flags & GIN_INSERT_ISDATA) ? 'T' : 'F',
                                          (xlrec->flags & GIN_INSERT_ISLEAF) ? 'T' : 'F');
         if (!(xlrec->flags & GIN_INSERT_ISLEAF))
         {
            char* payload = rec + sizeof(struct gin_xlog_insert);
            block_number leftChildBlkno;
            block_number rightChildBlkno;

            leftChildBlkno = BLOCK_ID_GET_BLOCK_NUMBER((block_id) payload);
            payload += sizeof(struct block_id_data);
            rightChildBlkno = BLOCK_ID_GET_BLOCK_NUMBER((block_id) payload);
            payload += sizeof(block_number);
            buf = pgmoneta_format_and_append(buf, " children: %u/%u",
                                             leftChildBlkno, rightChildBlkno);
         }
         if (XLogRecHasBlockImage(record, 0))
         {
            if (XLOG_REC_BLOCK_IMAGE_APPLY(record, 0))
            {
               buf = pgmoneta_format_and_append(buf, " (full page image)");
            }
            else
            {
               buf = pgmoneta_format_and_append(buf, " (full page image, for WAL verification)");
            }
         }
         else
         {
            char* payload = pgmoneta_wal_get_record_block_data(record, 0, NULL);

            if (!(xlrec->flags & GIN_INSERT_ISDATA))
            {
               buf = pgmoneta_format_and_append(buf, " isdelete: %c",
                                                (((struct gin_xlog_insert_entry*) payload)->isDelete) ? 'T' : 'F');
            }
            else if (xlrec->flags & GIN_INSERT_ISLEAF)
            {
               buf = desc_recompress_leaf(buf, (struct gin_xlog_recompress_data_leaf*) payload);
            }
            else
            {
               struct gin_xlog_insert_data_internal* insertData =
                  (struct gin_xlog_insert_data_internal*) payload;

               buf = pgmoneta_format_and_append(buf, " pitem: %u-%u/%u",
                                                POSTING_ITEM_GET_BLOCK_NUMBER(&insertData->newitem),
                                                ITEM_POINTER_GET_BLOCK_NUMBER(&insertData->newitem.key),
                                                ITEM_POINTER_GET_OFFSET_NUMBER(&insertData->newitem.key));
            }
         }
      }
      break;
      case XLOG_GIN_SPLIT: {
         struct gin_xlog_split* xlrec = (struct gin_xlog_split*) rec;

         buf = pgmoneta_format_and_append(buf, "isrootsplit: %c",
                                          (((struct gin_xlog_split*) rec)->flags & GIN_SPLIT_ROOT) ? 'T' : 'F');
         buf = pgmoneta_format_and_append(buf, " isdata: %c isleaf: %c",
                                          (xlrec->flags & GIN_INSERT_ISDATA) ? 'T' : 'F',
                                          (xlrec->flags & GIN_INSERT_ISLEAF) ? 'T' : 'F');
      }
      break;
      case XLOG_GIN_VACUUM_PAGE:
         /* no further information */
         break;
      case XLOG_GIN_VACUUM_DATA_LEAF_PAGE: {
         if (XLogRecHasBlockImage(record, 0))
         {
            if (XLOG_REC_BLOCK_IMAGE_APPLY(record, 0))
            {
               buf = pgmoneta_format_and_append(buf, " (full page image)");
            }
            else
            {
               buf = pgmoneta_format_and_append(buf, " (full page image, for WAL verification)");
            }
         }
         else
         {
            struct gin_xlog_vacuum_data_leaf_page* xlrec =
               (struct gin_xlog_vacuum_data_leaf_page*) pgmoneta_wal_get_record_block_data(record, 0, NULL);

            buf = desc_recompress_leaf(buf, &xlrec->data);
         }
      }
      break;
      case XLOG_GIN_DELETE_PAGE:
         /* no further information */
         break;
      case XLOG_GIN_UPDATE_META_PAGE:
         /* no further information */
         break;
      case XLOG_GIN_INSERT_LISTPAGE:
         /* no further information */
         break;
      case XLOG_GIN_DELETE_LISTPAGE:
         buf = pgmoneta_format_and_append(buf, "ndeleted: %d",
                                          ((struct gin_xlog_delete_list_pages*) rec)->ndeleted);
         break;
   }
   return buf;
}
