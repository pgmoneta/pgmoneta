# Developer guide

for Fedora 38

## Install PostgreSql

    dnf install postgresql-server

for Fedora 38, this will install PostgreSQL 15

## Install pgmoneta

### Pre-install

    dnf install git gcc cmake make libev libev-devel openssl openssl-devel systemd systemd-devel zlib zlib-devel libzstd libzstd-devel lz4 lz4-devel libssh libssh-devel libcurl libcurl-devel python3-docutils libatomic bzip2 bzip2-devel libarchive libarchive-devel cjson cjson-devel

### Choose what suits you, if above didn't work

    dnf install cmake
    dnf install bzip2-devel
    dnf install lz4-devel
    dnf install libev-devel
    dnf install libarchive-devel
    dnf install cjson cjson-devel
    dnf install python3-docutils
    dnf install libssh-devel
    dnf install libcurl-devel
    dnf install systemd-devel

### Build

    cd /usr/local
    git clone https://github.com/pgmoneta/pgmoneta.git
    cd pgmoneta
    mkdir build
    cd build
    cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
    make
    make install

This will install `pgmoneta` in the `/usr/local` hierarchy.

### Check version

You can navigate to `build/src` and execute `./pgmoneta -?` to make the call. Alternatively, you can install it into `/usr/local/` and call it directly using:

    pgmoneta -?

If you see an error saying `error while loading shared libraries: libpgmoneta.so.0: cannot open shared object` running the above command. you may need to locate where your `libpgmoneta.so.0` is. It could be in `/usr/local/lib` or `/usr/local/lib64` depending on your environment. Add the corresponding directory into `/etc/ld.so.conf`.

To enable these directories, you would typically add the following lines in your `/etc/ld.so.conf` file:

    /usr/local/lib
    /usr/local/lib64

Remember to run `ldconfig` to make the change effective.

If you see an error saying `pgmoneta: pgmoneta: Configuration not found: /etc/pgmoneta/pgmoneta.conf` running the above command. you may need to add the config file.

    cd /etc
    mkdir pgmoneta
    vi pgmoneta.conf

`pgmoneta.conf`'s content is

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

In our main section called `[pgmoneta]` we setup `pgmoneta` to listen on all network addresses. We will enable Prometheus metrics on port 5001 and have the backups live in the `/home/pgmoneta` directory. All backups are being compressed with zstd and kept for 7 days. Logging will be performed at `info` level and put in a file called `/tmp/pgmoneta.log`. Last we specify the location of the `unix_socket_dir` used for management operations and the path for the PostgreSQL command line tools.

Next we create a section called `[primary]` which has the information about our PostgreSQL instance. In this case it is running on localhost on port 5432 and we will use the repl user account to connect.

Finally, you should be able to obtain the version of pgmoneta. Cheers!

## Set up pgmoneta

Let's give it a first try. The basic idea here is that we will use two user spaces of the OS: one is `postgres`, which will run PostgreSQL, and one is `pgmoneta`, which will run our pgmoneta and monitor PostgreSQL to backup.

In many installations, there is also an operating system user named `postgres` that is used to run the PostgreSQL server. You can use the command

    getent passwd | grep postgres

to check if your OS has a user named postgres. if not use

    useradd -ms /bin/bash postgres
    passwd postgres

If the postgres user already exists, don't forget to set its password for convenience.

### 1. postgres

Open a new window, switch to the `postgres` user. This section will always operate within this user space.

    sudo su -
    su - postgres

#### Initialize cluster

If you use dnf to install your postgresql, chances are the binary file is in `/usr/bin/`

    export PATH=/usr/bin:$PATH
    initdb /tmp/pgsql

#### Remove default acess

Remove last lines from `/tmp/pgsql/pg_hba.conf`

    host    all             all             127.0.0.1/32            trust
    host    all             all             ::1/128                 trust
    host    replication     all             127.0.0.1/32            trust
    host    replication     all             ::1/128                 trust

#### Add access for users and a database

Add new lines to `/tmp/pgsql/pg_hba.conf`

    host    mydb             myuser          127.0.0.1/32            scram-sha-256
    host    mydb             myuser          ::1/128                 scram-sha-256
    host    postgres         repl            127.0.0.1/32            scram-sha-256
    host    postgres         repl            ::1/128                 scram-sha-256
    host    replication      repl            127.0.0.1/32            scram-sha-256
    host    replication      repl            ::1/128                 scram-sha-256

#### Set password_encryption

Set `password_encryption` value in `/tmp/pgsql/postgresql.conf` to be `scram-sha-256`

    password_encryption = scram-sha-256

For versions 12 and 13, the default is `md5`, while for version 14 and above, it is `scram-sha-256`. Therefore, you should ensure that the value in `/tmp/pgsql/postgresql.conf` matches the value in `/tmp/pgsql/pg_hba.conf`.

#### Set replication level

Set wal_level value in `/tmp/pgsql/postgresql.conf` to be `replica`

    wal_level = replica

#### Start PostgreSQL

    pg_ctl  -D /tmp/pgsql/ start

Here, you may encounter issues such as the port being occupied or permission being denied. If you experience a failure, you can go to `/tmp/pgsql/log` to check the reason.

You can use

    pg_isready

to test

#### Add users and a database

    createuser -P myuser
    createdb -E UTF8 -O myuser mydb

Then

    psql postgres
    CREATE ROLE repl WITH LOGIN REPLICATION PASSWORD 'secretpassword';
    \q

#### Verify access

For the user (standard) (using mypass)

    psql -h localhost -p 5432 -U myuser mydb
    \q

For the user (pgmoneta) (using secretpassword)

    psql -h localhost -p 5432 -U repl postgres
    \q

#### Add pgmoneta user

    sudo su -
    useradd -ms /bin/bash pgmoneta
    passwd pgmoneta
    exit

### 2. pgmoneta

Open a new window, switch to the `pgmoneta` user. This section will always operate within this user space.

    sudo su -
    su - pgmoneta

#### Create pgmoneta configuration

Add the master key

    pgmoneta-admin master-key

You have to choose a password for the master key and it must be at least 8 characters- remember it!

then create vault

    pgmoneta-admin -f pgmoneta_users.conf -U repl -P secretpassword add-user

Input the replication user and its password to grant `pgmoneta` access to the database. Ensure that the information is correct.

Create the `pgmoneta.conf` configuration file to use when running `pgmoneta`. The content remains the same as before.

    cat > pgmoneta.conf
    [pgmoneta]
    host = *
    metrics = 5001

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

#### Create base directory

    mkdir backup

#### Start pgmoneta

    pgmoneta -c pgmoneta.conf -u pgmoneta_users.conf

#### Create a backup

open a new terminal and log in with `pgmoneta`

    pgmoneta-cli -c pgmoneta.conf backup primary

#### View backup

    pgmoneta-cli -c pgmoneta.conf status details

#### Stop pgmoneta

    pgmoneta-cli -c pgmoneta.conf stop

## End

Now that we've attempted our first backup, take a moment to relax. There are a few things we need to pay attention to:

1. Since we initialized the database in `/tmp`, the data in this directory might be removed after you go offline, depending on your OS configuration. If you want to make it permanent, choose a different directory.

2. Always use uncrustify to format your code when you make modifications.

## Basic git guide

Here are some links that will help you

* [How to Squash Commits in Git](https://www.git-tower.com/learn/git/faq/git-squash)
* [ProGit book](https://github.com/progit/progit2/releases)

### Start by forking the repository

This is done by the "Fork" button on GitHub.

### Clone your repository locally

This is done by

```sh
git clone git@github.com:<username>/pgmoneta.git
```

### Add upstream

Do

```sh
cd pgmoneta
git remote add upstream https://github.com/pgmoneta/pgmoneta.git
```

### Do a work branch

```
git checkout -b mywork main
```

### Make the changes

Remember to verify the compile and execution of the code

### Multiple commits

If you have multiple commits on your branch then squash them

```
git rebase -i HEAD~2
```

for example. It is `p` for the first one, then `s` for the rest

### Rebase

Always rebase

```
git fetch upstream
git rebase -i upstream/main
```

### Force push

When you are done with your changes force push your branch

```
git push -f origin mywork
```

and then create a pull requests for it

### Repeat

Based on feedback keep making changes, squashing, rebasing and force pushing

### Undo

Normally you can reset to an earlier commit using `git reset <commit hash> --hard`. 
But if you accidentally squashed two or more commits, and you want to undo that, 
you need to know where to reset to, and the commit seems to have lost after you rebased. 

But they are not actually lost - using `git reflog`, you can find every commit the HEAD pointer
has ever pointed to. Find the commit you want to reset to, and do `git reset --hard`.