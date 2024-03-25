==============
pgmoneta-admin
==============

-----------------------------------
Administration utility for pgmoneta
-----------------------------------

:Manual section: 1

SYNOPSIS
========

pgmoneta-admin [ -f FILE ] [ COMMAND ]

DESCRIPTION
===========

pgmoneta-admin is an administration utility for pgmoneta.

OPTIONS
=======

-f, --file FILE
  Set the path to a user file

-U, --user USER
  Set the user name

-P, --password PASSWORD
  Set the password for the user

-g, --generate
  Generate a password

-l, --length
  Password length

-V, --version
  Display version information

-?, --help
  Display help

COMMANDS
========

master-key
  Create or update the master key. The master key will be created in the pgmoneta user home directory under ~/.pgmoneta

user add
  Add a user

user edit
  Update a user

user del
  Remove a user

user ls
  List all users

REPORTING BUGS
==============

pgmoneta is maintained on GitHub at https://github.com/pgmoneta/pgmoneta

COPYRIGHT
=========

pgmoneta is licensed under the 3-clause BSD License.

SEE ALSO
========

pgmoneta.conf(5), pgmoneta(1), pgmoneta-cli(1)
