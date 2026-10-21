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
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#ifndef PGMONETA_TEST_FAULT_INJECT_H
#define PGMONETA_TEST_FAULT_INJECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <mctf.h>

#include <signal.h>
#include <sys/resource.h>

/*
 * Fault injection for the "a failed final write must not look like success"
 * tests. C stdio only writes the last, partial buffer at fflush()/fclose(), so
 * RLIMIT_FSIZE set just below the final file size makes the earlier writes
 * succeed and only that last write fail with EFBIG. This is how the compressor,
 * decompressor and (de)cryptor close-check regressions are guarded.
 *
 * Usage, inside an MCTF_TEST_NEGATIVE with a `cleanup:` label:
 *
 *     struct rlimit orig;
 *     void (*old)(int) = SIG_ERR;
 *
 *     PGMONETA_FAULT_BEGIN(orig, old);
 *     ... produce `output` once to learn its size, remove it, re-create input ...
 *     PGMONETA_ASSERT_TRUNCATED_WRITE_FAILS(orig, size, op(input, output),
 *                                           input, output, "lz4");
 *     PGMONETA_FAULT_END(orig, old);
 *
 * The BEGIN/END pair must bracket the asserts; END is also safe to call again
 * in the cleanup block (it restores the original limit and signal handler).
 */

#define PGMONETA_FAULT_BEGIN(orig, old_handler)                                             \
   do                                                                                       \
   {                                                                                        \
      MCTF_ASSERT_INT_EQ(getrlimit(RLIMIT_FSIZE, &(orig)), 0, cleanup, "getrlimit failed"); \
      (old_handler) = signal(SIGXFSZ, SIG_IGN);                                             \
   }                                                                                        \
   while (0)

#define PGMONETA_FAULT_END(orig, old_handler) \
   do                                         \
   {                                          \
      setrlimit(RLIMIT_FSIZE, &(orig));       \
      if ((old_handler) != SIG_ERR)           \
      {                                       \
         signal(SIGXFSZ, (old_handler));      \
         (old_handler) = SIG_ERR;             \
      }                                       \
   }                                          \
   while (0)

/*
 * Run `call` (an int expression, e.g. a compress/encrypt call) with the file
 * size capped just under `bytes` so only its final buffered write fails, restore
 * the original limit `orig`, then assert the operation (1) reported failure,
 * (2) kept its source `src` and (3) left no truncated `out`. `label` names the
 * operation in the assertion messages. Jumps to `cleanup` on any failure.
 */
#define PGMONETA_ASSERT_TRUNCATED_WRITE_FAILS(orig, bytes, call, src, out, label)                         \
   do                                                                                                     \
   {                                                                                                      \
      struct rlimit pgm_fi_lim = (orig);                                                                  \
      int pgm_fi_ret;                                                                                     \
      pgm_fi_lim.rlim_cur = (rlim_t)(bytes) / 4096 * 4096;                                                \
      if (pgm_fi_lim.rlim_cur == (rlim_t)(bytes))                                                         \
      {                                                                                                   \
         pgm_fi_lim.rlim_cur = (rlim_t)(bytes) - 1;                                                       \
      }                                                                                                   \
      MCTF_ASSERT_INT_EQ(setrlimit(RLIMIT_FSIZE, &pgm_fi_lim), 0, cleanup, "setrlimit failed");           \
      pgm_fi_ret = (call);                                                                                \
      setrlimit(RLIMIT_FSIZE, &(orig));                                                                   \
      MCTF_ASSERT(pgm_fi_ret != 0, cleanup, "%s: a failed final write was reported as success", (label)); \
      MCTF_ASSERT(pgmoneta_exists(src), cleanup, "%s: the source was deleted", (label));                  \
      MCTF_ASSERT(!pgmoneta_exists(out), cleanup, "%s: a truncated output was kept", (label));            \
   }                                                                                                      \
   while (0)

#ifdef __cplusplus
}
#endif

#endif
