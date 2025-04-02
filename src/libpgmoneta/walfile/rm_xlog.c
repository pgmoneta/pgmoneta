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

#include <logging.h>
#include <pgmoneta.h>
#include <utils.h>
#include <wal.h>
#include <walfile/pg_control.h>
#include <walfile/rm.h>
#include <walfile/rm_xlog.h>
#include <walfile/wal_reader.h>
#include <walfile/transaction.h>

struct config_enum_entry wal_level_options[] = {
   {"minimal", WAL_LEVEL_MINIMAL, false},
   {"replica", WAL_LEVEL_REPLICA, false},
   {"archive", WAL_LEVEL_REPLICA, true},        /* deprecated */
   {"hot_standby", WAL_LEVEL_REPLICA, true},       /* deprecated */
   {"logical", WAL_LEVEL_LOGICAL, false},
   {NULL, 0, false}
};

static char*
get_wal_level_string(int wal_level)
{
   struct config_enum_entry* entry;
   char* wal_level_str = "?";

   for (entry = wal_level_options; entry->name; entry++)
   {
      if (entry->val == wal_level)
      {
         wal_level_str = entry->name;
         break;
      }
   }

   return wal_level_str;
};

struct xl_end_of_recovery*
create_xl_end_of_recovery(void)
{
   struct xl_end_of_recovery* wrapper = (struct xl_end_of_recovery*)malloc(sizeof(struct xl_end_of_recovery));

   if (server_config->version >= 17)
   {
      wrapper->parse = xl_end_of_recovery_parse_v17;
      wrapper->format = xl_end_of_recovery_format_v17;
   }
   else
   {
      wrapper->parse = xl_end_of_recovery_parse_v16;
      wrapper->format = xl_end_of_recovery_format_v16;
   }
   return wrapper;
}

struct check_point*
create_check_point(void)
{
   struct check_point* wrapper = (struct check_point*)malloc(sizeof(struct check_point));

   if (server_config->version >= 17)
   {
      wrapper->parse = check_point_parse_v17;
      wrapper->format = check_point_format_v17;
   }
   else
   {
      wrapper->parse = check_point_parse_v16;
      wrapper->format = check_point_format_v16;
   }
   return wrapper;
}
char*
check_point_format_v16(struct check_point* wrapper, char* buf)
{
   struct check_point_v16 checkpoint = wrapper->data.v16;

   char* oldest_xid_db = NULL;
   char* oldest_multi_db = NULL;

   if (pgmoneta_get_database_name(checkpoint.oldest_xid_db, &oldest_xid_db))
   {
      goto error;
   }

   if (pgmoneta_get_database_name(checkpoint.oldest_multi_db, &oldest_multi_db))
   {
      goto error;
   }

   buf = pgmoneta_format_and_append(buf, "redo %X/%X; "
                                    "tli %u; prev tli %u; fpw %s; xid %u:%u; oid %u; multi %u; offset %u; "
                                    "oldest xid %u in DB %s; oldest multi %u in DB %s; "
                                    "oldest/newest commit timestamp xid: %u/%u; "
                                    "oldest running xid %u;",
                                    LSN_FORMAT_ARGS(checkpoint.redo),
                                    checkpoint.this_timeline_id,
                                    checkpoint.prev_timeline_id,
                                    checkpoint.full_page_writes ? "true" : "false",
                                    EPOCH_FROM_FULL_TRANSACTION_ID(checkpoint.next_xid),
                                    XID_FROM_FULL_TRANSACTION_ID(checkpoint.next_xid),
                                    checkpoint.next_oid,
                                    checkpoint.next_multi,
                                    checkpoint.next_multi_offset,
                                    checkpoint.oldest_xid,
                                    oldest_xid_db,
                                    checkpoint.oldest_multi,
                                    oldest_multi_db,
                                    checkpoint.oldest_commit_ts_xid,
                                    checkpoint.newest_commit_ts_xid,
                                    checkpoint.oldest_active_xid);

   free(oldest_xid_db);
   free(oldest_multi_db);

   return buf;

error:
   free(oldest_xid_db);
   free(oldest_multi_db);

   return NULL;

}

char*
check_point_format_v17(struct check_point* wrapper, char* buf)
{
   struct check_point_v17 checkpoint = wrapper->data.v17;

   char* oldest_xid_db = NULL;
   char* oldest_multi_db = NULL;

   if (pgmoneta_get_database_name(checkpoint.oldest_xid_db, &oldest_xid_db))
   {
      goto error;
   }

   if (pgmoneta_get_database_name(checkpoint.oldest_multi_db, &oldest_multi_db))
   {
      goto error;
   }

   buf = pgmoneta_format_and_append(buf, "redo %X/%X; "
                                    "tli %u; prev tli %u; fpw %s; wal_level %s; xid %u:%u; oid %u; multi %u; offset %u; "
                                    "oldest xid %u in DB %s; oldest multi %u in DB %s; "
                                    "oldest/newest commit timestamp xid: %u/%u; "
                                    "oldest running xid %u",
                                    LSN_FORMAT_ARGS(checkpoint.redo),
                                    checkpoint.this_timeline_id,
                                    checkpoint.prev_timeline_id,
                                    checkpoint.full_page_writes ? "true" : "false",
                                    get_wal_level_string(checkpoint.wal_level),
                                    EPOCH_FROM_FULL_TRANSACTION_ID(checkpoint.next_xid),
                                    XID_FROM_FULL_TRANSACTION_ID(checkpoint.next_xid),
                                    checkpoint.next_oid,
                                    checkpoint.next_multi,
                                    checkpoint.next_multi_offset,
                                    checkpoint.oldest_xid,
                                    oldest_xid_db,
                                    checkpoint.oldest_multi,
                                    oldest_multi_db,
                                    checkpoint.oldest_commit_ts_xid,
                                    checkpoint.newest_commit_ts_xid,
                                    checkpoint.oldest_active_xid);

   free(oldest_xid_db);
   free(oldest_multi_db);

   return buf;

error:
   free(oldest_xid_db);
   free(oldest_multi_db);
   return NULL;
}

void
check_point_parse_v16(struct check_point* wrapper, void* rec)
{
   char* ptr = (char*)rec;
   memcpy(&wrapper->data.v16.redo, ptr, sizeof(xlog_rec_ptr));
   ptr += sizeof(xlog_rec_ptr);
   memcpy(&wrapper->data.v16.this_timeline_id, ptr, sizeof(uint32_t));
   ptr += sizeof(uint32_t);
   memcpy(&wrapper->data.v16.prev_timeline_id, ptr, sizeof(uint32_t));
   ptr += sizeof(uint32_t);
   memcpy(&wrapper->data.v16.full_page_writes, ptr, sizeof(bool));
   ptr += sizeof(bool);
   memcpy(&wrapper->data.v16.next_xid, ptr, sizeof(struct full_transaction_id));
   ptr += sizeof(struct full_transaction_id);
   memcpy(&wrapper->data.v16.next_oid, ptr, sizeof(oid));
   ptr += sizeof(oid);
   memcpy(&wrapper->data.v16.next_multi, ptr, sizeof(multi_xact_id));
   ptr += sizeof(multi_xact_id);
   memcpy(&wrapper->data.v16.next_multi_offset, ptr, sizeof(multi_xact_offset));
   ptr += sizeof(multi_xact_offset);
   memcpy(&wrapper->data.v16.oldest_xid, ptr, sizeof(transaction_id));
   ptr += sizeof(transaction_id);
   memcpy(&wrapper->data.v16.oldest_xid_db, ptr, sizeof(oid));
   ptr += sizeof(oid);
   memcpy(&wrapper->data.v16.oldest_multi, ptr, sizeof(multi_xact_id));
   ptr += sizeof(multi_xact_id);
   memcpy(&wrapper->data.v16.oldest_multi_db, ptr, sizeof(oid));
   ptr += sizeof(oid);
   memcpy(&wrapper->data.v16.oldest_commit_ts_xid, ptr, sizeof(transaction_id));
   ptr += sizeof(transaction_id);
   memcpy(&wrapper->data.v16.newest_commit_ts_xid, ptr, sizeof(transaction_id));
   ptr += sizeof(transaction_id);
   memcpy(&wrapper->data.v16.oldest_active_xid, ptr, sizeof(transaction_id));
}

void
check_point_parse_v17(struct check_point* wrapper, void* rec)
{
   wrapper->data.v17 = *((struct check_point_v17*)rec);
}

void
xl_end_of_recovery_parse_v17(struct xl_end_of_recovery* wrapper, void* rec)
{
   char* ptr = (char*)rec;
   memcpy(&wrapper->data.v17.this_timeline_id, ptr, sizeof(uint32_t));
   ptr += sizeof(uint32_t);
   memcpy(&wrapper->data.v17.prev_timeline_id, ptr, sizeof(uint32_t));
   ptr += sizeof(uint32_t);
   memcpy(&wrapper->data.v17.end_time, ptr, sizeof(timestamp_tz));
   ptr += sizeof(timestamp_tz);
   memcpy(&wrapper->data.v17.wal_level, ptr, sizeof(int));
}

void
xl_end_of_recovery_parse_v16(struct xl_end_of_recovery* wrapper, void* rec)
{
   char* ptr = (char*)rec;
   memcpy(&wrapper->data.v16.this_timeline_id, ptr, sizeof(uint32_t));
   ptr += sizeof(uint32_t);
   memcpy(&wrapper->data.v16.prev_timeline_id, ptr, sizeof(uint32_t));
   ptr += sizeof(uint32_t);
   memcpy(&wrapper->data.v16.end_time, ptr, sizeof(timestamp_tz));
}

char*
xl_end_of_recovery_format_v17(struct xl_end_of_recovery* wrapper, char* buf)
{
   struct xl_end_of_recovery_v17 xlrec = wrapper->data.v17;
   buf = pgmoneta_format_and_append(buf, "tli %u; prev tli %u; time %s; wal_level %s",
                                    xlrec.this_timeline_id, xlrec.prev_timeline_id,
                                    pgmoneta_wal_timestamptz_to_str(xlrec.end_time),
                                    get_wal_level_string(xlrec.wal_level));
   return buf;
}

char*
xl_end_of_recovery_format_v16(struct xl_end_of_recovery* wrapper, char* buf)
{
   struct xl_end_of_recovery_v16 xlrec = wrapper->data.v16;
   buf = pgmoneta_format_and_append(buf, "tli %u; prev tli %u; time %s",
                                    xlrec.this_timeline_id, xlrec.prev_timeline_id,
                                    pgmoneta_wal_timestamptz_to_str(xlrec.end_time));
   return buf;
}

char*
pgmoneta_wal_xlog_desc(char* buf, struct decoded_xlog_record* record)
{
   char* rec = record->main_data;
   uint8_t info = record->header.xl_info & ~XLR_INFO_MASK;
   buf = pgmoneta_append(buf, "");

   if (info == XLOG_CHECKPOINT_SHUTDOWN ||
       info == XLOG_CHECKPOINT_ONLINE)
   {
      struct check_point* checkpoint = create_check_point();
      checkpoint->parse(checkpoint, rec);
      buf = checkpoint->format(checkpoint, buf);
      buf = pgmoneta_format_and_append(buf, " %s", (info == XLOG_CHECKPOINT_SHUTDOWN) ? "shutdown" : "online");
      free(checkpoint);
   }
   else if (info == XLOG_NEXTOID)
   {
      oid nextOid;

      memcpy(&nextOid, rec, sizeof(oid));
      buf = pgmoneta_format_and_append(buf, "%u", nextOid);
   }
   else if (info == XLOG_RESTORE_POINT)
   {
      struct xl_restore_point* xlrec = (struct xl_restore_point*) rec;

      buf = pgmoneta_format_and_append(buf, xlrec->rp_name);
   }
   else if (info == XLOG_FPI || info == XLOG_FPI_FOR_HINT)
   {
      /* no further information to print */
   }
   else if (info == XLOG_BACKUP_END)
   {
      xlog_rec_ptr startpoint;

      memcpy(&startpoint, rec, sizeof(xlog_rec_ptr));
      buf = pgmoneta_format_and_append(buf, "%X/%X", LSN_FORMAT_ARGS(startpoint));
   }
   else if (info == XLOG_PARAMETER_CHANGE)
   {
      struct xl_parameter_change xlrec;
      char* wal_level_str;
      struct config_enum_entry* entry;

      memcpy(&xlrec, rec, sizeof(struct xl_parameter_change));

      /* Find a string representation for wal_level */
      wal_level_str = "?";
      for (entry = wal_level_options; entry->name; entry++)
      {
         if (entry->val == xlrec.wal_level)
         {
            wal_level_str = entry->name;
            break;
         }
      }

      buf = pgmoneta_format_and_append(buf, "max_connections=%d max_worker_processes=%d "
                                       "max_wal_senders=%d max_prepared_xacts=%d "
                                       "max_locks_per_xact=%d wal_level=%s "
                                       "wal_log_hints=%s track_commit_timestamp=%s",
                                       xlrec.max_connections,
                                       xlrec.max_worker_processes,
                                       xlrec.max_wal_senders,
                                       xlrec.max_prepared_xacts,
                                       xlrec.max_locks_per_xact,
                                       wal_level_str,
                                       xlrec.wal_log_hints ? "on" : "off",
                                       xlrec.track_commit_timestamp ? "on" : "off");
   }
   else if (info == XLOG_FPW_CHANGE)
   {
      bool fpw;

      memcpy(&fpw, rec, sizeof(bool));
      buf = pgmoneta_format_and_append(buf, fpw ? "true" : "false");
   }
   else if (info == XLOG_END_OF_RECOVERY)
   {
      struct xl_end_of_recovery* xlrec = create_xl_end_of_recovery();
      xlrec->parse(xlrec, rec);
      xlrec->format(xlrec, buf);
      free(xlrec);
   }
   else if (info == XLOG_OVERWRITE_CONTRECORD)
   {
      struct xl_overwrite_contrecord xlrec;

      memcpy(&xlrec, rec, sizeof(struct xl_overwrite_contrecord));
      buf = pgmoneta_format_and_append(buf, "lsn %X/%X; time %s",
                                       LSN_FORMAT_ARGS(xlrec.overwritten_lsn),
                                       pgmoneta_wal_timestamptz_to_str(xlrec.overwrite_time));
   }
   return buf;
}

pg_time_t
timestamptz_to_time_t(timestamp_tz t)
{
   pg_time_t result;

   result = (pg_time_t) (t / USECS_PER_SEC +
                         ((POSTGRES_EPOCH_JDATE - UNIX_EPOCH_JDATE) * SECS_PER_DAY));
   return result;
}

char*
pgmoneta_wal_timestamptz_to_str(timestamp_tz dt)
{
   static char buf[MAXDATELEN + 1];
   char ts[MAXDATELEN + 1];
   char zone[MAXDATELEN + 1];
   time_t result = (time_t) timestamptz_to_time_t(dt);
   struct tm* ltime = localtime(&result);

   strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", ltime);
   strftime(zone, sizeof(zone), "%Z", ltime);

   int written = snprintf(buf, sizeof(buf), "%s.%06d %s",
                          ts, (int) (dt % USECS_PER_SEC), zone);

   if (written < 0 || written >= sizeof(buf))
   {
      // Handle the truncation or error
      pgmoneta_log_fatal("Buffer overflow or encoding error in timestamptz_to_str\n");
      buf[0] = '\0';   // Ensure buffer is null-terminated
   }

   return buf;
}
