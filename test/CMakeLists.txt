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

  FILE(GLOB_RECURSE SOURCE_FILES "libpgmonetatest/*.c" "testcases/*.c")
  FILE(GLOB_RECURSE HEADER_FILES "include/*.h")

  set(SOURCES ${SOURCE_FILES} ${HEADER_FILES})
  set(ENABLE_COVERAGE OFF)

  add_compile_options(-O0)
  add_compile_options(-DDEBUG)

  if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    if (NOT ${CMAKE_SYSTEM_NAME} STREQUAL "OpenBSD")
      if (NOT ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64")
        if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 3.2)
          add_compile_options(-fsanitize=address)
          add_compile_options(-fno-sanitize=null)

          if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 3.3)
            add_compile_options(-fsanitize=undefined)
            add_compile_options(-fsanitize=float-divide-by-zero)
            add_compile_options(-fsanitize=float-cast-overflow)
          endif()

          if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 3.6)
            add_compile_options(-fno-sanitize-recover=all)
          endif()

          if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 3.8)
            add_compile_options(-fsanitize-recover=address)
          endif()

          if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 3.9)
            add_compile_options(-fsanitize-address-use-after-scope)
          endif()

          add_compile_options(-fno-sanitize=alignment)

          set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS_DEBUG} -fsanitize=address -fsanitize=undefined -fno-sanitize-recover=all -fsanitize=float-divide-by-zero -fsanitize=float-cast-overflow -fno-sanitize=null -fno-sanitize=alignment")
          set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -fsanitize=address -fsanitize=undefined -fno-sanitize-recover=all -fsanitize=float-divide-by-zero -fsanitize=float-cast-overflow -fno-sanitize=null -fno-sanitize=alignment")
        endif()
      endif()
    endif()
  elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    if (NOT ${CMAKE_SYSTEM_NAME} STREQUAL "OpenBSD")
      if (NOT ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64")
        if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 4.8)
          add_compile_options(-fsanitize=address)

          set(TEMP_EXE_LINKER_FLAGS "-fsanitize=address")
          set(TEMP_SHARED_LINKER_FLAGS "-fsanitize=address")

          if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 4.9)
            add_compile_options(-fsanitize=undefined)
            set(TEMP_EXE_LINKER_FLAGS "${TEMP_EXE_LINKER_FLAGS} -fsanitize=undefined")
            set(TEMP_SHARED_LINKER_FLAGS "${TEMP_SHARED_LINKER_FLAGS} -fsanitize=undefined")
          endif()

          if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 5.1)
            add_compile_options(-fno-sanitize=alignment)
            add_compile_options(-fno-sanitize-recover=all)
            add_compile_options(-fsanitize=float-cast-overflow)
            add_compile_options(-fsanitize=float-divide-by-zero)

            set(TEMP_EXE_LINKER_FLAGS "${TEMP_EXE_LINKER_FLAGS} -fno-sanitize=alignment -fno-sanitize-recover=all -fsanitize=float-cast-overflow -fsanitize=float-divide-by-zero")
            set(TEMP_SHARED_LINKER_FLAGS "${TEMP_SHARED_LINKER_FLAGS} -fno-sanitize=alignment -fno-sanitize-recover=all -fsanitize=float-cast-overflow -fsanitize=float-divide-by-zero")
          endif()

          if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 6.0)
            add_compile_options(-fsanitize-recover=address)
            set(TEMP_EXE_LINKER_FLAGS "${TEMP_EXE_LINKER_FLAGS} -fsanitize-recover=address")
            set(TEMP_SHARED_LINKER_FLAGS "${TEMP_SHARED_LINKER_FLAGS} -fsanitize-recover=address")
          endif()

          if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 7.0)
            add_compile_options(-fsanitize-address-use-after-scope)
            set(TEMP_EXE_LINKER_FLAGS "${TEMP_EXE_LINKER_FLAGS} -fsanitize-address-use-after-scope")
            set(TEMP_SHARED_LINKER_FLAGS "${TEMP_SHARED_LINKER_FLAGS} -fsanitize-address-use-after-scope")
          endif()

          set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${TEMP_EXE_LINKER_FLAGS}")
          set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${TEMP_SHARED_LINKER_FLAGS}")
        endif()
      endif()
    endif()
  endif()
  add_executable(pgmoneta-test runner.c ${SOURCES})
  target_include_directories(pgmoneta-test PRIVATE ${CMAKE_SOURCE_DIR}/src/include ${CMAKE_SOURCE_DIR}/test/include)

  if(EXISTS "/etc/debian_version")
    target_link_libraries(pgmoneta-test Check::check subunit pthread rt m pgmoneta)
  elseif(APPLE)
    target_link_libraries(pgmoneta-test Check::check m pgmoneta)
  else()
    target_link_libraries(pgmoneta-test Check::check pthread rt m pgmoneta)
  endif()

  add_custom_target(custom_clean
    COMMAND ${CMAKE_COMMAND} -E remove -f *.o pgmoneta-test
    COMMENT "Cleaning up..."
  )
endif()

if(container)

add_test(NAME container_test
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/check.sh
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

set_property(TEST container_test
             PROPERTY FAIL_REGULAR_EXPRESSION "Failures: [1-9][0-9]*")

endif()

configure_file(
  "${CMAKE_SOURCE_DIR}/test/check.sh"
  "${CMAKE_BINARY_DIR}/check.sh"
  COPYONLY
)