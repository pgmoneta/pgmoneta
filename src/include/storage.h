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

#ifndef PGMONETA_STORAGE_H
#define PGMONETA_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* pgmoneta */
#include <pgmoneta.h>
#include <workflow.h>

/* system */
#include <libssh/libssh.h>
#include <libssh/sftp.h>

/**
 * Create a workflow for the local storage engine
 * @return The workflow
 */
struct workflow*
pgmoneta_storage_create_local(void);

/**
 * Create a workflow for the SSH storage engine
 * @param workflow_type The workflow type
 * @return The workflow
 */
struct workflow*
pgmoneta_storage_create_ssh(int workflow_type);

/**
 * Create a workflow for the S3 storage engine
 * @return The workflow
 */
struct workflow*
pgmoneta_storage_create_s3(void);

/**
 * Create a workflow for the Azure storage engine
 * @return The workflow
 */
struct workflow*
pgmoneta_storage_create_azure(void);

/**
 * Open WAL shipping file in remote ssh server
 * @param srv The server index
 * @param filename WAL file name
 * @param segsize WAL segment size
 * @param sftp_file WAL streaming file
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_sftp_wal_open(int server, char* filename, int segsize, sftp_file* file);

/**
 * Close WAL shipping file in remote ssh server
 * @param srv The server index
 * @param filename WAL file name
 * @param partial Completed segment or not
 * @param sftp_file WAL streaming file
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_sftp_wal_close(int server, char* filename, bool partial, sftp_file* file);
#ifdef __cplusplus
}
#endif

#endif
