#!/bin/bash
#
#  Copyright (C) 2026 The pgmoneta community
#
#  Redistribution and use in source and binary forms, with or without modification,
#  are permitted provided that the following conditions are met:
#
#  1. Redistributions of source code must retain the above copyright notice, this list
#  of conditions and the following disclaimer.
#
#  2. Redistributions in binary form must reproduce the above copyright notice, this
#  list of conditions and the following disclaimer in the documentation and/or other
#  materials provided with the distribution.
#
#  3. Neither the name of the copyright holder nor the names of its contributors may
#  be used to endorse or promote products derived from this software without specific
#  prior written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
#  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
#  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
#  THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
#  OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
#  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
#  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

set -uo pipefail

SCRIPT_DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
PROJECT_DIR="$(realpath "$SCRIPT_DIR/..")"

BUILD_DIR="$PROJECT_DIR/build-bench"
RESULTS_DIR="${BENCH_RESULTS_DIR:-$SCRIPT_DIR/results}"
BENCH_BIN="$BUILD_DIR/benchmarks/pgmoneta-bench"

PG_VERSION="${TEST_PG_VERSION:-17}"
IMAGE_NAME="pgmoneta-test-postgresql$PG_VERSION-rocky10"
CONTAINER_NAME="pgmoneta-bench-postgresql$PG_VERSION"
TEST_PG_DIRECTORY="$PROJECT_DIR/test/postgresql/src/postgresql$PG_VERSION"

ROOT_DIR="/tmp/pgmoneta-bench"
BASE_DIR="$ROOT_DIR/base"
LOG_DIR="$ROOT_DIR/log"
PG_LOG_DIR="$ROOT_DIR/pg_log"
CONF_DIR="$BASE_DIR/conf"
PGCONF_DIR="$BASE_DIR/pg_conf"
PORT="${BENCH_PG_PORT:-6532}"

PG_DATABASE=mydb
PG_USER_NAME=myuser
PG_USER_PASSWORD=mypass
PG_REPL_USER_NAME=repl
PG_REPL_PASSWORD=replpass

ITERATIONS="${BENCH_ITERATIONS:-5}"
SCALE="${BENCH_SCALE:-0}"
CASE_FILTER=""

usage() {
   cat <<EOF
pgmoneta benchmarks

Usage:
  $0 run [-c CASE] [-i N]          Measure the current branch
  $0 compare BASELINE CANDIDATE -c CASE
  $0 list                          Show the registered cases
  $0 clean                         Remove the build and runtime state

Options:
  -c, --case NAME        Only this case
  -i, --iterations N     Measured iterations (default: $ITERATIONS)
  -s, --scale N          pgbench scale to seed (default: $SCALE, 0 = none)

Results are written to:
  $RESULTS_DIR/<branch>/<case>.<timestamp>.json

Compare branches measured on the SAME machine only. Absolute timings are
not portable between machines, and the worker count is capped by CPU count.

Typical use:
  git switch main         && $0 run
  git switch my-branch    && $0 run
  $0 compare main my-branch -c backup_azure
EOF
}

detect_container_engine() {
   if command -v podman &>/dev/null; then
      CONTAINER_ENGINE="podman"
      export TMPDIR="${TMPDIR:-$HOME/.local/share/containers/tmp}"
      mkdir -p "$TMPDIR"
   elif command -v docker &>/dev/null; then
      CONTAINER_ENGINE="docker"
   else
      echo "error: neither podman nor docker is installed"
      exit 1
   fi
}

# Release, in its own directory: the test build enables sanitizers and -O0,
# which would measure instrumentation rather than pgmoneta.
build() {
   echo "==> building (Release) into $BUILD_DIR"
   mkdir -p "$BUILD_DIR"
   if ! cmake -B "$BUILD_DIR" -S "$PROJECT_DIR" \
        -DCMAKE_BUILD_TYPE=Release -Dbenchmark=ON -Dcheck=OFF -DDOCS=FALSE >"$ROOT_DIR/cmake.log" 2>&1; then
      echo "error: cmake failed; see $ROOT_DIR/cmake.log"
      exit 1
   fi
   # Everything: the harness drives the real daemon and CLI, which must be
   # the Release binaries being measured.
   if ! cmake --build "$BUILD_DIR" >"$ROOT_DIR/build.log" 2>&1; then
      echo "error: build failed; see $ROOT_DIR/build.log"
      exit 1
   fi
}

start_postgresql() {
   echo "==> starting PostgreSQL $PG_VERSION"

   if ! $CONTAINER_ENGINE image exists "$IMAGE_NAME" 2>/dev/null &&
      ! $CONTAINER_ENGINE image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
      echo "error: image $IMAGE_NAME not found."
      echo "       Build it once with: ./test/check.sh (it creates the image)"
      exit 1
   fi

   $CONTAINER_ENGINE rm -f "$CONTAINER_NAME" >/dev/null 2>&1 || true

   # The container's postgres user has a different uid, so the log mount
   # must be writable by anyone or PostgreSQL exits during startup.
   mkdir -p "$PG_LOG_DIR"
   chmod 777 "$PG_LOG_DIR"

   # The image defaults do not enable replication, so without these the
   # daemon starts but every server stays offline.
   rm -rf "$PGCONF_DIR"
   mkdir -p "$PGCONF_DIR"
   cp -R "$TEST_PG_DIRECTORY/conf/." "$PGCONF_DIR/"
   chmod -R 777 "$PGCONF_DIR"

   $CONTAINER_ENGINE run -p "$PORT:5432" -v "$PG_LOG_DIR:/pglog:z" -v "$PGCONF_DIR:/conf:z" \
      --name "$CONTAINER_NAME" -d \
      -e PG_DATABASE=$PG_DATABASE \
      -e PG_USER_NAME=$PG_USER_NAME \
      -e PG_USER_PASSWORD=$PG_USER_PASSWORD \
      -e PG_REPL_USER_NAME=$PG_REPL_USER_NAME \
      -e PG_REPL_PASSWORD=$PG_REPL_PASSWORD \
      "$IMAGE_NAME" >/dev/null

   for _ in $(seq 1 30); do
      if $CONTAINER_ENGINE exec "$CONTAINER_NAME" \
         /usr/pgsql-$PG_VERSION/bin/pg_isready -h localhost -p 5432 >/dev/null 2>&1; then
         echo "    ready"
         return 0
      fi
      sleep 2
   done

   echo "error: PostgreSQL did not become ready"
   $CONTAINER_ENGINE logs "$CONTAINER_NAME" | tail -20
   exit 1
}

# Seed a deterministic dataset so both branches back up the same thing.
# pgbench gives a few large relations; the extra tables give many small files,
# which is the shape per-file upload parallelism actually affects.
seed_data() {
   [[ "$SCALE" -eq 0 ]] && return 0

   echo "==> seeding data (pgbench scale $SCALE)"
   if ! $CONTAINER_ENGINE exec "$CONTAINER_NAME" \
      /usr/pgsql-$PG_VERSION/bin/pgbench -i -s "$SCALE" \
      -h localhost -U "$PG_USER_NAME" "$PG_DATABASE" >/dev/null 2>&1; then
      echo "error: pgbench seeding failed"
      exit 1
   fi

   $CONTAINER_ENGINE exec "$CONTAINER_NAME" \
      /usr/pgsql-$PG_VERSION/bin/psql -h localhost -U "$PG_USER_NAME" -d "$PG_DATABASE" -q -c \
      "DO \$\$ BEGIN FOR i IN 1..200 LOOP EXECUTE format('CREATE TABLE IF NOT EXISTS t%s AS SELECT g, md5(g::text) FROM generate_series(1,2000) g', i); END LOOP; END \$\$;" \
      >/dev/null 2>&1

   $CONTAINER_ENGINE exec "$CONTAINER_NAME" \
      /usr/pgsql-$PG_VERSION/bin/psql -h localhost -U "$PG_USER_NAME" -d "$PG_DATABASE" -q -c "CHECKPOINT;" >/dev/null 2>&1

   echo "    seeded"
}

stop_postgresql() {
   $CONTAINER_ENGINE rm -f "$CONTAINER_NAME" >/dev/null 2>&1 || true
}

write_configuration() {
   mkdir -p "$CONF_DIR" "$LOG_DIR" "$BASE_DIR/backup" "$BASE_DIR/workspace" "$BASE_DIR/restore"

   cat >"$CONF_DIR/pgmoneta.conf" <<EOF
[pgmoneta]
host = localhost
base_dir = $BASE_DIR/backup
compression = zstd
encryption = none
log_type = file
log_level = info
log_path = $LOG_DIR/pgmoneta.log
unix_socket_dir = /tmp/
workspace = $BASE_DIR/workspace

[primary]
host = localhost
port = $PORT
user = $PG_REPL_USER_NAME
wal_slot = bench
create_slot = yes
EOF

   cp "$CONF_DIR/pgmoneta.conf" "$CONF_DIR/pgmoneta.conf.sample"

   : >"$CONF_DIR/pgmoneta_users.conf"
   if [[ ! -e "$HOME/.pgmoneta/master.key" ]]; then
      "$BUILD_DIR/src/pgmoneta-admin" master-key -P "$PG_REPL_PASSWORD" >/dev/null 2>&1
   fi
   "$BUILD_DIR/src/pgmoneta-admin" -f "$CONF_DIR/pgmoneta_users.conf" \
      -U "$PG_REPL_USER_NAME" -P "$PG_REPL_PASSWORD" user add >/dev/null 2>&1

   export PGMONETA_TEST_CONF="$CONF_DIR/pgmoneta.conf"
   export PGMONETA_TEST_CONF_SAMPLE="$CONF_DIR/pgmoneta.conf.sample"
   export PGMONETA_TEST_USER_CONF="$CONF_DIR/pgmoneta_users.conf"
   export PGMONETA_TEST_BASE_DIR="$BASE_DIR"
   export PGMONETA_TEST_EXECUTABLE_DIR="$BUILD_DIR/src"
   export PGMONETA_TEST_RESTORE_DIR="$BASE_DIR/restore"
   export PGMONETA_TEST_RETROSPECT_DIR="$ROOT_DIR/retrospect"
   export PGMONETA_TEST_HOT_STANDBY_DIR="$ROOT_DIR/standby"
   export PGMONETA_TEST_PORT="$PORT"
}

# Always build: cmake is incremental, and a stale binary would report
# results that do not match the source.
ensure_binary() {
   mkdir -p "$ROOT_DIR"
   build
}

do_run() {
   local branch commit

   branch="$(git -C "$PROJECT_DIR" rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
   commit="$(git -C "$PROJECT_DIR" rev-parse --short HEAD 2>/dev/null || echo unknown)"

   if [[ -n "$(git -C "$PROJECT_DIR" status --porcelain 2>/dev/null)" ]]; then
      echo "warning: working tree is dirty; the result will not match $commit exactly"
   fi

   # Backups from a previous run share this directory and the collector
   # takes the newest, so stale state would be reported as this run.
   rm -rf "$BASE_DIR"
   mkdir -p "$ROOT_DIR" "$PG_LOG_DIR" "$RESULTS_DIR"

   build
   detect_container_engine
   start_postgresql
   trap stop_postgresql EXIT
   seed_data
   write_configuration

   echo "==> running: branch=$branch commit=$commit iterations=$ITERATIONS"

   local args=(run --results "$RESULTS_DIR" --iterations "$ITERATIONS"
      --branch "$branch" --commit "$commit")
   [[ -n "$CASE_FILTER" ]] && args+=(--case "$CASE_FILTER")

   "$BENCH_BIN" "${args[@]}"
}

SUBCOMMAND="${1:-}"
[[ $# -gt 0 ]] && shift

POSITIONAL=()
while [[ $# -gt 0 ]]; do
   case "$1" in
      -c | --case)
         CASE_FILTER="${2:-}"
         shift 2
         ;;
      -i | --iterations)
         ITERATIONS="${2:-}"
         shift 2
         ;;
      -s | --scale)
         SCALE="${2:-}"
         shift 2
         ;;
      -h | --help)
         usage
         exit 0
         ;;
      *)
         POSITIONAL+=("$1")
         shift
         ;;
   esac
done

case "$SUBCOMMAND" in
   run)
      do_run
      ;;
   compare)
      if [[ ${#POSITIONAL[@]} -lt 2 ]]; then
         echo "error: compare needs BASELINE and CANDIDATE"
         exit 1
      fi
      if [[ -z "$CASE_FILTER" ]]; then
         echo "error: compare needs -c CASE"
         exit 1
      fi
      ensure_binary
      "$BENCH_BIN" compare "${POSITIONAL[0]}" "${POSITIONAL[1]}" \
         --case "$CASE_FILTER" --results "$RESULTS_DIR"
      ;;
   list)
      ensure_binary
      "$BENCH_BIN" list
      ;;
   clean)
      detect_container_engine
      stop_postgresql
      rm -rf "$BUILD_DIR" "$ROOT_DIR"
      echo "removed $BUILD_DIR and $ROOT_DIR (results kept)"
      ;;
   "" | -h | --help | help)
      usage
      ;;
   *)
      echo "error: unknown subcommand '$SUBCOMMAND'"
      usage
      exit 1
      ;;
esac
