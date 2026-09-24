# Benchmarks

pgmoneta records the elapsed time of every phase of a backup. The benchmark harness runs a workload a
fixed number of times, reads those timings, and compares two branches, so that a change claiming to
improve performance can show it.

Benchmarks answer *how long*; tests answer *pass or fail*. For correctness testing see
[TEST.md](https://github.com/pgmoneta/pgmoneta/blob/main/doc/TEST.md).

## Dependencies

docker or podman, and the PostgreSQL test image. The harness does not build the image; run
`<PATH_TO_PGMONETA>/test/check.sh` once first, which creates it.

## Running benchmarks

Measure a branch, switch, measure again, then compare:

```
git switch main
<PATH_TO_PGMONETA>/benchmarks/bench.sh run -s 25

git switch my-branch
<PATH_TO_PGMONETA>/benchmarks/bench.sh run -s 25

<PATH_TO_PGMONETA>/benchmarks/bench.sh compare main my-branch -c backup_azure
```

```
case         : backup_azure
iterations   : 5 (median)
build        : Release
machine      : 8 cores, Linux
baseline     : main @ d2e85f2
candidate    : my-branch @ 334c261

phase                     baseline     candidate  change
--------------------  ------------  ------------  ------------
total                      12690ms        8487ms  1.50x faster
basebackup                  3274ms        3383ms  no change

phase (emulated)          baseline     candidate  change
--------------------  ------------  ------------  ------------
remote_azure                8514ms        4889ms  1.74x faster
```

| option | |
|---|---|
| `-c <case>` | run a single case |
| `-i <n>` | measured iterations, default 5 |
| `-s <n>` | seed a pgbench dataset of scale `<n>`, default 0 (none) |

`bench.sh list` shows the registered cases; `bench.sh clean` removes the build and runtime state.

Results go to `benchmarks/results/<branch>/<case>.<timestamp>.json` and are **not** committed: they
describe one machine at one moment. Paste the `compare` output into the pull request instead.

## Choosing a dataset

Without `-s`, the harness backs up a bare `initdb` cluster, roughly 40 MB of mostly tiny files. That
exercises the code path but measures poorly: phases end up so small that ordinary jitter dominates
them, and a 5 ms wobble on a 9 ms phase reads as a large relative change.

`-s <n>` seeds a `pgbench` dataset of scale `<n>` plus a set of small tables before measuring, and
runs identically on both branches. Scale 25 gives roughly 430 MB.

Seed enough that the phase you care about takes seconds rather than milliseconds. The phases your
change does not touch will then report `no change` instead of reacting to noise. Note that per-file
upload parallelism depends on the *number* of files as much as total size, which is why the seeding
also creates many small tables.

## Reading the output

**Compare two branches measured on the same machine only.** Absolute timings depend on CPU, disk and
load, and the worker count is capped by core count. `compare` warns when the core count differs.

**Differences below 10% are reported as `no change`**, since a few percent of run-to-run variation is
normal even with a median.

**The `phase (emulated)` block is not a measurement.** Those phases upload to a local container
(Azurite, Garage, SFTP) rather than a real object store, so they show the direction of a change but
are not a figure to quote for production.

## Adding a benchmark case

Add a `.c` file under [cases](https://github.com/pgmoneta/pgmoneta/tree/main/benchmarks/cases) and use
`BENCH_CASE()`. Cases register themselves, so there is nothing else to edit.

```c
#include <bench.h>
#include <mctf_se.h>

BENCH_CASE(backup_azure, MCTF_BACKEND_AZURITE)
{
   return mctf_se_backup("primary");
}
```

The body runs one iteration of the work being measured. The harness brings the backend up, runs an
unmeasured warmup, runs the body N times, reads the per-phase timings from `backup.info`, takes the
median and writes the result. Backends are `MCTF_BACKEND_AZURITE`, `MCTF_BACKEND_GARAGE` (S3) and
`MCTF_BACKEND_SSH`.

Reported phases come from the `bench_phases` table in `benchmarks/src/bench.c`; each entry is a name
and the offset of a field in `struct backup`, so adding a phase is one line.

## The build

Benchmarks build **Release** into `build-bench/`, separate from `build/`.

This matters more than it looks: the test build enables AddressSanitizer, UndefinedBehaviorSanitizer
and `-O0`, so timings taken from it measure instrumentation rather than pgmoneta, and look entirely
plausible while doing so. `benchmarks/CMakeLists.txt` refuses to configure unless the build type is
`Release`.

## Limitations

Benchmarks are run manually, as in Apache DataFusion; they are not a CI gate, since an I/O and network
bound check on shared runners produces false positives. The unit of work is a whole backup rather than
a function, so there is no per-function timing, and every case starts a container because `mctf_se`
provides only the remote backends.

It is recommended that you run benchmarks before raising a PR that claims a performance improvement,
and attach the `compare` output to the PR description.
