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

archive
  Archive a backup from a server

delete
  Delete a backup from a server

retain
  Retain a backup from a server - exclude deletion by retention policy

expunge
  Expunge a backup from a server - include in deletion by retention policy

is-alive
  Is pgmoneta alive

stop
  Stop pgmoneta

status
  Status of pgmoneta

details
  Detailed status of pgmoneta

reload
  Reload the configuration

reset
  Reset the Prometheus statistics

decrypt
  Decrypt the file in place, remove encrypted file after successful decryption.

REPORTING BUGS
==============

pgmoneta is maintained on GitHub at https://github.com/pgmoneta/pgmoneta

COPYRIGHT
=========

pgmoneta is licensed under the 3-clause BSD License.

SEE ALSO
========

pgmoneta.conf(5), pgmoneta(1), pgmoneta-admin(1)
