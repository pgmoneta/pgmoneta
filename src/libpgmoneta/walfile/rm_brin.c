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

#include <walfile/rm_brin.h>

#include <utils.h>

char*
pgmoneta_wal_brin_desc(char* buf, struct decoded_xlog_record* record)
{
   char* rec = XLOG_REC_GET_DATA(record);
   uint8_t info = XLOG_REC_GET_INFO(record) & ~XLR_INFO_MASK;

   info &= XLOG_BRIN_OPMASK;
   if (info == XLOG_BRIN_CREATE_INDEX)
   {
      struct xl_brin_createidx* xlrec = (struct xl_brin_createidx*)rec;

      buf = pgmoneta_format_and_append(buf, "v%d pagesPerRange %u",
                                       xlrec->version, xlrec->pagesPerRange);
   }
   else if (info == XLOG_BRIN_INSERT)
   {
      struct xl_brin_insert* xlrec = (struct xl_brin_insert*)rec;

      buf = pgmoneta_format_and_append(buf, "heapBlk %u pagesPerRange %u offnum %u",
                                       xlrec->heapBlk,
                                       xlrec->pagesPerRange,
                                       xlrec->offnum);
   }
   else if (info == XLOG_BRIN_UPDATE)
   {
      struct xl_brin_update* xlrec = (struct xl_brin_update*)rec;

      buf = pgmoneta_format_and_append(buf, "heapBlk %u pagesPerRange %u old offnum %u, new offnum %u",
                                       xlrec->insert.heapBlk,
                                       xlrec->insert.pagesPerRange,
                                       xlrec->oldOffnum,
                                       xlrec->insert.offnum);
   }
   else if (info == XLOG_BRIN_SAMEPAGE_UPDATE)
   {
      struct xl_brin_samepage_update* xlrec = (struct xl_brin_samepage_update*)rec;

      buf = pgmoneta_format_and_append(buf, "offnum %u", xlrec->offnum);
   }
   else if (info == XLOG_BRIN_REVMAP_EXTEND)
   {
      struct xl_brin_revmap_extend* xlrec = (struct xl_brin_revmap_extend*)rec;

      buf = pgmoneta_format_and_append(buf, "targetBlk %u", xlrec->targetBlk);
   }
   else if (info == XLOG_BRIN_DESUMMARIZE)
   {
      struct xl_brin_desummarize* xlrec = (struct xl_brin_desummarize*)rec;

      buf = pgmoneta_format_and_append(buf, "pagesPerRange %u, heapBlk %u, page offset %u",
                                       xlrec->pagesPerRange, xlrec->heapBlk, xlrec->regOffset);
   }
   return buf;
}
