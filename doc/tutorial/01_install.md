## Install pgmoneta

This tutorial will show you how to do a simple installation of [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

At the end of this tutorial you will have a backup of a PostgreSQL cluster,
and will be streaming Write-Ahead Log (WAL) to [**pgmoneta**](https://github.com/pgmoneta/pgmoneta). 

Please note that inside the brackets at the end of each step it's the user account
you should be using, switch the account when needed.

### Preface

This tutorial assumes that you have an installation of PostgreSQL 13+ and [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

For RPM based distributions such as Fedora and RHEL you can add the
[PostgreSQL YUM repository](https://yum.postgresql.org/) and do the install via

```
dnf -qy module disable postgresql
dnf install -y postgresql13 postgresql13-server pgmoneta
```

### Initialize cluster
```
export PATH=/usr/pgsql-13/bin:$PATH
initdb /tmp/pgsql
```

(`postgres` user)

### Remove default access

Remove

```
host    all             all             127.0.0.1/32            trust
host    all             all             ::1/128                 trust
host    replication     all             127.0.0.1/32            trust
host    replication     all             ::1/128                 trust
```

from `/tmp/pgsql/pg_hba.conf`

(`postgres` user)

### Add access for users and a database

Add

```
host    mydb             myuser          127.0.0.1/32            md5
host    mydb             myuser          ::1/128                 md5
host    postgres         repl            127.0.0.1/32            md5
host    postgres         repl            ::1/128                 md5
host    replication      repl            127.0.0.1/32            md5
host    replication      repl            ::1/128                 md5
```

to `/tmp/pgsql/pg_hba.conf`

Remember to check the value of `password_encryption` in `/tmp/pgsql/postgresql.conf`
to setup the correct authentication type.

(`postgres` user)

### Make sure that replication level is set

Check that

```
wal_level = replica
```

is set in `/tmp/pgsql/postgresql.conf` - or `logical`

(`postgres` user)

### Start PostgreSQL

```
pg_ctl  -D /tmp/pgsql/ start
```

(`postgres` user)

### Add users and a database

```
createuser -P myuser
createdb -E UTF8 -O myuser mydb
```

with `mypass` as the password.

Then

```
psql postgres
CREATE ROLE repl WITH LOGIN REPLICATION PASSWORD 'secretpassword';
\q
```

(`postgres` user)

### Add Write-Ahead Log (WAL) replication slot

```
psql postgres
SELECT pg_create_physical_replication_slot('repl', true, false);
\q
```

(`postgres` user)

### Verify access

For the user (standard) (using `mypass`)

```
psql -h localhost -p 5432 -U myuser mydb
\q
```

For the user (pgmoneta) (using `secretpassword`)

```
psql -h localhost -p 5432 -U repl postgres
\q
```

(`postgres` user)

### Add pgmoneta user

```
sudo su -
useradd -ms /bin/bash pgmoneta
passwd pgmoneta
exit
```

(`postgres` user)

### Create pgmoneta configuration

Switch to the pgmoneta user

```
sudo su -
su - pgmoneta
```

Add the master key and create vault

```
pgmoneta-admin master-key
pgmoneta-admin -f pgmoneta_users.conf -U repl -P secretpassword user add
```
You have to choose a password for the master key - remember it !

If you see an error saying `error while loading shared libraries: libpgmoneta.so.0: cannot open shared object` running 
the above command. you may need to locate where your `libpgmoneta.so.0` is. It could be in `/usr/local/lib` or `/usr/local/lib64`
depending on your environment. Add the corresponding directory into `/etc/ld.so.conf`, or alternatively, create a file
called `pgmoneta_shared_library.conf` under `/etc/ld.so.conf.d/`, and add your directory into it. Remember to run `ldconfig` to make the change effective

Create the `pgmoneta.conf` configuration
```
cat > pgmoneta.conf
[pgmoneta]
host = *
metrics = 5001
create_slot = yes

base_dir = /home/pgmoneta/backup

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

and press `Ctrl-D`

(`postgres` user)

### Create base directory

```
mkdir backup
```

(`pgmoneta` user)

### Start pgmoneta

```
pgmoneta -c pgmoneta.conf -u pgmoneta_users.conf
```

(`pgmoneta` user)

### Create a backup

In another terminal

```
pgmoneta-cli -c pgmoneta.conf backup primary
```

(`pgmoneta` user)

### View backup

In another terminal

```
pgmoneta-cli -c pgmoneta.conf status details
```

(`pgmoneta` user)
### Shell completion

There is a minimal shell completion support for `pgmoneta-cli` and `pgmoneta-admin`. If you are running such commands from a Bash or Zsh, you can take some advantage of command completion.


#### Installing command completions in Bash

There is a completion script into `contrib/shell_comp/pgmoneta_comp.bash` that can be used
to help you complete the command line while you are typing.

It is required to source the script into your current shell, for instance
by doing:

``` shell
source contrib/shell_comp/pgmoneta_comp.bash
```

At this point, the completions should be active, so you can type the name of one the commands between `pgmoneta-cli` and `pgmoneta-admin` and hit `<TAB>` to help the command line completion.

#### Installing the command completions on Zsh

In order to enable completion into `zsh` you first need to have `compinit` loaded;
ensure your `.zshrc` file contains the following lines:

``` shell
autoload -U compinit
compinit
```

and add the sourcing of the `contrib/shell_comp/pgmoneta_comp.zsh` file into your `~/.zshrc`
also associating the `_pgmoneta_cli` and `_pgmoneta_admin` functions
to completion by means of `compdef`:

``` shell
source contrib/shell_comp/pgmoneta_comp.zsh
compdef _pgmoneta_cli    pgmoneta-cli
compdef _pgmoneta_admin  pgmoneta-admin
```

If you want completions only for one command, e.g., `pgmoneta-admin`, remove the `compdef` line that references the command you don't want to have automatic completion.
At this point, digit the name of a `pgmoneta-cli` or `pgmoneta-admin` command and hit `<TAB>` to trigger the completion system.
