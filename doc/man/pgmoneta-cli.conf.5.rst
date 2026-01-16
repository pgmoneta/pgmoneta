=================
pgmoneta-cli.conf
=================

--------------------------------------------------------
Configuration file for pgmoneta-cli command line utility
--------------------------------------------------------

:Manual section: 5

DESCRIPTION
===========

pgmoneta-cli.conf is the configuration file for pgmoneta-cli.

This file defines default settings for the pgmoneta-cli command line utility. It is loaded from the path specified with ``-c`` or from ``/etc/pgmoneta/pgmoneta_cli.conf`` if ``-c`` is not provided. Command-line flags always override values from the configuration file.

All properties are in the format ``key = value``.

The characters ``#`` and ``;`` can be used for comments; must be the first character on the line.
The ``Bool`` data type supports the following values: ``on``, ``1``, ``true``, ``off``, ``0`` and ``false``.

OPTIONS
=======

host
  Management host to connect to. If omitted, ``unix_socket_dir`` may be used for a local Unix socket connection.

port
  Management port to connect to. Required for remote TCP connections unless a Unix socket is used. Default is 0 (disabled)

unix_socket_dir
  Directory containing the pgmoneta Unix Domain Socket. Enables local management without host/port. Supports environment variable interpolation (e.g., $HOME).

compression
  Wire-protocol compression. Available options: ``none``, ``gzip``, ``zstd``, ``lz4``, ``bzip2``. Applies only to CLI<->server traffic. Default is ``none``

encryption
  Wire-protocol encryption. Available options: ``none``, ``aes256``, ``aes192``, ``aes128``. Applies only to CLI<->server traffic. Default is ``none``

output
  Default CLI output format. Available options: ``text``, ``json``, ``raw``. Default is ``text``

log_type
  Logging type for the CLI. Available options: ``console``, ``file``, ``syslog``. Default is ``console``

log_level
  Logging level. Available options: ``fatal``, ``error``, ``warn``, ``info``, ``debug`` (and ``debug1``-``debug5``). Default is ``info``

log_path
  Log file path when ``log_type = file``. Supports environment variable interpolation (e.g., $HOME). Default is ``pgmoneta-cli.log``

log_mode
  Log file mode. Available options: ``append``, ``create``. Default is ``append``

log_rotation_age
  Time-based log rotation. If this value is specified without units, it is taken as seconds. Setting this parameter to 0 disables log rotation based on time. It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes, 'H' for hours, 'D' for days, and 'W' for weeks. Default is 0 (disabled)

log_rotation_size
  Size-based log rotation. Supports suffixes: ``B`` (bytes), the default if omitted, ``K`` or ``KB`` (kilobytes), ``M`` or ``MB`` (megabytes), ``G`` or ``GB`` (gigabytes). A value of 0 (with or without suffix) disables. Default is 0 (disabled)

log_line_prefix
  A strftime(3) compatible string to use as prefix for every log line. Must be quoted if contains spaces. Default is ``%Y-%m-%d %H:%M:%S``

REPORTING BUGS
==============

pgmoneta is maintained on GitHub at https://github.com/pgmoneta/pgmoneta

COPYRIGHT
=========

pgmoneta is licensed under the 3-clause BSD License.

SEE ALSO
========

pgmoneta-cli(1), pgmoneta.conf(5), pgmoneta(1), pgmoneta-admin(1)
