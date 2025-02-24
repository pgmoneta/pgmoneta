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

#include <walfile/rm_commit_ts.h>
#include <utils.h>

struct xl_commit_ts_truncate*
create_xl_commit_ts_truncate()
{
   struct xl_commit_ts_truncate* wrapper = malloc(sizeof(struct xl_commit_ts_truncate));
   if (server_config->version >= 17)
   {
      wrapper->parse = xl_commit_ts_truncate_parse_v17;
      wrapper->format = xl_commit_ts_truncate_format_v17;
   }
   else
   {
      wrapper->parse = xl_commit_ts_truncate_parse_v16;
      wrapper->format = xl_commit_ts_truncate_format_v16;
   }
   return wrapper;
}

void
xl_commit_ts_truncate_parse_v16(struct xl_commit_ts_truncate* wrapper, char* rec)
{
   char* ptr = (char*)rec;
   memcpy(&wrapper->data.v16.pageno, ptr, sizeof(int64_t));
   ptr += sizeof(int64_t);
   memcpy(&wrapper->data.v16.oldestXid, ptr, sizeof(transaction_id));
}

void
xl_commit_ts_truncate_parse_v17(struct xl_commit_ts_truncate* wrapper, char* rec)
{
   char* ptr = (char*)rec;
   memcpy(&wrapper->data.v17.pageno, ptr, sizeof(int));
   ptr += sizeof(int);
   memcpy(&wrapper->data.v17.oldestXid, ptr, sizeof(transaction_id));
}

char*
xl_commit_ts_truncate_format_v16(struct xl_commit_ts_truncate* wrapper, char* buf)
{
   return pgmoneta_format_and_append(buf, "pageno %d, oldest_xid %u",
                                     wrapper->data.v16.pageno, wrapper->data.v16.oldestXid);
}

char*
xl_commit_ts_truncate_format_v17(struct xl_commit_ts_truncate* wrapper, char* buf)
{
   return pgmoneta_format_and_append(buf, "pageno %d, oldest_xid %u",
                                     wrapper->data.v17.pageno, wrapper->data.v17.oldestXid);
}

char*
pgmoneta_wal_commit_ts_desc(char* buf, struct decoded_xlog_record* record)
{
   char* rec = XLOG_REC_GET_DATA(record);
   uint8_t info = XLOG_REC_GET_INFO(record) & ~XLR_INFO_MASK;

   if (info == COMMIT_TS_ZEROPAGE)
   {
      int64_t pageno;

      memcpy(&pageno, rec, server_config->version >= 17 ? sizeof(int) : sizeof(int64_t));
      buf = pgmoneta_format_and_append(buf, "%d", pageno);
   }
   else if (info == COMMIT_TS_TRUNCATE)
   {
      struct xl_commit_ts_truncate* trunc = create_xl_commit_ts_truncate();
      buf = trunc->format(trunc, buf);
      free(trunc);
   }
   return buf;
}
