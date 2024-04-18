#
# pdflatex Support
#

find_program(PDFLATEX_EXECUTABLE 
  NAMES pdflatex
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Pdflatex DEFAULT_MSG
                                  PDFLATEX_EXECUTABLE)

if(PDFLATEX_FOUND)
  find_program(KPSEWHICH kpsewhich)
  if(NOT KPSEWHICH)
    message(FATAL_ERROR "kpsewhich not found. Please check your TeX installation.")
  endif()

  execute_process(
    COMMAND ${KPSEWHICH} footnote.sty
    OUTPUT_VARIABLE FOOTNOTE_STY_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  if(NOT FOOTNOTE_STY_PATH)
    message(FATAL_ERROR "footnote.sty not found. Please install the necessary LaTeX packages.")
  else()
    message(STATUS "footnote.sty found at: ${FOOTNOTE_STY_PATH}")
  endif()

endif()

mark_as_advanced(PDFLATEX_EXECUTABLE)