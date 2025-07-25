========
pgmoneta
========

----------------------------------------
Backup / restore solution for PostgreSQL
----------------------------------------

:Manual section: 1

SYNOPSIS
========

pgmoneta [ -c CONFIG_FILE ] [ -d ]

DESCRIPTION
===========

pgmoneta is a backup / restore solution for PostgreSQL.

OPTIONS
=======

-c, --config CONFIG_FILE
  Set the path to the pgmoneta.conf file

-u, --users USERS_FILE
  Set the path to the pgmoneta_users.conf file

-A, --admins ADMINS_FILE
  Set the path to the pgmoneta_admins.conf file

-D, --directory DIRECTORY_PATH
  Set the path to load configuration files

-d, --daemon
  Run as a daemon

-V, --version
  Display version information

-?, --help
  Display help

REPORTING BUGS
==============

pgmoneta is maintained on GitHub at https://github.com/pgmoneta/pgmoneta

COPYRIGHT
=========

pgmoneta is licensed under the 3-clause BSD License.

SEE ALSO
========

pgmoneta.conf(5), pgmoneta-cli(1), pgmoneta-admin(1)
