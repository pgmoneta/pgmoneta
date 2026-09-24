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
#include <json.h>
#include <utils.h>

/* system */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#ifdef HAVE_LINUX
#include <sys/sysinfo.h>
#endif

/* Below this, a difference is noise rather than a result */
#define BENCH_NOCHANGE_BAND 0.10

static void sanitize(const char* in, char* out, size_t size);
static int cores(void);
static int newest_result(const char* results_dir, const char* branch,
                         const char* case_name, char* out, size_t size);
static double phase_of(struct json* obj, const char* phase, bool* present);
static void classify(double base, double cand, char* out, size_t size);
static int render(struct json* base, struct json* cand, bool emulated);
static const char* string_of(struct json* obj, const char* key);

/* Writes <results_dir>/<branch>/<case>.<timestamp>.json */
int
bench_report_write(const char* results_dir, const char* case_name,
                   const char* branch, const char* commit, int iterations,
                   double* samples)
{
   char branch_dir[MAX_PATH];
   char path[MAX_PATH];
   char safe_branch[BENCH_NAME_LENGTH];
   char stamp[32];
   time_t now;
   struct tm tm_buffer;
   struct tm* tm = NULL;
   struct utsname u;
   struct json* root = NULL;
   struct json* median = NULL;
   struct json* machine = NULL;

   sanitize(branch, &safe_branch[0], sizeof(safe_branch));

   pgmoneta_snprintf(&branch_dir[0], sizeof(branch_dir), "%s/%s", results_dir, &safe_branch[0]);
   if (pgmoneta_mkdir(&branch_dir[0]))
   {
      goto error;
   }

   now = time(NULL);
   tm = gmtime_r(&now, &tm_buffer);
   if (tm == NULL)
   {
      goto error;
   }
   strftime(&stamp[0], sizeof(stamp), "%Y%m%dT%H%M%SZ", tm);

   pgmoneta_snprintf(&path[0], sizeof(path), "%s/%s.%s.json", &branch_dir[0], case_name, &stamp[0]);

   if (pgmoneta_json_create(&root) || pgmoneta_json_create(&median) ||
       pgmoneta_json_create(&machine))
   {
      goto error;
   }

   for (int p = 0; bench_phases[p].name != NULL; p++)
   {
      double m = bench_median(&samples[p * iterations], iterations);

      /*
       * A phase that did not run reports 0. Emitting it would make compare
       * show a row for every unused compression algorithm.
       */
      if (m > 0.0)
      {
         pgmoneta_json_put(median, (char*)bench_phases[p].name,
                           pgmoneta_value_from_double(m), ValueDouble);
      }
   }

   pgmoneta_json_put(machine, "cores", (uintptr_t)cores(), ValueInt32);
   if (uname(&u) == 0)
   {
      pgmoneta_json_put(machine, "os", (uintptr_t)&u.sysname[0], ValueString);
      pgmoneta_json_put(machine, "arch", (uintptr_t)&u.machine[0], ValueString);
   }

   pgmoneta_json_put(root, "case", (uintptr_t)case_name, ValueString);
   pgmoneta_json_put(root, "branch", (uintptr_t)branch, ValueString);
   pgmoneta_json_put(root, "commit", (uintptr_t)commit, ValueString);
   pgmoneta_json_put(root, "timestamp", (uintptr_t)&stamp[0], ValueString);
   pgmoneta_json_put(root, "build_type", (uintptr_t)BENCH_BUILD_TYPE, ValueString);
   pgmoneta_json_put(root, "pgmoneta_version", (uintptr_t)PGMONETA_VERSION, ValueString);
   pgmoneta_json_put(root, "iterations", (uintptr_t)iterations, ValueInt32);
   pgmoneta_json_put(root, "machine", (uintptr_t)machine, ValueJSON);
   pgmoneta_json_put(root, "median", (uintptr_t)median, ValueJSON);

   if (pgmoneta_json_write_file(&path[0], root))
   {
      goto error;
   }

   printf("  wrote %s\n", &path[0]);

   pgmoneta_json_destroy(root);

   return BENCH_OK;

error:

   pgmoneta_json_destroy(root);

   return BENCH_FAIL;
}

int
bench_compare(const char* results_dir, const char* baseline,
              const char* candidate, const char* case_name)
{
   char base_path[MAX_PATH];
   char cand_path[MAX_PATH];
   struct json* base = NULL;
   struct json* cand = NULL;
   struct json* base_machine = NULL;
   struct json* cand_machine = NULL;

   if (newest_result(results_dir, baseline, case_name, &base_path[0], sizeof(base_path)) != BENCH_OK)
   {
      fprintf(stderr, "bench: no result for case '%s' on branch '%s'\n", case_name, baseline);
      goto error;
   }

   if (newest_result(results_dir, candidate, case_name, &cand_path[0], sizeof(cand_path)) != BENCH_OK)
   {
      fprintf(stderr, "bench: no result for case '%s' on branch '%s'\n", case_name, candidate);
      goto error;
   }

   if (pgmoneta_json_read_file(&base_path[0], &base) || base == NULL ||
       pgmoneta_json_read_file(&cand_path[0], &cand) || cand == NULL)
   {
      fprintf(stderr, "bench: cannot read result files\n");
      goto error;
   }

   base_machine = (struct json*)pgmoneta_json_get(base, "machine");
   cand_machine = (struct json*)pgmoneta_json_get(cand, "machine");

   printf("case         : %s\n", string_of(base, "case"));
   printf("iterations   : %s (median)\n", string_of(base, "iterations"));
   printf("build        : %s\n", string_of(base, "build_type"));
   if (base_machine != NULL)
   {
      printf("machine      : %s cores, %s\n",
             string_of(base_machine, "cores"), string_of(base_machine, "os"));
   }
   printf("baseline     : %s @ %s\n", string_of(base, "branch"), string_of(base, "commit"));
   printf("candidate    : %s @ %s\n", string_of(cand, "branch"), string_of(cand, "commit"));

   if (base_machine != NULL && cand_machine != NULL &&
       pgmoneta_json_get(base_machine, "cores") != pgmoneta_json_get(cand_machine, "cores"))
   {
      printf("\nWARNING: core count differs between runs; results are not comparable.\n");
   }

   render(base, cand, false);

   if (render(base, cand, true))
   {
      printf("\nEmulated phases upload to a local container (Azurite, Garage, SFTP),\n");
      printf("not a real object store. They show the direction of a change, not a\n");
      printf("figure you can quote for production.\n");
   }

   printf("\nDifferences below %d%% are reported as no change.\n",
          (int)(BENCH_NOCHANGE_BAND * 100));

   pgmoneta_json_destroy(base);
   pgmoneta_json_destroy(cand);

   return BENCH_OK;

error:

   pgmoneta_json_destroy(base);
   pgmoneta_json_destroy(cand);

   return BENCH_FAIL;
}

/* Branch names go in the header block, keeping the columns fixed width */
static int
render(struct json* base, struct json* cand, bool emulated)
{
   struct json* bm = (struct json*)pgmoneta_json_get(base, "median");
   struct json* cm = (struct json*)pgmoneta_json_get(cand, "median");
   int rows = 0;

   if (bm == NULL || cm == NULL)
   {
      return 0;
   }

   for (int p = 0; bench_phases[p].name != NULL; p++)
   {
      bool in_base = false;
      bool in_cand = false;
      double b;
      double c;
      char bs[24];
      char cs[24];
      char change[32];

      if (bench_phases[p].emulated != emulated)
      {
         continue;
      }

      b = phase_of(bm, bench_phases[p].name, &in_base);
      c = phase_of(cm, bench_phases[p].name, &in_cand);

      if (!in_base && !in_cand)
      {
         continue;
      }

      if (rows == 0)
      {
         printf("\n%-20s  %12s  %12s  %s\n",
                emulated ? "phase (emulated)" : "phase", "baseline", "candidate", "change");
         printf("--------------------  ------------  ------------  ------------\n");
      }
      rows++;

      in_base ? pgmoneta_snprintf(&bs[0], sizeof(bs), "%.0fms", b) : pgmoneta_snprintf(&bs[0], sizeof(bs), "-");
      in_cand ? pgmoneta_snprintf(&cs[0], sizeof(cs), "%.0fms", c) : pgmoneta_snprintf(&cs[0], sizeof(cs), "-");

      if (in_base && in_cand)
      {
         classify(b, c, &change[0], sizeof(change));
      }
      else
      {
         pgmoneta_snprintf(&change[0], sizeof(change), "n/a");
      }

      printf("%-20s  %12s  %12s  %s\n", bench_phases[p].name, &bs[0], &cs[0], &change[0]);
   }

   return rows;
}

static void
classify(double base, double cand, char* out, size_t size)
{
   double delta;
   double ratio;

   if (base <= 0.0 || cand <= 0.0)
   {
      pgmoneta_snprintf(out, size, "n/a");
      return;
   }

   delta = base > cand ? base - cand : cand - base;

   if (delta / base < BENCH_NOCHANGE_BAND)
   {
      pgmoneta_snprintf(out, size, "no change");
      return;
   }

   ratio = base / cand;

   if (ratio > 1.0)
   {
      pgmoneta_snprintf(out, size, "%.2fx faster", ratio);
   }
   else
   {
      pgmoneta_snprintf(out, size, "%.2fx slower", 1.0 / ratio);
   }
}

static double
phase_of(struct json* obj, const char* phase, bool* present)
{
   enum value_type type = ValueNone;
   uintptr_t raw;

   *present = false;

   if (obj == NULL || !pgmoneta_json_contains_key(obj, (char*)phase))
   {
      return 0.0;
   }

   raw = pgmoneta_json_get_typed(obj, (char*)phase, &type);
   *present = true;

   if (type == ValueDouble)
   {
      return pgmoneta_value_to_double(raw);
   }
   if (type == ValueFloat)
   {
      return (double)pgmoneta_value_to_float(raw);
   }

   return (double)(int64_t)raw;
}

/* Timestamps are ordered, so the greatest file name is the newest run */
static int
newest_result(const char* results_dir, const char* branch,
              const char* case_name, char* out, size_t size)
{
   char directory[MAX_PATH];
   char prefix[BENCH_NAME_LENGTH + 8];
   char safe_branch[BENCH_NAME_LENGTH];
   char best[MAX_PATH];
   DIR* dir = NULL;
   struct dirent* entry = NULL;

   sanitize(branch, &safe_branch[0], sizeof(safe_branch));
   pgmoneta_snprintf(&directory[0], sizeof(directory), "%s/%s", results_dir, &safe_branch[0]);
   pgmoneta_snprintf(&prefix[0], sizeof(prefix), "%s.", case_name);

   memset(&best[0], 0, sizeof(best));

   dir = opendir(&directory[0]);
   if (dir == NULL)
   {
      return BENCH_FAIL;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (!pgmoneta_starts_with(entry->d_name, &prefix[0]) ||
          !pgmoneta_ends_with(entry->d_name, ".json"))
      {
         continue;
      }

      if (best[0] == '\0' || strcmp(entry->d_name, &best[0]) > 0)
      {
         pgmoneta_snprintf(&best[0], sizeof(best), "%s", entry->d_name);
      }
   }

   closedir(dir);

   if (best[0] == '\0')
   {
      return BENCH_FAIL;
   }

   pgmoneta_snprintf(out, size, "%s/%s", &directory[0], &best[0]);

   return BENCH_OK;
}

static const char*
string_of(struct json* obj, const char* key)
{
   static char buffer[MISC_LENGTH];
   enum value_type type = ValueNone;
   uintptr_t raw;

   if (obj == NULL || !pgmoneta_json_contains_key(obj, (char*)key))
   {
      return "?";
   }

   raw = pgmoneta_json_get_typed(obj, (char*)key, &type);

   if (type == ValueString || type == ValueStringRef)
   {
      return (const char*)raw;
   }

   pgmoneta_snprintf(&buffer[0], sizeof(buffer), "%" PRId64, (int64_t)raw);

   return &buffer[0];
}

/* Branch names contain '/', which cannot be a path component */
static void
sanitize(const char* in, char* out, size_t size)
{
   size_t i = 0;

   if (in == NULL)
   {
      pgmoneta_snprintf(out, size, "unknown");
      return;
   }

   for (; in[i] != '\0' && i < size - 1; i++)
   {
      out[i] = (in[i] == '/') ? '-' : in[i];
   }

   out[i] = '\0';

   if (out[0] == '\0')
   {
      pgmoneta_snprintf(out, size, "unknown");
   }
}

static int
cores(void)
{
#ifdef HAVE_LINUX
   return get_nprocs();
#else
   return (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif
}
