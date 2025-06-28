# Developer guide

## Install PostgreSQL

For RPM based distributions such as Fedora and RHEL you can add the
[PostgreSQL YUM repository](https://yum.postgresql.org/) and do the install via

**Fedora 42**

```sh
rpm -Uvh https://download.postgresql.org/pub/repos/yum/reporpms/F-42-x86_64/pgdg-redhat-repo-latest.noarch.rpm
```

**RHEL 9.x / Rocky Linux 9.x**

**x86_64**

```sh
dnf install https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm
rpm -Uvh https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-x86_64/pgdg-redhat-repo-latest.noarch.rpm
dnf config-manager --set-enabled crb
```

**aarch64**

```sh
dnf install https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm
rpm -Uvh https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-aarch64/pgdg-redhat-repo-latest.noarch.rpm
dnf config-manager --set-enabled crb
```

**PostgreSQL 17**

``` sh
dnf -qy module disable postgresql
dnf install -y postgresql17 postgresql17-server postgresql17-contrib postgresql17-libs
```

This will install PostgreSQL 17.

## Install pgmoneta

### Pre-install

#### Basic dependencies

``` sh
dnf install git gcc clang clang-analyzer cmake make libev libev-devel openssl openssl-devel systemd systemd-devel zlib zlib-devel libzstd libzstd-devel lz4 lz4-devel libssh libssh-devel python3-docutils libatomic bzip2 bzip2-devel libarchive libarchive-devel libasan libasan-static
```

#### Generate user and developer guide

This process is optional. If you choose not to generate the PDF and HTML files, you can opt out of downloading these dependencies, and the process will automatically skip the generation.

1. Download dependencies

    ``` sh
    dnf install pandoc texlive-scheme-basic
    ```

2. Download Eisvogel

    Use the command `pandoc --version` to locate the user data directory. On Fedora systems, this directory is typically located at `$HOME/.local/share/pandoc`.

    Download the `Eisvogel` template for `pandoc`, please visit the [pandoc-latex-template](https://github.com/Wandmalfarbe/pandoc-latex-template) repository. For a standard installation, you can follow the steps outlined below.

```sh
    wget https://github.com/Wandmalfarbe/pandoc-latex-template/releases/download/v3.2.0/Eisvogel-3.2.0.tar.gz
    tar -xzf Eisvogel-3.2.0.tar.gz
    mkdir -p $HOME/.local/share/pandoc/templates
    mv Eisvogel-3.2.0/eisvogel.latex $HOME/.local/share/pandoc/templates/
```

3. Add package for LaTeX

    Download the additional packages required for generating PDF and HTML files.

```sh
    dnf install 'tex(footnote.sty)' 'tex(footnotebackref.sty)' 'tex(pagecolor.sty)' 'tex(hardwrap.sty)' 'tex(mdframed.sty)' 'tex(sourcesanspro.sty)' 'tex(ly1enc.def)' 'tex(sourcecodepro.sty)' 'tex(titling.sty)' 'tex(csquotes.sty)' 'tex(zref-abspage.sty)' 'tex(needspace.sty)' 'tex(selnolig.sty)'
```

#### Generate API guide

This process is optional. If you choose not to generate the API HTML files, you can opt out of downloading these dependencies, and the process will automatically skip the generation.

Download dependencies

```sh
    dnf install graphviz doxygen
```

#### Generating Code Coverage

Code coverage is automatically enabled **only for GCC builds** if both `gcov` and `gcovr` are installed on your system.
To install the required tools, run:

```sh
dnf install gcovr gcc
```

> **Note:** In many distributions, `gcovr` may not be available as a DNF package. In such cases, you can install it using pip:
> ```sh
> pip3 install gcovr
> ```

When these tools are present and the compiler is set to GCC, the build system will detect them and enable code coverage generation automatically during the build process.
If you use Clang as the compiler, code coverage will not be enabled by default.

### Build

``` sh
cd /usr/local
git clone https://github.com/pgmoneta/pgmoneta.git
cd pgmoneta
mkdir build
cd build
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug ..
make
make install
```

This will install [**pgmoneta**](https://github.com/pgmoneta/pgmoneta) in the `/usr/local` hierarchy with the debug profile.

You can do

```
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-DCORE_DEBUG" ..
```

in order to get information from the core libraries too.

### Check version

You can navigate to `build/src` and execute `./pgmoneta -?` to make the call. Alternatively, you can install it into `/usr/local/` and call it directly using:

``` sh
pgmoneta -?
```

If you see an error saying `error while loading shared libraries: libpgmoneta.so.0: cannot open shared object` running the above command. you may need to locate where your `libpgmoneta.so.0` is. It could be in `/usr/local/lib` or `/usr/local/lib64` depending on your environment. Add the corresponding directory into `/etc/ld.so.conf`.

To enable these directories, you would typically add the following lines in your `/etc/ld.so.conf` file:

``` sh
/usr/local/lib
/usr/local/lib64
```

Remember to run `ldconfig` to make the change effective.

## Setup pgmoneta

Let's give it a try. The basic idea here is that we will use two users: one is `postgres`, which will run PostgreSQL, and one is [**pgmoneta**](https://github.com/pgmoneta/pgmoneta), which will run [**pgmoneta**](https://github.com/pgmoneta/pgmoneta) to do backup of PostgreSQL.

In many installations, there is already an operating system user named `postgres` that is used to run the PostgreSQL server. You can use the command

``` sh
getent passwd | grep postgres
```

to check if your OS has a user named postgres. If not use

``` sh
useradd -ms /bin/bash postgres
passwd postgres
```

If the postgres user already exists, don't forget to set its password for convenience.

### 1. postgres

Open a new window, switch to the `postgres` user. This section will always operate within this user space.

``` sh
sudo su -
su - postgres
```

#### Initialize cluster

If you use dnf to install your postgresql, chances are the binary file is in `/usr/bin/`

``` sh
export PATH=/usr/bin:$PATH
initdb -k /tmp/pgsql
```

#### Remove default acess

Remove last lines from `/tmp/pgsql/pg_hba.conf`

``` ini
host    all             all             127.0.0.1/32            trust
host    all             all             ::1/128                 trust
host    replication     all             127.0.0.1/32            trust
host    replication     all             ::1/128                 trust
```

#### Add access for users and a database

Add new lines to `/tmp/pgsql/pg_hba.conf`

``` ini
host    mydb             myuser          127.0.0.1/32            scram-sha-256
host    mydb             myuser          ::1/128                 scram-sha-256
host    postgres         repl            127.0.0.1/32            scram-sha-256
host    postgres         repl            ::1/128                 scram-sha-256
host    replication      repl            127.0.0.1/32            scram-sha-256
host    replication      repl            ::1/128                 scram-sha-256
```

#### Set password_encryption

Set `password_encryption` value in `/tmp/pgsql/postgresql.conf` to be `scram-sha-256`

``` sh
password_encryption = scram-sha-256
```

For version 13, the default is `md5`, while for version 14 and above, it is `scram-sha-256`. Therefore, you should ensure that the value in `/tmp/pgsql/postgresql.conf` matches the value in `/tmp/pgsql/pg_hba.conf`.

#### Set replication level

Set wal_level value in `/tmp/pgsql/postgresql.conf` to be `replica`

``` sh
wal_level = replica
```

#### Start PostgreSQL

``` sh
pg_ctl  -D /tmp/pgsql/ start
```

Here, you may encounter issues such as the port being occupied or permission being denied. If you experience a failure, you can go to `/tmp/pgsql/log` to check the reason.

You can use

``` sh
pg_isready
```

to test

#### Add users and a database

``` sh
export PATH=/usr/pgsql-17/bin:$PATH
createuser -P myuser
createdb -E UTF8 -O myuser mydb
```

Then

``` sh
psql postgres
CREATE ROLE repl WITH LOGIN REPLICATION PASSWORD 'secretpassword';
\q
```

#### Add replication slot

Add the required replication slot

``` sh
psql postgres
SELECT pg_create_physical_replication_slot('repl', true, false);
\q
```

#### Verify access

For the user `myuser` (standard) use `mypass`

``` sh
psql -h localhost -p 5432 -U myuser mydb
\q
```

For the user `repl` (pgmoneta) use `secretpassword`

``` sh
psql -h localhost -p 5432 -U repl postgres
\q
```

#### Add pgmoneta user

``` sh
sudo su -
useradd -ms /bin/bash pgmoneta
passwd pgmoneta
exit
```

### 2. pgmoneta

Open a new window, switch to the `pgmoneta` user. This section will always operate within this user space.

``` sh
sudo su -
su - pgmoneta
```

#### Create base directory

``` sh
mkdir backup
```

#### Create pgmoneta configuration

Add the master key

``` sh
pgmoneta-admin master-key
```

You have to choose a password for the master key and it must be at least 8 characters - remember it!

then create vault

``` sh
pgmoneta-admin -f pgmoneta_users.conf -U repl -P secretpassword user add
```

Input the replication user and its password to grant [**pgmoneta**](https://github.com/pgmoneta/pgmoneta) access to the database. Ensure that the information is correct.

Create the `pgmoneta.conf` configuration file to use when running [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

``` ini
cat > pgmoneta.conf
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

In our main section called `[pgmoneta]` we setup [**pgmoneta**](https://github.com/pgmoneta/pgmoneta) to listen on all network addresses. We will enable Prometheus metrics on port 5001 and have the backups live in the `/home/pgmoneta/backup` directory.
All backups are being compressed with zstd and kept for 7 days. Logging will be performed at `info` level and put in a file called `/tmp/pgmoneta.log`. Last we specify the location of the `unix_socket_dir` used for management operations.

Next we create a section called `[primary]` which has the information about our PostgreSQL instance. In this case it is running on localhost on port 5432 and we will use the repl user account to connect.

Finally, you should be able to obtain the version of [**pgmoneta**](https://github.com/pgmoneta/pgmoneta). Cheers!

#### Start pgmoneta

``` sh
pgmoneta -c pgmoneta.conf -u pgmoneta_users.conf
```

#### Create a backup

open a new terminal and log in with `pgmoneta`

``` sh
pgmoneta-cli -c pgmoneta.conf backup primary
```

#### View backup

``` sh
pgmoneta-cli -c pgmoneta.conf status details
```

#### Shutdown pgmoneta

``` sh
pgmoneta-cli -c pgmoneta.conf shutdown
```

## pgmoneta-walinfo

Alongside pgmoneta, you can use the `pgmoneta-walinfo` tool to read and display PostgreSQL Write-Ahead Log (WAL) files. This utility supports output in either `raw` or `json` format, making it an essential tool for examining WAL file contents.

In the `raw` format, the output is structured as:

### Usage

```sh
pgmoneta-walinfo
  Command line utility to read and display Write-Ahead Log (WAL) files

Usage:
  pgmoneta-walinfo <file>

Options:
  -c,  --config      Set the path to the pgmoneta_walinfo.conf file
  -u,  --users       Set the path to the pgmoneta_users.conf file
  -RT, --tablespaces Filter on tablspaces
  -RD, --databases   Filter on databases
  -RT, --relations   Filter on relations
  -R,  --filter      Combination of -RT, -RD, -RR
  -o,  --output      Output file
  -F,  --format      Output format (raw, json)
  -L,  --logfile     Set the log file
  -q,  --quiet       No output only result
       --color       Use colors (on, off)
  -r,  --rmgr        Filter on a resource manager
  -s,  --start       Filter on a start LSN
  -e,  --end         Filter on an end LSN
  -x,  --xid         Filter on an XID
  -l,  --limit       Limit number of outputs
  -v,  --verbose     Output result
  -S,  --summary     Show a summary of WAL record counts grouped by resource manager
  -V,  --version     Display version information
  -m,  --mapping     Provide mappings file for OID translation
  -t,  --translate   Translate OIDs to object names in XLOG records
  -?,  --help        Display help
```

For more details, please refer to the [wal documentation](./manual/dev-08-wal.md).

## End

Now that we've attempted our first backup, take a moment to relax. There are a few things we need to pay attention to:

1. Since we initialized the database in `/tmp`, the data in this directory might be removed after you go offline, depending on your OS configuration. If you want to make it permanent, choose a different directory.

2. Always use uncrustify to format your code when you make modifications.

## C programming

[**pgmoneta**](https://github.com/pgmoneta/pgmoneta) is developed using the [C programming language](https://en.wikipedia.org/wiki/C_(programming_language)) so it is a good
idea to have some knowledge about the language before you begin to make changes.

There are books like,

* [C in a Nutshell](https://www.oreilly.com/library/view/c-in-a/9781491924174/)
* [21st Century C](https://www.oreilly.com/library/view/21st-century-c/9781491904428/)

that can help you

### Debugging

In order to debug problems in your code you can use [gdb](https://www.sourceware.org/gdb/), or add extra logging using
the `pgmoneta_log_XYZ()` API

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

```sh
git checkout -b mywork main
```

### Make the changes

Remember to verify the compile and execution of the code

### AUTHORS

Remember to add your name to the following files,

```
AUTHORS
doc/manual/97-acknowledgement.md
doc/manual/advanced/en/97-acknowledgement.md
doc/manual/advanced/hi/97-acknowledgement.md
```

in your first pull request

### Multiple commits

If you have multiple commits on your branch then squash them

``` sh
git rebase -i HEAD~2
```

for example. It is `p` for the first one, then `s` for the rest

### Rebase

Always rebase

``` sh
git fetch upstream
git rebase -i upstream/main
```

### Force push

When you are done with your changes force push your branch

``` sh
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
