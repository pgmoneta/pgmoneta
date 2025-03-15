#
# Libbacktrace support
#

find_path(LIBBACKTRACE_INCLUDE_DIR
  NAMES
    backtrace.h backtrace-supported.h
)
find_library(LIBBACKTRACE_LIBRARY
  NAMES
    backtrace
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Libbacktrace DEFAULT_MSG
                                    LIBBACKTRACE_LIBRARY LIBBACKTRACE_INCLUDE_DIR)

if(LIBBACKTRACE_FOUND)
  set(LIBBACKTRACE_LIBRARIES ${LIBBACKTRACE_LIBRARY})
  set(LIBBACKTRACE_INCLUDE_DIRS ${LIBBACKTRACE_INCLUDE_DIR})
endif()

mark_as_advanced(LIBBACKTRACE_LIBRARY LIBBACKTRACE_INCLUDE_DIR)