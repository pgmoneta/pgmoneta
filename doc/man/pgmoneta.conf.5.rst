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

base_dir
  The base directory for the backup. Mandatory

metrics
  The metrics port. Default is 0 (disabled)

metrics_cache_max_age
  The time to keep a Prometheus (metrics) response in cache. If this value is specified without units,
  it is taken as seconds. Setting this parameter to 0 disables caching. It supports the following units
  as suffixes: 'S' for seconds (default), 'M' for minutes, 'H' for hours, 'D' for days, and 'W' for weeks.
  Default is 0 (disabled)

metrics_cache_max_size
  The maximum amount of data to keep in cache when serving Prometheus responses. Changes require restart.
  This parameter determines the size of memory allocated for the cache even if ``metrics_cache_max_age`` or
  ``metrics`` are disabled. Its value, however, is taken into account only if ``metrics_cache_max_age`` is set
  to a non-zero value. Supports suffixes: ``B`` (bytes), the default if omitted, ``K`` or ``KB`` (kilobytes),
  ``M`` or ``MB`` (megabytes), ``G`` or ``GB`` (gigabytes).
  Default is 256k

management
  The remote management port. Default is 0 (disabled)

compression
  The compression type (none, gzip, client-gzip, server-gzip, zstd, client-zstd, server-zstd, lz4, client-lz4, server-lz4, bzip2, client-bzip2). Default is zstd

compression_level
  The compression level. Default is 3

workers
  The number of workers that each process can use for its work.
  Use 0 to disable. Maximum is CPU count. Default is 0

workspace
  The directory for the workspace that incremental backup can use for its work.
  Default is /tmp/pgmoneta-workspace/

storage_engine
  The storage engine type (local, ssh, s3, azure). Default is local

encryption
  The encryption mode. Default is none.

  Available options:

  none: No encryption (default value)

  aes \| aes-256 \| aes-256-cbc: AES CBC (Cipher Block Chaining) mode with 256 bit key length

  aes-192 \| aes-192-cbc: AES CBC mode with 192 bit key length

  aes-128 \| aes-128-cbc: AES CBC mode with 128 bit key length

  aes-256-ctr: AES CTR (Counter) mode with 256 bit key length

  aes-192-ctr: AES CTR mode with 192 bit key length

  aes-128-ctr: AES CTR mode with 128 bit key length

create_slot
  Create a replication slot for all server. Valid values are: yes, no. Default is no

ssh_hostname
  Defines the hostname of the remote system for connection

ssh_username
  Defines the username of the remote system for connection

ssh_base_dir
  The base directory for the remote backup

ssh_ciphers
  The supported ciphers for communication.

  aes \| aes-256 \| aes-256-cbc: AES CBC (Cipher Block Chaining) mode with 256 bit key length

  aes-192 \| aes-192-cbc: AES CBC mode with 192 bit key length

  aes-128 \| aes-128-cbc: AES CBC mode with 128 bit key length

  aes-256-ctr: AES CTR (Counter) mode with 256 bit key length

  aes-192-ctr: AES CTR mode with 192 bit key length

  aes-128-ctr: AES CTR mode with 128 bit key length

  Otherwise verbatim. Default is aes-256-ctr, aes-192-ctr, aes-128-ctr

ssh_public_key_file
  The SSH public key file path. Supports environment variable interpolation (e.g., $HOME). Default is $HOME/.ssh/id_rsa.pub

ssh_private_key_file
  The SSH private key file path. Supports environment variable interpolation (e.g., $HOME). Default is $HOME/.ssh/id_rsa

s3_region
  The AWS region

s3_access_key_id
  The IAM access key ID

s3_secret_access_key
  The IAM secret access key

s3_bucket
  The IAM secret access key

s3_base_dir
  The base directory for the S3 bucket

azure_storage_account
  The Azure storage account name

azure_container
  The Azure container name

azure_shared_key
  The Azure storage account key

azure_base_dir
  The base directory for the Azure container

retention
  The retention time in days, weeks, months, years. Default is 7, - , - , -

retention_interval
  The retention check interval. Default is 300

log_type
  The logging type (console, file, syslog). Default is console

log_level
  The logging level, any of the (case insensitive) strings FATAL, ERROR, WARN, INFO and DEBUG
  (that can be more specific as DEBUG1 thru DEBUG5). Debug level greater than 5 will be set to DEBUG5.
  Not recognized values will make the log_level be INFO. Default is info

log_path
  The log file location. Default is pgmoneta.log. Can be a strftime(3) compatible string

log_rotation_age
  The time after which log file rotation is triggered. If this value is specified without units,
  it is taken as seconds. Setting this parameter to 0 disables log rotation based on time.
  It supports the following units as suffixes: 'S' for seconds (default), 'M' for minutes,
  'H' for hours, 'D' for days, and 'W' for weeks.
  Default is 0 (disabled)

log_rotation_size
  The size of the log file that will trigger a log rotation. Supports suffixes: B (bytes), the default if omitted,
  K or KB (kilobytes), M or MB (megabytes), G or GB (gigabytes). A value of 0 (with or without suffix) disables.
  Default is 0

log_line_prefix
  A strftime(3) compatible string to use as prefix for every log line. Must be quoted if contains spaces.
  Default is %Y-%m-%d %H:%M:%S

log_mode
  Append to or create the log file (append, create). Default is append

blocking_timeout
  The number of seconds the process will be blocking for a connection. If this value is specified without units,
  it is taken as seconds. Setting this parameter to 0 disables it. It supports the following units as suffixes:
  'S' for seconds (default), 'M' for minutes, 'H' for hours, 'D' for days, and 'W' for weeks.
  Default is 30

backup_max_rate
  The number of bytes of tokens added every one second to limit the backup rate. Use 0 to disable. Default is 0

network_max_rate
  The number of bytes of tokens added every one second to limit the netowrk backup rate. Use 0 to disable. Default is 0

progress
  Enable backup progress tracking. Default is off

tls
  Enable Transport Layer Security (TLS). Default is false

tls_cert_file
  Certificate file for TLS

tls_key_file
  Private key file for TLS

tls_ca_file
  Certificate Authority (CA) file for TLS

metrics_cert_file
  Certificate file for TLS for Prometheus metrics

metrics_key_file
  Private key file for TLS for Prometheus metrics

metrics_ca_file
  Certificate Authority (CA) file for TLS for Prometheus metrics

libev
  The libev backend to use. Valid options: auto, select, poll, epoll, iouring, devpoll and port. Default is auto

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

direct_io
  Direct I/O support for local storage (off, auto, on). When on, bypasses kernel page cache using O_DIRECT. When auto, attempts O_DIRECT and falls back to buffered I/O if unsupported. Linux only. Default is off

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

extra
  The source directory for retrieval on the server side

wal_slot
  The WAL slot. Mandatory

create_slot
  Create a replication slot for all server. Valid values are: yes, no. Default is no

follow
  Failover to this server if follow server fails

retention
  The retention for the server in days, weeks, months, years

wal_shipping
  The WAL shipping directory

workspace
  The directory for the workspace that incremental backup can use for its work.
  Default is /tmp/pgmoneta-workspace/

hot_standby
  Hot standby directory. Single directory or comma separated directories up to 8 (e.g., /path/to/hot/standby1,/path/to/hot/standby2)

hot_standby_overrides
  Files to override in the hot standby directory. If multiple hot standbys are specified then
  this setting is separated by a |

hot_standby_tablespaces
  Tablespace mappings for the hot standby. Syntax is [from -> to,?]+. If multiple hot standbys are specified
  then this setting is separated by a |

workers
  The number of workers that each process can use for its work.
  Use 0 to disable, -1 means use the global settting.  Maximum is CPU count.
  Default is -1

backup_max_rate
  The number of bytes of tokens added every one second to limit the backup rate. Use 0 to disable, -1 means use the global settting. Default is -1

network_max_rate
  The number of bytes of tokens added every one second to limit the netowrk backup rate. Use 0 to disable, -1 means use the global settting. Default is -1

progress
  Enable backup progress tracking. Use on to enable, off to disable, -1 means use the global setting. Default is -1

verification
  The time between verification of a backup. If this value is specified without units,
  it is taken as seconds. Setting this parameter to 0 disables verification. It supports the
  following units as suffixes: 'S' for seconds (default), 'M' for minutes, 'H' for hours, 'D'
  for days, and 'W' for weeks. Default is 0 (disabled).

tls_cert_file
  Certificate file for TLS. This file must be owned by either the user running pgmoneta or root.

tls_key_file
  Private key file for TLS. This file must be owned by either the user running pgmoneta or root. Additionally permissions must be at least 0640 when owned by root or 0600 otherwise.

tls_ca_file
  Certificate Authority (CA) file for TLS. This file must be owned by either the user running pgmoneta or root.

REPORTING BUGS
==============

pgmoneta is maintained on GitHub at https://github.com/pgmoneta/pgmoneta

COPYRIGHT
=========

pgmoneta is licensed under the 3-clause BSD License.

SEE ALSO
========

pgmoneta(1), pgmoneta-cli(1), pgmoneta-cli.conf(5), pgmoneta-admin(1)
