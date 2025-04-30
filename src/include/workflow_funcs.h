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

#ifndef PGMONETA_WORKFLOW_FUNCS_H
#define PGMONETA_WORKFLOW_FUNCS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>
#include <workflow.h>

#include <stdlib.h>
#include <stdbool.h>

/**
 * Create a workflow for the base backup
 * @return The workflow
 */
struct workflow*
pgmoneta_create_basebackup(void);

/**
 * Create a workflow for the restore
 * @return The workflow
 */
struct workflow*
pgmoneta_create_restore(void);

/**
 * Create a workflow for combining incremental backup
 * @return The workflow
 */
struct workflow*
pgmoneta_create_combine_incremental(void);

/**
 * Create a workflow for the verify
 * @return The workflow
 */
struct workflow*
pgmoneta_create_verify(void);

/**
 * Create a workflow for the archive
 * @return The workflow
 */
struct workflow*
pgmoneta_create_archive(void);

/**
 * Create a workflow for the retention
 * @return The workflow
 */
struct workflow*
pgmoneta_create_retention(void);

/**
 * Create a workflow for the SHA-256
 * @return The workflow
 */
struct workflow*
pgmoneta_create_sha256(void);

/**
 * Create a workflow for the SHA-256
 * @return The workflow
 */
struct workflow*
pgmoneta_create_sha512(void);

/**
 * Create a workflow for the delete backups
 * @return The workflow
 */
struct workflow*
pgmoneta_create_delete_backup(void);

/**
 * Create a workflow for GZIP
 * @param compress The compress
 * @return The workflow
 */
struct workflow*
pgmoneta_create_gzip(bool compress);

/**
 * Create a workflow for Zstandard
 * @param compress The compress
 * @return The workflow
 */
struct workflow*
pgmoneta_create_zstd(bool compress);

/**
 * Create a workflow for Lz4
 * @param compress The compress
 * @return The workflow
 */
struct workflow*
pgmoneta_create_lz4(bool compress);

/**
 * Create a workflow for BZip2
 * @param compress The compress
 * @return The workflow
 */
struct workflow*
pgmoneta_create_bzip2(bool compress);

/**
 * Create a workflow for symlinking
 * @return The workflow
 */
struct workflow*
pgmoneta_create_link(void);

struct workflow*
pgmoneta_create_copy_wal(void);

/**
 * Create a workflow for recovery info
 * @return The workflow
 */
struct workflow*
pgmoneta_create_recovery_info(void);

/**
 * Create a workflow to restore the excluded files in the first round of restore
 * @return The workflow
 */
struct workflow*
pgmoneta_restore_excluded_files(void);

/**
 * Create a workflow for permissions
 * @param type The type of operation
 * @return The workflow
 */
struct workflow*
pgmoneta_create_permissions(int type);

/**
 * Create a workflow for cleanup
 * @param type The type of operation
 * @return The workflow
 */
struct workflow*
pgmoneta_create_cleanup(int type);

/**
 * Create a workflow for encryption
 * @param encrypt true for encrypt and false for decrypt
 * @return The workflow
 */
struct workflow*
pgmoneta_encryption(bool encrypt);

/**
 * Create a workflow for manifest building
 * @return The workflow
 */
struct workflow*
pgmoneta_create_manifest(void);

/**
 * Create a workflow for extra files
 * @return The workflow
 */
struct workflow*
pgmoneta_create_extra(void);
#ifdef __cplusplus
}
#endif

#endif
