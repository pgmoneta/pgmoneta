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

set -e

OS=$(uname)

THIS_FILE=$(realpath "$0")
FILE_OWNER=$(ls -l "$THIS_FILE" | awk '{print $3}')
USER=$(whoami)

PORT=5432
PGPASSWORD="password"

PROJECT_DIRECTORY=$(pwd)
EXECUTABLE_DIRECTORY=$(pwd)/src
TEST_DIRECTORY=$(pwd)/test

LOG_DIRECTORY=$(pwd)/log
PGCTL_LOG_FILE=$LOG_DIRECTORY/logfile
PGMONETA_LOG_FILE=$LOG_DIRECTORY/pgmoneta.log

POSTGRES_OPERATION_DIR=$(pwd)/pgmoneta-postgresql
DATA_DIRECTORY=$POSTGRES_OPERATION_DIR/data

PGMONETA_OPERATION_DIR=$(pwd)/pgmoneta-testsuite
RESTORE_DIRECTORY=$PGMONETA_OPERATION_DIR/restore
BACKUP_DIRECTORY=$PGMONETA_OPERATION_DIR/backup
CONFIGURATION_DIRECTORY=$PGMONETA_OPERATION_DIR/conf

########################### UTILS ############################
is_port_in_use() {
    local port=$1
    if [[ "$OS" == "Linux" ]]; then
        ss -tuln | grep $port > /dev/null 2>&1
    elif [[ "$OS" == "Darwin" ]]; then
        lsof -i:$port > /dev/null 2>&1
    fi
    return $?
}

next_available_port() {
    local port=$1
    while true; do
        is_port_in_use $port
        if [ $? -ne 0 ]; then
            echo "$port"
            return 0
        else
            port=$((port + 1))
        fi
    done
}

##############################################################

############### CHECK POSTGRES DEPENDENCIES ##################
check_inidb() {
    if which initdb > /dev/null 2>&1; then
        echo "check initdb in path ... ok"
        return 0
    else
        echo "check initdb in path ... not present"
        return 1
    fi
}

check_pg_ctl() {
        if which pg_ctl > /dev/null 2>&1; then
        echo "check pg_ctl in path ... ok"
        return 0
    else
        echo "check pg_ctl in path ... not ok"
        return 1
    fi
}

check_psql() {
    if which psql > /dev/null 2>&1; then
        echo "check psql in path ... ok"
        return 0
    else
        echo "check psql in path ... not present"
        return 1
    fi
}

check_postgres_version() {
    version=$(psql --version | awk '{print $3}')
    major_version=$(echo "$version" | cut -d'.' -f1)
    required_major_version=$1
    if [ "$major_version" -ge "$required_major_version" ]; then
        echo "check postgresql version: $version ... ok"
        return 0
    else
        echo "check postgresql version: $version ... not ok"
        return 1
    fi
}

check_system_requirements() {
    echo -e "\e[34mCheck System Requirements \e[0m"
    echo "check system os ... $OS"
    check_inidb
    if [ $? -ne 0 ]; then
        exit 1
    fi
    check_pg_ctl
    if [ $? -ne 0 ]; then
        exit 1
    fi
    check_psql
    if [ $? -ne 0 ]; then
        exit 1
    fi
    check_postgres_version 17
    if [ $? -ne 0 ]; then
        exit 1
    fi
    echo ""
}

initialize_log_files() {
    echo -e "\e[34mInitialize Test logfiles \e[0m"
    mkdir -p $LOG_DIRECTORY
    echo "create log directory ... $LOG_DIRECTORY"
    touch $PGMONETA_LOG_FILE
    echo "create log file ... $PGMONETA_LOG_FILE"
    touch $PGCTL_LOG_FILE
    echo "create log file ... $PGCTL_LOG_FILE"
    echo ""
}
##############################################################

##################### POSTGRES OPERATIONS ####################
create_cluster() {
    local port=$1
    echo -e "\e[34mInitializing Cluster \e[0m"
    initdb -k -D $DATA_DIRECTORY 2> /dev/null
    set +e
    error_out=$(sed -i "s/^#\s*password_encryption\s*=\s*\(md5\|scram-sha-256\)/password_encryption = scram-sha-256/" $DATA_DIRECTORY/postgresql.conf 2>&1)
    if [ $? -ne 0 ]; then
        echo "setting password_encryption ... $error_out"
        clean
        exit 1
    else
        echo "setting password_encryption ... scram-sha-256"
    fi
    error_out=$(sed -i "s|#unix_socket_directories = '/var/run/postgresql'|unix_socket_directories = '/tmp'|" $DATA_DIRECTORY/postgresql.conf 2>&1)
    if [ $? -ne 0 ]; then
        echo "setting unix_socket_directories ... $error_out"
        clean
        exit 1
    else
        echo "setting unix_socket_directories ... '/tmp'"
    fi
    error_out=$(sed -i "s/shared_buffers = 128MB/shared_buffers = 2GB/" $DATA_DIRECTORY/postgresql.conf 2>&1)
    if [ $? -ne 0 ]; then
        echo "setting shared_buffers ... $error_out"
        clean
        exit 1
    else
        echo "setting shared_buffers ... 2GB"
    fi
    error_out=$(sed -i "s/#port = 5432/port = $port/" $DATA_DIRECTORY/postgresql.conf 2>&1)
    if [ $? -ne 0 ]; then
        echo "setting port ... $error_out"
        clean
        exit 1
    else
        echo "setting port ... $port"
    fi
    error_out=$(sed -i "s/#max_prepared_transactions = 0/max_prepared_transactions = 100/" $DATA_DIRECTORY/postgresql.conf 2>&1)
    if [ $? -ne 0 ]; then
        echo "setting max_prepared_transactions ... $error_out"
        clean
        exit 1
    else
        echo "setting max_prepared_transactions ... 100"
    fi
    error_out=$(sed -i "s/#work_mem = 4MB/work_mem = 16MB/" $DATA_DIRECTORY/postgresql.conf 2>&1)
    if [ $? -ne 0 ]; then
        echo "setting work_mem ... $error_out"
        clean
        exit 1
    else
        echo "setting work_mem ... 16MB"
    fi
    error_out=$(sed -i "s/#wal_level = replica/wal_level = replica/" $DATA_DIRECTORY/postgresql.conf 2>&1)
    if [ $? -ne 0 ]; then
        echo "setting wal_level ... $error_out"
        clean
        exit 1
    else
        echo "setting wal_level ... replica"
    fi
    error_out=$(sed -i "s/#wal_log_hints = off/wal_log_hints = on/" $DATA_DIRECTORY/postgresql.conf 2>&1)
    if [ $? -ne 0 ]; then
        echo "setting wal_log_hints ... $error_out"
        clean
        exit 1
    else
        echo "setting wal_log_hints ... on"
    fi
    error_out=$(sed -i "s/max_wal_size = 1GB/max_wal_size = 16GB/" $DATA_DIRECTORY/postgresql.conf 2>&1)
    if [ $? -ne 0 ]; then
        echo "setting max_wal_size ... $error_out"
        clean
        exit 1
    else
        echo "setting max_wal_size ... 16GB"
    fi
    error_out=$(sed -i "s/min_wal_size = 80MB/min_wal_size = 2GB/" $DATA_DIRECTORY/postgresql.conf 2>&1)
    if [ $? -ne 0 ]; then
        echo "setting min_wal_size ... $error_out"
        clean
        exit 1
    else
        echo "setting min_wal_size ... 2GB"
    fi
    error_out=$(sed -i "s/#summarize_wal = off/summarize_wal = on/" $DATA_DIRECTORY/postgresql.conf 2>&1)
    if [ $? -ne 0 ]; then
        echo "setting summarize_wal ... $error_out"
        clean
        exit 1
    else
      echo "setting summarize_wal ... on"
    fi
    # Uncomment if pgmoneta_ext is enabled
    # error_out=$(sed -i "s/#shared_preload_libraries = ''/shared_preload_libraries = 'pgmoneta_ext'/" $DATA_DIRECTORY/postgresql.conf 2>&1)
    # if [ $? -ne 0 ]; then
    #     echo "setting shared_preload_libraries ... $error_out"
    #     clean
    #     exit 1
    # else
    #   echo "setting shared_preload_libraries ... 'pgmoneta_ext'"
    # fi
    set -e
    echo ""
}

initialize_hba_configuration() {
    echo -e "\e[34mCreate HBA Configuration \e[0m"
    echo "
    local   all              all                                     trust
    local   replication      all                                     trust
    host    mydb             myuser          127.0.0.1/32            scram-sha-256
    host    mydb             myuser          ::1/128                 scram-sha-256
    host    postgres         repl            127.0.0.1/32            scram-sha-256
    host    postgres         repl            ::1/128                 scram-sha-256
    host    replication      repl            127.0.0.1/32            scram-sha-256
    host    replication      repl            ::1/128                 scram-sha-256
    " > $DATA_DIRECTORY/pg_hba.conf
    echo "initialize hba configuration at $DATA_DIRECTORY/pg_hba.conf ... ok"
    echo ""
}

initialize_cluster() {
    echo -e "\e[34mInitializing Cluster \e[0m"
    set +e
    pg_ctl -D $DATA_DIRECTORY -l $PGCTL_LOG_FILE start
    if [ $? -ne 0 ]; then
        clean
        exit 1
    fi
    pg_isready -h localhost -p $PORT
    if [ $? -eq 0 ]; then
        echo "postgres server is accepting requests ... ok"
    else
        echo "postgres server is not accepting response ... not ok"
        clean
        exit 1
    fi
    err_out=$(psql -h /tmp -p $PORT -U $USER -d postgres -c "CREATE ROLE repl WITH LOGIN REPLICATION PASSWORD '$PGPASSWORD';" 2>&1)
    if [ $? -ne 0 ]; then
        echo "create role repl ... $err_out"
        pg_ctl -D $DATA_DIRECTORY -l $PGCTL_LOG_FILE stop
        clean
        exit 1
    else
        echo "create role repl ... ok"
    fi
    err_out=$(psql -h /tmp -p $PORT -U $USER -d postgres -c "SELECT pg_create_physical_replication_slot('repl', true, false);" 2>&1)
    if [ $? -ne 0 ]; then
        echo "create replication slot for repl ... $err_out"
        pg_ctl -D $DATA_DIRECTORY -l $PGCTL_LOG_FILE stop
        clean
        exit 1
    else
        echo "create replication slot for repl ... ok"
    fi
    err_out=$(psql -h /tmp -p $PORT -U $USER -d postgres -c "CREATE USER myuser WITH PASSWORD '$PGPASSWORD';" 2>&1)
    if [ $? -ne 0 ]; then
        echo "create user myuser ... $err_out"
        pg_ctl -D $DATA_DIRECTORY -l $PGCTL_LOG_FILE stop
        clean
        exit 1
    else
        echo "create user myuser ... ok"
    fi
    err_out=$(psql -h /tmp -p $PORT -U $USER -d postgres -c "CREATE DATABASE mydb WITH OWNER myuser ENCODING 'UTF8';" 2>&1)
    if [ $? -ne 0 ]; then
        echo "create database mydb with owner myuser ... $err_out"
        pg_ctl -D $DATA_DIRECTORY -l $PGCTL_LOG_FILE stop
        clean
        exit 1
    else
        echo "create database mydb with owner myuser ... ok"
    fi
    set -e
    pg_ctl -D $DATA_DIRECTORY -l $PGCTL_LOG_FILE stop
    echo ""
}

clean_logs() {
    if [ -d $LOG_DIRECTORY ]; then
        rm -r $LOG_DIRECTORY
        echo "remove log directory $LOG_DIRECTORY ... ok"
    else
        echo "$LOG_DIRECTORY not present ... ok"
    fi
}

clean() {
    echo -e "\e[34mClean Test Resources \e[0m"
    if [ -d $POSTGRES_OPERATION_DIR ]; then
        rm -r $POSTGRES_OPERATION_DIR
        echo "remove postgres operations directory $POSTGRES_OPERATION_DIR ... ok"
    else
      echo "$POSTGRES_OPERATION_DIR not present ... ok"
    fi

    if [ -d $PGMONETA_OPERATION_DIR ]; then
        rm -r $PGMONETA_OPERATION_DIR
        echo "remove pgmoneta operations directory $PGMONETA_OPERATION_DIR ... ok"
    else
        echo "$PGMONETA_OPERATION_DIR not present ... ok"
    fi
}

##############################################################

#################### PGMONETA OPERATIONS #####################
pgmoneta_initialize_configuration() {
    echo -e "\e[34mInitialize pgmoneta configuration files \e[0m"
    mkdir -p $RESTORE_DIRECTORY
    echo "create restore directory $RESTORE_DIRECTORY ... ok"
    mkdir -p $CONFIGURATION_DIRECTORY
    echo "create configuration directory $CONFIGURATION_DIRECTORY ... ok"
    touch $CONFIGURATION_DIRECTORY/pgmoneta.conf $CONFIGURATION_DIRECTORY/pgmoneta_users.conf
    echo "create pgmoneta.conf and pgmoneta_users.conf inside $CONFIGURATION_DIRECTORY ... ok"
    cat << EOF > $CONFIGURATION_DIRECTORY/pgmoneta.conf
[pgmoneta]
host = localhost
metrics = 5001

base_dir = $BACKUP_DIRECTORY

compression = zstd

retention = 7

log_type = file
log_level = debug5
log_path = $PGMONETA_LOG_FILE

unix_socket_dir = /tmp/

[primary]
host = localhost
port = $PORT
user = repl
wal_slot = repl
EOF
    echo "add test configuration to pgmoneta.conf ... ok"
    $EXECUTABLE_DIRECTORY/pgmoneta-admin master-key -P $PGPASSWORD || true
    $EXECUTABLE_DIRECTORY/pgmoneta-admin -f $CONFIGURATION_DIRECTORY/pgmoneta_users.conf -U repl -P $PGPASSWORD user add
    echo "add user repl to pgmoneta_users.conf file ... ok"
    echo ""
}

execute_testcases() {
    echo -e "\e[34mExecute Testcases \e[0m"
    set +e
    pg_ctl -D $DATA_DIRECTORY -l $PGCTL_LOG_FILE start

    pg_isready -h localhost -p $PORT
    if [ $? -eq 0 ]; then
        echo "postgres server accepting requests ... ok"
    else
        echo "postgres server is not accepting response ... not ok"
        pg_ctl -D $DATA_DIRECTORY -l $PGCTL_LOG_FILE stop
        clean
        exit 1
    fi

    echo "starting pgmoneta server in daemon mode (wait time = 5 seconds)"
    $EXECUTABLE_DIRECTORY/pgmoneta -c $CONFIGURATION_DIRECTORY/pgmoneta.conf -u $CONFIGURATION_DIRECTORY/pgmoneta_users.conf -d
    sleep 5

    $EXECUTABLE_DIRECTORY/pgmoneta-cli -c $CONFIGURATION_DIRECTORY/pgmoneta.conf ping
    if [ $? -eq 0 ]; then
        echo "pgmoneta server started ... ok"
    else
        echo "pgmoneta server not started ... not ok"
        pg_ctl -D $DATA_DIRECTORY -l $PGCTL_LOG_FILE stop
        clean
        exit 1
    fi

    ### RUN TESTCASES ###
    $TEST_DIRECTORY/pgmoneta_test $PROJECT_DIRECTORY

    echo "running shutdown cli command"
    $EXECUTABLE_DIRECTORY/pgmoneta-cli -c $CONFIGURATION_DIRECTORY/pgmoneta.conf shutdown
    echo "shutdown pgmoneta server ... ok"

    pg_ctl -D $DATA_DIRECTORY -l $PGCTL_LOG_FILE stop
    set -e
    echo ""
}

execute_pgmoneta_ext_suite() {
    echo -e "\e[34mExecute pgmoneta_ext Testcases \e[0m"
    set +e
    pg_ctl -D $DATA_DIRECTORY -l $PGCTL_LOG_FILE start

    pg_isready -h localhost -p $PORT
    if [ $? -eq 0 ]; then
        echo "postgres server is accepting requests ... ok"
    else
        echo "postgres server is not accepting response ... not ok"
        clean
        exit 1
    fi

    ## create extension
    err_out=$(psql -h /tmp -p $PORT -U $USER -d postgres -t -c "DROP EXTENSION IF EXISTS pgmoneta_ext;" 2>&1)
    if [ $? -ne 0 ]; then
        echo "drop extension pgmoneta_ext ... $err_out"
    else
        echo "drop extension pgmoneta_ext ... ok"
    fi
    err_out=$(psql -h /tmp -p $PORT -U $USER -d postgres -t -c "CREATE EXTENSION pgmoneta_ext;" 2>&1)
    if [ $? -ne 0 ]; then
        echo "drop extension pgmoneta_ext ... $err_out"
    else
        echo "drop extension pgmoneta_ext ... ok"
    fi

    ## test extension
    out=$(psql -h /tmp -p $PORT -U $USER -d postgres -t -c "SELECT pgmoneta_ext_version();")
    echo "pgmoneta_ext version ... $out"
    out=$(psql -h /tmp -p $PORT -U $USER -d postgres -t -c "SELECT pgmoneta_ext_switch_wal();")
    echo "pgmoneta_ext switch wal ... $out"
    out=$(psql -h /tmp -p $PORT -U $USER -d postgres -t -c "SELECT pgmoneta_ext_checkpoint();")
    echo "pgmoneta_ext checkout ... $out"
    pg_ctl -D $DATA_DIRECTORY -l $PGCTL_LOG_FILE stop
    set -e
    echo ""
}
##############################################################

run_tests() {
    # Check if the user is pgmoneta
    if [ "$FILE_OWNER" == "$USER" ]; then
        ## Postgres operations
        check_system_requirements

        initialize_log_files

        PORT=$(next_available_port $PORT)
        create_cluster $PORT

        initialize_hba_configuration
        initialize_cluster

        ## pgmoneta operations
        pgmoneta_initialize_configuration
        # execute_pgmoneta_ext_suite Uncomment when pgmoneta_ext is enabled
        execute_testcases

        # clean cluster
        clean
    else
        echo "user should be $FILE_OWNER"
        exit 1
    fi
}

usage() {
    echo "Usage: $0 [sub-command]"
    echo "Subcommand:"
    echo " clean           clean up test suite environment"
    exit 1
}

if [ $# -gt 1 ]; then
    usage  # More than one argument, show usage and exit
elif [ $# -eq 1 ]; then
    if [ "$1" == "clean" ]; then
        # If the parameter is 'clean', run clean_function
        clean
        clean_logs
    else
        echo "Invalid parameter: $1"
        usage  # If an invalid parameter is provided, show usage and exit
    fi
else
    # If no arguments are provided, run function_without_param
    run_tests
fi
