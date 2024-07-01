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

install_dependencies_debian() {
  export DEBIAN_FRONTEND=noninteractive
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends git gcc cmake make wget build-essential libreadline-dev zlib1g-dev flex bison libxml2-dev libxslt1-dev libssl-dev libxml2-utils xsltproc ccache pkg-config libev4 libev-dev libsystemd-dev python3-docutils libzstd-dev liblz4-dev libssh-dev libarchive-dev libcurl4-nss-dev clang bzip2 libbz2-dev check
}

install_dependencies_redhat() {
  sudo yum install -y --allowerasing git gcc cmake make bison-devel readline-devel zlib-devel openssl-devel wget ccache libicu-devel flex libxml2-devel libxslt-devel perl libev libev-devel openssl openssl-devel systemd systemd-devel zlib zlib-devel libzstd libzstd-devel lz4 lz4-devel libssh libssh-devel libcurl libcurl-devel python3-docutils libatomic bzip2 bzip2-devel libarchive libarchive-devel bzip2 bzip2-devel check check-devel
  sudo yum groupinstall -y 'Development Tools'
}

check_postgresql_installed() {
  if command -v pg_config >/dev/null 2>&1; then
    return 0
  else
    return 1
  fi
}

check_pgmoneta_installed() {
  if command -v pgmoneta >/dev/null 2>&1; then
    return 0
  else
    return 1
  fi
}

install_postgresql() {
  version=${1:-13}
  git_repo="https://github.com/postgres/postgres.git"

  if [ -f /etc/debian_version ] || [ -f /etc/redhat-release ]; then
    cd /tmp
    sudo rm -rf /tmp/postgres
    git clone --branch "REL_${version}_STABLE" --single-branch --depth 1 "$git_repo"
    cd postgres
    ./configure --prefix=/usr/local/pgsql
    make
    sudo make install
    sudo -u postgres /usr/local/pgsql/bin/initdb -D /tmp/pgsql-${version}
    sudo -u postgres /usr/local/pgsql/bin/pg_ctl -D /tmp/pgsql-${version} -l /tmp/logfile start
    export PATH=/usr/local/pgsql/bin:$PATH
    export LD_LIBRARY_PATH=/usr/local/pgsql/lib
  else
    echo "Unsupported OS"
    exit 1
  fi

  check_postgresql_installed
  if [ $? -eq 0 ]; then
    echo "PostgreSQL installed successfully."
  else
    echo "Failed to install PostgreSQL."
    exit 1
  fi
}

config_postgresql(){
  version=${1:-13}
  if [ -f /etc/debian_version ] || [ -f /etc/redhat-release ]; then
    sudo -u postgres sed -i 's/^#\s*password_encryption\s*=\s*\(md5\|scram-sha-256\)/password_encryption = scram-sha-256/' /tmp/pgsql-${version}/postgresql.conf
    sudo -u postgres sed -i 's/#wal_level = replica/wal_level = replica/' /tmp/pgsql-${version}/postgresql.conf
    sudo -u postgres sed -i '/^host/d' /tmp/pgsql-${version}/pg_hba.conf

    sudo -u postgres bash -c "cat <<EOF >> /tmp/pgsql-${version}/pg_hba.conf
host    mydb             myuser          127.0.0.1/32            scram-sha-256
host    mydb             myuser          ::1/128                 scram-sha-256
host    postgres         repl            127.0.0.1/32            scram-sha-256
host    postgres         repl            ::1/128                 scram-sha-256
host    replication      repl            127.0.0.1/32            scram-sha-256
host    replication      repl            ::1/128                 scram-sha-256
EOF"

    sudo -u postgres /usr/local/pgsql/bin/pg_ctl -D /tmp/pgsql-${version} -l /tmp/logfile restart
    sudo -u postgres /usr/local/pgsql/bin/psql -U postgres -c "CREATE ROLE repl WITH LOGIN REPLICATION PASSWORD 'secretpassword';"
    sudo -u postgres /usr/local/pgsql/bin/psql -U postgres -c "SELECT pg_create_physical_replication_slot('repl', true, false);"
    sudo -u postgres /usr/local/pgsql/bin/psql -c "CREATE USER myuser WITH PASSWORD 'mypass';"
    sudo -u postgres /usr/local/pgsql/bin/psql -c "CREATE DATABASE mydb WITH OWNER myuser ENCODING 'UTF8';"
  else
    echo "Unsupported OS"
    exit 1
  fi
}

install_pgmoneta() {
  cd /tmp
  sudo rm -rf /tmp/pgmoneta
  git clone https://github.com/pgmoneta/pgmoneta.git
  cd pgmoneta
  mkdir build
  cd build
  cmake ..
  make
  sudo make install
}

config_pgmoneta(){
  if [ -f /etc/debian_version ] || [ -f /etc/redhat-release ]; then
    sudo chown pgmoneta /tmp/pgmoneta
    sudo rm -rf /home/pgmoneta/.pgmoneta/master.key

    sudo -u pgmoneta /tmp/pgmoneta/build/src/pgmoneta-admin master-key -P pgmonetamasterkey
    sudo -u pgmoneta /tmp/pgmoneta/build/src/pgmoneta-admin -f /tmp/pgmoneta/pgmoneta_users.conf -U repl -P secretpassword user add
    cat <<EOF > /tmp/pgmoneta/pgmoneta.conf
[pgmoneta]
host = *
metrics = 5001
create_slot = yes

base_dir = /home/pgmoneta/backup

compression = zstd

storage_engine = local

retention = 7

log_type = file
log_level = info
log_path = /tmp/pgmoneta.log

unix_socket_dir = /tmp/

[primary]
wal_slot = repl
host = localhost
port = 5432
user = repl
EOF
    mkdir /tmp/pgmoneta/backup
  else
    echo "Unsupported OS"
    exit 1
  fi
}

install_pgmoneta_ext() {
  cd /tmp
  sudo rm -rf /tmp/pgmoneta_ext
  git clone https://github.com/pgmoneta/pgmoneta_ext
  cd pgmoneta_ext
  mkdir build
  cd build
  cmake ..
  make
  sudo make install
}

run_tests() {
  local version=$1

  valid_versions=("13" "14" "15" "16")
  if [[ ! " ${valid_versions[@]} " =~ " ${version} " ]]; then
    echo "Invalid version. Please provide a version of 13, 14, 15, or 16."
    exit 1
  fi

  if [ -f /etc/debian_version ]; then
    install_dependencies_debian
  elif [ -f /etc/redhat-release ]; then
    install_dependencies_redhat
  else
    echo "Unsupported OS for installing dependencies."
    exit 1
  fi

  # Install and configure PostgreSQL part
  check_postgresql_installed
  if [ $? -eq 0 ]; then
    echo "Skipping PostgreSQL installation."
  else
    install_postgresql "$version"
    config_postgresql "$version"

    pg_isready
    if [ $? -eq 0 ]; then
      echo "PostgreSQL is ready to accept connections."
    else
      echo "PostgreSQL is not ready."
      exit 1
    fi
  fi

  # Install and configure pgmoneta part
  check_pgmoneta_installed
  if [ $? -eq 0 ]; then
    echo "Skipping pgmoneta installation."
  else
    echo "Installing pgmoneta..."
    install_pgmoneta
    if [ $? -eq 0 ]; then
      echo "pgmoneta installation successful."
    else
      echo "pgmoneta installation failed."
      exit 1
    fi

    echo "Configuring pgmoneta..."
    config_pgmoneta
    if [ $? -eq 0 ]; then
      echo "pgmoneta configuration successful."
    else
      echo "pgmoneta configuration failed."
      exit 1
    fi
  fi

  # Install pgmoneta_ext part
  echo "Installing pgmoneta_ext..."
  install_pgmoneta_ext
  if [ $? -eq 0 ]; then
    echo "pgmoneta_ext installation successful."
  else
    echo "pgmoneta_ext installation failed."
    exit 1
  fi
}

version=${1:-13}

run_tests "$version"
