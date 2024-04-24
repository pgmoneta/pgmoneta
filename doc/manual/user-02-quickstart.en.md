\newpage

# Quick start

## Compiling the source

We recommend using `Fedora` to test and run pgmoneta, but other Linux systems and macOS are also supported.

`pgmoneta` requires

* [gcc 8+](https://gcc.gnu.org) (C17)
* [cmake](https://cmake.org)
* [make](https://www.gnu.org/software/make/)
* [libev](http://software.schmorp.de/pkg/libev.html)
* [OpenSSL](http://www.openssl.org/)
* [zlib](https://zlib.net)
* [zstd](http://www.zstd.net)
* [lz4](https://lz4.github.io/lz4/)
* [bzip2](http://sourceware.org/bzip2/)
* [systemd](https://www.freedesktop.org/wiki/Software/systemd/)
* [rst2man](https://docutils.sourceforge.io/)
* [libssh](https://www.libssh.org/)
* [libcurl](https://curl.se/libcurl/)
* [libarchive](http://www.libarchive.org/)
* [cJSON](https://github.com/DaveGamble/cJSON)
* [pandoc](https://pandoc.org/)
* [texlive](https://www.tug.org/texlive/)

```sh
dnf install git gcc cmake make libev libev-devel openssl openssl-devel systemd systemd-devel zlib zlib-devel libzstd libzstd-devel lz4 lz4-devel libssh libssh-devel libcurl libcurl-devel python3-docutils libatomic bzip2 bzip2-devel libarchive libarchive-devel cjson cjson-devel pandoc texlive-scheme-basic 'tex(footnote.sty)'
```

Alternative [clang 8+](https://clang.llvm.org/) can be used.

## Build

### Release build

The following commands will install `pgmoneta` in the `/usr/local` hierarchy.

```sh
git clone https://github.com/pgmoneta/pgmoneta.git
cd pgmoneta
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
sudo make install
```

See [RPM](https://github.com/pgmoneta/pgmoneta/blob/main/doc/RPM.md) for how to build a RPM of `pgmoneta`.

### Debug build

The following commands will create a `DEBUG` version of `pgmoneta`.

```sh
git clone https://github.com/pgmoneta/pgmoneta.git
cd pgmoneta
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

## Check

Make sure that `pgmoneta` is installed and in your path by using `pgmoneta -?`. You should see

``` console
pgmoneta 0.11.0
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

If you encounter any issues following the above steps, you can refer to the **Developer Guide** under **Install pgmoneta** to see how to compile and install pgmoneta on your system.

## Configuration

Lets create a simple configuration file called `pgmoneta.conf` with the content

``` ini
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

In our main section called `[pgmoneta]` we setup `pgmoneta` to listen on all network addresses. We will enable Prometheus metrics on port 5001 and have the backups live in the `/home/pgmoneta` directory. All backups are being compressed with zstd and kept for 7 days. Logging will be performed at `info` level and put in a file called `/tmp/pgmoneta.log`. Last we specify the location of the `unix_socket_dir` used for management operations and the path for the PostgreSQL command line tools.

Next we create a section called `[primary]` which has the information about our [PostgreSQL](https://www.postgresql.org) instance. In this case it is running on `localhost` on port `5432` and we will use the `repl` user account to connect, and the Write+Ahead slot will be named `repl` as well.

The `repl` user must have the `REPLICATION` role and have access to the `postgres` database,
so for example

``` sh
CREATE ROLE repl WITH LOGIN REPLICATION PASSWORD 'secretpassword';
```

and in `pg_hba.conf`

``` ini
local   postgres        repl                                   scram-sha-256
host    postgres        repl           127.0.0.1/32            scram-sha-256
host    postgres        repl           ::1/128                 scram-sha-256
host    replication     repl           127.0.0.1/32            scram-sha-256
host    replication     repl           ::1/128                 scram-sha-256
```

The authentication type should be based on `postgresql.conf`'s `password_encryption` value.

Then, create a physical replication slot that will be used for Write-Ahead Log streaming, like

``` sh
SELECT pg_create_physical_replication_slot('repl', true, false);
```

Alternatively, configure automatically slot creation by adding `create_slot = yes` to `[pgmoneta]` or corresponding server section.

We will need a user vault for the `repl` account, so the following commands will add a master key, and the `repl` password. The master key should be longer than 8 characters.

``` sh
pgmoneta-admin master-key
pgmoneta-admin -f pgmoneta_users.conf user add
```

We are now ready to run `pgmoneta`.

See charpter **Configuration** for all configuration options.

## Running

We will run `pgmoneta` using the command

``` sh
pgmoneta -c pgmoneta.conf -u pgmoneta_users.conf
```

If this doesn't give an error, then we are ready to do backups.

`pgmoneta` is stopped by pressing Ctrl-C (`^C`) in the console where you started it, or by sending the `SIGTERM` signal to the process using `kill <pid>`.

## Run-time administration

`pgmoneta` has a run-time administration tool called `pgmoneta-cli`.

You can see the commands it supports by using `pgmoneta-cli -?` which will give

``` console
pgmoneta-cli 0.11.0
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
  encrypt                  Encrypt a file using master-key
  decrypt                  Decrypt a file using master-key
  ping                     Check if pgmoneta is alive
  stop                     Stop pgmoneta
  status [details]         Status of pgmoneta, with optional details
  conf <action>            Manage the configuration, with one of subcommands:
                           - 'reload' to reload the configuration
  clear <what>             Clear data, with:
                           - 'prometheus' to reset the Prometheus statistics
```

This tool can be used on the machine running `pgmoneta` to do a backup like

``` sh
pgmoneta-cli -c pgmoneta.conf backup primary
```

A restore would be

``` sh
pgmoneta-cli -c pgmoneta.conf restore primary <timestamp> /path/to/restore
```

To stop pgmoneta you would use

``` sh
pgmoneta-cli -c pgmoneta.conf stop
```

Check the outcome of the operations by verifying the exit code, like

``` sh
echo $?
```

or by using the `-v` flag.

If pgmoneta has both Transport Layer Security (TLS) and `management` enabled then `pgmoneta-cli` can connect with TLS using the files `~/.pgmoneta/pgmoneta.key` (must be 0600 permission), `~/.pgmoneta/pgmoneta.crt` and `~/.pgmoneta/root.crt`.

## Administration

`pgmoneta` has an administration tool called `pgmoneta-admin`, which is used to control user registration with `pgmoneta`.

You can see the commands it supports by using `pgmoneta-admin -?` which will give

``` console
pgmoneta-admin 0.11.0
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

``` sh
pgmoneta-admin -g master-key
```

The master key must be at least 8 characters.

Then use the other commands to add, update, remove or list the current user names, f.ex.

``` sh
pgmoneta-admin -f pgmoneta_users.conf user add
```

## Next Steps

Next steps in improving pgmoneta's configuration could be

* Update `pgmoneta.conf` with the required settings for your system
* Enable Transport Layer Security v1.2+ (TLS) for administrator access

See [Configuration](https://github.com/pgmoneta/pgmoneta/blob/main/doc/CONFIGURATION.md) for more information on these subjects.

## Tutorials

There are a few short tutorials available to help you better understand and configure `pgmoneta`:

* [Installing pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/01_install.md)
* [Using a replication slot](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/02_replication_slot.md)
* [Enabling remote management](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/03_remote_management.md)
* [Enabling Prometheus metrics](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/04_prometheus.md)
* [Doing backup and restore](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/05_backup_restore.md)
* [Creating an archive](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/06_archive.md)
* [Deleting a backup](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/07_delete.md)
* [Encryption and decryption](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/08_encryption.md)
* [Retention](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/09_retention.md)
* [Enabling Grafana dashboard](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/10_grafana.md)
* [Add WAL shipping](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/11_wal_shipping.md)
* [Working with Transport Level Security](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/12_tls.md)