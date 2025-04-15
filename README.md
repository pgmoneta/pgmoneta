# pgmoneta

**pgmoneta** is a backup / restore solution for [PostgreSQL](https://www.postgresql.org).

**pgmoneta** is named after the Roman Goddess of Memory.

## Features

* Full backup
* Incremental backup (PostgreSQL 17+)
* Restore
* Compression (gzip, zstd, lz4, bzip2)
* AES encryption support
* Symlink support
* WAL shipping support
* Hot standby
* Prometheus support
* Remote management
* Offline mode
* Transport Layer Security (TLS) v1.2+ support
* Daemon mode
* User vault

## Documentation

* [User guide](https://raw.githubusercontent.com/pgmoneta/pgmoneta.github.io/main/doc/pgmoneta-user-guide.pdf)
* [Developer guide](https://raw.githubusercontent.com/pgmoneta/pgmoneta.github.io/main/doc/pgmoneta-dev-guide.pdf)
* [Getting Started](./doc/GETTING_STARTED.md)
* [Configuration](./doc/CONFIGURATION.md)

## Overview

**pgmoneta** makes use of

* Process model
* Shared memory model across processes
* [libev](http://software.schmorp.de/pkg/libev.html) for fast network interactions
* [Atomic operations](https://en.cppreference.com/w/c/atomic) are used to keep track of state

See [Architecture](./doc/ARCHITECTURE.md) for the architecture of **pgmoneta**.

## Tested platforms

* [Fedora](https://getfedora.org/) 38+
* [RHEL 9.x](https://docs.redhat.com/en/documentation/red_hat_enterprise_linux/9)
* [Rocky Linux 9.x](https://rockylinux.org/)
* [FreeBSD](https://www.freebsd.org/)
* [OpenBSD](http://www.openbsd.org/)

## Compiling the source

**pgmoneta** requires

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
* [pandoc](https://pandoc.org/)
* [texlive](https://www.tug.org/texlive/)

```sh
dnf install git gcc clang clang-analyzer cmake make libev libev-devel openssl openssl-devel systemd systemd-devel zlib zlib-devel libzstd libzstd-devel lz4 lz4-devel libssh libssh-devel libcurl libcurl-devel python3-docutils libatomic bzip2 bzip2-devel libarchive libarchive-devel libasan libasan-static
```

Alternative [gcc](https://gcc.gnu.org) can be used.

### Release build

The following commands will install **pgmoneta** in the `/usr/local` hierarchy.

```sh
git clone https://github.com/pgmoneta/pgmoneta.git
cd pgmoneta
mkdir build
cd build
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
sudo make install
```

See [RPM](./doc/RPM.md) for how to build a RPM of **pgmoneta**.

### Debug build

The following commands will create a `DEBUG` version of **pgmoneta**.

```sh
git clone https://github.com/pgmoneta/pgmoneta.git
cd pgmoneta
mkdir build
cd build
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug ..
make
```

Remember to set the `log_level` configuration option to `debug5`.

## Contributing

Contributions to **pgmoneta** are managed on [GitHub.com](https://github.com/pgmoneta/pgmoneta/)

* [Ask a question](https://github.com/pgmoneta/pgmoneta/discussions)
* [Raise an issue](https://github.com/pgmoneta/pgmoneta/issues)
* [Feature request](https://github.com/pgmoneta/pgmoneta/issues)
* [Code submission](https://github.com/pgmoneta/pgmoneta/pulls)

Contributions are most welcome !

Please, consult our [Code of Conduct](./CODE_OF_CONDUCT.md) policies for interacting in our
community.

Consider giving the project a [star](https://github.com/pgmoneta/pgmoneta/stargazers) on
[GitHub](https://github.com/pgmoneta/pgmoneta/) if you find it useful. And, feel free to follow
the project on [Twitter](https://twitter.com/pgmoneta/) as well.

## License

[BSD-3-Clause](https://opensource.org/licenses/BSD-3-Clause)
