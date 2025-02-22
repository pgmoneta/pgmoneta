#
# LibSSH support
#

find_path(LIBSSH_INCLUDE_DIR
  NAMES
    libssh/libssh.h
)
find_library(LIBSSH_LIBRARY
  NAMES
    ssh
    libssh
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Libssh DEFAULT_MSG
                                  LIBSSH_LIBRARY LIBSSH_INCLUDE_DIR)

if(LIBSSH_FOUND)
  set(LIBSSH_LIBRARIES ${LIBSSH_LIBRARY})
  set(LIBSSH_INCLUDE_DIRS ${LIBSSH_INCLUDE_DIR})
endif()

mark_as_advanced(LIBSSH_INCLUDE_DIR LIBSSH_LIBRARY)
