# pgmoneta

[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](https://opensource.org/licenses/BSD-3-Clause)
[![Latest release](https://img.shields.io/github/v/release/pgmoneta/pgmoneta)](https://github.com/pgmoneta/pgmoneta/releases)
[![GitHub stars](https://img.shields.io/github/stars/pgmoneta/pgmoneta?style=social)](https://github.com/pgmoneta/pgmoneta/stargazers)
[![Discussions](https://img.shields.io/github/discussions/pgmoneta/pgmoneta)](https://github.com/pgmoneta/pgmoneta/discussions)

<p align="center">
  <img src="doc/images/logo-reversed-transparent.svg" alt="pgmoneta logo" width="256"/>
</p>

[English](https://github.com/pgmoneta/pgmoneta/releases/download/0.21.0/pgmoneta-en.pdf) |
[Spanish](https://github.com/pgmoneta/pgmoneta/releases/download/0.21.0/pgmoneta-es.pdf)

**pgmoneta** is a backup / restore solution for [PostgreSQL](https://www.postgresql.org).
It supports full and incremental backups, point-in-time restore, WAL shipping, hot
standby, compression, encryption, TLS, and a Prometheus interface for operations.

**pgmoneta** is named after the Roman Goddess of Memory.

Visit our [website](https://pgmoneta.github.io/) for documentation, tutorials and
release downloads.

## Table of Contents

- [Features](#features)
- [Installation](#installation)
  - [Install from the PostgreSQL YUM repository](#install-from-the-postgresql-yum-repository)
  - [Compile from source](#compile-from-source)
- [Documentation](#documentation)
- [Tested platforms](#tested-platforms)
- [Overview](#overview)
- [Related projects](#related-projects)
- [Contributing](#contributing)
- [Community](#community)
- [License](#license)

## Features

- **Full backup** of a PostgreSQL cluster
- **Incremental backup** (PostgreSQL 14+)
- **Restore** to any saved backup, with point-in-time recovery
- **Compression** &mdash; gzip, zstd, lz4, bzip2
- **AES encryption** of backups at rest
- **Symlink support** for all files
- **Hot standby** &mdash; keep a warm copy of the cluster ready
- **Prometheus** metrics endpoint for monitoring
- **Web console** for inspecting metrics
- **Remote management** via `pgmoneta-cli`
- **Offline detection** of unreachable instances
- **Transport Layer Security (TLS) v1.2+** for client and server connections
- **Daemon mode** with systemd integration
- **User vault** for managing PostgreSQL credentials securely
- **WAL tools** to inspect Write-Ahead Log (WAL) logs, and filter them

## Installation

### Install from the PostgreSQL YUM repository

For RPM-based distributions (Fedora, RHEL, Rocky Linux, AlmaLinux), `pgmoneta`
is available as a pre-built package from the official [PostgreSQL YUM
repository](https://yum.postgresql.org/) (PGDG).

Add the PGDG repository &mdash; example for **RHEL / Rocky Linux / AlmaLinux 10**:

    dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-10-x86_64/pgdg-redhat-repo-latest.noarch.rpm

For Fedora, pick the matching RPM from the [PGDG repo packages
page](https://yum.postgresql.org/repopackages/).

Then disable the distribution-supplied PostgreSQL module and install `pgmoneta`
alongside the PostgreSQL version of your choice (example for PostgreSQL 18):

    dnf -qy module disable postgresql
    dnf install -y postgresql18 postgresql18-server pgmoneta

See the [Getting Started](https://github.com/pgmoneta/pgmoneta/blob/main/doc/GETTING_STARTED.md)
guide for first-run configuration once the package is installed.

### Compile from source

#### Required dependencies

- [clang](https://clang.llvm.org/) (or [gcc](https://gcc.gnu.org))
- [cmake](https://cmake.org)
- [make](https://www.gnu.org/software/make/)
- [libev](http://software.schmorp.de/pkg/libev.html)
- [OpenSSL](http://www.openssl.org/)
- [zlib](https://zlib.net)
- [zstd](http://www.zstd.net)
- [lz4](https://lz4.github.io/lz4/)
- [bzip2](http://sourceware.org/bzip2/)
- [systemd](https://www.freedesktop.org/wiki/Software/systemd/)
- [libssh](https://www.libssh.org/)
- [libarchive](http://www.libarchive.org/)
- [rst2man](https://docutils.sourceforge.io/) (man pages)

#### Optional dependencies (for building documentation)

- [pandoc](https://pandoc.org/)
- [texlive](https://www.tug.org/texlive/)

#### Install dependencies on Fedora / RHEL

```sh
dnf install git gcc clang clang-analyzer clang-tools-extra cmake make \
            libev libev-devel openssl openssl-devel \
            systemd systemd-devel zlib zlib-devel \
            libzstd libzstd-devel lz4 lz4-devel \
            libssh libssh-devel python3-docutils libatomic \
            bzip2 bzip2-devel libarchive libarchive-devel \
            libasan libasan-static
```

#### Release build

The following commands will install `pgmoneta` under `/usr/local`:

```sh
git clone https://github.com/pgmoneta/pgmoneta.git
cd pgmoneta
mkdir build
cd build
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
sudo make install
```

See [RPM](https://github.com/pgmoneta/pgmoneta/blob/main/doc/RPM.md) for how to
build a RPM of `pgmoneta`.

#### Debug build

The following commands will create a `DEBUG` version of `pgmoneta`:

```sh
git clone https://github.com/pgmoneta/pgmoneta.git
cd pgmoneta
mkdir build
cd build
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug ..
make
```

Remember to set the `log_level` configuration option to `debug5`.

## Documentation

- [User guide / EN](https://github.com/pgmoneta/pgmoneta/tree/main/doc/manual/en)
- [User guide / ES](https://github.com/pgmoneta/pgmoneta/tree/main/doc/manual/es)
- [Getting Started](https://github.com/pgmoneta/pgmoneta/blob/main/doc/GETTING_STARTED.md)
- [Configuration](https://github.com/pgmoneta/pgmoneta/blob/main/doc/CONFIGURATION.md)
- [Web Console](https://github.com/pgmoneta/pgmoneta/blob/main/doc/CONSOLE.md)
- [Architecture](https://github.com/pgmoneta/pgmoneta/blob/main/doc/ARCHITECTURE.md)
- [Developer guide](https://github.com/pgmoneta/pgmoneta/blob/main/doc/DEVELOPERS.md)

PDFs of the documentation are available on our
[website](https://pgmoneta.github.io/) and on the
[releases page](https://github.com/pgmoneta/pgmoneta/releases).

## Tested platforms

- [Fedora](https://getfedora.org/) 42+
- [RHEL 10.x](https://docs.redhat.com/en/documentation/red_hat_enterprise_linux/10)
- [Rocky Linux 10.x](https://rockylinux.org/)
- [AlmaLinux 10.x](https://almalinux.org/)
- [FreeBSD](https://www.freebsd.org/)
- [OpenBSD](http://www.openbsd.org/)

## Overview

**pgmoneta** makes use of

- A process model
- A shared memory model across processes
- [libev](http://software.schmorp.de/pkg/libev.html) for fast network interactions
- [Atomic operations](https://en.cppreference.com/w/c/atomic) to keep track of state

See [Architecture](https://github.com/pgmoneta/pgmoneta/blob/main/doc/ARCHITECTURE.md)
for the architecture of `pgmoneta`.

## Related projects

- [pgmoneta_mcp](https://github.com/pgmoneta/pgmoneta_mcp) &mdash; a
  [Model Context Protocol](https://modelcontextprotocol.io/) server for `pgmoneta`,
  letting AI assistants interact with backups and restores.

## Contributing

Contributions to `pgmoneta` are managed on [GitHub](https://github.com/pgmoneta/pgmoneta/):

- [Ask a question](https://github.com/pgmoneta/pgmoneta/discussions)
- [Raise an issue](https://github.com/pgmoneta/pgmoneta/issues)
- [Feature request](https://github.com/pgmoneta/pgmoneta/issues)
- [Code submission](https://github.com/pgmoneta/pgmoneta/pulls)

Contributions are most welcome!

Please consult our [Code of Conduct](https://github.com/pgmoneta/pgmoneta/blob/main/CODE_OF_CONDUCT.md)
for interacting in our community. New contributors may also want to read the
[Developer guide](https://github.com/pgmoneta/pgmoneta/blob/main/doc/DEVELOPERS.md).

If you find `pgmoneta` useful, consider giving the project a
[star](https://github.com/pgmoneta/pgmoneta/stargazers) on GitHub.

## Community

- Website: <https://pgmoneta.github.io/>
- X / Twitter: [@pgmoneta](https://x.com/pgmoneta/)

## License

[BSD-3-Clause](https://opensource.org/licenses/BSD-3-Clause)
