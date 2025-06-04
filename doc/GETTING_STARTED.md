# Getting started with pgmoneta

First of all, make sure that `pgmoneta` is installed and in your path by
using `pgmoneta -?`. You should see

```
pgmoneta 0.17.2
  Backup / restore solution for PostgreSQL

Usage:
  pgmoneta [ -c CONFIG_FILE ] [ -u USERS_FILE ] [ -d ]

Options:
  -c, --config CONFIG_FILE Set the path to the pgmoneta.conf file
  -u, --users USERS_FILE   Set the path to the pgmoneta_users.conf file
  -A, --admins ADMINS_FILE Set the path to the pgmoneta_admins.conf file
  -d, --daemon             Run as a daemon
      --offline            Run in offline mode
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

[primary]
host = localhost
port = 5432
user = repl
wal_slot = repl
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
on `localhost` on port `5432` and we will use the `repl` user account to connect, and the
Write+Ahead slot will be named `repl` as well.

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

Then, create a physical replication slot that will be used for Write-Ahead Log streaming,
like

```
SELECT pg_create_physical_replication_slot('repl', true, false);
```

Alternatively, configure automatically slot creation by adding `create_slot = yes` to `[pgmoneta]`
or corresponding server section

We will need a user vault for the `repl` account, so the following commands will add
a master key, and the `repl` password

```
pgmoneta-admin master-key
pgmoneta-admin -f pgmoneta_users.conf user add
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
pgmoneta-cli 0.17.2
  Command line utility for pgmoneta

Usage:
  pgmoneta-cli [ -c CONFIG_FILE ] [ COMMAND ]

Options:
  -c, --config CONFIG_FILE                        Set the path to the pgmoneta.conf file
  -h, --host HOST                                 Set the host name
  -p, --port PORT                                 Set the port number
  -U, --user USERNAME                             Set the user name
  -P, --password PASSWORD                         Set the password
  -L, --logfile FILE                              Set the log file
  -v, --verbose                                   Output text string of result
  -V, --version                                   Display version information
  -F, --format text|json|raw                      Set the output format
  -C, --compress none|gz|zstd|lz4|bz2             Compress the wire protocol
  -E, --encrypt none|aes|aes256|aes192|aes128     Encrypt the wire protocol
  -?, --help                                      Display help

Commands:
  annotate                 Annotate a backup with comments
  archive                  Archive a backup from a server
  backup                   Backup a server
  clear <what>             Clear data, with:
                           - 'prometheus' to reset the Prometheus statistics
  compress                 Compress a file using configured method
  conf <action>            Manage the configuration, with one of subcommands:
                           - 'get' to obtain information about a runtime configuration value
                             conf get <parameter_name>
                           - 'ls' to print the configurations used
                           - 'reload' to reload the configuration
                           - 'set' to modify a configuration value;
                             conf set <parameter_name> <parameter_value>;
  decompress               Decompress a file using configured method
  decrypt                  Decrypt a file using master-key
  delete                   Delete a backup from a server
  encrypt                  Encrypt a file using master-key
  expunge                  Expunge a backup from a server
  info                     Information about a backup
  list-backup              List the backups for a server
  ping                     Check if pgmoneta is alive
  restore                  Restore a backup from a server
  retain                   Retain a backup from a server
  shutdown                 Shutdown pgmoneta
  status [details]         Status of pgmoneta, with optional details
  verify                   Verify a backup from a server
```

This tool can be used on the machine running `pgmoneta` to do a backup like

```
pgmoneta-cli -c pgmoneta.conf backup primary
```

A restore would be

```
pgmoneta-cli -c pgmoneta.conf restore primary <timestamp> /path/to/restore
```

To shutdown pgmoneta you would use

```
pgmoneta-cli -c pgmoneta.conf shutdown
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
pgmoneta-admin 0.17.2
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
  user <subcommand>       Manage a specific user, where <subcommand> can be
                          - add  to add a new user
                          - del  to remove an existing user
                          - edit to change the password for an existing user
                          - ls   to list all available users

```

In order to set the master key for all users you can use

```
pgmoneta-admin -g master-key
```

The master key must be at least 8 characters if provided interactively.

For scripted use, the master key can be provided using the `PGMONETA_PASSWORD` environment variable.

Then use the other commands to add, update, remove or list the current user names, f.ex.

```
pgmoneta-admin -f pgmoneta_users.conf user add
```

For scripted use, the user password can be provided using the `PGMONETA_PASSWORD` environment variable.

## Next Steps

Next steps in improving pgmoneta's configuration could be

* Update `pgmoneta.conf` with the required settings for your system
* Enable Transport Layer Security v1.2+ (TLS) for administrator access

See [Configuration](./CONFIGURATION.md) for more information on these subjects.

## Tutorials

There are a few short tutorials available to help you better understand and configure `pgmoneta`:
- [Installing pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/01_install.md)
- [Enabling remote management](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/02_remote_management.md)
- [Enabling Prometheus metrics](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/03_prometheus.md)
- [Doing backup and restore](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/04_backup_restore.md)
- [Verify a backup](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/05_verify.md)
- [Creating an archive](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/06_archive.md)
- [Deleting a backup](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/07_delete.md)
- [Encryption and decryption](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/08_encryption.md)
- [Retention](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/09_retention.md)
- [Enabling Grafana dashboard](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/10_grafana.md)
- [Add WAL shipping](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/11_wal_shipping.md)
- [Working with Transport Level Security](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/12_tls.md)
- [Hot standby](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/13_hot_standby.md)
- [Annotate](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/14_annotate.md)
- [Extra files](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/15_extra.md)
- [Incremental backup and restore](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/16_incremental_backup_restore.md)
- [Dockerize pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/17_docker.md)

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
