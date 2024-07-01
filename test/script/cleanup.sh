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

clean_up(){
  local version=$1

  valid_versions=("13" "14" "15" "16")
  if [[ ! " ${valid_versions[@]} " =~ " ${version} " ]]; then
    echo "Invalid version. Please provide a version of 13, 14, 15, or 16."
    exit 1
  fi

  if [ -f /etc/debian_version ] || [ -f /etc/redhat-release ]; then
    sudo -u postgres /usr/local/pgsql/bin/psql -U postgres -c "DROP ROLE repl;"
    sudo -u postgres /usr/local/pgsql/bin/pg_ctl -D /tmp/pgsql-${version} -l /tmp/logfile stop
    sudo -u pgmoneta /tmp/pgmoneta/build/src/pgmoneta-admin -f /tmp/pgmoneta/pgmoneta_users.conf -U repl user del

    sudo rm -rf /usr/local/pgsql
    sudo rm -rf /usr/local/bin/psql
    sudo rm -rf /usr/local/bin/pg_ctl
    sudo rm -rf /usr/local/lib/postgresql
    sudo rm -rf /tmp/postgresql-*
    sudo rm -rf /etc/postgresql
    sudo rm -rf /tmp/pgsql-*
    sudo rm -rf /tmp/pgsql
    sudo rm -rf /tmp/logfile
    sudo rm -rf /tmp/postgres
    sudo rm -rf /tmp/pgmoneta*
    sudo rm -rf /tmp/primary-*
    sudo rm -rf /home/pgmoneta/.pgmoneta/master.key
    sudo rm -rf /usr/local/bin/pgmoneta
    sudo rm -rf /etc/pgmoneta
    sudo rm -rf /usr/local/etc/pgmoneta
  else
    echo "Unsupported OS"
    exit 1
  fi
}

version=${1:-13}

clean_up "$version"
