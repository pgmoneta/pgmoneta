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
 *
 */

#ifndef PGMONETA_MCTF_SE_H
#define PGMONETA_MCTF_SE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * mctf_se: storage-engine integration test layer. mctf_se_up() starts an
 * emulated backend (e.g. Garage for S3) and a dedicated pgmoneta instance
 * configured for it; mctf_se_down() tears everything down. Tests express
 * pure intent with no container-management boilerplate.
 *
 * To add a backend: one driver file under backends/ + one registry row in
 * mctf_se.c. Tests and the orchestrator are unchanged.
 */

#include <mctf_container.h>

#include <stdbool.h>
#include <stdio.h>

/**
 * Storage backends supported by the integration-test layer.
 */
enum mctf_backend {
   MCTF_BACKEND_GARAGE = 0, /**< S3, emulated by Garage */
   MCTF_BACKEND_AZURITE = 1 /**< Azure Blob Storage, emulated by Azurite */
};

/**
 * Shared context for one active backend, filled by the driver's start()
 * and consumed by write_server_conf(). Fields cover object-storage and
 * remote backends; a driver uses only what applies to it.
 */
struct mctf_se
{
   const struct mctf_se_driver* driver; /**< The active driver */
   struct mctf_container container;     /**< The backend container */

   /* Resolved connection details (filled by driver->start). */
   char endpoint[128];   /**< Service host/endpoint */
   int port;             /**< Service port */
   bool use_tls;         /**< Whether the endpoint uses TLS */
   char region[64];      /**< Region (object storage) */
   char bucket[128];     /**< Bucket / container / share */
   char access_key[256]; /**< Access key / account name */
   char secret_key[256]; /**< Secret key / account key */
};

/**
 * The driver contract. One instance per backend, registered in mctf_se.c.
 */
struct mctf_se_driver
{
   const char* name;           /**< Short name, e.g. "garage" */
   const char* storage_engine; /**< pgmoneta storage_engine value, e.g. "s3" */

   /**
    * Bring the backend up (typically via MCTF_START_CONTAINER), provision
    * it, and fill the connection details in @p s.
    * @return MCTF_OK on success, otherwise MCTF_FAIL
    */
   int (*start)(struct mctf_se* s);

   /**
    * Tear the backend down. Idempotent and best-effort.
    */
   void (*stop)(struct mctf_se* s);

   /**
    * Emit backend-specific global configuration lines (e.g. azure_* keys)
    * into the [pgmoneta] section of the managed pgmoneta.conf.
    * Optional: set to NULL when all config belongs in the server section.
    * @return MCTF_OK on success, otherwise MCTF_FAIL
    */
   int (*write_global_conf)(struct mctf_se* s, FILE* f);

   /**
    * Emit backend-specific per-server configuration lines (e.g. s3_* keys)
    * into the [primary] section of the managed pgmoneta.conf.
    * Optional: set to NULL when all config belongs in the global section.
    * @return MCTF_OK on success, otherwise MCTF_FAIL
    */
   int (*write_server_conf)(struct mctf_se* s, FILE* f);
};

/**
 * Bring up a backend and a dedicated pgmoneta instance configured for it.
 *
 * @param backend The backend to start
 * @return MCTF_OK on success; MCTF_SKIPPED when the test environment or a
 *         container engine is unavailable (the test should MCTF_SKIP);
 *         MCTF_FAIL on error
 */
int
mctf_se_up(enum mctf_backend backend);

/**
 * Tear down the managed pgmoneta instance and the backend. Idempotent.
 */
void
mctf_se_down(void);

/**
 * Run "pgmoneta-cli -c <managed cli conf> <args>" against the managed
 * instance and return the process exit code.
 *
 * @param args The pgmoneta-cli arguments, e.g. "backup primary"
 * @param output Optional captured output (caller frees); may be NULL
 * @return The pgmoneta-cli exit code (0 on success), or MCTF_FAIL if not started
 */
int
mctf_se_cli(const char* args, char** output);

/**
 * Take a backup on @p server through the managed instance.
 * @return 0 on success, otherwise non-zero
 */
int
mctf_se_backup(const char* server);

/**
 * Restore a backup through the managed instance.
 * @param server The server name
 * @param backup_id The backup id ("newest", "oldest", or a label)
 * @param directory Target directory, or NULL for the test restore dir
 * @return 0 on success, otherwise non-zero
 */
int
mctf_se_restore(const char* server, const char* backup_id, const char* directory);

/**
 * List backups (JSON) on @p server through the managed instance.
 * @return 0 on success, otherwise non-zero
 */
int
mctf_se_list_backup(const char* server, char** output);

/**
 * List S3 objects for a specific backup label (JSON output).
 * @param server The server name
 * @param label The backup label
 * @param output Captured JSON (caller frees); may be NULL
 * @return 0 on success, otherwise non-zero
 */
int
mctf_se_s3_ls(const char* server, const char* label, char** output);

/**
 * Delete a backup through the managed instance.
 * @param server The server name
 * @param label The backup label ("newest", "oldest", or a timestamp label)
 * @return 0 on success, otherwise non-zero
 */
int
mctf_se_delete(const char* server, const char* label);

/**
 * Whether the managed instance has local metadata for at least one backup
 * of @p server. The local catalog must always be present and authoritative,
 * even when the data lives only on the remote backend.
 */
bool
mctf_se_has_local_metadata(const char* server);

/**
 * The private working directory of the managed instance (advanced use).
 * Empty string if no backend is active.
 */
const char*
mctf_se_run_dir(void);

/**
 * The active backend context (advanced assertions on connection details).
 * NULL if no backend is active.
 */
const struct mctf_se*
mctf_se_context(void);

/**
 * Count the number of blobs in the active Azurite container by querying
 * the Blob Storage REST API directly using SharedKey authentication.
 * This is the Azure equivalent of `pgmoneta-cli s3 ls`.
 *
 * @return Number of blobs (>= 0), or -1 on error / no active backend
 */
int
mctf_se_azure_blob_count(void);

#ifdef __cplusplus
}
#endif

#endif
