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
if (benchmark)

  if (NOT CMAKE_BUILD_TYPE MATCHES Release)
    message(FATAL_ERROR
      "Benchmarks must be built with -DCMAKE_BUILD_TYPE=Release. "
      "A Debug build enables sanitizers and -O0, which measures instrumentation "
      "overhead rather than pgmoneta. Use benchmarks/bench.sh, which sets this.")
  endif()

  # The test harness supplies the container, PostgreSQL and daemon lifecycle.
  # Those sources are compiled here rather than reused from the test target so
  # that they pick up this directory's options: test/CMakeLists.txt adds
  # -fsanitize=address and -O0 unconditionally, which would make every timing
  # meaningless.
  # The whole test library, minus the test cases themselves.
  file(GLOB_RECURSE HARNESS_SOURCES "${CMAKE_SOURCE_DIR}/test/libpgmonetatest/*.c")

  # Cases register themselves through a constructor, so a new case is a new
  # file here and nothing else.
  file(GLOB BENCH_CASES "${CMAKE_CURRENT_SOURCE_DIR}/cases/*.c")
  file(GLOB BENCH_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

  add_executable(pgmoneta-bench
    ${BENCH_SOURCES}
    ${BENCH_CASES}
    ${HARNESS_SOURCES})

  target_include_directories(pgmoneta-bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/include
    ${CMAKE_SOURCE_DIR}/test/include
    ${CMAKE_SOURCE_DIR}/test/libpgmonetatest
    ${LIBEV_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${CURSES_INCLUDE_DIRS}
    ${LIBYAML_INCLUDE_DIRS}
    ${LIBSSH_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIRS}
    ${LZ4_INCLUDE_DIRS}
    ${LibArchive_INCLUDE_DIRS})

  target_compile_definitions(pgmoneta-bench PRIVATE
    TEST_CONF_DIR="${CMAKE_SOURCE_DIR}/test/conf"
    BENCH_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    PGMONETA_VERSION="${VERSION_STRING}")

  if(EXISTS "/etc/debian_version")
    target_link_libraries(pgmoneta-bench pthread rt m pgmoneta)
  elseif(APPLE)
    target_link_libraries(pgmoneta-bench m pgmoneta)
  else()
    target_link_libraries(pgmoneta-bench pthread rt m pgmoneta)
  endif()

endif()
