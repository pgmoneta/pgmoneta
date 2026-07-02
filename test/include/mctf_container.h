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

#ifndef PGMONETA_MCTF_CONTAINER_H
#define PGMONETA_MCTF_CONTAINER_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * mctf_container: podman/docker container lifecycle for integration tests.
 * Use MCTF_START_CONTAINER / MCTF_STOP_CONTAINER; containers are tagged so
 * any leftovers from a previous crash are swept on the next run.
 */

#include <stdbool.h>
#include <stddef.h>

/* Result codes for the integration-test container/storage layers. */
#define MCTF_OK      0 /**< Success */
#define MCTF_FAIL    1 /**< Failure */
#define MCTF_SKIPPED 2 /**< Prerequisite missing (e.g. no container engine) */

/* Label applied to every container started by the test layer. */
#define MCTF_CONTAINER_LABEL "pgmoneta-mctf=1"

/**
 * Well-known container kinds the framework knows how to start.
 */
enum mctf_container_kind {
   MCTF_CONTAINER_GARAGE = 0, /**< Garage, an S3-compatible object store */
   MCTF_CONTAINER_AZURITE = 1 /**< Azurite, a local Azure Blob Storage emulator */
};

/**
 * A running container handle.
 */
struct mctf_container
{
   char engine[64]; /**< Container engine in use ("podman"/"docker") */
   char name[128];  /**< Unique container name */
   bool running;    /**< Whether the container is up */
};

/**
 * Start a well-known container kind and wait until it is ready.
 *
 * @param c Handle to populate
 * @param kind The container kind to start
 * @return MCTF_OK on success; MCTF_SKIPPED if no container engine exists;
 *         MCTF_FAIL on error
 */
#define MCTF_START_CONTAINER(c, kind) mctf_container_start((c), (kind))

/**
 * Stop and remove a running container (idempotent).
 *
 * @param c The container handle
 */
#define MCTF_STOP_CONTAINER(c) mctf_container_stop((c))

/**
 * Run a shell command (printf-style), returning the child exit code.
 * Combined stdout/stderr is captured in *output when non-NULL (caller frees).
 *
 * @param output Optional captured output (caller frees); may be NULL
 * @param fmt printf-style command format
 * @return The child exit code, or MCTF_FAIL on launch failure
 */
int
mctf_sh(char** output, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

/**
 * Detect an available container engine.
 *
 * @param out Buffer receiving the engine name ("podman" or "docker")
 * @param size Size of out
 * @return MCTF_OK if found; MCTF_SKIPPED otherwise
 */
int
mctf_container_engine(char* out, size_t size);

/**
 * Start a well-known container kind (see MCTF_START_CONTAINER).
 *
 * @param c Handle to populate
 * @param kind The container kind
 * @return MCTF_OK, MCTF_SKIPPED, or MCTF_FAIL
 */
int
mctf_container_start(struct mctf_container* c, enum mctf_container_kind kind);

/**
 * Pull a container image with retries (best-effort; a cached image is
 * accepted even on failure).
 *
 * @param engine The container engine ("podman"/"docker")
 * @param image Container image reference
 * @param retries Pull attempts
 * @return MCTF_OK if the pull succeeded, otherwise MCTF_FAIL
 */
int
mctf_container_pull(const char* engine, const char* image, int retries);

/**
 * Start an arbitrary container image and wait until a readiness command
 * succeeds. Used by the kind-specific starters and by backend drivers.
 *
 * @param c Handle to populate (engine must already be set)
 * @param name Unique container name
 * @param image Container image reference
 * @param run_args Additional "run" arguments (options + image trailer)
 * @param ready_cmd Command run inside the container to test readiness (may be NULL)
 * @param retries Readiness attempts (one per second)
 * @return MCTF_OK on success, otherwise MCTF_FAIL
 */
int
mctf_container_run(struct mctf_container* c, const char* name, const char* image,
                   const char* run_args, const char* ready_cmd, int retries);

/**
 * Execute a command inside a running container, capturing output.
 *
 * @param c The container handle
 * @param cmd The command to run inside the container
 * @param output Optional captured output (caller frees); may be NULL
 * @return The command exit code, or MCTF_FAIL on launch failure
 */
int
mctf_container_exec(struct mctf_container* c, const char* cmd, char** output);

/**
 * Stop and remove a container (idempotent, best-effort).
 *
 * @param c The container handle
 */
void
mctf_container_stop(struct mctf_container* c);

/**
 * Remove every container carrying MCTF_CONTAINER_LABEL (sweep leaks from a
 * previous crashed run).
 *
 * @param engine The container engine
 */
void
mctf_container_sweep(const char* engine);

/**
 * Resolve the project root from the running test binary path.
 *
 * @param out Buffer for the path
 * @param size Size of out
 * @return MCTF_OK on success, otherwise MCTF_FAIL
 */
int
mctf_project_root(char* out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
