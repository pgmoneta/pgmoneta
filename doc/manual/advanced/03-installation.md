\newpage

# Installation

## Rocky Linux 9.x

We can download the [Rocky Linux](https://www.rockylinux.org/) distruction from their web site

```
https://rockylinux.org/download
```

The installation and setup is beyond the scope of this guide.

Ideally, you would use dedicated user accounts to run [**PostgreSQL**][postgresql] and [**pgmoneta**][pgmoneta]

```
useradd postgres
usermod -a -G wheel postgres
useradd pgmoneta
usermod -a -G wheel pgmoneta
```

Add a configuration directory for [**pgmoneta**][pgmoneta]

```
mkdir /etc/pgmoneta
chown -R pgmoneta:pgmoneta /etc/pgmoneta
```

and lets open the ports in the firewall that we will need

```
firewall-cmd --permanent --zone=public --add-port=5001/tcp
firewall-cmd --permanent --zone=public --add-port=5002/tcp
firewall-cmd --permanent --zone=public --add-port=5003/tcp
```

## PostgreSQL 17

We will install PostgreSQL 17 from the official [YUM repository][yum] with the community binaries,

**x86_64**

```
dnf -qy module disable postgresql
dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-x86_64/pgdg-redhat-repo-latest.noarch.rpm
```

**aarch64**

```
dnf -qy module disable postgresql
dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-aarch64/pgdg-redhat-repo-latest.noarch.rpm
```

and do the install via

```
dnf install -y postgresql17 postgresql17-server postgresql17-contrib
```

First, we will update `~/.bashrc` with

```
cat >> ~/.bashrc
export PGHOST=/tmp
export PATH=/usr/pgsql-17/bin/:$PATH
```

then Ctrl-d to save, and

```
source ~/.bashrc
```

to reload the Bash environment.

Then we can do the PostgreSQL initialization

```
mkdir DB
initdb -k DB
```

and update configuration - for a 8 GB memory machine.

**postgresql.conf**
```
listen_addresses = '*'
port = 5432
max_connections = 100
unix_socket_directories = '/tmp'
password_encryption = scram-sha-256
shared_buffers = 2GB
huge_pages = try
max_prepared_transactions = 100
work_mem = 16MB
dynamic_shared_memory_type = posix
wal_level = replica
wal_log_hints = on
max_wal_size = 16GB
min_wal_size = 2GB
log_destination = 'stderr'
logging_collector = on
log_directory = 'log'
log_filename = 'postgresql.log'
log_rotation_age = 0
log_rotation_size = 0
log_truncate_on_rotation = on
log_line_prefix = '%p [%m] [%x] '
log_timezone = UTC
datestyle = 'iso, mdy'
timezone = UTC
lc_messages = 'en_US.UTF-8'
lc_monetary = 'en_US.UTF-8'
lc_numeric = 'en_US.UTF-8'
lc_time = 'en_US.UTF-8'
```

**pg_hba.conf**
```
local   all           all                   trust
host    postgres      repl   127.0.0.1/32   scram-sha-256
host    postgres      repl   ::1/128        scram-sha-256
host    replication   repl   127.0.0.1/32   scram-sha-256
host    replication   repl   ::1/128        scram-sha-256
```

Please, check with other sources in order to create a setup for your local setup.

Now, we are ready to start PostgreSQL

```
pg_ctl -D DB -l /tmp/ start
```

Lets connect, add the replication user, and create the Write-Ahead Log (WAL) slot that we need for [**pgmoneta**][pgmoneta]

```
psql postgres
CREATE ROLE repl WITH LOGIN REPLICATION PASSWORD 'repl';
SELECT pg_create_physical_replication_slot('repl', true, false);
\q
```

## pgmoneta

We will install [**pgmoneta**][pgmoneta] from the official [YUM repository][yum] as well,

```
dnf install -y pgmoneta
```

First, we will need to create a master security key for the [**pgmoneta**][pgmoneta] installation, by

```
pgmoneta-admin -g master-key
```

Then we will create the configuration for [**pgmoneta**][pgmoneta],

```
cat > /etc/pgmoneta/pgmoneta.conf
[pgmoneta]
host = *
metrics = 5001

base_dir = /home/pgmoneta/backup

compression = zstd

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

and end with a Ctrl-d to save the file.

Then, we will create the user configuration,

```
pgmoneta-admin -f /etc/pgmoneta/pgmoneta_users.conf -U repl -P repl user add
```

Lets create the base directory, and start [**pgmoneta**][pgmoneta] now, by

```
mkdir backup
pgmoneta -d
```
