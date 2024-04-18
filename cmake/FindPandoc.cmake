#
# pandoc Support
#

find_program(PANDOC_EXECUTABLE 
  NAMES pandoc
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Pandoc DEFAULT_MSG 
                                    PANDOC_EXECUTABLE)

mark_as_advanced(PANDOC_EXECUTABLE)