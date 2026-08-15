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

#ifndef PGMONETA_JOB_H
#define PGMONETA_JOB_H

#ifdef __cplusplus
extern "C" {
#endif

/* system */
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>
#include <openssl/ssl.h>

struct json;

#define JOB_STATE_NONE          0 /**< No job in the server slot */
#define JOB_STATE_STARTING      1 /**< The job is being started */
#define JOB_STATE_RUNNING       2 /**< The job is running */
#define JOB_STATE_COMPLETED     3 /**< The job completed successfully */
#define JOB_STATE_FAILED        4 /**< The job failed */

#define JOB_FILE_PREFIX         "job-"  /**< The job file prefix */
#define JOB_FILE_EXTENSION      ".json" /**< The job file extension */
#define JOB_FILE_TEMP_EXTENSION ".tmp"  /**< The job file temporary extension */

/**
 * @brief Initializes a job structure for a given server and workflow type.
 * 
 * @param server_id The server index
 * @param workflow_type The workflow type associated with the job
 * @return 0 if the job was initialized successfully, -1 otherwise
 */
int
pgmoneta_job_init(int server_id, int workflow_type);

/**
 * @brief Checks if a job is active for a given server
 * 
 * @param server The server index 
 * @return true if a job is active for the server, false otherwise
 */
bool
pgmoneta_job_is_active(int server);

/**
 * @brief Updates the current phase of a job for a given server
 * 
 * @param server The server index
 * @param current_phase The new workflow phase
 */
void
pgmoneta_job_update_phase(int server, int phase);

/**
 * @brief Updates the state of a job for a given server
 * 
 * @param server The server index
 * @param state The new job status
 */
void
pgmoneta_job_update_state(int server, int state);

/**
 * @brief Cleans up the job data for a given server
 *
 * @param server The server index
 */
void
pgmoneta_job_cleanup(int server);

/**
 * @brief Finishes the job for a given server
 * 
 * @param server The server index
 * @param payload The payload
 * @return 0 if the job was finished successfully, -1 otherwise
 */
int
pgmoneta_job_finish(int server, struct json* payload);

/**
 * @brief Adds asynchronous job data to the response for a given server
 * 
 * @param server The server index
 * @param response The response JSON structure to be filled
 * @return 0 if the response was filled successfully, -1 otherwise
 */
int
pgmoneta_job_add_async_data(int server, struct json* response);

/**
 * @brief Handles a job request
 *
 * @param ssl The SSL connection
 * @param client_fd The client
 * @param compression The compression method
 * @param encryption The encryption method
 * @param payload The payload
 */
void
pgmoneta_job_rq(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload);

#ifdef __cplusplus
}
#endif

#endif