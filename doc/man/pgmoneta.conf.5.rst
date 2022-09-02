=============
pgmoneta.conf
=============

------------------------------------
Main configuration file for pgmoneta
------------------------------------

:Manual section: 5

DESCRIPTION
===========

pgmoneta.conf is the main configuration file for pgmoneta.

The file is split into different sections specified by the ``[`` and ``]`` characters. The main section is called ``[pgmoneta]``.

Other sections specifies the PostgreSQL server configuration.

All properties are in the format ``key = value``.

The characters ``#`` and ``;`` can be used for comments; must be the first character on the line.
The ``Bool`` data type supports the following values: ``on``, ``1``, ``true``, ``off``, ``0`` and ``false``.

OPTIONS
=======

The options for the main section are

host
  The bind address for pgmoneta. Mandatory

unix_socket_dir
  The Unix Domain Socket location. Mandatory

metrics
  The metrics port. Default is 0 (disabled)

management
  The remote management port. Default is 0 (disabled)

compression
  The compression type (none, gzip, zstd). Default is zstd

compression_level
  The compression level. Default is 3

retention
  The retention for pgmoneta. Default is 7

link
  Use links to limit backup size. Default is true

log_type
  The logging type (console, file, syslog). Default is console

log_level
  The logging level (fatal, error, warn, info, debug1, ..., debug5). Default is info

log_path
  The log file location. Default is pgmoneta.log

log_mode
  Append to or create the log file (append, create). Default is append

blocking_timeout
  The number of seconds the process will be blocking for a connection (disable = 0). Default is 30

tls
  Enable Transport Layer Security (TLS). Default is false

tls_cert_file
  Certificate file for TLS

tls_key_file
  Private key file for TLS

tls_ca_file
  Certificate Authority (CA) file for TLS

libev
  The libev backend to use. Valid options: auto, select, poll, epoll, iouring, devpoll and port. Default is auto

buffer_size
  The network buffer size (SO_RCVBUF and SO_SNDBUF). Default is 65535

keep_alive
  Have SO_KEEPALIVE on sockets. Default is on

nodelay
  Have TCP_NODELAY on sockets. Default is on

non_blocking
  Have O_NONBLOCK on sockets. Default is on

backlog
  The backlog for listen(). Minimum 16. Default is 16

hugepage
  Huge page support. Default is try

pidfile
  Path to the PID file

update_process_title
  The behavior for updating the operating system process title. Allowed settings are: never (or off),
  does not update the process title; strict to set the process title without overriding the existing
  initial process title length; minimal to set the process title to the base description; verbose (or full)
  to set the process title to the full description. Please note that strict and minimal are honored
  only on those systems that do not provide a native way to set the process title (e.g., Linux).
  On other systems, there is no difference between strict and minimal and the assumed behaviour is minimal
  even if strict is used. never and verbose are always honored, on every system. On Linux systems the
  process title is always trimmed to 255 characters, while on system that provide a natve way to set the
  process title it can be longer. Default is verbose

The options for the PostgreSQL section are

host
  The address of the PostgreSQL instance. Mandatory

port
  The port of the PostgreSQL instance. Mandatory
  
user
  The user name for the replication role. Mandatory

backup_slot
  The backup slot

wal_slot
  The WAL slot

retention
  The retention for the server

synchronous
  Use synchronous receive. Default is off

REPORTING BUGS
==============

pgmoneta is maintained on GitHub at https://github.com/pgmoneta/pgmoneta

COPYRIGHT
=========

pgmoneta is licensed under the 3-clause BSD License.

SEE ALSO
========

pgmoneta(1), pgmoneta-cli(1), pgmoneta-admin(1)
