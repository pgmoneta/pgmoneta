#
# Check for Podman
#

find_program(PODMAN_EXECUTABLE
  NAMES podman
  PATHS /usr/local/bin /usr/bin /bin
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Podman DEFAULT_MSG PODMAN_EXECUTABLE)

if (PODMAN_FOUND)
  set(container TRUE)
  set(PODMAN_EXECUTABLE ${PODMAN_EXECUTABLE})
endif ()

mark_as_advanced(PODMAN_EXECUTABLE)
