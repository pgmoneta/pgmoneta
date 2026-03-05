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

#ifndef PGMONETA_PROMETHEUS_CLIENT_H
#define PGMONETA_PROMETHEUS_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <art.h>
#include <deque.h>

#include <time.h>

/** @struct prometheus_bridge
 * Defines a bridge containing discovered Prometheus metrics
 */
struct prometheus_bridge
{
   struct art* metrics; /**< Metric registry indexed by metric name */
};

/** @struct prometheus_attribute
 * Defines a Prometheus label key/value pair
 */
struct prometheus_attribute
{
   char* key;   /**< Label key */
   char* value; /**< Label value */
};

/** @struct prometheus_value
 * Defines a timestamped Prometheus sample value
 */
struct prometheus_value
{
   time_t timestamp; /**< Sample timestamp */
   char* value;      /**< Sample value as text */
};

/** @struct prometheus_attributes
 * Defines a metric definition containing labels and values
 */
struct prometheus_attributes
{
   struct deque* attributes; /**< List of struct prometheus_attribute entries */
   struct deque* values;     /**< List of struct prometheus_value entries */
};

/** @struct prometheus_metric
 * Defines a Prometheus metric with metadata and definitions
 */
struct prometheus_metric
{
   char* name;                /**< Metric name */
   char* help;                /**< Metric help text */
   char* type;                /**< Metric type (gauge, counter, etc.) */
   struct deque* definitions; /**< List of struct prometheus_attributes entries */
};

/**
 * Create a Prometheus metrics bridge
 * @param bridge The resulting bridge
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_prometheus_client_create_bridge(struct prometheus_bridge** bridge);

/**
 * Destroy a Prometheus metrics bridge
 * @param bridge The bridge
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_prometheus_client_destroy_bridge(struct prometheus_bridge* bridge);

/**
 * Fetch metrics from an endpoint and populate the bridge
 * @param endpoint The endpoint identifier
 * @param bridge The bridge
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_prometheus_client_get(int endpoint, struct prometheus_bridge* bridge);

#ifdef __cplusplus
}
#endif

#endif
