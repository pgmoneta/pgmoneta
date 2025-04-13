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

#include <walfile/relpath.h>
#include <walfile/rm_standby.h>
#include <walfile/rm_xact.h>
#include <utils.h>

#include <string.h>

typedef struct xl_xact_xinfo xl_xact_xinfo;

static char*xact_desc_relations(char* buf, char* label, int nrels, struct rel_file_node* xnodes);

static char*xact_desc_commit_v14(char* buf, uint8_t info, struct xl_xact_commit* xlrec, rep_origin_id origin_id);
static char*xact_desc_abort_v14(char* buf, uint8_t info, struct xl_xact_abort* xlrec);
static char*xact_desc_prepare_v14(char* buf, uint8_t info, struct xl_xact_prepare_v14* xlrec);

static char*xact_desc_commit_v15(char* buf, uint8_t info, struct xl_xact_commit* xlrec, rep_origin_id origin_id);
static char*xact_desc_abort_v15(char* buf, uint8_t info, struct xl_xact_abort* xlrec);
static char*xact_desc_prepare_v15(char* buf, uint8_t info, struct xl_xact_prepare_v15* xlrec);

static char*xact_desc_assignment(char* buf, struct xl_xact_assignment* xlrec);
static char*xact_desc_subxacts(char* buf, int nsubxacts, transaction_id* subxacts);

void parse_abort_record_v14(uint8_t info, struct xl_xact_abort* xlrec, struct xl_xact_parsed_abort_v14* parsed);
void parse_abort_record_v15(uint8_t info, struct xl_xact_abort* xlrec, struct xl_xact_parsed_abort_v15* parsed);

void parse_prepare_record_v14(uint8_t info, struct xl_xact_prepare_v14* xlrec, xl_xact_parsed_prepare_v14* parsed);
void parse_prepare_record_v15(uint8_t info, struct xl_xact_prepare_v15* xlrec, xl_xact_parsed_prepare_v15* parsed);

void parse_commit_record_v14(uint8_t info, struct xl_xact_commit* xlrec, struct xl_xact_parsed_commit_v14* parsed);
void parse_commit_record_v15(uint8_t info, struct xl_xact_commit* xlrec, struct xl_xact_parsed_commit_v15* parsed);

/**
 * Parses a version 14 xl_xact_prepare record.
 */
void
pgmoneta_wal_parse_xl_xact_prepare_v14(struct xl_xact_prepare* wrapper, void* rec)
{
   char* ptr = (char*)rec;
   memcpy(&wrapper->data.v14.magic, ptr, sizeof(uint32_t));
   ptr += sizeof(uint32_t);
   memcpy(&wrapper->data.v14.total_len, ptr, sizeof(uint32_t));
   ptr += sizeof(uint32_t);
   memcpy(&wrapper->data.v14.xid, ptr, sizeof(transaction_id));
   ptr += sizeof(transaction_id);
   memcpy(&wrapper->data.v14.database, ptr, sizeof(oid));
   ptr += sizeof(oid);
   memcpy(&wrapper->data.v14.prepared_at, ptr, sizeof(timestamp_tz));
   ptr += sizeof(timestamp_tz);
   memcpy(&wrapper->data.v14.owner, ptr, sizeof(oid));
   ptr += sizeof(oid);
   memcpy(&wrapper->data.v14.nsubxacts, ptr, sizeof(int32_t));
   ptr += sizeof(int32_t);
   memcpy(&wrapper->data.v14.ncommitrels, ptr, sizeof(int32_t));
   ptr += sizeof(int32_t);
   memcpy(&wrapper->data.v14.nabortrels, ptr, sizeof(int32_t));
   ptr += sizeof(int32_t);
   memcpy(&wrapper->data.v14.ninvalmsgs, ptr, sizeof(int32_t));
   ptr += sizeof(int32_t);
   memcpy(&wrapper->data.v14.initfileinval, ptr, sizeof(bool));
   ptr += sizeof(bool);
   memcpy(&wrapper->data.v14.gidlen, ptr, sizeof(uint16_t));
   ptr += sizeof(uint16_t);
   memcpy(&wrapper->data.v14.origin_lsn, ptr, sizeof(xlog_rec_ptr));
   ptr += sizeof(xlog_rec_ptr);
   memcpy(&wrapper->data.v14.origin_timestamp, ptr, sizeof(timestamp_tz));
}

/**
 * Parses a version 15 xl_xact_prepare record.
 */
void
pgmoneta_wal_parse_xl_xact_prepare_v15(struct xl_xact_prepare* wrapper, void* rec)
{
   char* ptr = (char*)rec;
   memcpy(&wrapper->data.v15.magic, ptr, sizeof(uint32_t));
   ptr += sizeof(uint32_t);
   memcpy(&wrapper->data.v15.total_len, ptr, sizeof(uint32_t));
   ptr += sizeof(uint32_t);
   memcpy(&wrapper->data.v15.xid, ptr, sizeof(transaction_id));
   ptr += sizeof(transaction_id);
   memcpy(&wrapper->data.v15.database, ptr, sizeof(oid));
   ptr += sizeof(oid);
   memcpy(&wrapper->data.v15.prepared_at, ptr, sizeof(timestamp_tz));
   ptr += sizeof(timestamp_tz);
   memcpy(&wrapper->data.v15.owner, ptr, sizeof(oid));
   ptr += sizeof(oid);
   memcpy(&wrapper->data.v15.nsubxacts, ptr, sizeof(int32_t));
   ptr += sizeof(int32_t);
   memcpy(&wrapper->data.v15.ncommitrels, ptr, sizeof(int32_t));
   ptr += sizeof(int32_t);
   memcpy(&wrapper->data.v15.nabortrels, ptr, sizeof(int32_t));
   ptr += sizeof(int32_t);
   memcpy(&wrapper->data.v15.ncommitstats, ptr, sizeof(int32_t));
   ptr += sizeof(int32_t);
   memcpy(&wrapper->data.v15.nabortstats, ptr, sizeof(int32_t));
   ptr += sizeof(int32_t);
   memcpy(&wrapper->data.v15.ninvalmsgs, ptr, sizeof(int32_t));
   ptr += sizeof(int32_t);
   memcpy(&wrapper->data.v15.initfileinval, ptr, sizeof(bool));
   ptr += sizeof(bool);
   memcpy(&wrapper->data.v15.gidlen, ptr, sizeof(uint16_t));
   ptr += sizeof(uint16_t);
   memcpy(&wrapper->data.v15.origin_lsn, ptr, sizeof(xlog_rec_ptr));
   ptr += sizeof(xlog_rec_ptr);
   memcpy(&wrapper->data.v15.origin_timestamp, ptr, sizeof(timestamp_tz));
}

/**
 * Formats a version 14 xl_xact_prepare record.
 */
char*
pgmoneta_wal_format_xl_xact_prepare_v14(struct xl_xact_prepare* wrapper __attribute__((unused)), char* rec __attribute__((unused)), char* buf)
{
   return buf;
}

/**
 * Formats a version 15 xl_xact_prepare record.
 */
char*
pgmoneta_wal_format_xl_xact_prepare_v15(struct xl_xact_prepare* wrapper __attribute__((unused)), char* rec __attribute__((unused)), char* buf)
{
   return buf;
}

/**
 * Creates an xl_xact_prepare wrapper.
 */
struct xl_xact_prepare*
pgmoneta_wal_create_xl_xact_prepare(void)
{
   struct xl_xact_prepare* wrapper = malloc(sizeof(struct xl_xact_prepare));

   if (server_config->version >= 15)
   {
      wrapper->parse = pgmoneta_wal_parse_xl_xact_prepare_v15;
      wrapper->format = pgmoneta_wal_format_xl_xact_prepare_v15;
   }
   else
   {
      wrapper->parse = pgmoneta_wal_parse_xl_xact_prepare_v14;
      wrapper->format = pgmoneta_wal_format_xl_xact_prepare_v14;
   }

   return wrapper;
}

/* Create parsed commit wrapper structure */
struct xl_xact_parsed_commit*
pgmoneta_wal_create_xact_parsed_commit(void)
{
   struct xl_xact_parsed_commit* wrapper = malloc(sizeof(struct xl_xact_parsed_commit));

   if (server_config->version >= 15)
   {
      wrapper->parse = pgmoneta_wal_parse_xact_commit_v15;
      wrapper->format = pgmoneta_wal_format_xact_commit_v15;
   }
   else
   {
      wrapper->parse = pgmoneta_wal_parse_xact_commit_v14;
      wrapper->format = pgmoneta_wal_format_xact_commit_v14;
   }

   return wrapper;
}

/* Parse commit record for version 14 */
void
pgmoneta_wal_parse_xact_commit_v14(struct xl_xact_parsed_commit* wrapper, void* rec)
{
   char* ptr = (char*)rec;
   memcpy(&wrapper->data.v14.xact_time, ptr, sizeof(timestamp_tz));
   ptr += sizeof(timestamp_tz);
   memcpy(&wrapper->data.v14.xinfo, ptr, sizeof(uint32_t));
   ptr += sizeof(uint32_t);
   memcpy(&wrapper->data.v14.db_id, ptr, sizeof(oid));
   ptr += sizeof(oid);
   memcpy(&wrapper->data.v14.ts_id, ptr, sizeof(oid));
   ptr += sizeof(oid);
   memcpy(&wrapper->data.v14.nsubxacts, ptr, sizeof(int));
   ptr += sizeof(int);
   memcpy(&wrapper->data.v14.subxacts, ptr, sizeof(transaction_id*));
   ptr += sizeof(transaction_id*);
   memcpy(&wrapper->data.v14.nrels, ptr, sizeof(int));
   ptr += sizeof(int);
   memcpy(&wrapper->data.v14.xnodes, ptr, sizeof(struct rel_file_node*));
   ptr += sizeof(struct rel_file_node*);
   memcpy(&wrapper->data.v14.nmsgs, ptr, sizeof(int));
   ptr += sizeof(int);
   memcpy(&wrapper->data.v14.msgs, ptr, sizeof(union shared_invalidation_message*));
   ptr += sizeof(union shared_invalidation_message*);
   memcpy(&wrapper->data.v14.twophase_xid, ptr, sizeof(transaction_id));
   ptr += sizeof(transaction_id);
   memcpy(&wrapper->data.v14.twophase_gid, ptr, GIDSIZE);
   ptr += GIDSIZE;
   memcpy(&wrapper->data.v14.nabortrels, ptr, sizeof(int));
   ptr += sizeof(int);
   memcpy(&wrapper->data.v14.abortnodes, ptr, sizeof(struct rel_file_node*));
   ptr += sizeof(struct rel_file_node*);
   memcpy(&wrapper->data.v14.origin_lsn, ptr, sizeof(xlog_rec_ptr));
   ptr += sizeof(xlog_rec_ptr);
   memcpy(&wrapper->data.v14.origin_timestamp, ptr, sizeof(timestamp_tz));
}

/* Parse commit record for version 15 */
void
pgmoneta_wal_parse_xact_commit_v15(struct xl_xact_parsed_commit* wrapper, void* rec)
{
   char* ptr = (char*)rec;
   memcpy(&wrapper->data.v15.xact_time, ptr, sizeof(timestamp_tz));
   ptr += sizeof(timestamp_tz);
   memcpy(&wrapper->data.v15.xinfo, ptr, sizeof(uint32_t));
   ptr += sizeof(uint32_t);
   memcpy(&wrapper->data.v15.db_id, ptr, sizeof(oid));
   ptr += sizeof(oid);
   memcpy(&wrapper->data.v15.ts_id, ptr, sizeof(oid));
   ptr += sizeof(oid);
   memcpy(&wrapper->data.v15.nsubxacts, ptr, sizeof(int));
   ptr += sizeof(int);
   memcpy(&wrapper->data.v15.subxacts, ptr, sizeof(transaction_id*));
   ptr += sizeof(transaction_id*);
   memcpy(&wrapper->data.v15.nrels, ptr, sizeof(int));
   ptr += sizeof(int);
   memcpy(&wrapper->data.v15.xnodes, ptr, sizeof(struct rel_file_node*));
   ptr += sizeof(struct rel_file_node*);
   memcpy(&wrapper->data.v15.nstats, ptr, sizeof(int));
   ptr += sizeof(int);
   memcpy(&wrapper->data.v15.stats, ptr, sizeof(struct xl_xact_stats_item*));
   ptr += sizeof(struct xl_xact_stats_item*);
   memcpy(&wrapper->data.v15.nmsgs, ptr, sizeof(int));
   ptr += sizeof(int);
   memcpy(&wrapper->data.v15.msgs, ptr, sizeof(union shared_invalidation_message*));
   ptr += sizeof(union shared_invalidation_message*);
   memcpy(&wrapper->data.v15.twophase_xid, ptr, sizeof(transaction_id));
   ptr += sizeof(transaction_id);
   memcpy(&wrapper->data.v15.twophase_gid, ptr, GIDSIZE);
   ptr += GIDSIZE;
   memcpy(&wrapper->data.v15.nabortrels, ptr, sizeof(int));
   ptr += sizeof(int);
   memcpy(&wrapper->data.v15.abortnodes, ptr, sizeof(struct rel_file_node*));
   ptr += sizeof(struct rel_file_node*);
   memcpy(&wrapper->data.v15.nabortstats, ptr, sizeof(int));
   ptr += sizeof(int);
   memcpy(&wrapper->data.v15.abortstats, ptr, sizeof(struct xl_xact_stats_item*));
   ptr += sizeof(struct xl_xact_stats_item*);
   memcpy(&wrapper->data.v15.origin_lsn, ptr, sizeof(xlog_rec_ptr));
   ptr += sizeof(xlog_rec_ptr);
   memcpy(&wrapper->data.v15.origin_timestamp, ptr, sizeof(timestamp_tz));
}

/* Format commit record for version 14 */
char*
pgmoneta_wal_format_xact_commit_v14(struct xl_xact_parsed_commit* wrapper __attribute__((unused)), char* rec __attribute__((unused)), char* buf)
{
   return buf;
}

/* Format commit record for version 15 */
char*
pgmoneta_wal_format_xact_commit_v15(struct xl_xact_parsed_commit* wrapper __attribute__((unused)), char* rec __attribute__((unused)), char* buf)
{
   return buf;
}

struct xl_xact_parsed_abort*
pgmoneta_wal_create_xl_xact_parsed_abort(void)
{
   struct xl_xact_parsed_abort* wrapper = malloc(sizeof(struct xl_xact_parsed_abort));

   if (server_config->version >= 15)
   {
      wrapper->parse = pgmoneta_wal_parse_xl_xact_parsed_abort_v15;
      wrapper->format = pgmoneta_wal_format_xl_xact_parsed_abort_v15;
   }
   else
   {
      wrapper->parse = pgmoneta_wal_parse_xl_xact_parsed_abort_v14;
      wrapper->format = pgmoneta_wal_format_xl_xact_parsed_abort_v14;
   }

   return wrapper;
}

void
pgmoneta_wal_parse_xl_xact_parsed_abort_v14(struct xl_xact_parsed_abort* wrapper, void* rec)
{
   char* ptr = (char*)rec;
   memcpy(&wrapper->data.v14.xact_time, ptr, sizeof(timestamp_tz));
   ptr += sizeof(timestamp_tz);
   memcpy(&wrapper->data.v14.xinfo, ptr, sizeof(uint32_t));
   ptr += sizeof(uint32_t);
   memcpy(&wrapper->data.v14.dbId, ptr, sizeof(oid));
   ptr += sizeof(oid);
   memcpy(&wrapper->data.v14.tsId, ptr, sizeof(oid));
   ptr += sizeof(oid);
   memcpy(&wrapper->data.v14.nsubxacts, ptr, sizeof(int));
   ptr += sizeof(int);
   memcpy(&wrapper->data.v14.subxacts, ptr, sizeof(transaction_id*));
   ptr += sizeof(transaction_id*);
   memcpy(&wrapper->data.v14.nrels, ptr, sizeof(int));
   ptr += sizeof(int);
   memcpy(&wrapper->data.v14.xnodes, ptr, sizeof(struct rel_file_node*));
   ptr += sizeof(struct rel_file_node*);
   memcpy(&wrapper->data.v14.twophase_xid, ptr, sizeof(transaction_id));
   ptr += sizeof(transaction_id);
   memcpy(&wrapper->data.v14.twophase_gid, ptr, GIDSIZE);
   ptr += GIDSIZE;
   memcpy(&wrapper->data.v14.origin_lsn, ptr, sizeof(xlog_rec_ptr));
   ptr += sizeof(xlog_rec_ptr);
   memcpy(&wrapper->data.v14.origin_timestamp, ptr, sizeof(timestamp_tz));
}

void
pgmoneta_wal_parse_xl_xact_parsed_abort_v15(struct xl_xact_parsed_abort* wrapper, void* rec)
{
   char* ptr = (char*)rec;
   memcpy(&wrapper->data.v15.xact_time, ptr, sizeof(timestamp_tz));
   ptr += sizeof(timestamp_tz);
   memcpy(&wrapper->data.v15.xinfo, ptr, sizeof(uint32_t));
   ptr += sizeof(uint32_t);
   memcpy(&wrapper->data.v15.db_id, ptr, sizeof(oid));
   ptr += sizeof(oid);
   memcpy(&wrapper->data.v15.ts_id, ptr, sizeof(oid));
   ptr += sizeof(oid);
   memcpy(&wrapper->data.v15.nsubxacts, ptr, sizeof(int));
   ptr += sizeof(int);
   memcpy(&wrapper->data.v15.subxacts, ptr, sizeof(transaction_id*));
   ptr += sizeof(transaction_id*);
   memcpy(&wrapper->data.v15.nrels, ptr, sizeof(int));
   ptr += sizeof(int);
   memcpy(&wrapper->data.v15.xnodes, ptr, sizeof(struct rel_file_node*));
   ptr += sizeof(struct rel_file_node*);
   memcpy(&wrapper->data.v15.nstats, ptr, sizeof(int));
   ptr += sizeof(int);
   memcpy(&wrapper->data.v15.stats, ptr, sizeof(struct xl_xact_stats_item*));
   ptr += sizeof(struct xl_xact_stats_item*);
   memcpy(&wrapper->data.v15.twophase_xid, ptr, sizeof(transaction_id));
   ptr += sizeof(transaction_id);
   memcpy(&wrapper->data.v15.twophase_gid, ptr, GIDSIZE);
   ptr += GIDSIZE;
   memcpy(&wrapper->data.v15.origin_lsn, ptr, sizeof(xlog_rec_ptr));
   ptr += sizeof(xlog_rec_ptr);
   memcpy(&wrapper->data.v15.origin_timestamp, ptr, sizeof(timestamp_tz));
}

char*
pgmoneta_wal_format_xl_xact_parsed_abort_v14(struct xl_xact_parsed_abort* wrapper __attribute__((unused)), char* rec __attribute__((unused)), char* buf)
{
   return buf;
}

char*
pgmoneta_wal_format_xl_xact_parsed_abort_v15(struct xl_xact_parsed_abort* wrapper __attribute__((unused)), char* rec __attribute__((unused)), char* buf)
{
   return buf;
}

static char*
xact_desc_relations(char* buf, char* label, int nrels,
                    struct rel_file_node* xnodes)
{
   int i;

   if (nrels > 0)
   {
      buf = pgmoneta_format_and_append(buf, "; %s:", label);
      for (i = 0; i < nrels; i++)
      {
         char* path = RELPATHPERM(xnodes[i], MAIN_FORKNUM);

         buf = pgmoneta_format_and_append(buf, " %s", path);
         free(path);
      }
   }
   return buf;
}

static char*
xact_desc_commit_v14(char* buf, uint8_t info, struct xl_xact_commit* xlrec, rep_origin_id origin_id)
{
   struct xl_xact_parsed_commit_v14 parsed;
   parse_commit_record_v14(info, xlrec, &parsed);

   /* If this is a prepared xact, show the xid of the original xact */
   if (TRANSACTION_ID_IS_VALID(parsed.twophase_xid))
   {
      buf = pgmoneta_format_and_append(buf, "%u: ", parsed.twophase_xid);
   }

   buf = pgmoneta_format_and_append(buf, pgmoneta_wal_timestamptz_to_str(xlrec->xact_time));

   buf = xact_desc_relations(buf, "rels", parsed.nrels, parsed.xnodes);
   buf = xact_desc_subxacts(buf, parsed.nsubxacts, parsed.subxacts);

   buf = pgmoneta_wal_standby_desc_invalidations(buf, parsed.nmsgs, parsed.msgs, parsed.db_id,
                                                 parsed.ts_id,
                                                 XACT_COMPLETION_RELCACHE_INIT_FILE_INVAL(parsed.xinfo));

   if (XACT_COMPLETION_FORCE_SYNC_COMMIT(parsed.xinfo))
   {
      buf = pgmoneta_format_and_append(buf, "; sync");
   }

   if (parsed.xinfo & XACT_XINFO_HAS_ORIGIN)
   {
      buf = pgmoneta_format_and_append(buf, "; origin: node %u, lsn %X/%X, at %s",
                                       origin_id,
                                       LSN_FORMAT_ARGS(parsed.origin_lsn),
                                       pgmoneta_wal_timestamptz_to_str(parsed.origin_timestamp));
   }
   return buf;
}

static char*
xact_desc_commit_v15(char* buf, uint8_t info, struct xl_xact_commit* xlrec, rep_origin_id origin_id)
{
   struct xl_xact_parsed_commit_v15 parsed;
   parse_commit_record_v15(info, xlrec, &parsed);

   /* If this is a prepared xact, show the xid of the original xact */
   if (TRANSACTION_ID_IS_VALID(parsed.twophase_xid))
   {
      buf = pgmoneta_format_and_append(buf, "%u: ", parsed.twophase_xid);
   }

   buf = pgmoneta_format_and_append(buf, pgmoneta_wal_timestamptz_to_str(xlrec->xact_time));

   buf = xact_desc_relations(buf, "rels", parsed.nrels, parsed.xnodes);
   buf = xact_desc_subxacts(buf, parsed.nsubxacts, parsed.subxacts);

   buf = pgmoneta_wal_standby_desc_invalidations(buf, parsed.nmsgs, parsed.msgs, parsed.db_id,
                                                 parsed.ts_id,
                                                 XACT_COMPLETION_RELCACHE_INIT_FILE_INVAL(parsed.xinfo));

   if (XACT_COMPLETION_FORCE_SYNC_COMMIT(parsed.xinfo))
   {
      buf = pgmoneta_format_and_append(buf, "; sync");
   }

   if (parsed.xinfo & XACT_XINFO_HAS_ORIGIN)
   {
      buf = pgmoneta_format_and_append(buf, "; origin: node %u, lsn %X/%X, at %s",
                                       origin_id,
                                       LSN_FORMAT_ARGS(parsed.origin_lsn),
                                       pgmoneta_wal_timestamptz_to_str(parsed.origin_timestamp));
   }
   return buf;
}

static char*
xact_desc_abort_v14(char* buf, uint8_t info, struct xl_xact_abort* xlrec)
{
   struct xl_xact_parsed_abort_v14 parsed;

   parse_abort_record_v14(info, xlrec, &parsed);

   /* If this is a prepared xact, show the xid of the original xact */
   if (TRANSACTION_ID_IS_VALID(parsed.twophase_xid))
   {
      buf = pgmoneta_format_and_append(buf, "%u: ", parsed.twophase_xid);
   }

   buf = pgmoneta_format_and_append(buf, pgmoneta_wal_timestamptz_to_str(xlrec->xact_time));

   buf = xact_desc_relations(buf, "rels", parsed.nrels, parsed.xnodes);
   buf = xact_desc_subxacts(buf, parsed.nsubxacts, parsed.subxacts);

   return buf;
}
static char*
xact_desc_abort_v15(char* buf, uint8_t info, struct xl_xact_abort* xlrec)
{
   struct xl_xact_parsed_abort_v15 parsed;

   parse_abort_record_v15(info, xlrec, &parsed);

   /* If this is a prepared xact, show the xid of the original xact */
   if (TRANSACTION_ID_IS_VALID(parsed.twophase_xid))
   {
      buf = pgmoneta_format_and_append(buf, "%u: ", parsed.twophase_xid);
   }

   buf = pgmoneta_format_and_append(buf, pgmoneta_wal_timestamptz_to_str(xlrec->xact_time));

   buf = xact_desc_relations(buf, "rels", parsed.nrels, parsed.xnodes);
   buf = xact_desc_subxacts(buf, parsed.nsubxacts, parsed.subxacts);

   return buf;
}

static char*
xact_desc_prepare_v14(char* buf, uint8_t info, struct xl_xact_prepare_v14* xlrec)
{
   xl_xact_parsed_prepare_v14 parsed;

   parse_prepare_record_v14(info, xlrec, &parsed);

   buf = pgmoneta_format_and_append(buf, "gid %s: ", parsed.twophase_gid);
   buf = pgmoneta_format_and_append(buf, pgmoneta_wal_timestamptz_to_str(parsed.xact_time));

   buf = xact_desc_relations(buf, "rels(commit)", parsed.nrels, parsed.xnodes);
   buf = xact_desc_relations(buf, "rels(abort)", parsed.nabortrels,
                             parsed.abortnodes);
   buf = xact_desc_subxacts(buf, parsed.nsubxacts, parsed.subxacts);

   buf = pgmoneta_wal_standby_desc_invalidations(buf, parsed.nmsgs, parsed.msgs, parsed.db_id,
                                                 parsed.ts_id, xlrec);
   return buf;
}
static char*
xact_desc_prepare_v15(char* buf, uint8_t info, struct xl_xact_prepare_v15* xlrec)
{
   xl_xact_parsed_prepare_v15 parsed;

   parse_prepare_record_v15(info, xlrec, &parsed);

   buf = pgmoneta_format_and_append(buf, "gid %s: ", parsed.twophase_gid);
   buf = pgmoneta_format_and_append(buf, pgmoneta_wal_timestamptz_to_str(parsed.xact_time));

   buf = xact_desc_relations(buf, "rels(commit)", parsed.nrels, parsed.xnodes);
   buf = xact_desc_relations(buf, "rels(abort)", parsed.nabortrels,
                             parsed.abortnodes);
   buf = xact_desc_subxacts(buf, parsed.nsubxacts, parsed.subxacts);

   buf = pgmoneta_wal_standby_desc_invalidations(buf, parsed.nmsgs, parsed.msgs, parsed.db_id,
                                                 parsed.ts_id, xlrec->initfileinval);
   return buf;
}

static char*
xact_desc_assignment(char* buf, struct xl_xact_assignment* xlrec)
{
   int i;

   buf = pgmoneta_format_and_append(buf, "subxacts:");

   for (i = 0; i < xlrec->nsubxacts; i++)
   {
      buf = pgmoneta_format_and_append(buf, " %u", xlrec->xsub[i]);
   }
   return buf;
}

static char*
xact_desc_subxacts(char* buf, int nsubxacts, transaction_id* subxacts)
{
   int i;

   if (nsubxacts > 0)
   {
      buf = pgmoneta_format_and_append(buf, "; subxacts:");
      for (i = 0; i < nsubxacts; i++)
      {
         buf = pgmoneta_format_and_append(buf, " %u", subxacts[i]);
      }
   }
   return buf;
}

char*
pgmoneta_wal_xact_desc(char* buf, struct decoded_xlog_record* record)
{
   char* rec = XLOG_REC_GET_DATA(record);
   uint8_t info = XLOG_REC_GET_INFO(record) & XLOG_XACT_OPMASK;

   if (info == XLOG_XACT_COMMIT || info == XLOG_XACT_COMMIT_PREPARED)
   {
      struct xl_xact_commit* xlrec = (struct xl_xact_commit*) rec;

      if (server_config->version >= 15)
      {
         buf = xact_desc_commit_v15(buf, XLOG_REC_GET_INFO(record), xlrec, XLOG_REC_GET_ORIGIN(record));
      }
      else
      {
         buf = xact_desc_commit_v14(buf, XLOG_REC_GET_INFO(record), xlrec, XLOG_REC_GET_ORIGIN(record));
      }
   }
   else if (info == XLOG_XACT_ABORT || info == XLOG_XACT_ABORT_PREPARED)
   {
      struct xl_xact_abort* xlrec = (struct xl_xact_abort*) rec;
      if (server_config->version >= 15)
      {
         buf = xact_desc_abort_v15(buf, XLOG_REC_GET_INFO(record), xlrec);
      }
      else
      {
         buf = xact_desc_abort_v14(buf, XLOG_REC_GET_INFO(record), xlrec);
      }

   }
   else if (info == XLOG_XACT_PREPARE)
   {

      if (server_config->version >= 15)
      {
         struct xl_xact_prepare_v15* xlrec = (struct xl_xact_prepare_v15*) rec;
         buf = xact_desc_prepare_v15(buf, XLOG_REC_GET_INFO(record), xlrec);
      }
      else
      {
         struct xl_xact_prepare_v14* xlrec = (struct xl_xact_prepare_v14*) rec;
         buf = xact_desc_prepare_v14(buf, XLOG_REC_GET_INFO(record), xlrec);
      }

   }
   else if (info == XLOG_XACT_ASSIGNMENT)
   {
      struct xl_xact_assignment* xlrec = (struct xl_xact_assignment*) rec;

      /*
       * Note that we ignore the WAL record's xid, since we're more
       * interested in the top-level xid that issued the record and which
       * xids are being reported here.
       */
      buf = pgmoneta_format_and_append(buf, "xtop %u: ", xlrec->xtop);
      buf = xact_desc_assignment(buf, xlrec);
   }
   else if (info == XLOG_XACT_INVALIDATIONS)
   {
      struct xl_xact_invals* xlrec = (struct xl_xact_invals*) rec;

      buf = pgmoneta_wal_standby_desc_invalidations(buf, xlrec->nmsgs, xlrec->msgs, InvalidOid,
                                                    InvalidOid, false);
   }
   return buf;
}

// v14

void
parse_abort_record_v14(uint8_t info, struct xl_xact_abort* xlrec, struct xl_xact_parsed_abort_v14* parsed)
{
   char* data = ((char*) xlrec) + MIN_SIZE_OF_XACT_ABORT;

   memset(parsed, 0, sizeof(*parsed));

   parsed->xinfo = 0;         /* default, if no XLOG_XACT_HAS_INFO is
                               * present */

   parsed->xact_time = xlrec->xact_time;

   if (info & XLOG_XACT_HAS_INFO)
   {
      xl_xact_xinfo* xl_xinfo = (xl_xact_xinfo*) data;

      parsed->xinfo = xl_xinfo->xinfo;

      data += sizeof(xl_xact_xinfo);
   }

   if (parsed->xinfo & XACT_XINFO_HAS_DBINFO)
   {
      struct xl_xact_dbinfo* xl_dbinfo = (struct xl_xact_dbinfo*) data;

      parsed->dbId = xl_dbinfo->db_id;
      parsed->tsId = xl_dbinfo->ts_id;

      data += sizeof(struct xl_xact_dbinfo);
   }

   if (parsed->xinfo & XACT_XINFO_HAS_SUBXACTS)
   {
      struct xl_xact_subxacts* xl_subxacts = (struct xl_xact_subxacts*) data;

      parsed->nsubxacts = xl_subxacts->nsubxacts;
      parsed->subxacts = xl_subxacts->subxacts;

      data += MIN_SIZE_OF_XACT_SUBXACTS;
      data += parsed->nsubxacts * sizeof(transaction_id);
   }

   if (parsed->xinfo & XACT_XINFO_HAS_RELFILENODES)
   {
      struct xl_xact_relfilenodes* xl_relfilenodes = (struct xl_xact_relfilenodes*) data;

      parsed->nrels = xl_relfilenodes->nrels;
      parsed->xnodes = xl_relfilenodes->xnodes;

      data += MIN_SIZE_OF_XACT_RELFILENODES;
      data += xl_relfilenodes->nrels * sizeof(struct rel_file_node);
   }

   if (parsed->xinfo & XACT_XINFO_HAS_TWOPHASE)
   {
      struct xl_xact_twophase* xl_twophase = (struct xl_xact_twophase*) data;

      parsed->twophase_xid = xl_twophase->xid;

      data += sizeof(struct xl_xact_twophase);

      if (parsed->xinfo & XACT_XINFO_HAS_GID)
      {
         snprintf(parsed->twophase_gid, sizeof(parsed->twophase_gid), "%s", data);;
         data += strlen(data) + 1;
      }
   }

   /* Note: no alignment is guaranteed after this point */

   if (parsed->xinfo & XACT_XINFO_HAS_ORIGIN)
   {
      struct xl_xact_origin xl_origin;

      /* no alignment is guaranteed, so copy onto stack */
      memcpy(&xl_origin, data, sizeof(xl_origin));

      parsed->origin_lsn = xl_origin.origin_lsn;
      parsed->origin_timestamp = xl_origin.origin_timestamp;

      data += sizeof(struct xl_xact_origin);
   }
}

void
parse_prepare_record_v14(uint8_t info __attribute__((unused)), struct xl_xact_prepare_v14* xlrec, xl_xact_parsed_prepare_v14* parsed)
{
   char* bufptr;

   bufptr = ((char*) xlrec) + MAXALIGN(sizeof(struct xl_xact_prepare));

   memset(parsed, 0, sizeof(*parsed));

   parsed->xact_time = xlrec->prepared_at;
   parsed->origin_lsn = xlrec->origin_lsn;
   parsed->origin_timestamp = xlrec->origin_timestamp;
   parsed->twophase_xid = xlrec->xid;
   parsed->db_id = xlrec->database;
   parsed->nsubxacts = xlrec->nsubxacts;
   parsed->nrels = xlrec->ncommitrels;
   parsed->nabortrels = xlrec->nabortrels;
   parsed->nmsgs = xlrec->ninvalmsgs;

   strncpy(parsed->twophase_gid, bufptr, xlrec->gidlen);
   bufptr += MAXALIGN(xlrec->gidlen);

   parsed->subxacts = (transaction_id*) bufptr;
   bufptr += MAXALIGN(xlrec->nsubxacts * sizeof(transaction_id));

   parsed->xnodes = (struct rel_file_node*) bufptr;
   bufptr += MAXALIGN(xlrec->ncommitrels * sizeof(struct rel_file_node));

   parsed->abortnodes = (struct rel_file_node*) bufptr;
   bufptr += MAXALIGN(xlrec->nabortrels * sizeof(struct rel_file_node));

   parsed->msgs = (union shared_invalidation_message*) bufptr;
   bufptr += MAXALIGN(xlrec->ninvalmsgs * sizeof(union shared_invalidation_message));
}

void
parse_commit_record_v14(uint8_t info, struct xl_xact_commit* xlrec, struct xl_xact_parsed_commit_v14* parsed)
{
   char* data = ((char*) xlrec) + MIN_SIZE_OF_XACT_COMMIT;

   memset(parsed, 0, sizeof(*parsed));

   parsed->xinfo = 0;         /* default, if no XLOG_XACT_HAS_INFO is
                               * present */

   parsed->xact_time = xlrec->xact_time;

   if (info & XLOG_XACT_HAS_INFO)
   {
      xl_xact_xinfo* xl_xinfo = (xl_xact_xinfo*) data;

      parsed->xinfo = xl_xinfo->xinfo;

      data += sizeof(xl_xact_xinfo);
   }

   if (parsed->xinfo & XACT_XINFO_HAS_DBINFO)
   {
      struct xl_xact_dbinfo* xl_dbinfo = (struct xl_xact_dbinfo*) data;

      parsed->db_id = xl_dbinfo->db_id;
      parsed->ts_id = xl_dbinfo->ts_id;

      data += sizeof(struct xl_xact_dbinfo);
   }

   if (parsed->xinfo & XACT_XINFO_HAS_SUBXACTS)
   {
      struct xl_xact_subxacts* xl_subxacts = (struct xl_xact_subxacts*) data;

      parsed->nsubxacts = xl_subxacts->nsubxacts;
      parsed->subxacts = xl_subxacts->subxacts;

      data += MIN_SIZE_OF_XACT_SUBXACTS;
      data += parsed->nsubxacts * sizeof(transaction_id);
   }

   if (parsed->xinfo & XACT_XINFO_HAS_RELFILENODES)
   {
      struct xl_xact_relfilenodes* xl_relfilenodes = (struct xl_xact_relfilenodes*) data;

      parsed->nrels = xl_relfilenodes->nrels;
      parsed->xnodes = xl_relfilenodes->xnodes;

      data += MIN_SIZE_OF_XACT_RELFILENODES;
      data += xl_relfilenodes->nrels * sizeof(struct rel_file_node);
   }

   if (parsed->xinfo & XACT_XINFO_HAS_INVALS)
   {
      struct xl_xact_invals* xl_invals = (struct xl_xact_invals*) data;

      parsed->nmsgs = xl_invals->nmsgs;
      parsed->msgs = xl_invals->msgs;

      data += MIN_SIZE_OF_XACT_INVALS;
      data += xl_invals->nmsgs * sizeof(union shared_invalidation_message);
   }

   if (parsed->xinfo & XACT_XINFO_HAS_TWOPHASE)
   {
      struct xl_xact_twophase* xl_twophase = (struct xl_xact_twophase*) data;

      parsed->twophase_xid = xl_twophase->xid;

      data += sizeof(struct xl_xact_twophase);

      if (parsed->xinfo & XACT_XINFO_HAS_GID)
      {
         snprintf(parsed->twophase_gid, sizeof(parsed->twophase_gid), "%s", data);;
         data += strlen(data) + 1;
      }
   }

   /* Note: no alignment is guaranteed after this point */

   if (parsed->xinfo & XACT_XINFO_HAS_ORIGIN)
   {
      struct xl_xact_origin xl_origin;

      /* no alignment is guaranteed, so copy onto stack */
      memcpy(&xl_origin, data, sizeof(xl_origin));

      parsed->origin_lsn = xl_origin.origin_lsn;
      parsed->origin_timestamp = xl_origin.origin_timestamp;

      data += sizeof(struct xl_xact_origin);
   }
}

// v15
void
parse_abort_record_v15(uint8_t info, struct xl_xact_abort* xlrec, struct xl_xact_parsed_abort_v15* parsed)
{
   char* data = ((char*) xlrec) + MIN_SIZE_OF_XACT_ABORT;

   memset(parsed, 0, sizeof(*parsed));

   parsed->xinfo = 0;         /* default, if no XLOG_XACT_HAS_INFO is
                               * present */

   parsed->xact_time = xlrec->xact_time;

   if (info & XLOG_XACT_HAS_INFO)
   {
      struct xl_xact_xinfo* xl_xinfo = (xl_xact_xinfo*) data;

      parsed->xinfo = xl_xinfo->xinfo;

      data += sizeof(xl_xact_xinfo);
   }

   if (parsed->xinfo & XACT_XINFO_HAS_DBINFO)
   {
      struct xl_xact_dbinfo* xl_dbinfo = (struct xl_xact_dbinfo*) data;

      parsed->db_id = xl_dbinfo->db_id;
      parsed->ts_id = xl_dbinfo->ts_id;

      data += sizeof(struct xl_xact_dbinfo);
   }

   if (parsed->xinfo & XACT_XINFO_HAS_SUBXACTS)
   {
      struct xl_xact_subxacts* xl_subxacts = (struct xl_xact_subxacts*) data;

      parsed->nsubxacts = xl_subxacts->nsubxacts;
      parsed->subxacts = xl_subxacts->subxacts;

      data += MIN_SIZE_OF_XACT_SUBXACTS;
      data += parsed->nsubxacts * sizeof(transaction_id);
   }

   if (parsed->xinfo & XACT_XINFO_HAS_RELFILENODES)
   {
      struct xl_xact_relfilenodes* xl_relfilenodes = (struct xl_xact_relfilenodes*) data;

      parsed->nrels = xl_relfilenodes->nrels;
      parsed->xnodes = xl_relfilenodes->xnodes;

      data += MIN_SIZE_OF_XACT_RELFILENODES;
      data += xl_relfilenodes->nrels * sizeof(struct rel_file_node);
   }

   if (parsed->xinfo & XACT_XINFO_HAS_DROPPED_STATS)
   {
      struct xl_xact_stats_items* xl_drops = (struct xl_xact_stats_items*) data;

      parsed->nstats = xl_drops->nitems;
      parsed->stats = xl_drops->items;

      data += MIN_SIZE_OF_XACT_STATS_ITEMS;
      data += xl_drops->nitems * sizeof(struct xl_xact_stats_item);
   }

   if (parsed->xinfo & XACT_XINFO_HAS_TWOPHASE)
   {
      struct xl_xact_twophase* xl_twophase = (struct xl_xact_twophase*) data;

      parsed->twophase_xid = xl_twophase->xid;

      data += sizeof(struct xl_xact_twophase);

      if (parsed->xinfo & XACT_XINFO_HAS_GID)
      {
         snprintf(parsed->twophase_gid, sizeof(parsed->twophase_gid), "%s", data);;
         data += strlen(data) + 1;
      }
   }

   /* Note: no alignment is guaranteed after this point */

   if (parsed->xinfo & XACT_XINFO_HAS_ORIGIN)
   {
      struct xl_xact_origin xl_origin;

      /* no alignment is guaranteed, so copy onto stack */
      memcpy(&xl_origin, data, sizeof(xl_origin));

      parsed->origin_lsn = xl_origin.origin_lsn;
      parsed->origin_timestamp = xl_origin.origin_timestamp;

      data += sizeof(struct xl_xact_origin);
   }
}

/*
 * ParsePrepareRecord
 */
void
parse_prepare_record_v15(uint8_t info __attribute__((unused)), struct xl_xact_prepare_v15* xlrec, xl_xact_parsed_prepare_v15* parsed)
{
   char* bufptr;

   bufptr = ((char*) xlrec) + MAXALIGN(sizeof(struct xl_xact_prepare));

   memset(parsed, 0, sizeof(*parsed));

   parsed->xact_time = xlrec->prepared_at;
   parsed->origin_lsn = xlrec->origin_lsn;
   parsed->origin_timestamp = xlrec->origin_timestamp;
   parsed->twophase_xid = xlrec->xid;
   parsed->db_id = xlrec->database;
   parsed->nsubxacts = xlrec->nsubxacts;
   parsed->nrels = xlrec->ncommitrels;
   parsed->nabortrels = xlrec->nabortrels;
   parsed->nmsgs = xlrec->ninvalmsgs;

   strncpy(parsed->twophase_gid, bufptr, xlrec->gidlen);
   bufptr += MAXALIGN(xlrec->gidlen);

   parsed->subxacts = (transaction_id*) bufptr;
   bufptr += MAXALIGN(xlrec->nsubxacts * sizeof(transaction_id));

   parsed->xnodes = (struct rel_file_node*) bufptr;
   bufptr += MAXALIGN(xlrec->ncommitrels * sizeof(struct rel_file_node));

   parsed->abortnodes = (struct rel_file_node*) bufptr;
   bufptr += MAXALIGN(xlrec->nabortrels * sizeof(struct rel_file_node));

   parsed->stats = (struct xl_xact_stats_item*) bufptr;
   bufptr += MAXALIGN(xlrec->ncommitstats * sizeof(struct xl_xact_stats_item));

   parsed->abortstats = (struct xl_xact_stats_item*) bufptr;
   bufptr += MAXALIGN(xlrec->nabortstats * sizeof(struct xl_xact_stats_item));

   parsed->msgs = (union shared_invalidation_message*) bufptr;
   bufptr += MAXALIGN(xlrec->ninvalmsgs * sizeof(union shared_invalidation_message));
}

void
parse_commit_record_v15(uint8_t info, struct xl_xact_commit* xlrec, struct xl_xact_parsed_commit_v15* parsed)
{
   char* data = ((char*) xlrec) + MIN_SIZE_OF_XACT_COMMIT;

   memset(parsed, 0, sizeof(*parsed));

   parsed->xinfo = 0;         /* default, if no XLOG_XACT_HAS_INFO is
                               * present */

   parsed->xact_time = xlrec->xact_time;

   if (info & XLOG_XACT_HAS_INFO)
   {
      xl_xact_xinfo* xl_xinfo = (xl_xact_xinfo*) data;

      parsed->xinfo = xl_xinfo->xinfo;

      data += sizeof(xl_xact_xinfo);
   }

   if (parsed->xinfo & XACT_XINFO_HAS_DBINFO)
   {
      struct xl_xact_dbinfo* xl_dbinfo = (struct xl_xact_dbinfo*) data;

      parsed->db_id = xl_dbinfo->db_id;
      parsed->ts_id = xl_dbinfo->ts_id;

      data += sizeof(struct xl_xact_dbinfo);
   }

   if (parsed->xinfo & XACT_XINFO_HAS_SUBXACTS)
   {
      struct xl_xact_subxacts* xl_subxacts = (struct xl_xact_subxacts*) data;

      parsed->nsubxacts = xl_subxacts->nsubxacts;
      parsed->subxacts = xl_subxacts->subxacts;

      data += MIN_SIZE_OF_XACT_SUBXACTS;
      data += parsed->nsubxacts * sizeof(transaction_id);
   }

   if (parsed->xinfo & XACT_XINFO_HAS_RELFILENODES)
   {
      struct xl_xact_relfilenodes* xl_relfilenodes = (struct xl_xact_relfilenodes*) data;

      parsed->nrels = xl_relfilenodes->nrels;
      parsed->xnodes = xl_relfilenodes->xnodes;

      data += MIN_SIZE_OF_XACT_RELFILENODES;
      data += xl_relfilenodes->nrels * sizeof(struct rel_file_node);
   }

   if (parsed->xinfo & XACT_XINFO_HAS_DROPPED_STATS)
   {
      struct xl_xact_stats_items* xl_drops = (struct xl_xact_stats_items*) data;

      parsed->nstats = xl_drops->nitems;
      parsed->stats = xl_drops->items;

      data += MIN_SIZE_OF_XACT_STATS_ITEMS;
      data += xl_drops->nitems * sizeof(struct xl_xact_stats_item);
   }

   if (parsed->xinfo & XACT_XINFO_HAS_INVALS)
   {
      struct xl_xact_invals* xl_invals = (struct xl_xact_invals*) data;

      parsed->nmsgs = xl_invals->nmsgs;
      parsed->msgs = xl_invals->msgs;

      data += MIN_SIZE_OF_XACT_INVALS;
      data += xl_invals->nmsgs * sizeof(union shared_invalidation_message);
   }

   if (parsed->xinfo & XACT_XINFO_HAS_TWOPHASE)
   {
      struct xl_xact_twophase* xl_twophase = (struct xl_xact_twophase*) data;

      parsed->twophase_xid = xl_twophase->xid;

      data += sizeof(struct xl_xact_twophase);

      if (parsed->xinfo & XACT_XINFO_HAS_GID)
      {
         snprintf(parsed->twophase_gid, sizeof(parsed->twophase_gid), "%s", data);;
         data += strlen(data) + 1;
      }
   }

   /* Note: no alignment is guaranteed after this point */

   if (parsed->xinfo & XACT_XINFO_HAS_ORIGIN)
   {
      struct xl_xact_origin xl_origin;

      /* no alignment is guaranteed, so copy onto stack */
      memcpy(&xl_origin, data, sizeof(xl_origin));

      parsed->origin_lsn = xl_origin.origin_lsn;
      parsed->origin_timestamp = xl_origin.origin_timestamp;

      data += sizeof(struct xl_xact_origin);
   }
}
