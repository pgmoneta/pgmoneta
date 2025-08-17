#!/bin/bash
#
# Copyright (C) 2025 The pgmoneta community
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
IMAGE_NAME="pgmoneta-test-postgresql17-rocky9"
CONTAINER_NAME="pgmoneta-test-postgresql17"

SCRIPT_DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
PROJECT_DIRECTORY=$(realpath "$SCRIPT_DIR/..")
EXECUTABLE_DIRECTORY=$PROJECT_DIRECTORY/build/src
TEST_DIRECTORY=$PROJECT_DIRECTORY/build/test
TEST_PG17_DIRECTORY=$PROJECT_DIRECTORY/test/postgresql/src/postgresql17

PGMONETA_OPERATION_DIR="/tmp/pgmoneta-test"
PGMONETA_WORKSPACE="/tmp/pgmoneta-workspace"
LOG_DIR="$PGMONETA_OPERATION_DIR/log"
PG_LOG_DIR="$PGMONETA_OPERATION_DIR/pg_log"
COVERAGE_DIR="$PGMONETA_OPERATION_DIR/coverage"
RESTORE_DIRECTORY=$PGMONETA_OPERATION_DIR/restore
BACKUP_DIRECTORY=$PGMONETA_OPERATION_DIR/backup
CONFIGURATION_DIRECTORY=$PGMONETA_OPERATION_DIR/conf

PG_DATABASE=mydb
PG_USER_NAME=myuser
PG_USER_PASSWORD=mypass
PG_REPL_USER_NAME=repl
PG_REPL_PASSWORD=replpass
USER=$(whoami)
MODE="dev"

# Detect container engine: Docker or Podman
if command -v docker &> /dev/null; then
  CONTAINER_ENGINE="docker"
elif command -v podman &> /dev/null; then
  CONTAINER_ENGINE="podman"
else
  echo "Neither Docker nor Podman is installed. Please install one to proceed."
  exit 1
fi


cleanup() {
   echo "Clean up"
   set +e
   echo "Shutdown pgmoneta"
   if [[ -f "/tmp/pgmoneta.localhost.pid" ]]; then
     $EXECUTABLE_DIRECTORY/pgmoneta-cli -c $CONFIGURATION_DIRECTORY/pgmoneta.conf shutdown
     sleep 5
     if [[ -f "/tmp/pgmoneta.localhost.pid" ]]; then
       echo "Force stop pgmoneta"
       kill -9 $(pgrep pgmoneta)
       sudo rm -f "/tmp/pgmoneta.localhost.pid"
     fi
   fi

   if [[ -d $PGMONETA_WORKSPACE ]]; then
     echo "Removing workspace $PGMONETA_WORKSPACE"
     sudo rm -Rf $PGMONETA_WORKSPACE
   fi

   echo "Clean Test Resources"
   if [[ -d $PGMONETA_OPERATION_DIR ]]; then
      if ! sudo chown -R "$USER:$USER" "$PGMONETA_OPERATION_DIR"; then
        echo " Could not change ownership. You might need to clean manually."
      fi
      sudo rm -Rf "$RESTORE_DIRECTORY" "$BACKUP_DIRECTORY" "$CONFIGURATION_DIRECTORY"
      if ls "$COVERAGE_DIR"/*.profraw >/dev/null 2>&1; then
       echo "Generating coverage report, expect error when the binary is not covered at all"
       llvm-profdata merge -sparse $COVERAGE_DIR/*.profraw -o $COVERAGE_DIR/coverage.profdata

       echo "Generating $COVERAGE_DIR/coverage-report-libpgmoneta.txt"
       llvm-cov report $EXECUTABLE_DIRECTORY/libpgmoneta.so \
         --instr-profile=$COVERAGE_DIR/coverage.profdata \
         --format=text > $COVERAGE_DIR/coverage-report-libpgmoneta.txt
       echo "Generating $COVERAGE_DIR/coverage-report-pgmoneta.txt"
       llvm-cov report $EXECUTABLE_DIRECTORY/pgmoneta \
         --instr-profile=$COVERAGE_DIR/coverage.profdata \
         --format=text > $COVERAGE_DIR/coverage-report-pgmoneta.txt
      echo "Generating $COVERAGE_DIR/coverage-report-pgmoneta-cli.txt"
      llvm-cov report $EXECUTABLE_DIRECTORY/pgmoneta-cli \
         --instr-profile=$COVERAGE_DIR/coverage.profdata \
         --format=text > $COVERAGE_DIR/coverage-report-pgmoneta-cli.txt
      echo "Generating $COVERAGE_DIR/coverage-report-pgmoneta-admin.txt"
      llvm-cov report $EXECUTABLE_DIRECTORY/pgmoneta-admin \
         --instr-profile=$COVERAGE_DIR/coverage.profdata \
         --format=text > $COVERAGE_DIR/coverage-report-pgmoneta-admin.txt
      # echo "Generating $COVERAGE_DIR/coverage-report-pgmoneta-walinfo.txt"
      # llvm-cov report $EXECUTABLE_DIRECTORY/pgmoneta-walinfo
      #    --instr-profile=$COVERAGE_DIR/coverage.profdata \
      #    --format=text > $COVERAGE_DIR/coverage-report-pgmoneta-walinfo.txt

       echo "Generating $COVERAGE_DIR/coverage-libpgmoneta.txt"
       llvm-cov show $EXECUTABLE_DIRECTORY/libpgmoneta.so \
         --instr-profile=$COVERAGE_DIR/coverage.profdata \
         --format=text > $COVERAGE_DIR/coverage-libpgmoneta.txt
       echo "Generating $COVERAGE_DIR/coverage-pgmoneta.txt"
       llvm-cov show $EXECUTABLE_DIRECTORY/pgmoneta \
         --instr-profile=$COVERAGE_DIR/coverage.profdata \
         --format=text > $COVERAGE_DIR/coverage-pgmoneta.txt
      echo "Generating $COVERAGE_DIR/coverage-pgmoneta-cli.txt"
      llvm-cov show $EXECUTABLE_DIRECTORY/pgmoneta-cli \
         --instr-profile=$COVERAGE_DIR/coverage.profdata \
         --format=text > $COVERAGE_DIR/coverage-pgmoneta-cli.txt
      echo "Generating $COVERAGE_DIR/coverage-pgmoneta-admin.txt"
      llvm-cov show $EXECUTABLE_DIRECTORY/pgmoneta-admin \
         --instr-profile=$COVERAGE_DIR/coverage.profdata \
         --format=text > $COVERAGE_DIR/coverage-pgmoneta-admin.txt
      # echo "Generating $COVERAGE_DIR/coverage-pgmoneta-walinfo.txt"
      # llvm-cov show $EXECUTABLE_DIRECTORY/pgmoneta-walinfo
      #    --instr-profile=$COVERAGE_DIR/coverage.profdata \
      #    --format=text > $COVERAGE_DIR/coverage-pgmoneta-walinfo.txt
       echo "Coverage --> $COVERAGE_DIR"
     fi
     echo "Logs --> $LOG_DIR, $PG_LOG_DIR"
     sudo chmod -R 700 "$PGMONETA_OPERATION_DIR"
   else
      echo "$PGMONETA_OPERATION_DIR not present ... ok"
   fi

   if [[ $MODE != "ci" ]]; then
     echo "Removing postgres 17 container"
     remove_postgresql_container
   fi

   unset LLVM_PROFILE_FILE

   set -e
}

build_postgresql_image() {
  echo "Building the PostgreSQL 17 image $IMAGE_NAME"
  CUR_DIR=$(pwd)
  cd $TEST_PG17_DIRECTORY
  set +e
  sudo make clean
  set -e
  sudo make build
  cd $CUR_DIR
}

cleanup_postgresql_image() {
  set +e
  echo "Cleanup of the PostgreSQL 17 image $IMAGE_NAME"
  CUR_DIR=$(pwd)
  cd $TEST_PG17_DIRECTORY
  sudo make clean
  cd $CUR_DIR
  set -e
}

start_postgresql_container() {
  sudo $CONTAINER_ENGINE run -p 5432:5432 -v "$PG_LOG_DIR:/pglog:z" \
  --name $CONTAINER_NAME -d \
  -e PG_DATABASE=$PG_DATABASE \
  -e PG_USER_NAME=$PG_USER_NAME \
  -e PG_USER_PASSWORD=$PG_USER_PASSWORD \
  -e PG_REPL_USER_NAME=$PG_REPL_USER_NAME \
  -e PG_REPL_PASSWORD=$PG_REPL_PASSWORD \
  -e PG_LOG_LEVEL=debug5 \
  $IMAGE_NAME

  echo "Checking PostgreSQL 17 container readiness"
  sleep 3
  if sudo $CONTAINER_ENGINE exec $CONTAINER_NAME /usr/pgsql-17/bin/pg_isready -h localhost -p 5432 >/dev/null 2>&1; then
    echo "PostgreSQL 17 is ready!"
  else
    echo "Wait for 10 seconds and retry"
    sleep 10
    if sudo $CONTAINER_ENGINE exec $CONTAINER_NAME /usr/pgsql-17/bin/pg_isready -h localhost -p 5432 >/dev/null 2>&1; then
      echo "PostgreSQL 17 is ready!"
    else
      echo "Printing container logs..."
      sudo $CONTAINER_ENGINE logs $CONTAINER_NAME
      echo ""
      echo "PostgreSQL 17 is not ready, exiting"
      cleanup_postgresql_image
      exit 1
    fi
  fi
}

start_postgresql() {
  echo "Setting up PostgreSQL 17 directory"
  set +e
  sudo rm -Rf /conf /pgconf /pgdata /pgwal
  sudo cp -R $TEST_PG17_DIRECTORY/root /
  sudo ls /root
  sudo mkdir -p /conf /pgconf /pgdata /pgwal /pglog
  sudo cp -R $TEST_PG17_DIRECTORY/conf/* /conf/
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
  sudo $CONTAINER_ENGINE stop $CONTAINER_NAME 2>/dev/null || true
  sudo $CONTAINER_ENGINE rm -f $CONTAINER_NAME 2>/dev/null || true
}

pgmoneta_initialize_configuration() {
   touch $CONFIGURATION_DIRECTORY/pgmoneta.conf $CONFIGURATION_DIRECTORY/pgmoneta_users.conf
   echo "Creating pgmoneta.conf and pgmoneta_users.conf inside $CONFIGURATION_DIRECTORY ... ok"
   cat <<EOF >$CONFIGURATION_DIRECTORY/pgmoneta.conf
[pgmoneta]
host = localhost
metrics = 5001

base_dir = $BACKUP_DIRECTORY

compression = zstd

retention = 7

log_type = file
log_level = debug5
log_path = $LOG_DIR/pgmoneta.log

unix_socket_dir = /tmp/
create_slot = yes
workspace = $PGMONETA_WORKSPACE

[primary]
host = localhost
port = 5432
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
  echo "export PGMONETA_TEST_CONF=$CONFIGURATION_DIRECTORY/pgmoneta.conf"
  export PGMONETA_TEST_CONF=$CONFIGURATION_DIRECTORY/pgmoneta.conf

  echo "export PGMONETA_TEST_CONF_SAMPLE=$CONFIGURATION_DIRECTORY/pgmoneta.conf.sample"
  export PGMONETA_TEST_CONF_SAMPLE=$CONFIGURATION_DIRECTORY/pgmoneta.conf.sample

  echo "export PGMONETA_TEST_RESTORE_DIR=$PGMONETA_OPERATION_DIR/restore"
  export PGMONETA_TEST_RESTORE_DIR=$PGMONETA_OPERATION_DIR/restore
}

execute_testcases() {
   echo "Execute Testcases"
   set +e
   echo "Starting pgmoneta server in daemon mode"
   $EXECUTABLE_DIRECTORY/pgmoneta -c $CONFIGURATION_DIRECTORY/pgmoneta.conf -u $CONFIGURATION_DIRECTORY/pgmoneta_users.conf -d
   echo "Wait for pgmoneta to be ready"
   sleep 10
   $EXECUTABLE_DIRECTORY/pgmoneta-cli -c $CONFIGURATION_DIRECTORY/pgmoneta.conf status details
   if [[ $? -eq 0 ]]; then
      echo "pgmoneta server started ... ok"
   else
      echo "pgmoneta server not started ... not ok"
      exit 1
   fi

   echo "Start running tests"
   $TEST_DIRECTORY/pgmoneta-test
   if [[ $? -ne 0 ]]; then
      exit 1
   fi
   set -e
}

usage() {
   echo "Usage: $0 [sub-command]"
   echo "Subcommand:"
   echo " clean       Clean up test suite environment and remove PostgreSQL image"
   echo " setup       Install dependencies and build PostgreSQL image"
   exit 1
}

run_tests() {
  echo "Building PostgreSQL 17 image if necessary"
  if sudo $CONTAINER_ENGINE image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
    echo "Image $IMAGE_NAME exists, skip building"
  else
    if [[ $MODE != "ci" ]]; then
      build_postgresql_image
    fi
  fi

  echo "Preparing the pgmoneta directory"
  export LLVM_PROFILE_FILE="$COVERAGE_DIR/coverage-%p.profraw"
  sudo rm -Rf "$PGMONETA_OPERATION_DIR"
  mkdir -p "$PGMONETA_OPERATION_DIR"
  mkdir -p "$LOG_DIR" "$PG_LOG_DIR" "$COVERAGE_DIR" "$RESTORE_DIRECTORY" "$BACKUP_DIRECTORY" "$CONFIGURATION_DIRECTORY"
  sudo chmod -R 777 "$PGMONETA_OPERATION_DIR"

  echo "Building pgmoneta"
  mkdir -p "$PROJECT_DIRECTORY/build"
  cd "$PROJECT_DIRECTORY/build"
  export CC=$(which clang)
  cmake -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug ..
  make -j$(nproc)
  cd ..

  if [[ $MODE == "ci" ]]; then
    echo "Start PostgreSQL 17 locally"
    start_postgresql
  else
    echo "Start PostgreSQL 17 container"
    start_postgresql_container
  fi

  echo "Initialize pgmoneta"
  pgmoneta_initialize_configuration

  export_pgmoneta_test_variables

  execute_testcases
}

if [[ $# -gt 1 ]]; then
   usage # More than one argument, show usage and exit
elif [[ $# -eq 1 ]]; then
   if [[ "$1" == "setup" ]]; then
      build_postgresql_image
      dnf install -y \
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
        llvm
   elif [[ "$1" == "clean" ]]; then
      sudo rm -Rf $COVERAGE_DIR
      cleanup
      cleanup_postgresql_image
      sudo rm -Rf $PGMONETA_OPERATION_DIR
   elif [[ "$1" == "ci" ]]; then
      MODE="ci"
      run_tests
   else
      echo "Invalid parameter: $1"
      usage # If an invalid parameter is provided, show usage and exit
   fi
else
   # If no arguments are provided, run function_without_param
   trap cleanup EXIT SIGINT
   run_tests
fi
