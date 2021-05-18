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

stop
  Stop pgmoneta

status
  Status of pgmoneta

details
  Detailed status of pgmoneta

reset
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
