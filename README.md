# pgmoneta

`pgmoneta` is a backup / restore solution for [PostgreSQL](https://www.postgresql.org).

`pgmoneta` is named after the Roman Goddess of Memory.

## Features

* Full backup
* Restore
* Compression (gzip, zstd, lz4)
* Symlink support
* Prometheus support
* Remote management
* Transport Layer Security (TLS) v1.2+ support
* Daemon mode
* User vault

See [Getting Started](./doc/GETTING_STARTED.md) on how to get started with `pgmoneta`.

See [Configuration](./doc/CONFIGURATION.md) on how to configure `pgmoneta`.

## Overview

`pgmoneta` makes use of

* Process model
* Shared memory model across processes
* [libev](http://software.schmorp.de/pkg/libev.html) for fast network interactions
* [Atomic operations](https://en.cppreference.com/w/c/atomic) are used to keep track of state
* The [PostgreSQL](https://www.postgresql.org) command line tools

See [Architecture](./doc/ARCHITECTURE.md) for the architecture of `pgmoneta`.

## Tested platforms

* [Fedora](https://getfedora.org/) 32+
* [RHEL](https://www.redhat.com/en/technologies/linux-platforms/enterprise-linux) 8.x with
  [AppStream](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/8/html/installing_managing_and_removing_user-space_components/using-appstream_using-appstream)

* [FreeBSD](https://www.freebsd.org/)
* [OpenBSD](http://www.openbsd.org/)

## Compiling the source

`pgmoneta` requires

* [gcc 8+](https://gcc.gnu.org) (C17)
* [cmake](https://cmake.org)
* [make](https://www.gnu.org/software/make/)
* [libev](http://software.schmorp.de/pkg/libev.html)
* [OpenSSL](http://www.openssl.org/)
* [zlib](https://zlib.net)
* [zstd](http://www.zstd.net)
* [lz4](https://lz4.github.io/lz4/)
* [systemd](https://www.freedesktop.org/wiki/Software/systemd/)
* [rst2man](https://docutils.sourceforge.io/)
* [libssh](https://www.libssh.org/)

```sh
dnf install git gcc cmake make libev libev-devel openssl openssl-devel systemd systemd-devel zlib zlib-devel libzstd libzstd-devel lz4 lz4-devel libssh libssh-dev python3-docutils
```

Alternative [clang 8+](https://clang.llvm.org/) can be used.

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

Note, that `pgmoneta` requires [PostgreSQL](https://www.postgresql.org) 10 or later to run as the command line tools
are required. These can be installed from the [PostgreSQL YUM](https://yum.postgresql.org/) repository, or from the
official distribution repository, if supported, like

```sh
dnf install -y postgresql
```

See [RPM](./doc/RPM.md) for how to build a RPM of `pgmoneta`.

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

## Contributing

Contributions to `pgmoneta` are managed on [GitHub.com](https://github.com/pgmoneta/pgmoneta/)

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
