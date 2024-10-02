\newpage

# Installation

## Fedora

You need to add the [PostgreSQL YUM repository](https://yum.postgresql.org/), for example for Fedora 40

```
dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/F-40-x86_64/pgdg-fedora-repo-latest.noarch.rpm
```

and do the install via

```
dnf install -y pgmoneta
```

Additional information

* [PostgreSQL YUM](https://yum.postgresql.org/howto/)
* [Linux downloads](https://www.postgresql.org/download/linux/redhat/)

## RHEL 9 / RockyLinux 9

**x86_64**

```
dnf install -y https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm
dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-x86_64/pgdg-redhat-repo-latest.noarch.rpm
```

**aarch64**

```
dnf install -y https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm
dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-aarch64/pgdg-redhat-repo-latest.noarch.rpm
```

and do the install via

```
dnf install -y pgmoneta
```

## Compiling the source

We recommend using Fedora to test and run [**pgmoneta**][pgmoneta], but other Linux systems, FreeBSD and MacOS are also supported.

[**pgmoneta**][pgmoneta] requires

* [clang](https://clang.llvm.org/)
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

```sh
dnf install git gcc clang clang-analyzer cmake make libev libev-devel \
            openssl openssl-devel \
            systemd systemd-devel zlib zlib-devel \
            libzstd libzstd-devel \
            lz4 lz4-devel libssh libssh-devel \
            libcurl libcurl-devel \
            python3-docutils libatomic \
            bzip2 bzip2-devel \
            libarchive libarchive-devel 
```

Alternative [gcc](https://gcc.gnu.org) can be used.


### RHEL / RockyLinux

On RHEL / Rocky, before you install the required packages some additional repositories need to be enabled or installed first.

First you need to install the subscription-manager

``` sh
dnf install subscription-manager
```

It is ok to disregard the registration and subscription warning.

Otherwise, if you have a Red Hat corporate account (you need to specify the company/organization name in your account), you can register using

``` sh
subscription-manager register --username <your-account-email-or-login> --password <your-password> --auto-attach
```

Then install the EPEL repository,

``` sh
dnf install epel-release
```

Then to enable powertools

``` sh
dnf config-manager --set-enabled codeready-builder-for-rhel-9-rhui-rpms
dnf config-manager --set-enabled crb
dnf install https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm
```

Then use the `dnf` command for [**pgmoneta**][pgmoneta] to install the required packages.


### FreeBSD

On FreeBSD, `pkg` is used instead of `dnf` or `yum`.

Use `pkg install <package name>` to install the following packages

``` sh
git gcc cmake libev openssl libssh zlib-ng zstd liblz4 bzip2 curl \
    py39-docutils libarchive
```

### Build

#### Release build

The following commands will install [**pgmoneta**][pgmoneta] in the `/usr/local` hierarchy.

```sh
git clone https://github.com/pgmoneta/pgmoneta.git
cd pgmoneta
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
sudo make install
```

See [RPM](https://github.com/pgmoneta/pgmoneta/blob/main/doc/RPM.md) for how to build a RPM of [**pgmoneta**][pgmoneta].

#### Debug build

The following commands will create a `DEBUG` version of [**pgmoneta**][pgmoneta].

```sh
git clone https://github.com/pgmoneta/pgmoneta.git
cd pgmoneta
mkdir build
cd build
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug ..
make
```

## Compiling the documentation

[**pgmoneta**][pgmoneta]'s documentation requires

* [pandoc](https://pandoc.org/)
* [texlive](https://www.tug.org/texlive/)

```sh
dnf install pandoc texlive-scheme-basic \
            'tex(footnote.sty)' 'tex(footnotebackref.sty)' \
            'tex(pagecolor.sty)' 'tex(hardwrap.sty)' \
            'tex(mdframed.sty)' 'tex(sourcesanspro.sty)' \
            'tex(ly1enc.def)' 'tex(sourcecodepro.sty)' \
            'tex(titling.sty)' 'tex(csquotes.sty)' \
            'tex(zref-abspage.sty)' 'tex(needspace.sty)'

```

You will need the `Eisvogel` template as well which you can install through

```
wget https://github.com/Wandmalfarbe/pandoc-latex-template/releases/download/2.4.2/Eisvogel-2.4.2.tar.gz
tar -xzf Eisvogel-2.4.2.tar.gz
mkdir -p $HOME/.local/share/pandoc/templates
mv eisvogel.latex $HOME/.local/share/pandoc/templates
```

where `$HOME` is your home directory.

#### Generate API guide

This process is optional. If you choose not to generate the API HTML files, you can opt out of downloading these dependencies, and the process will automatically skip the generation.

Download dependencies

``` sh
dnf install graphviz doxygen
```

### Build

These packages will be detected during `cmake` and built as part of the main build.

## Extension installation

When you configure the `extra` parameter in the server section of `pgmoneta.conf`, it requires the server side to have the [pgmoneta_ext][pgmoneta_ext] extension installed to make it work.

The following instructions can help you easily install `pgmoneta_ext`. If you encounter any problems, please refer to the more detailed instructions in the [DEVELOPERS][ext_developers] documentation.

### Install pgmoneta_ext

After you have successfully installed `pgmoneta`, the following commands will help you install `pgmoneta_ext`:

```sh
dnf install -y pgmoneta_ext
```

You need to add the `pgmoneta_ext` library for PostgreSQL in `postgrersql.conf` as well:

```ini
shared_preload_libraries = 'pgmoneta_ext'
```

And remember to restart PostgreSQL to make it work.

### Verify success

You can use the `postgres` role to test.

1. Log into PostgreSQL

    ``` sh
    psql
    ```

2. Create a new test database

    ``` sql
    CREATE DATABASE testdb;
    ```

3. Enter the database

    ``` sql
    \c testdb
    ```

4. Follow the SQL commands below to check the function

    ``` sql
    DROP EXTENSION IF EXISTS pgmoneta_ext;
    CREATE EXTENSION pgmoneta_ext;
    SELECT pgmoneta_ext_version();
    ```

You should see

``` console
 pgmoneta_ext_version 
----------------------
 0.1.0
(1 row)
```

### Granting SUPERUSER Privileges

Some functions in `pgmoneta_ext` require `SUPERUSER` privileges. To enable these, grant the `repl` role superuser privileges using the command below. **Please proceed with caution**: granting superuser privileges bypasses all permission checks, allowing unrestricted access to the database, which can pose security risks. We are committed to enhancing privilege security in future updates.

```sql
ALTER ROLE repl WITH SUPERUSER;
```

To revoke superuser privileges from the `repl` role, use the following command:

```sql
ALTER ROLE repl WITH NOSUPERUSER;
```
