# Distributions

## Requirements
`pgmoneta` requires:

* [gcc](https://gcc.gnu.org) (C17)
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
* [libarchive](http://www.libarchive.org/)
* [pandoc](https://pandoc.org/)
* [texlive](https://www.tug.org/texlive/)

On Fedora, these can be installed using `dnf` or `yum`:
```
dnf install git gcc clang clang-analyzer cmake make libev libev-devel openssl openssl-devel systemd systemd-devel zlib zlib-devel libzstd libzstd-devel lz4 lz4-devel libssh libssh-devel python3-docutils libatomic bzip2 bzip2-devel libarchive libarchive-devel libasan libasan-static pandoc texlive-scheme-basic 'tex(footnote.sty)'
```
On Rocky, before you install the required packages, some additional repositories, CodeReady Builder and EPEL in this case, need to be enabled or installed first.
```
dnf install epel-release
```
Then in order to enable CodeReady Builder, you need to first install subscription-manager
```
dnf install subscription-manager
```
Then
```
dnf config-manager --set-enabled crb
```
It is OK to disregard the registration and subscription warning.
You can verify the repos using
```
dnf repolist
```
Check if epel and powertools/crb are listed. Then use the command above to install the required packages.

On RHEL, similarly to Rocky, some additional repos need to be enabled/installed first. The only difference is that we'll use
the CodeReady Linux Builder repository instead of PowerTools.
First you will still need to install EPEL, use
```
dnf install https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm
```
Then for the CodeReady Linux Builder repository,
```
dnf install subscription-manager
dnf config-manager --set-enabled codeready-builder-for-rhel-9-rhui-rpms
```
Alternatively, after installing subscription manager, if you have a RedHat corporate account
(you need to specify the company/organization name in your account), you can first register with subscription manager using
```
subscription-manager register --username <your-account-email-or-login> --password <your-password> --auto-attach
```
Then do
```
subscription-manager repos --enable codeready-builder-for-rhel-9-x86_64-rpms
```
Also verify the repos using
```
dnf repolist
```
Install required packages after the previous steps.

On FreeBSD, `pkg` is used instead of `dnf` or `yum`.

Use `pkg install <package name>` to install the following packages
```
git gcc cmake libev openssl libssh zlib-ng zstd liblz4 bzip2 py39-docutils libarchive
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
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug ..
make
```

Remember to set the `log_level` configuration option to `debug5`.

## Install
The installation process of `pgmoneta` is the same on all platforms. Please follow the
instructions in the [tutorial](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/01_install.md).
