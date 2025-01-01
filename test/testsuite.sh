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

check_podman_installed() {
  if command -v podman &> /dev/null; then
    return 0
  else
    return 1
  fi
}

run_podman(){
  local dockerfile=${1:-'Dockerfile.rocky8'}
  local version=${2:-13}

  if ! podman build -f "$dockerfile" --build-arg PGVERSION="$version" -t postgres_pgmoneta_image .; then
    echo "Podman build failed."
    clean_podman
    exit 1
  fi

  podman run -d --name postgres_pgmoneta_container postgres_pgmoneta_image

  sleep 5

  podman logs -f postgres_pgmoneta_container

  clean_podman
  
}

clean_podman(){
  podman stop postgres_pgmoneta_container || true
  podman rm postgres_pgmoneta_container || true
  podman rmi postgres_pgmoneta_image || true
  podman builder prune -f || true
}

check_docker_installed() {
  if command -v docker &> /dev/null; then
    return 0
  else
    return 1
  fi
}

run_docker(){
  local dockerfile=${1:-'Dockerfile.rocky8'}
  local version=${2:-13}

  if ! systemctl restart docker; then
    echo "Failed to restart Docker. Please ensure Docker is installed and running."
    clean_docker
    exit 1
  fi

  if ! docker build -f "$dockerfile" --build-arg PGVERSION="$version" -t postgres_pgmoneta_image .; then
    echo "Docker build failed."
    clean_docker
    exit 1
  fi

  docker run --name postgres_pgmoneta_container postgres_pgmoneta_image & container_id=$!

  wait $container_id

  clean_docker
}

clean_docker(){
  docker stop postgres_pgmoneta_container || true
  docker rm postgres_pgmoneta_container || true
  docker rmi postgres_pgmoneta_image || true
  docker builder prune -f || true
}

run_tests() {
  local dir=$1
  local dockerfile=$2
  local version=$3

  valid_versions=("13" "14" "15" "16")
  if [[ ! " ${valid_versions[@]} " =~ " ${version} " ]]; then
    echo "Invalid version. Please provide a version of 13, 14, 15, or 16."
    exit 1
  fi

  cd "$dir"

  if check_podman_installed; then
    echo "Podman is installed."
    run_podman "$dockerfile" "$version"
  else
    echo "Podman is not installed."
    if check_docker_installed; then
      echo "Docker is installed."
      run_docker "$dockerfile" "$version"
    else
      echo "Please install Podman or Docker to proceed."
      exit 1
    fi
  fi
}

dir=${1:-'./'}
dockerfile=${2:-'Dockerfile.rocky8'}
version=${3:-13}

run_tests "$dir" "$dockerfile" "$version"
