\newpage

# Distributions

This tutorial will show you how to compile and install pgmoneta on various platforms.

Currently, primarily supported platforms covered in this tutorial are:

1. Fedora (37, 38)
2. Rocky (8.x, 9.x)
3. RHEL (8.x, 9.x)
4. FreeBSD 14

## Requirements

`pgmoneta` requires:

* [gcc 8+][gcc](C17)
* [cmake][cmake]
* [make][make]
* [libev][libev]
* [OpenSSL][openssl]
* [zlib][zlib]
* [zstd][zstd]
* [lz4][lz4]
* [bzip2][bzip2]
* [systemd][systemd]
* [rst2man][rst2man]
* [libssh][libssh]
* [libcurl][libcurl]
* [libarchive][libarchive]
* [cJSON][cjson]
* [pandoc][pandoc]
* [texlive][texlive]

On Fedora, these can be installed using `dnf` or `yum`:

``` sh
dnf install git gcc cmake make libev libev-devel openssl openssl-devel systemd systemd-devel zlib zlib-devel libzstd libzstd-devel lz4 lz4-devel libssh libssh-devel libcurl libcurl-devel python3-docutils libatomic bzip2 bzip2-devel libarchive libarchive-devel cjson cjson-devel pandoc texlive-scheme-basic 'tex(footnote.sty)'
```

On Rocky, before you install the required packages, some additional repositories, PowerTools and EPEL in this case, need to be enabled or installed first.

``` sh
dnf install epel-release
```

Then in order to enable powertools, you need to first install subscription-manager

``` sh
dnf install subscription-manager
```

Then

``` sh
# On Rocky 8.x
dnf config-manager --set-enabled powertools

# On Rocky 9.x, PowerTools is called crb (CodeReady Builder)
dnf config-manager --set-enabled crb
``` 

It is OK to disregard the registration and subscription warning.

You can verify the repos using

``` sh
dnf repolist
```

Check if epel and powertools/crb are listed. Then use the command above to install the required packages.

On RHEL, similarly to Rocky, some additional repos need to be enabled/installed first. The only difference is that we'll use the CodeReady Linux Builder repository instead of PowerTools.

First you will still need to install EPEL, use

``` sh
# On RHEL 8.x
dnf install https://dl.fedoraproject.org/pub/epel/epel-release-latest-8.noarch.rpm
# On RHEL 9.x
dnf install https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm
```

Then for the CodeReady Linux Builder repository,

``` sh
dnf install subscription-manager
# On RHEL 8.x
dnf config-manager --set-enabled codeready-builder-for-rhel-8-rhui-rpms
# On RHEL 9.x
dnf config-manager --set-enabled codeready-builder-for-rhel-9-rhui-rpms
```

Alternatively, after installing subscription manager, if you have a RedHat corporate account (you need to specify the company/organization name in your account), you can first register with subscription manager using

``` sh
subscription-manager register --username <your-account-email-or-login> --password <your-password> --auto-attach
```

Then do

``` sh
# On RHEL 8.x
subscription-manager repos --enable codeready-builder-for-rhel-8-x86_64-rpms
# On RHEL 9.x
subscription-manager repos --enable codeready-builder-for-rhel-9-x86_64-rpms
```

Also verify the repos using

``` sh
dnf repolist
```

Install required packages after the previous steps.

On FreeBSD, `pkg` is used instead of `dnf` or `yum`.

Use `pkg install <package name>` to install the following packages

``` sh
git gcc cmake libev openssl libssh zlib-ng zstd liblz4 bzip2 curl py39-docutils libarchive libcjson
```

## Compile

Compiling pgmoneta on different platforms is the same.

### Release build

The following commands will build and install `pgmoneta` in the `/usr/local` hierarchy.

```sh
git clone https://github.com/pgmoneta/pgmoneta.git
cd pgmoneta
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
sudo make install
```

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

Remember to set the `log_level` configuration option to `debug5`.

## Install

The installation process of `pgmoneta` is the same on all platforms. Please follow the instructions in the [tutorial][t_install].
