/*
 * Copyright (C) 2026 The pgmoneta community
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

#ifndef PGMONETA_SE_OBJECT_H
#define PGMONETA_SE_OBJECT_H

#ifdef __cplusplus
extern "C" {
#endif

/* pgmoneta */
#include <pgmoneta.h>
#include <http.h>

/** @struct object_storage_ops
 * The per-backend operations the shared restore path needs
 */
struct object_storage_ops
{
   const char* name; /**< Backend name, used in log messages */
   /**
    * Fetch a single object, streaming through response->write_cb if it is set
    * @param server The server index
    * @param root The remote base path of the backup
    * @param relative_path The object path below root
    * @param response The HTTP response
    * @return 0 on success, otherwise 1
    */
   int (*get_object)(int server, char* root, char* relative_path, struct http_response** response);
};

/**
 * Download the backup metadata files and verify backup.info against its SHA512
 * @param ops The backend operations
 * @param root The remote base path of the backup
 * @param server The server index
 * @param local_root The local backup directory
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_object_bootstrap(const struct object_storage_ops* ops, char* root, int server, char* local_root);

/**
 * Download every file listed in the backup manifest using the server's worker pool
 * @param ops The backend operations
 * @param root The remote base path of the backup
 * @param local_root The local backup directory
 * @param server The server index
 * @param compression The compression method of the backup
 * @param encryption The encryption method of the backup
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_object_download_files(const struct object_storage_ops* ops, char* root, char* local_root,
                               int server, int compression, int encryption);

#ifdef __cplusplus
}
#endif

#endif
