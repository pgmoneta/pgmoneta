#
# Libcurl support
#
  
find_path(CURL_INCLUDE_DIR
  NAMES 
    curl/curl.h
)
find_library(CURL_LIBRARY 
  NAMES
    curl
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Libcurl DEFAULT_MSG
                                  CURL_LIBRARY CURL_INCLUDE_DIR)

if(LIBCURL_FOUND)
  set(CURL_LIBRARIES ${CURL_LIBRARY})
  set(CURL_INCLUDE_DIRS ${CURL_INCLUDE_DIR})
endif()

mark_as_advanced(CURL_LIBRARY CURL_INCLUDE_DIR)