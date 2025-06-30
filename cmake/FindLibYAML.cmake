# - Try to find libyaml
# Once done this will define
#  LIBYAML_FOUND        - System has LIBYAML
#  LIBYAML_INCLUDE_DIRS - The LIBYAML include directories
#  LIBYAML_LIBRARIES    - The libraries needed to use LIBYAML

FIND_PATH(LIBYAML_INCLUDE_DIR NAMES yaml.h)
FIND_LIBRARY(LIBYAML_LIBRARY NAMES yaml)


INCLUDE(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set LIBYAML_FOUND to TRUE
# if all listed variables are TRUE and the requested version matches.
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LibYAML DEFAULT_MSG LIBYAML_LIBRARY LIBYAML_INCLUDE_DIR)

if(LIBYAML_FOUND)
    set(LIBYAML_INCLUDE_DIRS ${LIBYAML_INCLUDE_DIR})
    set(LIBYAML_LIBRARIES ${LIBYAML_LIBRARY})
endif()

MARK_AS_ADVANCED(LIBYAML_INCLUDE_DIRS LIBYAML_LIBRARIES)