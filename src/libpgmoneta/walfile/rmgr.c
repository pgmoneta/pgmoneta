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

#include <walfile/rmgr.h>

struct rmgr_data rmgr_table[RM_MAX_ID + 1] = {
   PG_RMGR(RM_XLOG_ID, "XLOG", pgmoneta_wal_xlog_desc)
   PG_RMGR(RM_XACT_ID, "Transaction", pgmoneta_wal_xact_desc)
   PG_RMGR(RM_SMGR_ID, "Storage", pgmoneta_wal_storage_desc)
   PG_RMGR(RM_CLOG_ID, "CLOG", pgmoneta_wal_clog_desc)
   PG_RMGR(RM_DBASE_ID, "Database", pgmoneta_wal_database_desc)
   PG_RMGR(RM_TBLSPC_ID, "Tablespace", pgmoneta_wal_tablespace_desc)
   PG_RMGR(RM_MULTIXACT_ID, "MultiXact", pgmoneta_wal_multixact_desc)
   PG_RMGR(RM_RELMAP_ID, "RelMap", pgmoneta_wal_relmap_desc)
   PG_RMGR(RM_STANDBY_ID, "Standby", pgmoneta_wal_standby_desc)
   PG_RMGR(RM_HEAP2_ID, "Heap2", pgmoneta_wal_heap2_desc)
   PG_RMGR(RM_HEAP_ID, "Heap", pgmoneta_wal_heap_desc)
   PG_RMGR(RM_BTREE_ID, "Btree", pgmoneta_wal_btree_desc)
   PG_RMGR(RM_HASH_ID, "Hash", pgmoneta_wal_hash_desc)
   PG_RMGR(RM_GIN_ID, "Gin", pgmoneta_wal_gin_desc)
   PG_RMGR(RM_GIST_ID, "Gist", pgmoneta_wal_gist_desc)
   PG_RMGR(RM_SEQ_ID, "Sequence", pgmoneta_wal_seq_desc)
   PG_RMGR(RM_SPGIST_ID, "SPGist", pgmoneta_wal_spg_desc)
   PG_RMGR(RM_BRIN_ID, "BRIN", pgmoneta_wal_brin_desc)
   PG_RMGR(RM_COMMIT_TS_ID, "CommitTs", pgmoneta_wal_commit_ts_desc)
   PG_RMGR(RM_REPLORIGIN_ID, "ReplicationOrigin", pgmoneta_wal_replorigin_desc)
   PG_RMGR(RM_GENERIC_ID, "Generic", pgmoneta_wal_generic_desc)
   PG_RMGR(RM_LOGICALMSG_ID, "LogicalMessage", pgmoneta_wal_logicalmsg_desc)
};

struct rmgr_summary rmgr_summary_table[RM_MAX_ID + 1] = {
   PG_RMGR_SUMMARY(RM_XLOG_ID, "XLOG", 0)
   PG_RMGR_SUMMARY(RM_XACT_ID, "Transaction", 0)
   PG_RMGR_SUMMARY(RM_SMGR_ID, "Storage", 0)
   PG_RMGR_SUMMARY(RM_CLOG_ID, "CLOG", 0)
   PG_RMGR_SUMMARY(RM_DBASE_ID, "Database", 0)
   PG_RMGR_SUMMARY(RM_TBLSPC_ID, "Tablespace", 0)
   PG_RMGR_SUMMARY(RM_MULTIXACT_ID, "MultiXact", 0)
   PG_RMGR_SUMMARY(RM_RELMAP_ID, "RelMap", 0)
   PG_RMGR_SUMMARY(RM_STANDBY_ID, "Standby", 0)
   PG_RMGR_SUMMARY(RM_HEAP2_ID, "Heap2", 0)
   PG_RMGR_SUMMARY(RM_HEAP_ID, "Heap", 0)
   PG_RMGR_SUMMARY(RM_BTREE_ID, "Btree", 0)
   PG_RMGR_SUMMARY(RM_HASH_ID, "Hash", 0)
   PG_RMGR_SUMMARY(RM_GIN_ID, "Gin", 0)
   PG_RMGR_SUMMARY(RM_GIST_ID, "Gist", 0)
   PG_RMGR_SUMMARY(RM_SEQ_ID, "Sequence", 0)
   PG_RMGR_SUMMARY(RM_SPGIST_ID, "SPGist", 0)
   PG_RMGR_SUMMARY(RM_BRIN_ID, "BRIN", 0)
   PG_RMGR_SUMMARY(RM_COMMIT_TS_ID, "CommitTs", 0)
   PG_RMGR_SUMMARY(RM_REPLORIGIN_ID, "ReplicationOrigin", 0)
   PG_RMGR_SUMMARY(RM_GENERIC_ID, "Generic", 0)
   PG_RMGR_SUMMARY(RM_LOGICALMSG_ID, "LogicalMessage", 0)
};

struct rmgr_stats rmgr_stats_table[RM_MAX_ID + 1] = {
   PG_RMGR_STATS(RM_XLOG_ID, "XLOG")
   PG_RMGR_STATS(RM_XACT_ID, "Transaction")
   PG_RMGR_STATS(RM_SMGR_ID, "Storage")
   PG_RMGR_STATS(RM_CLOG_ID, "CLOG")
   PG_RMGR_STATS(RM_DBASE_ID, "Database")
   PG_RMGR_STATS(RM_TBLSPC_ID, "Tablespace")
   PG_RMGR_STATS(RM_MULTIXACT_ID, "MultiXact")
   PG_RMGR_STATS(RM_RELMAP_ID, "RelMap")
   PG_RMGR_STATS(RM_STANDBY_ID, "Standby")
   PG_RMGR_STATS(RM_HEAP2_ID, "Heap2")
   PG_RMGR_STATS(RM_HEAP_ID, "Heap")
   PG_RMGR_STATS(RM_BTREE_ID, "Btree")
   PG_RMGR_STATS(RM_HASH_ID, "Hash")
   PG_RMGR_STATS(RM_GIN_ID, "Gin")
   PG_RMGR_STATS(RM_GIST_ID, "Gist")
   PG_RMGR_STATS(RM_SEQ_ID, "Sequence")
   PG_RMGR_STATS(RM_SPGIST_ID, "SPGist")
   PG_RMGR_STATS(RM_BRIN_ID, "BRIN")
   PG_RMGR_STATS(RM_COMMIT_TS_ID, "CommitTs")
   PG_RMGR_STATS(RM_REPLORIGIN_ID, "ReplicationOrigin")
   PG_RMGR_STATS(RM_GENERIC_ID, "Generic")
   PG_RMGR_STATS(RM_LOGICALMSG_ID, "LogicalMessage")
};