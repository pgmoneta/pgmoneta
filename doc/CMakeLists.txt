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
  TARGET man
  COMMAND ${RST2MAN_EXECUTABLE} ${PGMONETA_SRC_FILE} ${PGMONETA_DST_FILE}
  OUTPUTS ${PGMONETA_DST_FILE}
)

# pgmoneta-cli.1
add_custom_command(
  TARGET man
  COMMAND ${RST2MAN_EXECUTABLE} ${PGMONETA_CLI_SRC_FILE} ${PGMONETA_CLI_DST_FILE}
  OUTPUTS ${PGMONETA_CLI_DST_FILE}
)

# pgmoneta-admin.1
add_custom_command(
  TARGET man
  COMMAND ${RST2MAN_EXECUTABLE} ${PGMONETA_ADMIN_SRC_FILE} ${PGMONETA_ADMIN_DST_FILE}
  OUTPUTS ${PGMONETA_ADMIN_DST_FILE}
)

# pgmoneta.conf.5
add_custom_command(
  TARGET man
  COMMAND ${RST2MAN_EXECUTABLE} ${PGMONETA_CONF_SRC_FILE} ${PGMONETA_CONF_DST_FILE}
  OUTPUTS ${PGMONETA_CONF_DST_FILE}
)

# man pages
add_custom_command(
  TARGET man
  DEPENDS ${PGMONETA_DST_FILE} ${PGMONETA_CLI_DST_FILE} ${PGMONETA_ADMIN_DST_FILE} ${PGMONETA_CONF_DST_FILE}
)

#
# Install configuration and documentation
#
install(FILES etc/pgmoneta.conf DESTINATION share/doc/pgmoneta/etc)

install(DIRECTORY . DESTINATION share/doc/pgmoneta FILES_MATCHING PATTERN "*.md" PATTERN "etc" EXCLUDE PATTERN "man" EXCLUDE)

install(FILES ${PGMONETA_DST_FILE} DESTINATION share/man/man1)
install(FILES ${PGMONETA_CLI_DST_FILE} DESTINATION share/man/man1)
install(FILES ${PGMONETA_ADMIN_DST_FILE} DESTINATION share/man/man1)
install(FILES ${PGMONETA_CONF_DST_FILE} DESTINATION share/man/man5)
