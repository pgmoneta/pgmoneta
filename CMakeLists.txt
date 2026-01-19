cmake_minimum_required(VERSION 3.14)

set(VERSION_MAJOR "0")
set(VERSION_MINOR "20")
set(VERSION_PATCH "1")
set(VERSION_STRING ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

#
# Avoid source tree pollution
#
set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not permitted. Make a separate folder for building:\nmkdir build; cd build; cmake ..\nBefore that, remove the files already created:\nrm -rf CMakeCache.txt CMakeFiles")
endif(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)

project(pgmoneta VERSION ${VERSION_STRING} LANGUAGES  C)

include(CTest)
enable_testing()

set(CPACK_PACKAGE_VERSION_MAJOR ${VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${VERSION_PATCH})
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_PACKAGE_FILE_NAME
  "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(CPACK_SOURCE_IGNORE_FILES
  "/build/;/test/;/.cache/;/.git/;/.github/;/*.patch;/.bundle/;/_site/;/vendor/;~$;${CPACK_SOURCE_IGNORE_FILES}")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
include(CPack)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "pgmoneta ${VERSION_STRING}")

if (NOT DEFINED DOCS)
  set(DOCS TRUE)
endif()
set(check TRUE)
set(container FALSE)

include(CheckCCompilerFlag)
include(CheckCSourceCompiles)
include(CheckLinkerFlag)
include(FindPackageHandleStandardArgs)
include(GNUInstallDirs)
include(CheckIncludeFile)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build, options are: Debug Release" FORCE)
endif ()

message(STATUS "Build type is ${CMAKE_BUILD_TYPE}")
message(STATUS "System is ${CMAKE_SYSTEM_NAME}")

set(SUPPORTED_COMPILERS "GNU" "Clang" "AppleClang")

# Check for a supported compiler
if (NOT CMAKE_C_COMPILER_ID IN_LIST SUPPORTED_COMPILERS)
   message(FATAL_ERROR "Unsupported compiler ${CMAKE_C_COMPILER_ID}. Supported compilers are: ${SUPPORTED_COMPILERS}")
endif ()

CHECK_C_COMPILER_FLAG("-std=c17" COMPILER_SUPPORTS_C17)
if(NOT COMPILER_SUPPORTS_C17)
  message(FATAL_ERROR "The compiler ${CMAKE_C_COMPILER} has no C17 support. Please use a different C compiler.")
endif()

CHECK_C_COMPILER_FLAG("-msse4.2" COMPILER_SUPPORTS_SSE42)
if(NOT COMPILER_SUPPORTS_SSE42)
  message(NOTICE "The compiler ${CMAKE_C_COMPILER} has no SSE4.2 support.")
endif()

if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
  CHECK_C_COMPILER_FLAG(-msse4.2 HAVE_CRC32_SSE42)
  if (${HAVE_CRC32_SSE42})
    message(STATUS "CPU have -msse4.2, defined HAVE_CRC32_SSE42")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DHAVE_CRC32_SSE42 -msse4.2")
  endif ()
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
  CHECK_C_COMPILER_FLAG(-march=armv8.1-a HAVE_CRC32_HARDWARE)
  if (${HAVE_CRC32_HARDWARE})
    message(STATUS "CPU have -march=armv8.1-a, defined HAVE_CRC32C")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DHAVE_CRC32C -march=armv8.1-a")
  endif ()
endif ()

if(NOT HAVE_CRC32_SSE42)
  message(STATUS "CRC32C implementation will use the software version")
elseif(HAVE_CRC32_SSE42)
  message(STATUS "CRC32C implementation will use SSE 4.2")
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    # Homebrew ships libarchive keg only, include dirs have to be set manually
    execute_process(
      COMMAND brew --prefix libarchive
      OUTPUT_VARIABLE LIBARCHIVE_PREFIX
      OUTPUT_STRIP_TRAILING_WHITESPACE
      COMMAND_ERROR_IS_FATAL ANY
    )
    set(LibArchive_INCLUDE_DIR "${LIBARCHIVE_PREFIX}/include")

    execute_process(
        COMMAND brew --prefix pandoc
        OUTPUT_VARIABLE PANDOC_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )

endif()

find_package(Check)
if (CHECK_FOUND)
  message(STATUS "check found")
  add_library(Check::check SHARED IMPORTED)
  set_target_properties(Check::check PROPERTIES
    IMPORTED_LOCATION ${CHECK_LIBRARY}
    INTERFACE_INCLUDE_DIRECTORIES ${CHECK_INCLUDE_DIR})
else ()
  set(check FALSE)
  message(STATUS "check needed. The test suite process will be skipped.")
endif()

find_package(Docker)
find_package(Podman)
if (DOCKER_FOUND OR PODMAN_FOUND)
  set(container TRUE)
  message(STATUS "Docker or podman found")
else ()
  message(STATUS "Docker or podman needed. The test suite will be skipped.")
endif()

if (DOCS)
  find_package(Pandoc)
  if (PANDOC_FOUND)
    message(STATUS "pandoc found")
  else ()
    set(DOCS FALSE)
    message(STATUS "pandoc needed. The generation process will be skipped.")
  endif()

  find_package(Pdflatex)
  if (PDFLATEX_FOUND)
    message(STATUS "pdflatex found")
  else ()
    set(DOCS FALSE)
    message(STATUS "pdflatex needed. The generation process will be skipped.")
  endif()
endif()

find_package(ZLIB)
if (ZLIB_FOUND)
  message(STATUS "zlib found")
else ()
  message(FATAL_ERROR "zlib needed")
endif()

find_package(BZip2)
if (BZIP2_FOUND)
  message(STATUS "bzip2 found")
else ()
  message(FATAL_ERROR "bzip2 needed")
endif()

find_package(Zstd)
if (ZSTD_FOUND)
  message(STATUS "zstd found")
else ()
  message(FATAL_ERROR "zstd needed")
endif()

find_package(Lz4)
if (LZ4_FOUND)
  message(STATUS "lz4 found")
else ()
  message(FATAL_ERROR "lz4 needed")
endif()

find_package(Libev 4.11)
if (LIBEV_FOUND)
  message(STATUS "libev found")
else ()
  message(FATAL_ERROR "libev needed")
endif()

find_package(OpenSSL)
if (OPENSSL_FOUND)
  message(STATUS "OpenSSL found")
else ()
  message(FATAL_ERROR "OpenSSL needed")
endif()

find_package(LibArchive)
if (LibArchive_FOUND)
  message(STATUS "libarchive found")
else ()
  message(FATAL_ERROR "libarchive needed")
endif()

if (DOCS)
  find_package(Rst2man)
  if (RST2MAN_FOUND)
    message(STATUS "rst2man found")
  else ()
    message(FATAL_ERROR "rst2man needed")
  endif()
endif()

find_package(Libssh)
if (LIBSSH_FOUND)
  message(STATUS "libssh found")
else ()
  message(FATAL_ERROR "libssh needed")
endif()

find_package(LibYAML)
if (LIBYAML_FOUND)
  message(STATUS "LibYAML found")
else ()
  message(FATAL_ERROR "LibYAML needed")
endif()

find_package(THREAD)
if (THREAD_FOUND)
  message(STATUS "pthread found")
else ()
  message(FATAL_ERROR "pthread needed")
endif()

if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  find_package(Libatomic)
  if (LIBATOMIC_FOUND)
    message(STATUS "libatomic found")
  else ()
    message(FATAL_ERROR "libatomic needed")
  endif()

  find_package(Systemd)
  if (SYSTEMD_FOUND)
    message(STATUS "systemd found")
  else ()
    message(STATUS "systemd not found; building without systemd support")
  endif()
endif()

find_package(Doxygen)

if (DOXYGEN_FOUND)
  message(status "Doxygen found: ${DOXYGEN_EXECUTABLE}")
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/src/")

if (DOCS)
  add_subdirectory(doc)
endif()
add_subdirectory(src)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
  add_subdirectory(test)
else()
  message(STATUS "Test directory not found, skipping tests")
endif()
