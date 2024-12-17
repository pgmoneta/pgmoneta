=====================
pgmoneta-walinfo
=====================

-------------------------------------------------
Command line utility to read and display Write-Ahead Log (WAL) files
-------------------------------------------------

:Manual section: 1

SYNOPSIS
========

pgmoneta-walinfo <file>

DESCRIPTION
===========

pgmoneta-walinfo is a command line utility to read and display information about PostgreSQL Write-Ahead Log (WAL) files. It provides details of the WAL file in either raw or JSON format.

OPTIONS
=======

-c, --config CONFIG_FILE
  Set the path to the pgmoneta.conf file

-o, --output FILE
  Output file

-F, --format raw|json
  Set the output format. Default is `raw`.

-L, --logfile FILE
  Set the log file

-q, --quiet
  No output only result

--color
  Use colors (on, off)

-v, --verbose
  Output result

-V, --version
  Display version information

-?, --help
  Display help and usage information.

ARGUMENTS
=========

<file>
  The path to the WAL file to be analyzed.

USAGE
=====

To display information about a WAL file in raw format:

    pgmoneta-walinfo /path/to/walfile

To display information in JSON format:

    pgmoneta-walinfo -F json /path/to/walfile

REPORTING BUGS
==============

pgmoneta is maintained on GitHub at https://github.com/pgmoneta/pgmoneta

COPYRIGHT
=========

pgmoneta-walinfo is licensed under the 3-clause BSD License.

SEE ALSO
========

pgmoneta.conf(5), pgmoneta(1), pgmoneta-admin(1)
