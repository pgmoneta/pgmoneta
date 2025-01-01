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

#ifndef PGMONETA_RMGR_H
#define PGMONETA_RMGR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <walfile/rm_xlog.h>
#include <walfile/rm_xact.h>
#include <walfile/rm_storage.h>
#include <walfile/rm_clog.h>
#include <walfile/rm_database.h>
#include <walfile/rm_tablespace.h>
#include <walfile/rm_mxact.h>
#include <walfile/rm_relmap.h>
#include <walfile/rm_standby.h>
#include <walfile/rm_heap.h>
#include <walfile/rm_btree.h>
#include <walfile/rm_hash.h>
#include <walfile/rm_gin.h>
#include <walfile/rm_gist.h>
#include <walfile/rm_seq.h>
#include <walfile/rm_spgist.h>
#include <walfile/rm_brin.h>
#include <walfile/rm_generic.h>
#include <walfile/rm_commit_ts.h>
#include <walfile/rm_replorigin.h>
#include <walfile/rm_logicalmsg.h>

#include <stdint.h>

#define PG_RMGR(symname, name, desc) {name, desc},
#define RM_MAX_ID           UINT8_MAX

/**
 * @struct rmgr_data
 * @brief Represents a Resource Manager (RMGR) data structure.
 *
 * Fields:
 * - name: The name of the resource manager.
 * - rm_desc: A function pointer to the description function for the resource manager.
 */
struct rmgr_data
{
   char* name;                                    /**< The name of the resource manager */
   char* (*rm_desc)(char* buf, struct decoded_xlog_record* record);  /**< Function pointer to the RMGR description function */
};

/**
 * Table of resource managers. Each entry corresponds to a specific RMGR identified by an ID.
 */
struct rmgr_data RmgrTable[RM_MAX_ID + 1] = {
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

#ifdef __cplusplus
}
#endif

#endif // PGMONETA_RMGR_H
