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
#define PG_RMGR_SUMMARY(symname, name, number_of_records) {name, number_of_records},
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
 * @struct rmgr_summary
 * @brief Represents a Resource Manager (RMGR) data structure.
 *
 * Fields:
 * - name: The name of the resource manager.
 * - number_of_records: A function pointer to the description function for the resource manager.
 */
struct rmgr_summary
{
   char* name;             /**< The name of the resource manager */
   int number_of_records;  /**< The number of records of a specific type */
};

/**
 * Table of resource managers. Each entry corresponds to a specific RMGR identified by an ID.
 */
extern struct rmgr_data rmgr_table[RM_MAX_ID + 1];
extern struct rmgr_summary rmgr_summary_table[RM_MAX_ID + 1];

#ifdef __cplusplus
}
#endif

#endif // PGMONETA_RMGR_H
