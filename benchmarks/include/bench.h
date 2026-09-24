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

#ifndef PGMONETA_BENCH_H
#define PGMONETA_BENCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#define BENCH_OK      0
#define BENCH_FAIL    1
#define BENCH_SKIPPED 2

#define BENCH_MAX_CASES          32
#define BENCH_MAX_ITERATIONS    100
#define BENCH_DEFAULT_ITERATIONS  5
#define BENCH_NAME_LENGTH        64

/** @struct bench_phase
 * Defines a measured phase
 */
struct bench_phase
{
   const char* name; /**< The phase name */
   size_t offset;    /**< The field offset */
   bool emulated;    /**< The emulated flag */
};

/** The measured phases, NULL terminated */
extern const struct bench_phase bench_phases[];

/** The case body */
typedef int (*bench_func_t)(void);

/**
 * Get the number of phases
 * @return The number of phases
 */
int
bench_number_of_phases(void);

/**
 * Get the median of a sample set
 * @param values The samples
 * @param n The number of samples
 * @return The median, or 0.0 if there are no samples
 */
double
bench_median(double* values, int n);

/**
 * Register a case
 * @param name The case name
 * @param backend The backend
 * @param func The case body
 */
void
bench_register_case(const char* name, int backend, bench_func_t func);

/**
 * Define and register a benchmark case
 *
 * The body runs one iteration of the work to measure; the harness handles
 * setup, repetition, collection and teardown.
 *
 * Usage: BENCH_CASE(backup_azure, MCTF_BACKEND_AZURITE) { ... }
 */
#define BENCH_CASE(name, backend)                                       \
   static int name(void);                                               \
   static void __attribute__((constructor)) bench_register_##name(void) \
   {                                                                    \
      bench_register_case(#name, (backend), name);                      \
   }                                                                    \
   static int name(void)

/**
 * Run the cases and write a result per case
 * @param filter The case name, or NULL for all
 * @param iterations The number of iterations
 * @param branch The branch
 * @param commit The commit
 * @param results_dir The results directory
 * @return BENCH_OK upon success, otherwise BENCH_FAIL
 */
int
bench_run(const char* filter, int iterations, const char* branch,
          const char* commit, const char* results_dir);

/**
 * Compare the latest result for a case on two branches
 * @param results_dir The results directory
 * @param baseline The baseline branch
 * @param candidate The candidate branch
 * @param case_name The case name
 * @return BENCH_OK upon success, otherwise BENCH_FAIL
 */
int
bench_compare(const char* results_dir, const char* baseline,
              const char* candidate, const char* case_name);

/**
 * Print the registered cases
 */
void
bench_list(void);

/**
 * Write a result file
 * @param results_dir The results directory
 * @param case_name The case name
 * @param branch The branch
 * @param commit The commit
 * @param iterations The number of iterations
 * @param samples The samples, indexed [phase * iterations + iteration]
 * @return BENCH_OK upon success, otherwise BENCH_FAIL
 */
int
bench_report_write(const char* results_dir, const char* case_name,
                   const char* branch, const char* commit, int iterations,
                   double* samples);

#ifdef __cplusplus
}
#endif

#endif
