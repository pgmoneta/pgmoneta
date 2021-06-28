#
# libtar support
#

find_path(LIBTAR_INCLUDE_DIR
  NAMES libtar.h
)
find_library(LIBTAR_LIBRARY
  NAMES tar
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Libtar REQUIRED_VARS
                                  LIBTAR_LIBRARY LIBTAR_INCLUDE_DIR)

if(LIBTAR_FOUND)
  set(LIBTAR_LIBRARIES     ${LIBTAR_LIBRARY})
  set(LIBTAR_INCLUDE_DIRS  ${LIBTAR_INCLUDE_DIR})
endif()

mark_as_advanced(LIBTAR_INCLUDE_DIR LIBTAR_LIBRARY)
