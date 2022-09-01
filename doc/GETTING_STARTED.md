# Getting started with pgmoneta

First of all, make sure that `pgmoneta` is installed and in your path by
using `pgmoneta -?`. You should see

```
pgmoneta 0.6.0
  Backup / restore solution for PostgreSQL

Usage:
  pgmoneta [ -c CONFIG_FILE ] [ -u USERS_FILE ] [ -d ]

Options:
  -c, --config CONFIG_FILE Set the path to the pgmoneta.conf file
  -u, --users USERS_FILE   Set the path to the pgmoneta_users.conf file
  -A, --admins ADMINS_FILE Set the path to the pgmoneta_admins.conf file
  -d, --daemon             Run as a daemon
  -V, --version            Display version information
  -?, --help               Display help
```

If you don't have `pgmoneta` in your path see [README](../README.md) on how to
compile and install `pgmoneta` in your system.

## Configuration

Lets create a simple configuration file called `pgmoneta.conf` with the content

```
[pgmoneta]
host = *
metrics = 5001

base_dir = /home/pgmoneta

compression = zstd

storage_engine = local

retention = 7

log_type = file
log_level = info
log_path = /tmp/pgmoneta.log

unix_socket_dir = /tmp/
pgsql_dir = /usr/bin/

[primary]
host = localhost
port = 5432
user = repl
```

In our main section called `[pgmoneta]` we setup `pgmoneta` to listen on all
network addresses. We will enable Prometheus metrics on port 5001 and have the backups
live in the `/home/pgmoneta` directory. All backups are being compressed with zstd and kept
for 7 days. Logging will be performed at `info` level and
put in a file called `/tmp/pgmoneta.log`. Last we specify the
location of the `unix_socket_dir` used for management operations and the path for the
PostgreSQL command line tools.

Next we create a section called `[primary]` which has the information about our
[PostgreSQL](https://www.postgresql.org) instance. In this case it is running
on `localhost` on port `5432` and we will use the `repl` user account to connect.

The `repl` user must have the `REPLICATION` role and have access to the `postgres` database,
so for example

```
CREATE ROLE repl WITH LOGIN REPLICATION PASSWORD 'secretpassword';
```

and in `pg_hba.conf`

```
local   postgres        repl                                   scram-sha-256
host    postgres        repl           127.0.0.1/32            scram-sha-256
host    postgres        repl           ::1/128                 scram-sha-256
host    replication     repl           127.0.0.1/32            scram-sha-256
host    replication     repl           ::1/128                 scram-sha-256
```

The authentication type should be based on `postgresql.conf`'s `password_encryption` value.

Optionally, create a physical replication slot that can be used for Write-Ahead Log streaming,
like

```
SELECT pg_create_physical_replication_slot('repl');
```

and add that to the `pgmoneta.conf` configuration under `[primary]`, as

```
wal_slot = repl
```

We will need a user vault for the `repl` account, so the following commands will add
a master key, and the `repl` password

```
pgmoneta-admin master-key
pgmoneta-admin -f pgmoneta_users.conf add-user
```

We are now ready to run `pgmoneta`.

See [Configuration](./CONFIGURATION.md) for all configuration options.

## Running

We will run `pgmoneta` using the command

```
pgmoneta -c pgmoneta.conf -u pgmoneta_users.conf
```

If this doesn't give an error, then we are ready to do backups.

`pgmoneta` is stopped by pressing Ctrl-C (`^C`) in the console where you started it, or by sending
the `SIGTERM` signal to the process using `kill <pid>`.

## Run-time administration

`pgmoneta` has a run-time administration tool called `pgmoneta-cli`.

You can see the commands it supports by using `pgmoneta-cli -?` which will give

```
pgmoneta-cli 0.6.0
  Command line utility for pgmoneta

Usage:
  pgmoneta-cli [ -c CONFIG_FILE ] [ COMMAND ]

Options:
  -c, --config CONFIG_FILE Set the path to the pgmoneta.conf file
  -h, --host HOST          Set the host name
  -p, --port PORT          Set the port number
  -U, --user USERNAME      Set the user name
  -P, --password PASSWORD  Set the password
  -L, --logfile FILE       Set the log file
  -v, --verbose            Output text string of result
  -V, --version            Display version information
  -?, --help               Display help

Commands:
  backup                   Backup a server
  list-backup              List the backups for a server
  restore                  Restore a backup from a server
  archive                  Archive a backup from a server
  delete                   Delete a backup from a server
  retain                   Retain a backup from a server
  expunge                  Expunge a backup from a server
  is-alive                 Is pgmoneta alive
  stop                     Stop pgmoneta
  status                   Status of pgmoneta
  details                  Detailed status of pgmoneta
  reload                   Reload the configuration
  reset                    Reset the Prometheus statistics
```

This tool can be used on the machine running `pgmoneta` to do a backup like

```
pgmoneta-cli -c pgmoneta.conf backup primary
```

A restore would be

```
pgmoneta-cli -c pgmoneta.conf restore primary <timestamp> /path/to/restore
```

To stop pgmoneta you would use

```
pgmoneta-cli -c pgmoneta.conf stop
```

Check the outcome of the operations by verifying the exit code, like

```
echo $?
```

or by using the `-v` flag.

If pgmoneta has both Transport Layer Security (TLS) and `management` enabled then `pgmoneta-cli` can
connect with TLS using the files `~/.pgmoneta/pgmoneta.key` (must be 0600 permission),
`~/.pgmoneta/pgmoneta.crt` and `~/.pgmoneta/root.crt`.

## Administration

`pgmoneta` has an administration tool called `pgmoneta-admin`, which is used to control user
registration with `pgmoneta`.

You can see the commands it supports by using `pgmoneta-admin -?` which will give

```
pgmoneta-admin 0.6.0
  Administration utility for pgmoneta

Usage:
  pgmoneta-admin [ -f FILE ] [ COMMAND ]

Options:
  -f, --file FILE         Set the path to a user file
  -U, --user USER         Set the user name
  -P, --password PASSWORD Set the password for the user
  -g, --generate          Generate a password
  -l, --length            Password length
  -V, --version           Display version information
  -?, --help              Display help

Commands:
  master-key              Create or update the master key
  add-user                Add a user
  update-user             Update a user
  remove-user             Remove a user
  list-users              List all users
```

In order to set the master key for all users you can use

```
pgmoneta-admin -g master-key
```

The master key must be at least 8 characters.

Then use the other commands to add, update, remove or list the current user names, f.ex.

```
pgmoneta-admin -f pgmoneta_users.conf add-user
```

## Next Steps

Next steps in improving pgmoneta's configuration could be

* Update `pgmoneta.conf` with the required settings for your system
* Enable Transport Layer Security v1.2+ (TLS) for administrator access

See [Configuration](./CONFIGURATION.md) for more information on these subjects.

## Tutorials

There are a few short tutorials available to help you better understand and configure `pgmoneta`:
- [Installing pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/master/doc/tutorial/01_install.md)
- [Using a replication slot](https://github.com/pgmoneta/pgmoneta/blob/master/doc/tutorial/02_replication_slot.md)
- [Enabling remote management](https://github.com/pgmoneta/pgmoneta/blob/master/doc/tutorial/03_remote_management.md)
- [Enabling Prometheus metrics](https://github.com/pgmoneta/pgmoneta/blob/master/doc/tutorial/04_prometheus.md)
- [Doing backup and restore](https://github.com/pgmoneta/pgmoneta/blob/master/doc/tutorial/05_backup_restore.md)
- [Creating an archive](https://github.com/pgmoneta/pgmoneta/blob/master/doc/tutorial/06_archive.md)
- [Deleting a backup](https://github.com/pgmoneta/pgmoneta/blob/master/doc/tutorial/07_delete.md)

## Closing

The [pgmoneta](https://github.com/pgmoneta/pgmoneta) community hopes that you find
the project interesting.

Feel free to

* [Ask a question](https://github.com/pgmoneta/pgmoneta/discussions)
* [Raise an issue](https://github.com/pgmoneta/pgmoneta/issues)
* [Submit a feature request](https://github.com/pgmoneta/pgmoneta/issues)
* [Write a code submission](https://github.com/pgmoneta/pgmoneta/pulls)

All contributions are most welcome !

Please, consult our [Code of Conduct](../CODE_OF_CONDUCT.md) policies for interacting in our
community.

Consider giving the project a [star](https://github.com/pgmoneta/pgmoneta/stargazers) on
[GitHub](https://github.com/pgmoneta/pgmoneta/) if you find it useful. And, feel free to follow
the project on [Twitter](https://twitter.com/pgmoneta/) as well.
