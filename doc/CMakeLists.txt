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
if(generation)
  # Files
  FILE(GLOB PREDS "manual/0?-*.md")
  FILE(GLOB POSTDS "manual/9?-*.md")
  FILE(GLOB TDS "tutorial/??_*.md")
  FILE(GLOB UMDS_PRE "manual/user-0?-*.md")
  FILE(GLOB UMDS_POST "manual/user-1?-*.md")
  FILE(GLOB DMDS "manual/dev-??-*.md")
  FILE(GLOB AG "manual/advanced/??-*.md")

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
    OUTPUT ${MANUAL_OUTPUT_DIR}/pgmoneta-user-guide.pdf
    COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgmoneta-user-guide.pdf --from markdown --template eisvogel --listings -N --toc ${PREDS} ${UMDS_PRE} ${TDS} ${UMDS_POST} ${POSTDS}
    DEPENDS ${UMDS}
    COMMENT "Generating User Guide PDF documentation"
  )
  add_custom_command(
    OUTPUT ${MANUAL_OUTPUT_DIR}/pgmoneta-dev-guide.pdf
    COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgmoneta-dev-guide.pdf --from markdown --template eisvogel --listings -N --toc ${PREDS} ${DMDS} ${POSTDS}
    DEPENDS ${DMDS}
    COMMENT "Generating Developer Guide PDF documentation"
  )

  add_custom_command(
    OUTPUT ${MANUAL_OUTPUT_DIR}/pgmoneta-user-guide.html
    COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgmoneta-user-guide.html -s -f markdown-smart -N --toc -t html5 ${PREDS} ${UMDS_PRE} ${TDS} ${UMDS_POST} ${POSTDS}
    DEPENDS ${UMDS}
    COMMENT "Generating User Guide HTML documentation"
  )
  add_custom_command(
    OUTPUT ${MANUAL_OUTPUT_DIR}/pgmoneta-dev-guide.html
    COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgmoneta-dev-guide.html -s -f markdown-smart -N --toc -t html5 ${PREDS} ${DMDS} ${POSTDS}
    DEPENDS ${DMDS}
    COMMENT "Generating Developer Guide HTML documentation"
  )

  add_custom_command(
    OUTPUT ${MANUAL_OUTPUT_DIR}/pgmoneta-advanced.pdf
    COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgmoneta-advanced.pdf --from markdown --template eisvogel --listings -N --toc ${AG}
    DEPENDS ${AG}
    COMMENT "Generating Advanced PDF documentation"
  )

  add_custom_command(
    OUTPUT ${MANUAL_OUTPUT_DIR}/pgmoneta-advanced.html
    COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgmoneta-advanced.html -s -f markdown-smart -N --toc -t html5 ${AG}
    DEPENDS ${AG}
    COMMENT "Generating Advanced HTML documentation"
  )

  add_custom_target(
    all_docs ALL
    DEPENDS copy_images ${MANUAL_OUTPUT_DIR}/pgmoneta-user-guide.pdf ${MANUAL_OUTPUT_DIR}/pgmoneta-user-guide.html ${MANUAL_OUTPUT_DIR}/pgmoneta-dev-guide.pdf ${MANUAL_OUTPUT_DIR}/pgmoneta-dev-guide.html ${MANUAL_OUTPUT_DIR}/pgmoneta-advanced.pdf ${MANUAL_OUTPUT_DIR}/pgmoneta-advanced.html
  )

  install(FILES ${MANUAL_OUTPUT_DIR}/pgmoneta-user-guide.pdf DESTINATION share/doc/pgmoneta/manual)
  install(FILES ${MANUAL_OUTPUT_DIR}/pgmoneta-user-guide.html DESTINATION share/doc/pgmoneta/manual)
  install(FILES ${MANUAL_OUTPUT_DIR}/pgmoneta-dev-guide.pdf DESTINATION share/doc/pgmoneta/manual)
  install(FILES ${MANUAL_OUTPUT_DIR}/pgmoneta-dev-guide.html DESTINATION share/doc/pgmoneta/manual)
  install(FILES ${MANUAL_OUTPUT_DIR}/pgmoneta-advanced.pdf DESTINATION share/doc/pgmoneta/manual)
  install(FILES ${MANUAL_OUTPUT_DIR}/pgmoneta-advanced.html DESTINATION share/doc/pgmoneta/manual)

endif()

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

  if (${DOXYGEN_VERSION} STREQUAL "1.10.0" OR ${DOXYGEN_VERSION} STREQUAL "1.11.0" OR ${DOXYGEN_VERSION} STREQUAL "1.12.0")
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
