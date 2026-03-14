=====================
pgmoneta-walinfo
=====================

Command line utility to read and display Write-Ahead Log (WAL) files

:Manual section: 1

SYNOPSIS
========

pgmoneta-walinfo <file|directory|tar_archive>

DESCRIPTION
===========

pgmoneta-walinfo is a command line utility to read and display information about PostgreSQL Write-Ahead Log (WAL) files. It supports individual WAL files, directories containing WAL files, TAR archives (optionally compressed with .gz, .lz4, .zstd, or .bz2), and encrypted files (.aes). It provides details of the WAL file in either raw or JSON format.

In addition to standard WAL files, pgmoneta-walinfo also supports:

- Encrypted WAL files (**.aes**)
- Compressed WAL files: **.zstd**, **.gz**, **.lz4**, and **.bz2**
- TAR archives containing WAL files (**.tar**)
- Directories containing WAL files

OPTIONS
=======

-I, --interactive 
  Interactive mode for WAL files with ncurses

-c, --config CONFIG_FILE
  Set the path to the pgmoneta.conf file

-u, --users USERS_FILE
  Set the path to the pgmoneta_users.conf file

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

-S, --summary
  Show a summary of WAL record counts grouped by resource manager

-V, --version
  Display version information

-t, --translate
  Translate the OIDs in the XLOG records to the corresponding object (database/tablespace/relation) names

-m, --mapping
  The JSON file that contains the mapping of the OIDs to the corresponding object names

-RT, --tablespaces
  Filter on tablspaces

-RD, --databases
  Filter on databases

-RT, --relations
  Filter on relations

-R,   --filter
  Combination of -RT, -RD, -RR

-?, --help
  Display help and usage information.

ARGUMENTS
=========

<file|directory|tar_archive>
  The path to the WAL file, directory containing WAL files, or TAR archive to be analyzed.

USAGE
=====

To display information about a WAL file in raw format:

    pgmoneta-walinfo /path/to/walfile

To display information about a WAL file in interactive mode using ncurses
    
    pgmoneta-walinfo -I /path/to/walfile

To display information in JSON format:

    pgmoneta-walinfo -F json /path/to/walfile

To analyze WAL files from a TAR archive:

    pgmoneta-walinfo /path/to/wal_backup.tar.gz

To display information and translate the OIDs to the corresponding object names:

    pgmoneta-walinfo -c pgmoneta_walinfo.conf -t -m /path/to/mapping.json /path/to/walfile
    or
    pgmoneta-walinfo -c pgmoneta_walinfo.conf -t -u /path/to/pgmoneta_users.conf /path/to/walfile

REPORTING BUGS
==============

pgmoneta is maintained on GitHub at https://github.com/pgmoneta/pgmoneta

COPYRIGHT
=========

pgmoneta-walinfo is licensed under the 3-clause BSD License.

SEE ALSO
========

pgmoneta.conf(5), pgmoneta(1), pgmoneta-admin(1)
