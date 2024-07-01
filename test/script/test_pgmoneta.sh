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

check_pgmoneta_installed() {
  if command -v pgmoneta >/dev/null 2>&1; then
    return 0
  else
    return 1
  fi
}

run_pgmoneta_check(){
  if [ -f /etc/debian_version ] || [ -f /etc/redhat-release ]; then
    echo "complie pgmoneta"
    cd ../pgmoneta
    sudo rm -rf ./build
    mkdir build
    cd build
    cmake ..
    sudo make
    ./pgmoneta_test 
  else
    echo "Unsupported OS"
    exit 1
  fi
}

test_pgmoneta(){
  if [ -f /etc/debian_version ] || [ -f /etc/redhat-release ]; then
    echo "clear pgmoneta log file"
    sudo rm -rf /tmp/pgmoneta.log

    echo "start pgmoneta ..."
    sudo -u pgmoneta /tmp/pgmoneta/build/src/pgmoneta -c /tmp/pgmoneta/pgmoneta.conf -u /tmp/pgmoneta/pgmoneta_users.conf -d
    sleep 5

    echo "run pgmoneta check ..."
    run_pgmoneta_check

    sudo -u pgmoneta /tmp/pgmoneta/build/src/pgmoneta-cli -c /tmp/pgmoneta/pgmoneta.conf delete primary newest

    echo "stop pgmoneta ..."
    sudo -u pgmoneta /tmp/pgmoneta/build/src/pgmoneta-cli -c /tmp/pgmoneta/pgmoneta.conf stop
  else
    echo "Unsupported OS"
    exit 1
  fi
}

run_tests() {

  check_pgmoneta_installed
  if [ $? -eq 0 ]; then
    test_pgmoneta
  else
    echo "pgmoneta not installed."
    exit 1
  fi

}

run_tests
