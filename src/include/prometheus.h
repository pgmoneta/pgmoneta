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

#ifndef PGMONETA_PROMETHEUS_H
#define PGMONETA_PROMETHEUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>

#include <ev.h>
#include <stdlib.h>
#include <openssl/ssl.h>

/*
 * Value to disable the Prometheus cache,
 * it is equivalent to set `metrics_cache`
 * to 0 (seconds).
 */
#define PGMONETA_PROMETHEUS_CACHE_DISABLED 0

/**
 * Max size of the cache (in bytes).
 * If the cache request exceeds this size
 * the caching should be aborted in some way.
 */
#define PROMETHEUS_MAX_CACHE_SIZE (1024 * 1024)

/**
 * The default cache size in the case
 * the user did not set any particular
 * configuration option.
 */
#define PROMETHEUS_DEFAULT_CACHE_SIZE (256 * 1024)

/**
 * Create a prometheus instance
 * @param client_ssl The client SSL structure
 * @param fd The client descriptor
 */
void
pgmoneta_prometheus(SSL* client_ssl, int fd);

/**
 * Reset the counters and histograms
 */
void
pgmoneta_prometheus_reset(void);

/**
 * Allocates, for the first time, the Prometheus cache.
 *
 * The cache structure, as well as its dynamically sized payload,
 * are created as shared memory chunks.
 *
 * Assumes the shared memory for the cofiguration is already set.
 *
 * The cache will be allocated as soon as this method is invoked,
 * even if the cache has not been configured at all!
 *
 * If the memory cannot be allocated, the function issues errors
 * in the logs and disables the caching machinaery.
 *
 * @param p_size a pointer to where to store the size of
 * allocated chunk of memory
 * @param p_shmem the pointer to the pointer at which the allocated chunk
 * of shared memory is going to be inserted
 *
 * @return 0 on success
 */
int
pgmoneta_init_prometheus_cache(size_t* p_size, void** p_shmem);

/**
 * Add a logging count
 * @param logging The logging type
 */
void
pgmoneta_prometheus_logging(int logging);

#ifdef __cplusplus
}
#endif

#endif
