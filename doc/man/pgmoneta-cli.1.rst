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
  Set the path to the pgmoneta.conf file

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

-?, --help
  Display help

COMMANDS
========

backup
  Backup a server

list-backup
  List the backups for a server

restore
  Restore a backup from a server

verify
  Verify a backup from a server

archive
  Archive a backup from a server

delete
  Delete a backup from a server

retain
  Retain a backup from a server - exclude deletion by retention policy

expunge
  Expunge a backup from a server - include in deletion by retention policy

encrypt
  Encrypt the file in place, remove unencrypted file after successful encryption.

decrypt
  Decrypt the file in place, remove encrypted file after successful decryption.

compress
  Compress the file in place, remove uncompressed file after successful compression.

decompress
  Decompress the file in place, remove compressed file after successful decompression.

info
  Information about a backup

annotate
  Annotate a backup with comments

ping
  Check if pgmoneta is alive

stop
  Stop pgmoneta

status [details]
  Status of pgmoneta

conf [reload]
  Reload the configuration

clear [prometheus]
  Reset the Prometheus statistics

REPORTING BUGS
==============

pgmoneta is maintained on GitHub at https://github.com/pgmoneta/pgmoneta

COPYRIGHT
=========

pgmoneta is licensed under the 3-clause BSD License.

SEE ALSO
========

pgmoneta.conf(5), pgmoneta(1), pgmoneta-admin(1)
