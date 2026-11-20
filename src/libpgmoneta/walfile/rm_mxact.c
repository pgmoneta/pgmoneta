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

#include <walfile/rm.h>
#include <walfile/rm_mxact.h>
#include <utils.h>

#include <inttypes.h>
#include <stddef.h>
#include <string.h>

static char*
out_member(char* buf, struct multi_xact_member* member)
{
   buf = pgmoneta_format_and_append(buf, "%u ", member->xid);
   switch (member->status)
   {
      case MULTI_XACT_STATUS_FOR_KEY_SHARE:
         buf = pgmoneta_format_and_append(buf, "(keysh) ");
         break;
      case MULTI_XACT_STATUS_FOR_SHARE:
         buf = pgmoneta_format_and_append(buf, "(sh) ");
         break;
      case MULTI_XACT_STATUS_FOR_NO_KEY_UPDATE:
         buf = pgmoneta_format_and_append(buf, "(fornokeyupd) ");
         break;
      case MULTI_XACT_STATUS_FOR_UPDATE:
         buf = pgmoneta_format_and_append(buf, "(forupd) ");
         break;
      case MULTI_XACT_STATUS_NO_KEY_UPDATE:
         buf = pgmoneta_format_and_append(buf, "(nokeyupd) ");
         break;
      case MULTI_XACT_STATUS_UPDATE:
         buf = pgmoneta_format_and_append(buf, "(upd) ");
         break;
      default:
         buf = pgmoneta_format_and_append(buf, "(unk) ");
         break;
   }
   return buf;
}

char*
pgmoneta_wal_multixact_desc(char* buf, struct decoded_xlog_record* record)
{
   char* rec = XLOG_REC_GET_DATA(record);
   uint32_t rec_len = XLOG_REC_GET_DATA_LEN(record);
   uint8_t info = XLOG_REC_GET_INFO(record) & ~XLR_INFO_MASK;

   if (info == XLOG_MULTIXACT_ZERO_OFF_PAGE ||
       info == XLOG_MULTIXACT_ZERO_MEM_PAGE)
   {
      if (server_config->version >= 17)
      {
         /* PostgreSQL 17 widened SLRU page numbers to 64 bits */
         int64_t pageno = 0;
         size_t pageno_len = sizeof(int64_t);

         if (pageno_len > rec_len)
         {
            pageno_len = rec_len;
         }
         memcpy(&pageno, rec, pageno_len);
         buf = pgmoneta_format_and_append(buf, "%" PRId64, pageno);
      }
      else
      {
         int pageno = 0;
         size_t pageno_len = sizeof(int);

         if (pageno_len > rec_len)
         {
            pageno_len = rec_len;
         }
         memcpy(&pageno, rec, pageno_len);
         buf = pgmoneta_format_and_append(buf, "%d", pageno);
      }
   }
   else if (info == XLOG_MULTIXACT_CREATE_ID)
   {
      multi_xact_id mid = 0;
      int32_t nmembers = 0;
      int32_t max_members;
      size_t hdr_len;
      char* members;

      if (server_config->version >= 19)
      {
         /* PostgreSQL 19 widened the member offset to 64 bits */
         uint64_t moff = 0;

         hdr_len = SIZE_OF_MULTIXACT_CREATE_V19;
         if (rec_len < hdr_len)
         {
            return buf;
         }
         memcpy(&mid, rec + offsetof(struct xl_multixact_create_v19, mid), sizeof(mid));
         memcpy(&moff, rec + offsetof(struct xl_multixact_create_v19, moff), sizeof(moff));
         memcpy(&nmembers, rec + offsetof(struct xl_multixact_create_v19, nmembers), sizeof(nmembers));
         buf = pgmoneta_format_and_append(buf, "%u offset %" PRIu64 " nmembers %d: ", mid, moff, nmembers);
      }
      else
      {
         multi_xact_offset moff = 0;

         hdr_len = SIZE_OF_MULTIXACT_CREATE;
         if (rec_len < hdr_len)
         {
            return buf;
         }
         memcpy(&mid, rec + offsetof(struct xl_multixact_create, mid), sizeof(mid));
         memcpy(&moff, rec + offsetof(struct xl_multixact_create, moff), sizeof(moff));
         memcpy(&nmembers, rec + offsetof(struct xl_multixact_create, nmembers), sizeof(nmembers));
         buf = pgmoneta_format_and_append(buf, "%u offset %u nmembers %d: ", mid, moff, nmembers);
      }

      /* Never read members beyond the decoded payload */
      members = rec + hdr_len;
      max_members = (int32_t)((rec_len - hdr_len) / sizeof(struct multi_xact_member));
      if (nmembers > max_members)
      {
         nmembers = max_members;
      }

      for (int32_t i = 0; i < nmembers; i++)
      {
         struct multi_xact_member member;

         memcpy(&member, members + (size_t)i * sizeof(struct multi_xact_member), sizeof(member));
         /* out_member() may reallocate the buffer */
         buf = out_member(buf, &member);
      }
   }
   else if (info == XLOG_MULTIXACT_TRUNCATE_ID)
   {
      if (server_config->version >= 19)
      {
         struct xl_multixact_truncate_v19 xlrec;

         if (rec_len < SIZE_OF_MULTIXACT_TRUNCATE_V19)
         {
            return buf;
         }
         memcpy(&xlrec, rec, sizeof(xlrec));
         buf = pgmoneta_format_and_append(buf, "oldestMulti %u, oldestOffset %" PRIu64,
                                          xlrec.oldest_multi, xlrec.oldest_offset);
      }
      else
      {
         struct xl_multixact_truncate xlrec;

         if (rec_len < SIZE_OF_MULTIXACT_TRUNCATE)
         {
            return buf;
         }
         memcpy(&xlrec, rec, sizeof(xlrec));
         buf = pgmoneta_format_and_append(buf, "offsets [%u, %u), members [%u, %u)",
                                          xlrec.start_trunc_off, xlrec.end_trunc_off,
                                          xlrec.start_trunc_memb, xlrec.end_trunc_memb);
      }
   }
   return buf;
}
