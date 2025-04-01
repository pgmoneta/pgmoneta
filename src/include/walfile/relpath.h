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

#ifndef PGMONETA_RELPATH_H
#define PGMONETA_RELPATH_H

#include <walfile/wal_reader.h>

typedef int backend_id;        /* unique currently active backend identifier */

#define INVALID_BACKEND_ID       (-1)

#define DEFAULTTABLESPACE_OID    1663
#define GLOBALTABLESPACE_OID     1664

#define RELPATHBACKEND(rnode, backend, forknum) \
        pgmoneta_wal_get_relation_path((rnode).dbNode, (rnode).spcNode, (rnode).relNode, \
                                       backend, forknum)

/* First argument is a rel_file_node */
#define RELPATHPERM(rnode, forknum) \
        RELPATHBACKEND(rnode, INVALID_BACKEND_ID, forknum)

/**
 * @brief Generates the filesystem path for a relation file.
 *
 * Constructs the path to a relation file using the database OID, tablespace OID,
 * relation OID, backend ID, and fork number. The path reflects the storage
 * location in the `global`, `base`, or `pg_tblspc` directories based on the
 * tablespace and backend ID.
 *
 * @param dbNode      OID of the database containing the relation.
 * @param spcNode     OID of the tablespace where the relation resides.
 * @param relNode     OID of the relation (table, index, etc.).
 * @param backendId   Backend ID for temporary relations; use `INVALID_BACKEND_ID` for others.
 * @param forkNumber  Fork number (e.g., `MAIN_FORKNUM`, `FSM_FORKNUM`).
 *
 * @return Dynamically allocated string with the relation file path, or `NULL` on failure.
 *
 * @note Caller is responsible for freeing the returned string.
 */
char*
pgmoneta_wal_get_relation_path(oid dbNode, oid spcNode, oid relNode, int backendId, enum fork_number forkNumber);

/**
 * @brief Get the tablespace version directory.
 *
 * This function retrieves the directory where the tablespace version is stored.
 * It is used to identify the specific location for tablespace versions in the WAL context.
 *
 * @return The path to the tablespace version directory as a string.
 */
char*
pgmoneta_wal_get_tablespace_version_directory(void);

/**
 * @brief Get the catalog version number.
 *
 * This function retrieves the catalog version number from the WAL context.
 * The catalog version number is used to ensure compatibility between different
 * versions of the catalog during WAL operations.
 *
 * @return The catalog version number as a string.
 */
char*
pgmoneta_wal_get_catalog_version_number(void);

#endif // PGMONETA_RELPATH_H
