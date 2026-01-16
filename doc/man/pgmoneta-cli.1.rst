============
pgmoneta-cli
============

---------------------------------
Command line utility for pgmoneta
---------------------------------

:Manual section: 1

SYNOPSIS
========

pgmoneta-cli [ -c CONFIG_FILE ] [ COMMAND ]

DESCRIPTION
===========

pgmoneta-cli is a command line utility for pgmoneta.

OPTIONS
=======

-c, --config CONFIG_FILE
  Set the path to the pgmoneta_cli.conf file

-h, --host HOST
  Set the host name

-p, --port PORT
  Set the port number

-U, --user USERNAME
  Set the user name

-P, --password PASSWORD
  Set the password

-L, --logfile FILE
  Set the logfile

-v, --verbose
  Output text string of result

-V, --version
  Display version information

-F, --format text|json|raw
  Set the output format

-C, --compress none|gz|zstd|lz4|bz2
  Compress the wire protocol

-E, --encrypt none|aes|aes256|aes192|aes128
  Encrypt the wire protocol

-s, --sort asc|desc
  Sort result (for list-backup)

--cascade
  Cascade a retain/expunge backup

-?, --help
  Display help

COMMANDS
========

annotate
  Annotate a backup with comments

archive
  Archive a backup from a server

backup
  Backup a server

clear [prometheus]
  Clear Prometheus data

compress
  Compress a file using configured method

conf [get|ls|reload|set]
  Manage the configuration

decompress
  Decompress a file using configured method

decrypt
  Decrypt a file using master-key

delete
  Delete a backup from a server

encrypt
  Encrypt a file using master-key

expunge
  Expunge a backup from a server

info
  Information about a backup

list-backup
  List the backups for a server

mode
  Switch the mode for a server

ping
  Check if pgmoneta is alive

restore
  Restore a backup from a server

retain
  Retain a backup from a server

shutdown
  Shutdown pgmoneta

status [details]
  Status of pgmoneta, with optional details

verify
  Verify a backup from a server

REPORTING BUGS
==============

pgmoneta is maintained on GitHub at https://github.com/pgmoneta/pgmoneta

COPYRIGHT
=========

pgmoneta is licensed under the 3-clause BSD License.

SEE ALSO
========

pgmoneta-cli.conf(5), pgmoneta.conf(5), pgmoneta(1), pgmoneta-admin(1)
