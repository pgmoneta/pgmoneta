#
# LibSSH support
#

find_path(CJSON_INCLUDE_DIR
  NAMES
    cjson/cJSON.h
)
find_library(CJSON_LIBRARY
  NAMES
    cjson
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CJSON DEFAULT_MSG
                                  CJSON_LIBRARY CJSON_INCLUDE_DIR)

if(CJSON_FOUND)
  set(CJSON_LIBRARIES ${CJSON_LIBRARY})
  set(CJSON_INCLUDE_DIRS ${CJSON_INCLUDE_DIR})
endif()

mark_as_advanced(CJSON_INCLUDE_DIR CJSON_LIBRARY)
