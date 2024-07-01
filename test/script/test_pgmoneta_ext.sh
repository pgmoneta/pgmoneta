: '
 * Copyright (C) 2024 The pgmoneta community
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
 *'

#!/bin/bash

check_pgmoneta_extension() {
  output=$(sudo -u postgres /usr/local/pgsql/bin/psql -U postgres -c "SELECT * FROM pg_available_extensions WHERE name = 'pgmoneta_ext';" 2>&1)

  if echo "$output" | grep -q "pgmoneta_ext"; then
    return 0
  else
    return 1
  fi
}

create_pgmoneta_ext(){
  if [ -f /etc/debian_version ] || [ -f /etc/redhat-release ]; then
    sudo -u postgres /usr/local/pgsql/bin/psql -U postgres -c "DROP EXTENSION IF EXISTS pgmoneta_ext;"
    sudo -u postgres /usr/local/pgsql/bin/psql -U postgres -c "CREATE EXTENSION pgmoneta_ext;"
  else
    echo "Unsupported OS"
    exit 1
  fi
}

run_pgmoneta_ext_check(){
  passwd=${1:-'secretpassword'}
  if [ -f /etc/debian_version ] || [ -f /etc/redhat-release ]; then
    echo "complie pgmoneta_ext"
    cd ../pgmoneta_ext
    sudo rm -rf ./build
    mkdir build
    cd build
    cmake ..
    sudo make
    export PGPASSWORD="$passwd"
    ./pgmoneta_ext_test
    unset PGPASSWORD
  else
    echo "Unsupported OS"
    exit 1
  fi
}

run_tests() {
  local passwd=$1

  check_pgmoneta_extension
  if [ $? -nq 0 ]; then
    echo "pgmoneta_ext extension is not available."
    exit 1
  else
    echo "pgmoneta_ext extension is available."
  fi

  echo "Create pgmoneta_ext..."
  create_pgmoneta_ext

  run_pgmoneta_ext_check "$passwd"
}

passwd=${1:-'secretpassword'}

run_tests "$passwd"
