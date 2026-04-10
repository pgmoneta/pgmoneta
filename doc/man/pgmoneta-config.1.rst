===============
pgmoneta-config
===============

--------------------------------------
Configuration utility for pgmoneta
--------------------------------------

:Manual section: 1

SYNOPSIS
========

pgmoneta-config [ -o FILE ] [ COMMAND ]

DESCRIPTION
===========

pgmoneta-config is a configuration utility for pgmoneta.

It can generate an initial configuration file interactively, or get/set individual
configuration values in an existing configuration file.

OPTIONS
=======

-o, --output FILE
  Set the output file path for the init command (default: ./pgmoneta.conf)

-q, --quiet
  Generate default options without prompts (for init)

-F, --force
  Force overwrite if the output file already exists

-V, --version
  Display version information

-?, --help
  Display help

COMMANDS
========

init
  Generate a pgmoneta.conf file interactively. The user will be prompted for
  required values such as host, base_dir, and server connection details. Use
  with the -q flag to generate a template file without prompting.

get <file> <section> <key>
  Get a configuration value from a file. Prints the value to stdout.

  Example: pgmoneta-config get pgmoneta.conf primary port

set <file> <section> <key> <value>
  Set a configuration value in a file. Creates the section and key if they
  do not exist. Preserves comments and formatting.

  Example: pgmoneta-config set pgmoneta.conf pgmoneta compression lz4

del <file> <section> [key]
  Delete a section entirely or a specific key from a section.

  Example: pgmoneta-config del pgmoneta.conf pgmoneta compression

ls <file> [section]
  List all sections within a configuration file, or list all keys within a
  specific section.

  Example: pgmoneta-config ls pgmoneta.conf pgmoneta

EXAMPLES
========

Generate a new configuration file interactively::

  pgmoneta-config init

Generate a template automatically without prompting::

  pgmoneta-config -q init

Generate a configuration file to a specific path::

  pgmoneta-config -o /etc/pgmoneta/pgmoneta.conf init

Read the port of the primary server::

  pgmoneta-config get /etc/pgmoneta/pgmoneta.conf primary port

Change the compression algorithm::

  pgmoneta-config set /etc/pgmoneta/pgmoneta.conf pgmoneta compression lz4

REPORTING BUGS
==============

pgmoneta is maintained on GitHub at https://github.com/pgmoneta/pgmoneta

COPYRIGHT
=========

pgmoneta is licensed under the 3-clause BSD License.

SEE ALSO
========

pgmoneta.conf(5), pgmoneta(1), pgmoneta-cli(1), pgmoneta-admin(1)
