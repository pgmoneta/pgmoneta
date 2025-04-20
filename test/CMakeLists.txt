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
    tsclient.c
    testcases/pgmoneta_test_1.c
    testcases/pgmoneta_test_2.c
    runner.c
  )

  add_compile_options(-O0)
  add_compile_options(-DDEBUG)
  
  if (CMAKE_BUILD_TYPE MATCHES Debug)
    if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
      if (NOT ${CMAKE_SYSTEM_NAME} STREQUAL "OpenBSD")
        if (NOT ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64")
          add_compile_options(-fsanitize=address)
          add_compile_options(-fsanitize=undefined)
          add_compile_options(-fno-sanitize-recover=all)
          add_compile_options(-fsanitize=float-divide-by-zero)
          add_compile_options(-fsanitize=float-cast-overflow)
          add_compile_options(-fno-sanitize=null)
          add_compile_options(-fno-sanitize=alignment)

          set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS_DEBUG} -fsanitize=address -fsanitize=undefined -fno-sanitize-recover=all -fsanitize=float-divide-by-zero -fsanitize=float-cast-overflow -fno-sanitize=null -fno-sanitize=alignment")
        endif()
      endif()
    endif()
  endif()

  add_executable(pgmoneta_test ${SOURCES})
  target_include_directories(pgmoneta_test PRIVATE ${CMAKE_SOURCE_DIR}/src/include ${CMAKE_SOURCE_DIR}/test/include)

  if(EXISTS "/etc/debian_version")
    target_link_libraries(pgmoneta_test Check::check subunit pthread rt m pgmoneta)
  elseif(APPLE)
    target_link_libraries(pgmoneta_test Check::check m pgmoneta)
  else()
    target_link_libraries(pgmoneta_test Check::check pthread rt m pgmoneta)
  endif()

  add_custom_target(custom_clean
    COMMAND ${CMAKE_COMMAND} -E remove -f *.o pgmoneta_test
    COMMENT "Cleaning up..."
  )
endif()

if(container)

add_test(NAME container_test
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/coverage.sh
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

set_property(TEST container_test
             PROPERTY FAIL_REGULAR_EXPRESSION "Failures: [1-9][0-9]*")

endif()

configure_file(
  "${CMAKE_SOURCE_DIR}/test/testsuite.sh"
  "${CMAKE_BINARY_DIR}/testsuite.sh"
  COPYONLY
)
configure_file(
  "${CMAKE_SOURCE_DIR}/test/coverage.sh"
  "${CMAKE_BINARY_DIR}/coverage.sh"
  COPYONLY
)
configure_file(
  "${CMAKE_SOURCE_DIR}/test/Dockerfile.testsuite"
  "${CMAKE_BINARY_DIR}/Dockerfile.testsuite"
  COPYONLY
)