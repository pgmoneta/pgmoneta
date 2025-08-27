# man target
add_custom_target(man ALL)


# man page definitions
set(PGMONETA_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgmoneta.1.rst")
set(PGMONETA_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgmoneta.1")
set(PGMONETA_CLI_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgmoneta-cli.1.rst")
set(PGMONETA_CLI_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgmoneta-cli.1")
set(PGMONETA_ADMIN_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgmoneta-admin.1.rst")
set(PGMONETA_ADMIN_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgmoneta-admin.1")
set(PGMONETA_CONF_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgmoneta.conf.5.rst")
set(PGMONETA_CONF_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgmoneta.conf.5")


# pgmoneta.1
add_custom_command(
  OUTPUT ${PGMONETA_DST_FILE}
  COMMAND ${RST2MAN_EXECUTABLE} ${PGMONETA_SRC_FILE} ${PGMONETA_DST_FILE}
  DEPENDS ${PGMONETA_SRC_FILE}
  COMMENT "Generating man page: pgmoneta.1"
)
# pgmoneta-cli.1
add_custom_command(
  OUTPUT ${PGMONETA_CLI_DST_FILE}
  COMMAND ${RST2MAN_EXECUTABLE} ${PGMONETA_CLI_SRC_FILE} ${PGMONETA_CLI_DST_FILE}
  DEPENDS ${PGMONETA_CLI_SRC_FILE}
  COMMENT "Generating man page: pgmoneta-cli.1"
)
# pgmoneta-admin.1
add_custom_command(
  OUTPUT ${PGMONETA_ADMIN_DST_FILE}
  COMMAND ${RST2MAN_EXECUTABLE} ${PGMONETA_ADMIN_SRC_FILE} ${PGMONETA_ADMIN_DST_FILE}
  DEPENDS ${PGMONETA_ADMIN_SRC_FILE}
  COMMENT "Generating man page: pgmoneta-admin.1"
)
# pgmoneta.conf.5
add_custom_command(
  OUTPUT ${PGMONETA_CONF_DST_FILE}
  COMMAND ${RST2MAN_EXECUTABLE} ${PGMONETA_CONF_SRC_FILE} ${PGMONETA_CONF_DST_FILE}
  DEPENDS ${PGMONETA_CONF_SRC_FILE}
  COMMENT "Generating man page: pgmoneta.conf.5"
)

# Group man page outputs into a target
add_custom_target(manpages ALL
  DEPENDS ${PGMONETA_DST_FILE} ${PGMONETA_CLI_DST_FILE} ${PGMONETA_ADMIN_DST_FILE} ${PGMONETA_CONF_DST_FILE}
)

# Make 'man' depend on 'manpages'
add_dependencies(man manpages)

#
# Install configuration and documentation
#
install(FILES etc/pgmoneta.conf DESTINATION share/doc/pgmoneta/etc)

install(DIRECTORY . DESTINATION share/doc/pgmoneta FILES_MATCHING PATTERN "*.md" PATTERN "etc" EXCLUDE PATTERN "man" EXCLUDE PATTERN "manual" EXCLUDE)

install(FILES ${PGMONETA_DST_FILE} DESTINATION share/man/man1)
install(FILES ${PGMONETA_CLI_DST_FILE} DESTINATION share/man/man1)
install(FILES ${PGMONETA_ADMIN_DST_FILE} DESTINATION share/man/man1)
install(FILES ${PGMONETA_CONF_DST_FILE} DESTINATION share/man/man5)

#
# Generate manual
#
FILE(GLOB EN "manual/en/??-*.md")
FILE(GLOB HI "manual/hi/??-*.md")

# Manuals definitions
set(MANUAL_OUTPUT_DIR "${CMAKE_BINARY_DIR}/doc")
get_filename_component(PARENT_DIR ${CMAKE_BINARY_DIR} DIRECTORY)
set(IMAGE_DIR "${PARENT_DIR}/doc/images")

add_custom_target(
  copy_images
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${IMAGE_DIR} ${CMAKE_BINARY_DIR}/images
  COMMENT "Copy images"
)

add_custom_command(
  OUTPUT ${MANUAL_OUTPUT_DIR}/pgmoneta-en.pdf
  COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgmoneta-en.pdf --from markdown --template eisvogel --listings -N --toc ${EN}
  DEPENDS ${EN}
  COMMENT "Generating English PDF documentation"
)

add_custom_command(
  OUTPUT ${MANUAL_OUTPUT_DIR}/pgmoneta-hi.pdf
  COMMAND ${PANDOC_EXECUTABLE} --quiet -o ${MANUAL_OUTPUT_DIR}/pgmoneta-hi.pdf --pdf-engine=lualatex --template eisvogel --listings -N --toc ${HI}
  DEPENDS ${AG_HI}
  COMMENT "Generating Hindi PDF documentation"
)

add_custom_command(
  OUTPUT ${MANUAL_OUTPUT_DIR}/pgmoneta-en.html
  COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgmoneta-en.html -s -f markdown-smart -N --toc -t html5 ${EN}
  DEPENDS ${EN}
  COMMENT "Generating English HTML documentation"
)

add_custom_command(
  OUTPUT ${MANUAL_OUTPUT_DIR}/pgmoneta-hi.html
  COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgmoneta-hi.html -s -f markdown-smart -N --toc -t html5 ${HI}
  DEPENDS ${HI}
  COMMENT "Generating Hindi HTML documentation"
)

add_custom_target(
  all_docs ALL
  DEPENDS copy_images ${MANUAL_OUTPUT_DIR}/pgmoneta-en.pdf ${MANUAL_OUTPUT_DIR}/pgmoneta-en.html ${MANUAL_OUTPUT_DIR}/pgmoneta-hi.pdf  ${MANUAL_OUTPUT_DIR}/pgmoneta-hi.html
)

install(FILES ${MANUAL_OUTPUT_DIR}/pgmoneta-en.pdf DESTINATION share/doc/pgmoneta/manual)
install(FILES ${MANUAL_OUTPUT_DIR}/pgmoneta-en.html DESTINATION share/doc/pgmoneta/manual)
install(FILES ${MANUAL_OUTPUT_DIR}/pgmoneta-hi.pdf DESTINATION share/doc/pgmoneta/manual)
install(FILES ${MANUAL_OUTPUT_DIR}/pgmoneta-hi.html DESTINATION share/doc/pgmoneta/manual)

#
# Generate API docs
#
if (DOXYGEN_FOUND)
  add_custom_target(api ALL)

  set(DOXYGEN_GENERATE_HTML YES)
  set(DOXYGEN_GENERATE_MAN NO)
  set(DOXYGEN_WARN_AS_ERROR FAIL_ON_WARNINGS)
  set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.doxygen)

  doxygen_add_docs(doxygen ${PROJECT_SOURCE_DIR}/src/include/)

  if (${DOXYGEN_VERSION} STREQUAL "1.10.0" OR
      ${DOXYGEN_VERSION} STREQUAL "1.11.0" OR
      ${DOXYGEN_VERSION} STREQUAL "1.12.0" OR
      ${DOXYGEN_VERSION} STREQUAL "1.13.0" OR
      ${DOXYGEN_VERSION} STREQUAL "1.13.1" OR
      ${DOXYGEN_VERSION} STREQUAL "1.13.2" OR
      ${DOXYGEN_VERSION} STREQUAL "1.14.0")

    add_custom_command(
      TARGET api
      COMMAND ${DOXYGEN_EXECUTABLE} -q ${DOXYFILE_OUT}
      COMMENT "Generating API documentation"
      POST_BUILD
    )
  else()
    add_custom_command(
      TARGET api
      COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
      COMMENT "Generating API documentation"
        POST_BUILD
    )
  endif()
endif()
