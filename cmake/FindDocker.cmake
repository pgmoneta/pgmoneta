#
# Check for Docker
#

find_program(DOCKER_EXECUTABLE
  NAMES docker
  PATHS /usr/local/bin /usr/bin /bin
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Docker DEFAULT_MSG DOCKER_EXECUTABLE)

if (DOCKER_FOUND)
  set(container TRUE)
  set(DOCKER_EXECUTABLE ${DOCKER_EXECUTABLE})
endif ()

mark_as_advanced(DOCKER_EXECUTABLE)
