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

run_tests() {
  local dir=$1
  local cleanup=$2
  local version=$3

  if [[ ! "$cleanup" =~ ^[ny]$ ]]; then
    echo "Invalid cleanup value. Please provide 'n' or 'y'."
    exit 1
  fi

  valid_versions=("13" "14" "15" "16")
  if [[ ! " ${valid_versions[@]} " =~ " ${version} " ]]; then
    echo "Invalid version. Please provide a version of 13, 14, 15, or 16."
    exit 1
  fi

  # Install all necessary dependencies and configuration.
  sudo chmod +x "$dir/script/installation.sh"
  "$dir/script/installation.sh" "$version"

  # Test pgmoneta
  sudo chmod +x "$dir/script/test_pgmoneta.sh"
  "$dir/script/test_pgmoneta.sh"

  # Test pgmoneta_ext
  sudo chmod +x "$dir/script/test_pgmoneta_ext.sh"
  "$dir/script/test_pgmoneta_ext.sh"

  # clean up
  if [[ "${cleanup}" == "y" ]]; then
    sudo chmod +x "$dir/script/cleanup.sh"
    "$dir/script/cleanup.sh" "$version"
  fi

}

dir=${1:-'./'}
cleanup=${2:-'y'}
version=${3:-13}

run_tests "$dir" "$cleanup" "$version"
