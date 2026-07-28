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

/* bench */
#include <bench.h>

/* pgmoneta */
#include <pgmoneta.h>
#include <info.h>
#include <logging.h>
#include <utils.h>

/* test harness */
#include <mctf_se.h>

/* system */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Recorded by pgmoneta on every backup; remote_* runs against an emulated
 * backend, so it is reported but never treated as signal.
 */
const struct bench_phase bench_phases[] = {
   {"total", offsetof(struct backup, total_elapsed_time), false},
   {"basebackup", offsetof(struct backup, basebackup_elapsed_time), false},
   {"manifest", offsetof(struct backup, manifest_elapsed_time), false},
   {"hash", offsetof(struct backup, hash_elapsed_time), false},
   {"compression_gzip", offsetof(struct backup, compression_gzip_elapsed_time), false},
   {"compression_zstd", offsetof(struct backup, compression_zstd_elapsed_time), false},
   {"compression_lz4", offsetof(struct backup, compression_lz4_elapsed_time), false},
   {"compression_bzip2", offsetof(struct backup, compression_bzip2_elapsed_time), false},
   {"encryption", offsetof(struct backup, encryption_elapsed_time), false},
   {"linking", offsetof(struct backup, linking_elapsed_time), false},
   {"remote_ssh", offsetof(struct backup, remote_ssh_elapsed_time), true},
   {"remote_s3", offsetof(struct backup, remote_s3_elapsed_time), true},
   {"remote_azure", offsetof(struct backup, remote_azure_elapsed_time), true},
   {NULL, 0, false}
};

struct bench_case
{
   char name[BENCH_NAME_LENGTH];
   int backend;
   bench_func_t func;
};

static struct bench_case cases[BENCH_MAX_CASES];
static int number_of_cases = 0;

static int newest_backup_label(const char* server, char* out, size_t size);
static int collect(const char* server, double* out, char* label_out, size_t label_size);
static int compare_double(const void* a, const void* b);
static int run_case(struct bench_case* c, int iterations, const char* branch,
                    const char* commit, const char* results_dir);

void
bench_register_case(const char* name, int backend, bench_func_t func)
{
   if (number_of_cases >= BENCH_MAX_CASES)
   {
      fprintf(stderr, "bench: too many cases, ignoring %s\n", name);
      return;
   }

   if (strlen(name) >= BENCH_NAME_LENGTH)
   {
      fprintf(stderr, "bench: case name too long: %s\n", name);
      return;
   }

   pgmoneta_snprintf(cases[number_of_cases].name, BENCH_NAME_LENGTH, "%s", name);
   cases[number_of_cases].backend = backend;
   cases[number_of_cases].func = func;
   number_of_cases++;
}

void
bench_list(void)
{
   printf("Registered cases (%d):\n", number_of_cases);

   for (int i = 0; i < number_of_cases; i++)
   {
      printf("  %s\n", cases[i].name);
   }
}

int
bench_run(const char* filter, int iterations, const char* branch,
          const char* commit, const char* results_dir)
{
   int selected = 0;
   int failed = 0;

   if (iterations < 1 || iterations > BENCH_MAX_ITERATIONS)
   {
      fprintf(stderr, "bench: iterations must be between 1 and %d\n", BENCH_MAX_ITERATIONS);
      return BENCH_FAIL;
   }

   for (int i = 0; i < number_of_cases; i++)
   {
      if (filter != NULL && strcmp(filter, cases[i].name) != 0)
      {
         continue;
      }

      selected++;

      if (run_case(&cases[i], iterations, branch, commit, results_dir) != BENCH_OK)
      {
         failed++;
      }
   }

   if (number_of_cases == 0)
   {
      fprintf(stderr, "bench: no cases are registered; add one under benchmarks/cases\n");
      return BENCH_FAIL;
   }

   if (selected == 0)
   {
      fprintf(stderr, "bench: no case named '%s'\n", filter);
      return BENCH_FAIL;
   }

   return failed == 0 ? BENCH_OK : BENCH_FAIL;
}

static int
run_case(struct bench_case* c, int iterations, const char* branch,
         const char* commit, const char* results_dir)
{
   int np = bench_number_of_phases();
   double* samples = NULL;
   double* values = NULL;
   int rc = BENCH_FAIL;
   int up = 0;
   char previous_label[MISC_LENGTH];

   memset(&previous_label[0], 0, sizeof(previous_label));

   printf("\ncase: %s (%d iterations)\n", c->name, iterations);
   fflush(stdout);

   /* Indexed [phase * iterations + iteration] */
   samples = (double*)calloc((size_t)np * iterations, sizeof(double));
   values = (double*)calloc((size_t)np, sizeof(double));
   if (samples == NULL || values == NULL)
   {
      goto error;
   }

   up = mctf_se_up(c->backend);
   if (up == MCTF_SKIPPED)
   {
      printf("  skipped: backend unavailable\n");
      rc = BENCH_SKIPPED;
      goto done;
   }
   if (up != MCTF_OK)
   {
      fprintf(stderr, "  failed: could not start backend\n");
      goto error;
   }

   /* Unmeasured: the first backup of a fresh cluster pays for page cache
    * misses that later runs do not.
    */
   printf("  warmup ...\n");
   fflush(stdout);
   if (c->func() != 0)
   {
      fprintf(stderr, "  failed: warmup iteration failed\n");
      goto error;
   }

   if (collect("primary", values, &previous_label[0], sizeof(previous_label)) != BENCH_OK)
   {
      fprintf(stderr, "  failed: warmup produced no backup\n");
      goto error;
   }

   for (int i = 0; i < iterations; i++)
   {
      char label[MISC_LENGTH];

      printf("  iteration %d/%d ...\n", i + 1, iterations);
      fflush(stdout);

      if (c->func() != 0)
      {
         fprintf(stderr, "  failed: iteration %d failed\n", i + 1);
         goto error;
      }

      memset(values, 0, sizeof(double) * np);
      memset(&label[0], 0, sizeof(label));

      if (collect("primary", values, &label[0], sizeof(label)) != BENCH_OK)
      {
         fprintf(stderr, "  failed: could not read backup.info\n");
         goto error;
      }

      /*
       * The iteration must have produced its own backup. Without this the
       * harness would silently report timings left behind by an earlier run.
       */
      if (!strcmp(&label[0], &previous_label[0]))
      {
         fprintf(stderr, "  failed: iteration %d produced no new backup (still %s)\n",
                 i + 1, &label[0]);
         goto error;
      }
      pgmoneta_snprintf(&previous_label[0], sizeof(previous_label), "%s", &label[0]);

      for (int p = 0; p < np; p++)
      {
         samples[p * iterations + i] = values[p];
      }
   }

   if (bench_report_write(results_dir, c->name, branch, commit, iterations, samples) != BENCH_OK)
   {
      fprintf(stderr, "  failed: could not write result\n");
      goto error;
   }

   rc = BENCH_OK;
   goto done;

error:

   rc = BENCH_FAIL;

done:

   if (up == MCTF_OK)
   {
      mctf_se_down();
   }

   free(samples);
   free(values);

   return rc;
}

/* Read the timings of the newest backup into out, in milliseconds */
static int
collect(const char* server, double* out, char* label_out, size_t label_size)
{
   char directory[MAX_PATH];
   char label[MISC_LENGTH];
   struct backup* backup = NULL;

   pgmoneta_snprintf(directory, sizeof(directory), "%s/backup/%s/backup/",
            mctf_se_run_dir(), server);

   if (newest_backup_label(server, &label[0], sizeof(label)) != BENCH_OK)
   {
      goto error;
   }

   if (pgmoneta_load_info(&directory[0], &label[0], &backup) || backup == NULL)
   {
      goto error;
   }

   for (int p = 0; bench_phases[p].name != NULL; p++)
   {
      double seconds = *(double*)((char*)backup + bench_phases[p].offset);

      out[p] = seconds * 1000.0;
   }

   pgmoneta_snprintf(label_out, label_size, "%s", &label[0]);

   free(backup);

   return BENCH_OK;

error:

   free(backup);

   return BENCH_FAIL;
}

static int
newest_backup_label(const char* server, char* out, size_t size)
{
   char directory[MAX_PATH];
   char** dirs = NULL;
   int number_of_dirs = 0;
   int best = -1;

   pgmoneta_snprintf(directory, sizeof(directory), "%s/backup/%s/backup",
            mctf_se_run_dir(), server);

   pgmoneta_get_directories(&directory[0], &number_of_dirs, &dirs);

   if (number_of_dirs <= 0 || dirs == NULL)
   {
      return BENCH_FAIL;
   }

   /* Labels are timestamps, so the greatest is the newest */
   for (int i = 0; i < number_of_dirs; i++)
   {
      if (best < 0 || strcmp(dirs[i], dirs[best]) > 0)
      {
         best = i;
      }
   }

   pgmoneta_snprintf(out, size, "%s", dirs[best]);

   for (int i = 0; i < number_of_dirs; i++)
   {
      free(dirs[i]);
   }
   free(dirs);

   return out[0] != '\0' ? BENCH_OK : BENCH_FAIL;
}

int
bench_number_of_phases(void)
{
   int n = 0;

   while (bench_phases[n].name != NULL)
   {
      n++;
   }

   return n;
}

static int
compare_double(const void* a, const void* b)
{
   double x = *(const double*)a;
   double y = *(const double*)b;

   if (x < y)
   {
      return -1;
   }
   if (x > y)
   {
      return 1;
   }

   return 0;
}

double
bench_median(double* values, int n)
{
   double* copy = NULL;
   double result = 0.0;

   if (values == NULL || n <= 0)
   {
      return 0.0;
   }

   copy = (double*)malloc(sizeof(double) * n);
   if (copy == NULL)
   {
      return 0.0;
   }

   memcpy(copy, values, sizeof(double) * n);
   qsort(copy, n, sizeof(double), compare_double);

   if (n % 2 == 1)
   {
      result = copy[n / 2];
   }
   else
   {
      result = (copy[n / 2 - 1] + copy[n / 2]) / 2.0;
   }

   free(copy);

   return result;
}
