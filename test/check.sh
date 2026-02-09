#!/bin/bash
#
# Copyright (C) 2026 The pgmoneta community
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this list
# of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this
# list of conditions and the following disclaimer in the documentation and/or other
# materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors may
# be used to endorse or promote products derived from this software without specific
# prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
# THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
# OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
# TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
set -eo pipefail

# Variables
PG_VERSION="${TEST_PG_VERSION:-17}"
export TEST_PG_VERSION="${TEST_PG_VERSION:-17}"

IMAGE_NAME="pgmoneta-test-postgresql$PG_VERSION-rocky9"
CONTAINER_NAME="pgmoneta-test-postgresql$PG_VERSION"

SCRIPT_DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
PROJECT_DIRECTORY=$(realpath "$SCRIPT_DIR/..")
EXECUTABLE_DIRECTORY=$PROJECT_DIRECTORY/build/src
TEST_DIRECTORY=$PROJECT_DIRECTORY/build/test
TEST_PG_DIRECTORY="$PROJECT_DIRECTORY/test/postgresql/src/postgresql$PG_VERSION"

PGMONETA_ROOT_DIR="/tmp/pgmoneta-test"
BASE_DIR="$PGMONETA_ROOT_DIR/base"
COVERAGE_DIR="$PGMONETA_ROOT_DIR/coverage"
LOG_DIR="$PGMONETA_ROOT_DIR/log"
PG_LOG_DIR="$PGMONETA_ROOT_DIR/pg_log"

# BASE DIR holds all the run time data
WORKSPACE_DIRECTORY="$BASE_DIR/pgmoneta-workspace/"
CONFIGURATION_DIRECTORY=$BASE_DIR/conf
RESTORE_DIRECTORY=$BASE_DIR/restore
BACKUP_DIRECTORY=$BASE_DIR/backup
RESOURCE_DIRECTORY=$BASE_DIR/resource
PGCONF_DIRECTORY=$BASE_DIR/pg_conf

PG_DATABASE=mydb
PG_USER_NAME=myuser
PG_USER_PASSWORD=mypass
PG_REPL_USER_NAME=repl
PG_REPL_PASSWORD=replpass
USER=$(whoami)
MODE="dev"
PORT=6432

# Detect container engine: Docker or Podman
if command -v podman &> /dev/null; then
  CONTAINER_ENGINE="podman"
elif command -v docker &> /dev/null; then
  CONTAINER_ENGINE="sudo docker"
else
  echo "Neither Docker nor Podman is installed. Please install one to proceed."
  exit 1
fi

if [ -n "$PGMONETA_TEST_PORT" ]; then
    PORT=$PGMONETA_TEST_PORT
fi
echo "Container port is set to: $PORT"

cleanup() {
   echo "Clean up"
   set +e
   echo "Shutdown pgmoneta"
   if [[ -f "/tmp/pgmoneta.localhost.pid" ]]; then
     $EXECUTABLE_DIRECTORY/pgmoneta-cli -c $CONFIGURATION_DIRECTORY/pgmoneta_cli.conf shutdown
     sleep 5
     if [[ -f "/tmp/pgmoneta.localhost.pid" ]]; then
       echo "Force stop pgmoneta"
       kill -9 $(pgrep pgmoneta)
       rm -f "/tmp/pgmoneta.localhost.pid"
     fi
   fi
   
   echo "Cleaning up shared memory segments"
   ipcs -m 2>/dev/null | awk '/^0x0/ {print $2}' | xargs -r -I {} ipcrm -m {} 2>/dev/null || true

   echo "Clean Test Resources"
   if [[ -d $PGMONETA_ROOT_DIR ]]; then
      if [[ -d $BASE_DIR ]]; then
        rm -Rf "$BASE_DIR"
      fi

      if ls "$COVERAGE_DIR"/*.profraw >/dev/null 2>&1; then
       if command -v llvm-profdata >/dev/null 2>&1 && command -v llvm-cov >/dev/null 2>&1; then
         echo "Generating coverage report, expect error when the binary is not covered at all"
         llvm-profdata merge -sparse $COVERAGE_DIR/*.profraw -o $COVERAGE_DIR/coverage.profdata 2>/dev/null || true

         echo "Generating $COVERAGE_DIR/coverage-report-libpgmoneta.txt"
         llvm-cov report $EXECUTABLE_DIRECTORY/libpgmoneta.so \
           --instr-profile=$COVERAGE_DIR/coverage.profdata \
           --format=text > $COVERAGE_DIR/coverage-report-libpgmoneta.txt 2>/dev/null || true
         echo "Generating $COVERAGE_DIR/coverage-report-pgmoneta.txt"
         llvm-cov report $EXECUTABLE_DIRECTORY/pgmoneta \
           --instr-profile=$COVERAGE_DIR/coverage.profdata \
           --format=text > $COVERAGE_DIR/coverage-report-pgmoneta.txt 2>/dev/null || true
        echo "Generating $COVERAGE_DIR/coverage-report-pgmoneta-cli.txt"
        llvm-cov report $EXECUTABLE_DIRECTORY/pgmoneta-cli \
           --instr-profile=$COVERAGE_DIR/coverage.profdata \
           --format=text > $COVERAGE_DIR/coverage-report-pgmoneta-cli.txt 2>/dev/null || true
        echo "Generating $COVERAGE_DIR/coverage-report-pgmoneta-admin.txt"
        llvm-cov report $EXECUTABLE_DIRECTORY/pgmoneta-admin \
           --instr-profile=$COVERAGE_DIR/coverage.profdata \
           --format=text > $COVERAGE_DIR/coverage-report-pgmoneta-admin.txt 2>/dev/null || true

         echo "Generating $COVERAGE_DIR/coverage-libpgmoneta.txt"
         llvm-cov show $EXECUTABLE_DIRECTORY/libpgmoneta.so \
           --instr-profile=$COVERAGE_DIR/coverage.profdata \
           --format=text > $COVERAGE_DIR/coverage-libpgmoneta.txt 2>/dev/null || true
         echo "Generating $COVERAGE_DIR/coverage-pgmoneta.txt"
         llvm-cov show $EXECUTABLE_DIRECTORY/pgmoneta \
           --instr-profile=$COVERAGE_DIR/coverage.profdata \
           --format=text > $COVERAGE_DIR/coverage-pgmoneta.txt 2>/dev/null || true
        echo "Generating $COVERAGE_DIR/coverage-pgmoneta-cli.txt"
        llvm-cov show $EXECUTABLE_DIRECTORY/pgmoneta-cli \
           --instr-profile=$COVERAGE_DIR/coverage.profdata \
           --format=text > $COVERAGE_DIR/coverage-pgmoneta-cli.txt 2>/dev/null || true
        echo "Generating $COVERAGE_DIR/coverage-pgmoneta-admin.txt"
        llvm-cov show $EXECUTABLE_DIRECTORY/pgmoneta-admin \
           --instr-profile=$COVERAGE_DIR/coverage.profdata \
           --format=text > $COVERAGE_DIR/coverage-pgmoneta-admin.txt 2>/dev/null || true
         echo "Coverage --> $COVERAGE_DIR"
       else
         echo "Coverage tools (llvm-profdata, llvm-cov) not found, skipping coverage report generation"
       fi
     fi
     echo "Logs --> $LOG_DIR, $PG_LOG_DIR"
   else
     echo "$PGMONETA_ROOT_DIR not present ... ok"
   fi

   if [[ $MODE != "ci" ]]; then
     echo "Removing postgres $PG_VERSION container"
     remove_postgresql_container
   fi

   echo "Unsetting environment variables"
   unset_pgmoneta_test_variables

   set -e
}

build_postgresql_image() {
  echo "Building the PostgreSQL $PG_VERSION image $IMAGE_NAME"
  CUR_DIR=$(pwd)
  cd $TEST_PG_DIRECTORY
  set +e
  make clean
  set -e
  make build
  cd $CUR_DIR
}

cleanup_postgresql_image() {
  set +e
  echo "Cleanup of the PostgreSQL $PG_VERSION image $IMAGE_NAME"
  CUR_DIR=$(pwd)
  cd $TEST_PG_DIRECTORY
  make clean
  cd $CUR_DIR
  set -e
}

start_postgresql_container() {
  # Remove existing container so we can reuse the name 
  remove_postgresql_container
  $CONTAINER_ENGINE run -p $PORT:5432 -v "$PG_LOG_DIR:/pglog:z" -v "$PGCONF_DIRECTORY:/conf:z"\
  --name $CONTAINER_NAME -d \
  -e PG_DATABASE=$PG_DATABASE \
  -e PG_USER_NAME=$PG_USER_NAME \
  -e PG_USER_PASSWORD=$PG_USER_PASSWORD \
  -e PG_REPL_USER_NAME=$PG_REPL_USER_NAME \
  -e PG_REPL_PASSWORD=$PG_REPL_PASSWORD \
  -e PG_LOG_LEVEL=debug5 \
  $IMAGE_NAME

  echo "Checking PostgreSQL $PG_VERSION container readiness"
  sleep 3
  if $CONTAINER_ENGINE exec $CONTAINER_NAME /usr/pgsql-$PG_VERSION/bin/pg_isready -h localhost -p 5432 >/dev/null 2>&1; then
    echo "PostgreSQL $PG_VERSION is ready!"
  else
    echo "Wait for 10 seconds and retry"
    sleep 10
    if $CONTAINER_ENGINE exec $CONTAINER_NAME /usr/pgsql-$PG_VERSION/bin/pg_isready -h localhost -p 5432 >/dev/null 2>&1; then
      echo "PostgreSQL $PG_VERSION is ready!"
    else
      echo "Printing container logs..."
      $CONTAINER_ENGINE logs $CONTAINER_NAME
      echo ""
      echo "PostgreSQL $PG_VERSION is not ready, exiting"
      cleanup_postgresql_image
      exit 1
    fi
  fi
}

start_postgresql() {
  echo "Setting up PostgreSQL $PG_VERSION directory"
  set +e
  sudo rm -Rf /conf /pgconf /pgdata /pgwal
  sudo cp -R $TEST_PG_DIRECTORY/root /
  sudo ls /root
  sudo mkdir -p /conf /pgconf /pgdata /pgwal /pglog
  sudo cp -R $TEST_PG_DIRECTORY/conf/* /conf/
  sudo ls /conf
  sudo chown -R postgres:postgres /conf /pgconf /pgdata /pgwal /pglog
  sudo chmod -R 777 /conf /pgconf /pgdata /pgwal /pglog /root
  sudo chmod +x /root/usr/bin/run-postgresql-local
  sudo mkdir -p /root/usr/local/bin

  echo "Setting up env variables"
  export PG_DATABASE=${PG_DATABASE}
  export PG_USER_NAME=${PG_USER_NAME}
  export PG_USER_PASSWORD=${PG_USER_PASSWORD}
  export PG_REPL_USER_NAME=${PG_REPL_USER_NAME}
  export PG_REPL_PASSWORD=${PG_REPL_PASSWORD}

  sudo -E -u postgres /root/usr/bin/run-postgresql-local
  set -e
}

remove_postgresql_container() {
  $CONTAINER_ENGINE stop $CONTAINER_NAME 2>/dev/null || true
  $CONTAINER_ENGINE rm -f $CONTAINER_NAME 2>/dev/null || true
}

pgmoneta_initialize_configuration() {
  touch $CONFIGURATION_DIRECTORY/pgmoneta.conf $CONFIGURATION_DIRECTORY/pgmoneta_users.conf $CONFIGURATION_DIRECTORY/pgmoneta_cli.conf 
  echo "Creating pgmoneta.conf, pgmoneta_users.conf and pgmoneta_cli.conf inside $CONFIGURATION_DIRECTORY ... ok"
  cat <<EOF >$CONFIGURATION_DIRECTORY/pgmoneta_cli.conf
# CLI configuration
unix_socket_dir = /tmp/
log_type = file
log_level = info
log_path = $LOG_DIR/pgmoneta-cli.log
EOF
   cat <<EOF >$CONFIGURATION_DIRECTORY/pgmoneta.conf
# Main configuration
[pgmoneta]
host = localhost
metrics = 5001

base_dir = $BACKUP_DIRECTORY

compression = zstd

retention = 7
retention_interval = 3600 # 1h

log_type = file
log_level = debug5
log_path = $LOG_DIR/pgmoneta.log

unix_socket_dir = /tmp/
create_slot = yes
workspace = $WORKSPACE_DIRECTORY

# primary configuration
[primary]
host = localhost
port = $PORT
user = $PG_REPL_USER_NAME
wal_slot = repl
EOF
   echo "Add test configuration to pgmoneta.conf ... ok"
   if [[ ! -e $HOME/.pgmoneta/master.key ]]; then
     $EXECUTABLE_DIRECTORY/pgmoneta-admin master-key -P $PG_REPL_PASSWORD
   fi
   $EXECUTABLE_DIRECTORY/pgmoneta-admin -f $CONFIGURATION_DIRECTORY/pgmoneta_users.conf -U $PG_REPL_USER_NAME -P $PG_REPL_PASSWORD user add
   echo "Add user $PG_REPL_USER_NAME to pgmoneta_users.conf file ... ok"
   echo "Keep a sample pgmoneta configuration"
   cp $CONFIGURATION_DIRECTORY/pgmoneta.conf $CONFIGURATION_DIRECTORY/pgmoneta.conf.sample
   echo ""
}

export_pgmoneta_test_variables() {
  echo "export PGMONETA_TEST_BASE_DIR=$BASE_DIR"
  export PGMONETA_TEST_BASE_DIR=$BASE_DIR

  echo "export PGMONETA_TEST_CONF=$CONFIGURATION_DIRECTORY/pgmoneta.conf"
  export PGMONETA_TEST_CONF=$CONFIGURATION_DIRECTORY/pgmoneta.conf

  echo "export PGMONETA_TEST_CONF_SAMPLE=$CONFIGURATION_DIRECTORY/pgmoneta.conf.sample"
  export PGMONETA_TEST_CONF_SAMPLE=$CONFIGURATION_DIRECTORY/pgmoneta.conf.sample

  echo "export PGMONETA_TEST_USER_CONF=$CONFIGURATION_DIRECTORY/pgmoneta_users.conf"
  export PGMONETA_TEST_USER_CONF=$CONFIGURATION_DIRECTORY/pgmoneta_users.conf

  echo "export PGMONETA_TEST_RESTORE_DIR=$RESTORE_DIRECTORY"
  export PGMONETA_TEST_RESTORE_DIR=$RESTORE_DIRECTORY
}

unset_pgmoneta_test_variables() {
  unset PGMONETA_TEST_BASE_DIR
  unset PGMONETA_TEST_CONF
  unset PGMONETA_TEST_USER_CONF
  unset PGMONETA_TEST_CONF_SAMPLE
  unset PGMONETA_TEST_RESTORE_DIR
  unset LLVM_PROFILE_FILE
  unset CC
}

# Returns 0 if setup is already done, 1 if setup is needed.
need_build() {
  # Require test runner and server binary 
  if [[ ! -f "$TEST_DIRECTORY/pgmoneta-test" ]] || [[ ! -f "$EXECUTABLE_DIRECTORY/pgmoneta" ]]; then
    return 1
  fi
  if [[ $MODE != "ci" ]]; then
    if ! $CONTAINER_ENGINE image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
      return 1
    fi
  fi
  if [[ ! -d "$PGMONETA_ROOT_DIR" ]] || [[ ! -f "$CONFIGURATION_DIRECTORY/pgmoneta.conf" ]]; then
    return 1
  fi
  return 0
}

do_setup() {
  local always_build="${1:-}"
  echo "Building PostgreSQL $PG_VERSION image if necessary"
  if $CONTAINER_ENGINE image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
    echo "Image $IMAGE_NAME exists, skip building"
  else
    if [[ $MODE != "ci" ]]; then
      build_postgresql_image
    fi
  fi

  echo "Preparing the pgmoneta directory"
  export LLVM_PROFILE_FILE="$COVERAGE_DIR/coverage-%p.profraw"
  rm -Rf "$PGMONETA_ROOT_DIR"
  mkdir -p "$PGMONETA_ROOT_DIR"
  mkdir -p "$LOG_DIR" "$PG_LOG_DIR" "$COVERAGE_DIR" "$BASE_DIR"
  mkdir -p "$RESTORE_DIRECTORY" "$BACKUP_DIRECTORY" "$CONFIGURATION_DIRECTORY" "$WORKSPACE_DIRECTORY" "$RESOURCE_DIRECTORY" "$PGCONF_DIRECTORY"
  cp -R "$PROJECT_DIRECTORY/test/resource" $BASE_DIR
  cp -R $TEST_PG_DIRECTORY/conf/* $PGCONF_DIRECTORY/
  chmod -R 777 $PG_LOG_DIR
  chmod -R 777 $PGCONF_DIRECTORY

  if [[ "$always_build" == "force" ]] || [[ ! -f "$TEST_DIRECTORY/pgmoneta-test" ]] || [[ ! -f "$EXECUTABLE_DIRECTORY/pgmoneta" ]]; then
    echo "Building pgmoneta"
    mkdir -p "$PROJECT_DIRECTORY/build"
    cd "$PROJECT_DIRECTORY/build"
    export CC=$(which clang)
    cmake -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug -DDOCS=FALSE ..
    make -j$(nproc)
    cd ..
  else
    echo "pgmoneta binaries already present, skipping build"
  fi

  if [[ $MODE == "ci" ]]; then
    echo "Start PostgreSQL $PG_VERSION locally"
    start_postgresql
  else
    echo "Start PostgreSQL $PG_VERSION container"
    start_postgresql_container
  fi

  echo "Initialize pgmoneta"
  pgmoneta_initialize_configuration

  export_pgmoneta_test_variables
}

execute_testcases() {
   echo "Execute MCTF Testcases"
   set +e
   
   if pgrep -f pgmoneta >/dev/null 2>&1 || [[ -f "/tmp/pgmoneta.localhost.pid" ]]; then
      echo "Clean up any existing pgmoneta processes"
      if [[ -f "/tmp/pgmoneta.localhost.pid" ]]; then
         $EXECUTABLE_DIRECTORY/pgmoneta-cli -c $CONFIGURATION_DIRECTORY/pgmoneta.conf shutdown 2>/dev/null || true
         sleep 3
      fi
      if pgrep -f pgmoneta >/dev/null 2>&1; then
         echo "Killing existing pgmoneta processes"
         pkill -9 -f "${EXECUTABLE_DIRECTORY}/pgmoneta " || true
         pkill -9 -f "${EXECUTABLE_DIRECTORY}/pgmoneta-cli " || true
         sleep 2
      fi
      rm -f "/tmp/pgmoneta.localhost.pid"
      
      echo "Cleaning up shared memory segments"
      ipcs -m 2>/dev/null | awk '/^0x0/ {print $2}' | xargs -r -I {} ipcrm -m {} 2>/dev/null || true
      sleep 1
   fi
   
   echo "Starting pgmoneta server in daemon mode"
   $EXECUTABLE_DIRECTORY/pgmoneta -c $CONFIGURATION_DIRECTORY/pgmoneta.conf -u $CONFIGURATION_DIRECTORY/pgmoneta_users.conf -d
   echo "Wait for pgmoneta to be ready"
   sleep 10
   
   for i in {1..5}; do
      if [[ -f "/tmp/pgmoneta.localhost.pid" ]]; then
         if $EXECUTABLE_DIRECTORY/pgmoneta-cli -c $CONFIGURATION_DIRECTORY/pgmoneta_cli.conf status details >/dev/null 2>&1; then
            echo "pgmoneta server started ... ok"
            break
         fi
      fi
      if [[ $i -eq 5 ]]; then
         echo "pgmoneta server not started ... not ok"
         echo "Checking logs:"
         tail -20 $LOG_DIR/pgmoneta.log 2>/dev/null || echo "Log file not found"
         exit 1
      fi
      sleep 2
   done

   echo "Start running MCTF tests"
   if [[ -f "$TEST_DIRECTORY/pgmoneta-test" ]]; then
      TEST_FILTER_ARGS=()
      if [[ -n "${TEST_FILTER:-}" ]]; then
         TEST_FILTER_ARGS=(-t "$TEST_FILTER")
      elif [[ -n "${MODULE_FILTER:-}" ]]; then
         TEST_FILTER_ARGS=(-m "$MODULE_FILTER")
      fi
      $TEST_DIRECTORY/pgmoneta-test "${TEST_FILTER_ARGS[@]}"
      if [[ $? -ne 0 ]]; then
         exit 1
      fi
   else
      echo "Test binary not found: $TEST_DIRECTORY/pgmoneta-test"
      echo "Please build the project first"
      exit 1
   fi
   set -e
}

usage() {
   echo "Usage: $0 [options] [sub-command]"
   echo "Subcommands:"
   echo " build          Set up environment (image, build, PostgreSQL, pgmoneta) without running tests"
   echo " clean          Clean up test suite environment and remove PostgreSQL image"
   echo " setup          Install dependencies and build PostgreSQL image"
   echo " ci             Run full test suite in CI mode (local PostgreSQL)"
   echo "Options (run tests with optional filter; default is full suite):"
   echo " -t, --test NAME     Run only tests matching NAME"
   echo " -m, --module NAME   Run all tests in module NAME"
   echo "Examples:"
   echo "  $0                  Run full test suite"
   echo "  $0 build            Set up environment only; then run e.g. $0 -t backup_full"
   echo "  $0 -t backup_full   Run test matching 'backup_full' (runs build if needed)"
   echo "  $0 -m restore       Run all tests in module 'restore'"
   exit 1
}

run_tests() {
  if need_build; then
    do_setup
  else
    # Double-check: binaries and config must exist (cleanup removes BASE_DIR/conf, so config can be missing)
    if [[ ! -f "$EXECUTABLE_DIRECTORY/pgmoneta" ]] || [[ ! -f "$TEST_DIRECTORY/pgmoneta-test" ]] \
       || [[ ! -f "$CONFIGURATION_DIRECTORY/pgmoneta.conf" ]]; then
      echo "Environment incomplete (binaries or config missing), running build"
      do_setup
    else
      echo "Environment already ready, skipping build"
    fi
  fi
  execute_testcases
}

TEST_FILTER=""
MODULE_FILTER=""
SUBCOMMAND=""
while [[ $# -gt 0 ]]; do
   case "$1" in
      -t|--test)
         [[ -n "$MODULE_FILTER" ]] && { echo "Error: Cannot specify both -t and -m options"; usage; }
         shift
         [[ $# -eq 0 ]] && { echo "Error: -t/--test requires NAME"; usage; }
         TEST_FILTER="$1"
         shift
         ;;
      -m|--module)
         [[ -n "$TEST_FILTER" ]] && { echo "Error: Cannot specify both -t and -m options"; usage; }
         shift
         [[ $# -eq 0 ]] && { echo "Error: -m/--module requires NAME"; usage; }
         MODULE_FILTER="$1"
         shift
         ;;
      build)
         [[ -n "$SUBCOMMAND" ]] && usage
         SUBCOMMAND="build"
         shift
         ;;
      setup)
         [[ -n "$SUBCOMMAND" ]] && usage
         SUBCOMMAND="setup"
         shift
         ;;
      clean)
         [[ -n "$SUBCOMMAND" ]] && usage
         SUBCOMMAND="clean"
         shift
         ;;
      ci)
         [[ -n "$SUBCOMMAND" ]] && usage
         SUBCOMMAND="ci"
         shift
         ;;
      -h|--help)
         usage
         ;;
      -*)
         echo "Invalid option: $1"
         usage
         ;;
      *)
         echo "Invalid parameter: $1"
         usage
         ;;
   esac
done

if [[ "$SUBCOMMAND" == "build" ]]; then
   do_setup force
   exit 0
fi
if [[ "$SUBCOMMAND" == "setup" ]]; then
   build_postgresql_image
   sudo dnf install -y \
      clang \
      clang-analyzer \
      cmake \
      make \
      libev libev-devel \
      openssl openssl-devel \
      systemd systemd-devel \
      zlib zlib-devel \
      libzstd libzstd-devel \
      lz4 lz4-devel \
      libssh libssh-devel \
      libatomic \
      bzip2 bzip2-devel \
      libarchive libarchive-devel \
      libasan libasan-static \
      check check-devel check-static \
      llvm \
      libyaml-devel \
      ncurses-devel
   exit 0
fi
if [[ "$SUBCOMMAND" == "clean" ]]; then
   rm -Rf $COVERAGE_DIR
   cleanup
   cleanup_postgresql_image
   rm -Rf $PGMONETA_ROOT_DIR
   exit 0
fi
if [[ "$SUBCOMMAND" == "ci" ]]; then
   MODE="ci"
   PORT=5432
   trap cleanup EXIT SIGINT
   run_tests
   exit 0
fi
# Default: run tests (full suite or with -t/-m filter)
trap cleanup EXIT SIGINT
run_tests

