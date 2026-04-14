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

#ifndef PGMONETA_PROGRESS_H
#define PGMONETA_PROGRESS_H

#ifdef __cplusplus
extern "C" {
#endif

/* pgmoneta */
#include <art.h>

/* system */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define WORKFLOW_PROGRESS_NONE          0 /* Not reporting progress */
#define WORKFLOW_PROGRESS_RUNNING       1 /* Reporting progress */

#define NODE_PROGRESS_LIMIT_BACKUP      "progress_limit_backup"      /* Backup phase limit */
#define NODE_PROGRESS_LIMIT_COMPRESSION "progress_limit_compression" /* Compression phase limit */
#define NODE_PROGRESS_LIMIT_ENCRYPTION  "progress_limit_encryption"  /* Encryption phase limit */
#define NODE_PROGRESS_LIMIT_LINK        "progress_limit_link"        /* Link phase limit */
#define NODE_PROGRESS_LIMIT_MANIFEST    "progress_limit_manifest"    /* Manifest phase limit */
#define NODE_PROGRESS_LIMIT_SHA512      "progress_limit_sha512"      /* SHA512 phase limit */

struct workflow;

/** @struct progress
 * The progress of a workflow
 */
struct progress
{
   atomic_int state;            /**< The progress state */
   atomic_int workflow_type;    /**< The workflow type (WORKFLOW_TYPE_*) */
   atomic_int current_phase;    /**< The current phase (PHASE_*) */
   atomic_llong done;           /**< Units of work completed (bytes or files) */
   atomic_llong total;          /**< Total units of work for current phase */
   atomic_llong start_time;     /**< The start time */
   atomic_llong elapsed;        /**< The elapsed time */
   atomic_int percentage;       /**< The overall percentage 0-100 */
   atomic_int prev_phase_limit; /**< Previous phase cumulative percentage */
   atomic_int phase_limit;      /**< Current phase cumulative percentage */
};

/**
 * Map a workflow step display name to a progress phase
 * @param name The step name from the workflow
 * @return The phase constant, or -1 if not tracked
 */
int
pgmoneta_progress_phase_from_workflow_name(char* name);

/**
 * ART node key storing the cumulative percentage limit for a phase
 * @param phase The phase constant
 * @return The key string, or NULL if unknown
 */
char*
pgmoneta_progress_limit_node_key(int phase);

/**
 * Is progress enabled for a server
 * @param server The server
 * @return True if enabled, otherwise false
 */
bool
pgmoneta_is_progress_enabled(int server);

/**
 * Initialize progress tracking for a workflow
 * @param server The server index
 * @param workflow The workflow chain
 * @param nodes The art nodes
 * @param workflow_type The workflow type
 */
void
pgmoneta_progress_setup(int server, struct workflow* workflow, struct art* nodes, int workflow_type);

/**
 * Move to the next progress phase
 * @param server The server index
 * @param phase The phase
 * @param nodes The art nodes
 */
void
pgmoneta_progress_next_phase(int server, int phase, struct art* nodes);

/**
 * Set the total count for the current progress phase
 * @param server The server index
 * @param total The total count
 */
void
pgmoneta_progress_set_total(int server, int64_t total);

/**
 * Get the total count for the current progress phase
 * @param server The server index
 * @return The total count
 */
int64_t
pgmoneta_progress_get_total(int server);

/**
 * Set the absolute done count for the current progress phase
 * @param server The server index
 * @param done The done count
 */
void
pgmoneta_progress_update_done(int server, int64_t done);

/**
 * Calculate the remaining time (in seconds) for a server's progress
 * @param server The server index
 * @return The remaining time in seconds, or 0
 */
int64_t
pgmoneta_progress_remaining(int server);

/**
 * Report progress for a server
 * @param server The server index
 */
void
pgmoneta_progress_report(int server);

/**
 * Increment the done count for progress tracking
 * @param server The server index
 * @param amount The amount to increment by
 */
void
pgmoneta_progress_increment(int server, int64_t amount);

/**
 * Complete and reset progress tracking
 * @param server The server index
 */
void
pgmoneta_progress_teardown(int server);

#ifdef __cplusplus
}
#endif

#endif
