#
#  Copyright (C) 2025 The pgmoneta community
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
if (check)

  set(SOURCES
    testcases/common.c
    testcases/pgmoneta_test_1.c
    testcases/pgmoneta_test_2.c
    testcases/runner.c
  )

  add_executable(pgmoneta_test ${SOURCES})

  if(EXISTS "/etc/debian_version")
    target_link_libraries(pgmoneta_test Check::check subunit pthread rt m)
  elseif(APPLE)
    target_link_libraries(pgmoneta_test Check::check m)
  else()
    target_link_libraries(pgmoneta_test Check::check pthread rt m)
  endif()

  add_custom_target(custom_clean
    COMMAND ${CMAKE_COMMAND} -E remove -f *.o pgmoneta_test
    COMMENT "Cleaning up..."
  )
endif()

if(container)

  add_test(test_version_13_rocky9 "${CMAKE_CURRENT_SOURCE_DIR}/../test/testsuite.sh" "${CMAKE_CURRENT_SOURCE_DIR}/../test" "Dockerfile.rocky9" 13)
  add_test(test_version_14_rocky9 "${CMAKE_CURRENT_SOURCE_DIR}/../test/testsuite.sh" "${CMAKE_CURRENT_SOURCE_DIR}/../test" "Dockerfile.rocky9" 14)
  add_test(test_version_15_rocky9 "${CMAKE_CURRENT_SOURCE_DIR}/../test/testsuite.sh" "${CMAKE_CURRENT_SOURCE_DIR}/../test" "Dockerfile.rocky9" 15)
  add_test(test_version_16_rocky9 "${CMAKE_CURRENT_SOURCE_DIR}/../test/testsuite.sh" "${CMAKE_CURRENT_SOURCE_DIR}/../test" "Dockerfile.rocky9" 16)

  set(failRegex
      "Failures: [1-9][0-9]*"
  )

  set_property (TEST test_version_13_rocky9
                PROPERTY FAIL_REGULAR_EXPRESSION "${failRegex}")
  set_property (TEST test_version_14_rocky9
                PROPERTY FAIL_REGULAR_EXPRESSION "${failRegex}")
  set_property (TEST test_version_15_rocky9
                PROPERTY FAIL_REGULAR_EXPRESSION "${failRegex}")
  set_property (TEST test_version_16_rocky9
                PROPERTY FAIL_REGULAR_EXPRESSION "${failRegex}")
endif()

configure_file(
  "${CMAKE_SOURCE_DIR}/test/testsuite.sh"
  "${CMAKE_BINARY_DIR}/testsuite.sh"
  COPYONLY
)