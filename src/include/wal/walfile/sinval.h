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

#ifndef PGMONETA_SINVAL_H
#define PGMONETA_SINVAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <wal/walfile/wal_reader.h>

typedef signed char int8; /**< Signed 8-bit integer. */

#define SHAREDINVALCATALOG_ID   (-1)
#define SHAREDINVALRELCACHE_ID  (-2)
#define SHAREDINVALSMGR_ID      (-3)
#define SHAREDINVALRELMAP_ID    (-4)
#define SHAREDINVALSNAPSHOT_ID  (-5)

/**
 * @struct shared_inval_catcache_msg
 * @brief Message structure for shared invalidation of catalog caches.
 *
 * Fields:
 * - id: Cache ID (must be first).
 * - db_id: Database ID, or 0 if it's a shared relation.
 * - hashValue: Hash value of the key for this catalog cache.
 */
struct shared_inval_catcache_msg
{
    int8 id;                   /**< Cache ID --- must be first */
    oid db_id;                 /**< Database ID, or 0 if a shared relation */
    uint32_t hashValue;        /**< Hash value of key for this catcache */
};

/**
 * @struct shared_inval_catalog_msg
 * @brief Message structure for shared invalidation of catalogs.
 *
 * Fields:
 * - id: Type field (must be first).
 * - db_id: Database ID, or 0 if it's a shared catalog.
 * - cat_id: ID of the catalog whose contents are invalid.
 */
struct shared_inval_catalog_msg
{
    int8 id;                   /**< Type field --- must be first */
    oid db_id;                 /**< Database ID, or 0 if a shared catalog */
    oid cat_id;                /**< ID of catalog whose contents are invalid */
};

/**
 * @struct shared_inval_relcache_msg
 * @brief Message structure for shared invalidation of relation caches.
 *
 * Fields:
 * - id: Type field (must be first).
 * - db_id: Database ID, or 0 if it's a shared relation.
 * - rel_id: Relation ID, or 0 if the whole relcache is invalid.
 */
struct shared_inval_relcache_msg
{
    int8 id;                   /**< Type field --- must be first */
    oid db_id;                 /**< Database ID, or 0 if a shared relation */
    oid rel_id;                /**< Relation ID, or 0 if whole relcache */
};

/**
 * @struct shared_inval_smgr_msg
 * @brief Message structure for shared invalidation of storage manager data.
 *
 * Fields:
 * - id: Type field (must be first).
 * - backend_hi: High bits of backend process number, if it's a temporary relation.
 * - backend_lo: Low bits of backend process number, if it's a temporary relation.
 * - rlocator: Rel file locator structure containing spcOid, dbOid, and relNumber.
 */
struct shared_inval_smgr_msg
{
    int8 id;                           /**< Type field --- must be first */
    int8 backend_hi;                   /**< High bits of backend procno, if temprel */
    uint16_t backend_lo;               /**< Low bits of backend procno, if temprel */
    struct rel_file_locator rlocator;  /**< spcOid, dbOid, relNumber */
};

/**
 * @struct shared_inval_relmap_msg
 * @brief Message structure for shared invalidation of relation mapping.
 *
 * Fields:
 * - id: Type field (must be first).
 * - db_id: Database ID, or 0 for shared catalogs.
 */
struct shared_inval_relmap_msg
{
    int8 id;                   /**< Type field --- must be first */
    oid db_id;                 /**< Database ID, or 0 for shared catalogs */
};

/**
 * @struct shared_inval_snapshot_msg
 * @brief Message structure for shared invalidation of snapshots.
 *
 * Fields:
 * - id: Type field (must be first).
 * - db_id: Database ID, or 0 if it's a shared relation.
 * - rel_id: Relation ID.
 */
struct shared_inval_snapshot_msg
{
    int8 id;                   /**< Type field --- must be first */
    oid db_id;                 /**< Database ID, or 0 if a shared relation */
    oid rel_id;                /**< Relation ID */
};

/**
 * @union shared_invalidation_message
 * @brief Union of all shared invalidation message types.
 *
 * Fields:
 * - id: Type field (must be first).
 * - cc: Catalog cache invalidation message.
 * - cat: Catalog invalidation message.
 * - rc: Relation cache invalidation message.
 * - sm: Storage manager invalidation message.
 * - rm: Relation mapping invalidation message.
 * - sn: Snapshot invalidation message.
 */
union shared_invalidation_message {
    int8 id;                             /**< Type field --- must be first */
    struct shared_inval_catcache_msg cc; /**< Catalog cache invalidation message */
    struct shared_inval_catalog_msg cat; /**< Catalog invalidation message */
    struct shared_inval_relcache_msg rc; /**< Relation cache invalidation message */
    struct shared_inval_smgr_msg sm;     /**< Storage manager invalidation message */
    struct shared_inval_relmap_msg rm;   /**< Relation mapping invalidation message */
    struct shared_inval_snapshot_msg sn; /**< Snapshot invalidation message */
};

#ifdef __cplusplus
}
#endif

#endif // PGMONETA_SINVAL_H
