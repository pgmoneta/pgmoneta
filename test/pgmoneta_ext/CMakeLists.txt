# 
#  Copyright (C) 2024 The pgmoneta community
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

cmake_minimum_required(VERSION 3.14)

project(pgmoneta_ext_test C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

find_library(CHECK_LIBRARY check PATHS /usr/lib64)
find_path(CHECK_INCLUDE_DIR check.h PATHS /usr/include)

if(CHECK_LIBRARY AND CHECK_INCLUDE_DIR)
    add_library(Check::check SHARED IMPORTED)
    set_target_properties(Check::check PROPERTIES
        IMPORTED_LOCATION ${CHECK_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${CHECK_INCLUDE_DIR})
else()
    message(FATAL_ERROR "Check library not found")
endif()

if(EXISTS "/etc/debian_version")
    include_directories(/usr/include/subunit)
endif()

set(SOURCES
    pgmoneta_ext_test.c
    runner.c
)

add_executable(pgmoneta_ext_test ${SOURCES})

if(EXISTS "/etc/debian_version")
    target_link_libraries(pgmoneta_ext_test Check::check subunit pthread rt m)
else()
    target_link_libraries(pgmoneta_ext_test Check::check pthread rt m)
endif()


add_custom_target(custom_clean
    COMMAND ${CMAKE_COMMAND} -E remove -f *.o pgmoneta_ext_test
    COMMENT "Cleaning up..."
)