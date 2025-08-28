#
# Add files for libpgmoneta
#
FILE(GLOB_RECURSE SOURCE_FILES "libpgmoneta/*.c")
FILE(GLOB_RECURSE HEADER_FILES "include/*.h")

set(SOURCES ${SOURCE_FILES} ${HEADER_FILES})

#
# OS
#
if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")

  add_compile_options(-DHAVE_LINUX)
  add_compile_options(-D_POSIX_C_SOURCE=200809L)

  check_include_file("execinfo.h" HAVE_EXECINFO_H)
  if (HAVE_EXECINFO_H)
    add_compile_options(-DHAVE_EXECINFO_H)
  endif()
  #
  # Include directories
  #
  include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ZLIB_INCLUDE_DIRS}
    ${BZIP2_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIRS}
    ${LZ4_INCLUDE_DIRS}
    ${LIBEV_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${LIBSSH_INCLUDE_DIRS}
    ${LibArchive_INCLUDE_DIRS}
    ${THREAD_INCLUDE_DIRS}
  )

  #
  # Library directories
  #
  link_libraries(
    ${ZLIB_LIBRARIES}
    ${BZIP2_LIBRARIES}
    ${ZSTD_LIBRARIES}
    ${LZ4_LIBRARIES}
    ${LIBEV_LIBRARIES}
    ${OPENSSL_CRYPTO_LIBRARY}
    ${OPENSSL_SSL_LIBRARY}
    ${LIBSSH_LIBRARIES}
    ${LIBATOMIC_LIBRARY}
    ${LibArchive_LIBRARY}
    ${THREAD_LIBRARY}
  )

  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")

elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
  find_program(HOMEBREW_EXECUTABLE brew)
  if(EXISTS ${HOMEBREW_EXECUTABLE})
    execute_process(
      COMMAND ${HOMEBREW_EXECUTABLE} --prefix
      OUTPUT_VARIABLE HOMEBREW_PREFIX
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    message(STATUS "Homebrew found at ${HOMEBREW_PREFIX}")
  endif()

  add_compile_options(-DHAVE_DARWIN)
  add_compile_options(-D_POSIX_C_SOURCE=200809L)
  add_compile_options(-D_DARWIN_C_SOURCE)
  add_compile_options(-DHAVE_OSX)

  #
  # Include directories
  #
  include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ZLIB_INCLUDE_DIRS}
    ${BZIP2_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIRS}
    ${LZ4_INCLUDE_DIRS}
    ${LIBEV_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${LIBSSH_INCLUDE_DIRS}
    ${LibArchive_INCLUDE_DIRS}
    ${THREAD_INCLUDE_DIRS}
  )

  #
  # Library directories
  #
  link_libraries(
    ${ZLIB_LIBRARIES}
    ${BZIP2_LIBRARIES}
    ${ZSTD_LIBRARIES}
    ${LZ4_LIBRARIES}
    ${LIBEV_LIBRARIES}
    ${OPENSSL_CRYPTO_LIBRARY}
    ${OPENSSL_SSL_LIBRARY}
    ${LIBSSH_LIBRARIES}
    ${LIBATOMIC_LIBRARY}
    ${LibArchive_LIBRARY}
    ${THREAD_LIBRARY}
  )

else()

  add_compile_options(-D_XOPEN_SOURCE=700)
  add_compile_options(-D_BSD_SOURCE)
  add_compile_options(-D_DEFAULT_SOURCE)
  add_compile_options(-D__BSD_VISIBLE)

  #
  # Include directories
  #
  include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ZLIB_INCLUDE_DIRS}
    ${BZIP2_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIRS}
    ${LZ4_INCLUDE_DIRS}
    ${LIBEV_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${LIBSSH_INCLUDE_DIRS}
    ${LibArchive_INCLUDE_DIRS}
    ${THREAD_INCLUDE_DIRS}
  )

  #
  # Library directories
  #
  link_libraries(
    ${ZLIB_LIBRARIES}
    ${ZSTD_LIBRARIES}
    ${BZIP2_LIBRARIES}
    ${LZ4_LIBRARIES}
    ${LIBEV_LIBRARIES}
    ${OPENSSL_CRYPTO_LIBRARY}
    ${OPENSSL_SSL_LIBRARY}
    ${LIBSSH_LIBRARIES}
    ${LibArchive_LIBRARIES}
    ${THREAD_LIBRARIES}
  )

  if (${CMAKE_SYSTEM_NAME} STREQUAL "OpenBSD")
    add_compile_options(-DHAVE_OPENBSD)
  elseif (${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
     add_compile_options(-DHAVE_FREEBSD)
  endif()
endif()

if (SYSTEMD_FOUND)
  add_compile_options(-DHAVE_SYSTEMD)

  include_directories(${SYSTEMD_INCLUDE_DIRS})
  link_libraries(${SYSTEMD_LIBRARIES})
endif()

#
# Compile options
#
add_compile_options(-g)
add_compile_options(-Wall)
add_compile_options(-Werror)
add_compile_options(-std=c17)
add_compile_options(-D__USE_ISOC11)
add_compile_options(-D_GNU_SOURCE)
add_compile_options(-Wno-deprecated)
add_compile_options(-Wno-deprecated-declarations)
add_compile_options(-Wunused)
add_compile_options(-Wunused-result)
add_compile_options(-Wsign-compare)

#
# version number and string management
#
add_compile_options(-DPGMONETA_MAJOR_VERSION=${VERSION_MAJOR})
add_compile_options(-DPGMONETA_MINOR_VERSION=${VERSION_MINOR})
add_compile_options(-DPGMONETA_PATCH_VERSION=${VERSION_PATCH})
add_compile_options(-DPGMONETA_VERSION="${VERSION_STRING}")

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-Wstrict-prototypes)
endif()

# Debug
if (CMAKE_BUILD_TYPE MATCHES Debug)
  add_compile_options(-O0)
  add_compile_options(-DDEBUG)

  if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(STATUS "Coverage tools found")
    add_compile_options(-fprofile-instr-generate)
    add_compile_options(-fcoverage-mapping)
    add_link_options(-fprofile-instr-generate)

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
    message(STATUS "Coverage tools not found (Use Clang)")
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

  check_c_compiler_flag(-fno-omit-frame-pointer HAS_NO_OMIT_FRAME_POINTER)
  if (HAS_NO_OMIT_FRAME_POINTER)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer")
  endif()

  check_c_compiler_flag(-Wextra HAS_WEXTRA)
  if (HAS_WEXTRA)
    if (NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Darwin" AND NOT ${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
      add_compile_options(-Wextra)

      check_c_compiler_flag(-Wno-sign-compare HAS_NO_SIGN_COMPARE)
      if (HAS_NO_SIGN_COMPARE)
        add_compile_options(-Wno-sign-compare)
      endif()
    endif()
  endif()
endif()

# Release
if (CMAKE_BUILD_TYPE MATCHES Release OR CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
  add_compile_options(-O2)
  add_compile_options(-DNDEBUG)
endif (CMAKE_BUILD_TYPE MATCHES Release OR CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)

check_c_compiler_flag(-Wformat HAS_FORMAT)
if (HAS_FORMAT)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wformat")
endif()

check_c_compiler_flag(-Wformat-security HAS_FORMAT_SECURITY)
if (HAS_FORMAT_SECURITY)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wformat-security")
endif()

check_c_compiler_flag(-fstack-protector-strong HAS_STACKPROTECTOR_STRONG)
if (HAS_STACKPROTECTOR_STRONG)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-strong")
else()
  check_c_compiler_flag(-fstack-protector HAS_STACKPROTECTOR)
  if (HAS_STACKPROTECTOR)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector")
  endif()
endif()

check_c_compiler_flag(-rdynamic HAS_DYNAMIC)
if (HAS_DYNAMIC)
  set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -rdynamic")
  set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")
endif()

check_c_compiler_flag(-fPIC HAS_PIC)
if (HAS_PIC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
endif()

if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  include(CheckPIESupported)
  check_pie_supported()
endif()

check_c_compiler_flag(-Wl,arg HAS_LINKER)
check_linker_flag(C "-z relro" LINKER_HAS_RELRO)
if (HAS_LINKER AND LINKER_HAS_RELRO)
  set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,relro")
  set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,relro")
endif()

check_linker_flag(C "-z now" LINKER_HAS_NOW)
if (HAS_LINKER AND LINKER_HAS_NOW)
  set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,now")
  set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,now")
endif()

#
# Build libpgmoneta
#
add_library(pgmoneta SHARED ${SOURCES})
set_target_properties(pgmoneta PROPERTIES LINKER_LANGUAGE C VERSION ${VERSION_STRING}
                               SOVERSION ${VERSION_MAJOR})
target_link_libraries(pgmoneta PUBLIC)
install(TARGETS pgmoneta DESTINATION ${CMAKE_INSTALL_LIBDIR}/)

#
# Build pgmoneta
#
add_executable(pgmoneta-bin main.c)
if (CMAKE_C_LINK_PIE_SUPPORTED)
  set_target_properties(pgmoneta-bin PROPERTIES LINKER_LANGUAGE C OUTPUT_NAME pgmoneta POSITION_INDEPENDENT_CODE TRUE)
else()
  set_target_properties(pgmoneta-bin PROPERTIES LINKER_LANGUAGE C OUTPUT_NAME pgmoneta POSITION_INDEPENDENT_CODE FALSE)
endif()
target_link_libraries(pgmoneta-bin pgmoneta)

install(TARGETS pgmoneta-bin DESTINATION ${CMAKE_INSTALL_BINDIR})

#
# Build pgmoneta-cli
#
add_executable(pgmoneta-cli-bin cli.c)
if (CMAKE_C_LINK_PIE_SUPPORTED)
  set_target_properties(pgmoneta-cli-bin PROPERTIES LINKER_LANGUAGE C OUTPUT_NAME pgmoneta-cli POSITION_INDEPENDENT_CODE TRUE)
else()
  set_target_properties(pgmoneta-cli-bin PROPERTIES LINKER_LANGUAGE C OUTPUT_NAME pgmoneta-cli POSITION_INDEPENDENT_CODE FALSE)
endif()
target_link_libraries(pgmoneta-cli-bin pgmoneta)

install(TARGETS pgmoneta-cli-bin DESTINATION ${CMAKE_INSTALL_BINDIR})

#
# Build pgmoneta-admin
#
add_executable(pgmoneta-admin-bin admin.c)
if (CMAKE_C_LINK_PIE_SUPPORTED)
  set_target_properties(pgmoneta-admin-bin PROPERTIES LINKER_LANGUAGE C OUTPUT_NAME pgmoneta-admin POSITION_INDEPENDENT_CODE TRUE)
else()
  set_target_properties(pgmoneta-admin-bin PROPERTIES LINKER_LANGUAGE C OUTPUT_NAME pgmoneta-admin POSITION_INDEPENDENT_CODE FALSE)
endif()
target_link_libraries(pgmoneta-admin-bin pgmoneta)

install(TARGETS pgmoneta-admin-bin DESTINATION ${CMAKE_INSTALL_BINDIR})

#
# Build pgmoneta-walinfo
#
add_executable(pgmoneta-walinfo-bin walinfo.c)
if (CMAKE_C_LINK_PIE_SUPPORTED)
  set_target_properties(pgmoneta-walinfo-bin PROPERTIES LINKER_LANGUAGE C OUTPUT_NAME pgmoneta-walinfo POSITION_INDEPENDENT_CODE TRUE)
else()
  set_target_properties(pgmoneta-walinfo-bin PROPERTIES LINKER_LANGUAGE C OUTPUT_NAME pgmoneta-walinfo POSITION_INDEPENDENT_CODE FALSE)
endif()
target_link_libraries(pgmoneta-walinfo-bin pgmoneta)

install(TARGETS pgmoneta-walinfo-bin DESTINATION ${CMAKE_INSTALL_BINDIR})
