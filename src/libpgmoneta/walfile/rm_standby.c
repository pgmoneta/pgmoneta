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

#include <walfile/rm_standby.h>
#include <utils.h>

static char*
standby_desc_running_xacts(char* buf, struct xl_running_xacts* xlrec)
{
   int i;

   buf = pgmoneta_format_and_append(buf, "next_xid %u latest_completed_xid %u oldest_running_xid %u",
                                    xlrec->next_xid,
                                    xlrec->latest_completed_xid,
                                    xlrec->oldest_running_xid);
   if (xlrec->xcnt > 0)
   {
      buf = pgmoneta_format_and_append(buf, "; %d xacts:", xlrec->xcnt);
      for (i = 0; i < xlrec->xcnt; i++)
      {
         buf = pgmoneta_format_and_append(buf, " %u", xlrec->xids[i]);
      }
   }

   if (xlrec->subxid_overflow)
   {
      buf = pgmoneta_format_and_append(buf, "; subxid overflowed");
   }

   if (xlrec->subxcnt > 0)
   {
      buf = pgmoneta_format_and_append(buf, "; %d subxacts:", xlrec->subxcnt);
      for (i = 0; i < xlrec->subxcnt; i++)
      {
         buf = pgmoneta_format_and_append(buf, " %u", xlrec->xids[xlrec->xcnt + i]);
      }
   }
   return buf;
}

char*
pgmoneta_wal_standby_desc_invalidations(char* buf, int nmsgs, union shared_invalidation_message* msgs, oid dbId, oid tsId, bool rel_cache_init_file_inval
                                        )
{
   int i;

   /* Do nothing if there are no invalidation messages */
   if (nmsgs <= 0)
   {
      return buf;
   }

   if (rel_cache_init_file_inval)
   {
      buf = pgmoneta_format_and_append(buf, "; relcache init file inval db_id %u ts_id %u", dbId, tsId);
   }

   buf = pgmoneta_format_and_append(buf, "; inval msgs:");
   for (i = 0; i < nmsgs; i++)
   {

      union shared_invalidation_message* msg = &msgs[i];

      if (msg->id >= 0)
      {
         buf = pgmoneta_format_and_append(buf, " catcache %d", msg->id);
      }
      else if (msg->id == SHAREDINVALCATALOG_ID)
      {
         pgmoneta_format_and_append(buf, " catalog %u", msg->cat.cat_id);
      }
      else if (msg->id == SHAREDINVALRELCACHE_ID)
      {
         buf = pgmoneta_format_and_append(buf, " relcache %u", msg->rc.rel_id);
      }
      /* not expected, but print something anyway */
      else if (msg->id == SHAREDINVALSMGR_ID)
      {
         buf = pgmoneta_format_and_append(buf, " smgr");
      }
      /* not expected, but print something anyway */
      else if (msg->id == SHAREDINVALRELMAP_ID)
      {
         buf = pgmoneta_format_and_append(buf, " relmap db %u", msg->rm.db_id);
      }
      else if (msg->id == SHAREDINVALSNAPSHOT_ID)
      {
         buf = pgmoneta_format_and_append(buf, " snapshot %u", msg->sn.rel_id);
      }
      else
      {
         buf = pgmoneta_format_and_append(buf, " unrecognized id %d", msg->id);
      }
   }
   return buf;
}

char*
pgmoneta_wal_standby_desc(char* buf, struct decoded_xlog_record* record)
{
   char* rec = record->main_data;
   uint8_t info = record->header.xl_info & ~XLR_INFO_MASK;

   if (info == XLOG_STANDBY_LOCK)
   {
      struct xl_standby_locks* xlrec = (struct xl_standby_locks*) rec;
      int i;

      for (i = 0; i < xlrec->nlocks; i++)
      {
         buf = pgmoneta_format_and_append(buf, "xid %u db %u rel %u ",
                                          xlrec->locks[i].xid, xlrec->locks[i].db_oid,
                                          xlrec->locks[i].rel_oid);
      }
   }
   else if (info == XLOG_RUNNING_XACTS)
   {
      struct xl_running_xacts* xlrec = (struct xl_running_xacts*) rec;

      buf = standby_desc_running_xacts(buf, xlrec);
   }
   else if (info == XLOG_INVALIDATIONS)
   {
      struct xl_invalidations* xlrec = (struct xl_invalidations*) rec;
      buf = pgmoneta_wal_standby_desc_invalidations(buf, xlrec->nmsgs, xlrec->msgs,
                                                    xlrec->dbId, xlrec->tsId,
                                                    xlrec->relcacheInitFileInval);

   }
   return buf;
}
