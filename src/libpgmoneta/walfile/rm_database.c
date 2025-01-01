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

#include <walfile/rm_database.h>
#include <walfile/rm.h>
#include <utils.h>

static char*
database_desc_v17(char* buf, char* rec, uint8_t info)
{

   if (info == XLOG_DBASE_CREATE_FILE_COPY)
   {
      struct xl_dbase_create_file_copy_rec* xlrec =
         (struct xl_dbase_create_file_copy_rec*) rec;

      buf = pgmoneta_format_and_append(buf, "copy dir %u/%u to %u/%u",
                                       xlrec->src_tablespace_id, xlrec->src_db_id,
                                       xlrec->tablespace_id, xlrec->db_id);
   }
   else if (info == XLOG_DBASE_CREATE_WAL_LOG)
   {
      struct xl_dbase_create_wal_log_rec* xlrec =
         (struct xl_dbase_create_wal_log_rec*) rec;

      buf = pgmoneta_format_and_append(buf, "create dir %u/%u",
                                       xlrec->tablespace_id, xlrec->db_id);
   }
   else if (info == XLOG_DBASE_DROP_V17)
   {
      struct xl_dbase_drop_rec* xlrec = (struct xl_dbase_drop_rec*) rec;
      int i;

      buf = pgmoneta_format_and_append(buf, "dir");
      for (i = 0; i < xlrec->ntablespaces; i++)
      {
         buf = pgmoneta_format_and_append(buf, " %u/%u",
                                          xlrec->tablespace_ids[i], xlrec->db_id);
      }
   }
   return buf;
}

char*
pgmoneta_wal_database_desc(char* buf, struct decoded_xlog_record* record)
{
   char* rec = XLOG_REC_GET_DATA(record);
   uint8_t info = XLOG_REC_GET_INFO(record) & ~XLR_INFO_MASK;

   if (server_config->version >= 17)
   {
      buf = database_desc_v17(buf, rec, info);
      return buf;
   }

   if (info == XLOG_DBASE_CREATE)
   {
      struct xl_dbase_create_rec* xlrec = (struct xl_dbase_create_rec*) rec;

      buf = pgmoneta_format_and_append(buf, "copy dir %u/%u to %u/%u",
                                       xlrec->src_tablespace_id, xlrec->src_db_id,
                                       xlrec->tablespace_id, xlrec->db_id);
   }
   else if (info == XLOG_DBASE_DROP)
   {
      struct xl_dbase_drop_rec* xlrec = (struct xl_dbase_drop_rec*) rec;
      int i;

      buf = pgmoneta_format_and_append(buf, "dir");
      for (i = 0; i < xlrec->ntablespaces; i++)
      {
         buf = pgmoneta_format_and_append(buf, " %u/%u",
                                          xlrec->tablespace_ids[i], xlrec->db_id);
      }
   }
   return buf;
}
