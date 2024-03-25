# pgmoneta architecture

## Overview

`pgmoneta` use a process model (`fork()`), where each process handles one Write-Ahead Log (WAL) receiver to
[PostgreSQL](https://www.postgresql.org).

The main process is defined in [main.c](../src/main.c).

Backup is handled in [backup.h](../src/include/backup.h) ([backup.c](../src/libpgmoneta/backup.c)).

Restore is handled in [restore.h](../src/include/restore.h) ([restore.c](../src/libpgmoneta/restore.c)) with linking
handled in [link.h](../src/include/link.h) ([link.c](../src/libpgmoneta/link.c)).

Archive is handled in [achv.h](../src/include/achv.h) ([archive.c](../src/libpgmoneta/archive.c)) backed by
restore.

Write-Ahead Log is handled in [wal.h](../src/include/wal.h) ([wal.c](../src/libpgmoneta/wal.c)).

Backup information is handled in [info.h](../src/include/info.h) ([info.c](../src/libpgmoneta/info.c)).

Retention is handled in [retention.h](../src/include/retention.h) ([retention.c](../src/libpgmoneta/retention.c)).

Compression is handled in [gzip.h](../src/include/gzip.h) ([gzip.c](../src/libpgmoneta/gzip.c)) and
[zstandard.h](../src/include/zstandard.h) ([zstandard.c](../src/libpgmoneta/zstandard.c)).

## Shared memory

A memory segment ([shmem.h](../src/include/shmem.h)) is shared among all processes which contains the `pgmoneta`
state containing the configuration and the list of servers.

The configuration of `pgmoneta` (`struct configuration`) and the configuration of the servers (`struct server`)
is initialized in this shared memory segment. These structs are all defined in [pgmoneta.h](../src/include/pgmoneta.h).

The shared memory segment is created using the `mmap()` call.

## Network and messages

All communication is abstracted using the `struct message` data type defined in [messge.h](../src/include/message.h).

Reading and writing messages are handled in the [message.h](../src/include/message.h) ([message.c](../src/libpgmoneta/message.c))
files.

Network operations are defined in [network.h](../src/include/network.h) ([network.c](../src/libpgmoneta/network.c)).

## Memory

Each process uses a fixed memory block for its network communication, which is allocated upon startup of the process.

That way we don't have to allocate memory for each network message, and more importantly free it after end of use.

The memory interface is defined in [memory.h](../src/include/memory.h) ([memory.c](../src/libpgmoneta/memory.c)).

## Management

`pgmoneta` has a management interface which defines the administrator abilities that can be performed when it is running.
This include for example taking a backup. The `pgmoneta-cli` program is used for these operations ([cli.c](../src/cli.c)).

The management interface use Unix Domain Socket for communication.

The management interface is defined in [management.h](../src/include/management.h). The management interface
uses its own protocol which always consist of a header

| Field      | Type | Description |
|------------|------|-------------|
| `id` | Byte | The identifier of the message type |

The rest of the message is depending on the message type.

### Remote management

The remote management functionality uses the same protocol as the standard management method.

However, before the management packet is sent the client has to authenticate using SCRAM-SHA-256 using the
same message format that PostgreSQL uses, e.g. StartupMessage, AuthenticationSASL, AuthenticationSASLContinue,
AuthenticationSASLFinal and AuthenticationOk. The SSLRequest message is supported.

The remote management interface is defined in [remote.h](../src/include/remote.h) ([remote.c](../src/libpgmoneta/remote.c)).

## libev usage

[libev](http://software.schmorp.de/pkg/libev.html) is used to handle network interactions, which is "activated"
upon an `EV_READ` event.

Each process has its own event loop, such that the process only gets notified when data related only to that process
is ready. The main loop handles the system wide "services" such as idle timeout checks and so on.

## Signals

The main process of `pgmoneta` supports the following signals `SIGTERM`, `SIGINT` and `SIGALRM`
as a mechanism for shutting down. The `SIGABRT` is used to request a core dump (`abort()`).
The `SIGHUP` signal will trigger a reload of the configuration.

It should not be needed to use `SIGKILL` for `pgmoneta`. Please, consider using `SIGABRT` instead, and share the
core dump and debug logs with the `pgmoneta` community.

## Reload

The `SIGHUP` signal will trigger a reload of the configuration.

However, some configuration settings requires a full restart of `pgmoneta` in order to take effect. These are

* `hugepage`
* `libev`
* `log_path`
* `log_type`
* `unix_socket_dir`
* `pidfile`

The configuration can also be reloaded using `pgmoneta-cli -c pgmoneta.conf conf reload`. The command is only supported
over the local interface, and hence doesn't work remotely.

## Prometheus

pgmoneta has support for [Prometheus](https://prometheus.io/) when the `metrics` port is specified.

The module serves two endpoints

* `/` - Overview of the functionality (`text/html`)
* `/metrics` - The metrics (`text/plain`)

All other URLs will result in a 403 response.

The metrics endpoint supports `Transfer-Encoding: chunked` to account for a large amount of data.

The implementation is done in [prometheus.h](../src/include/prometheus.h) and
[prometheus.c](../src/libpgmoneta/prometheus.c).

## Logging

Simple logging implementation based on a `atomic_schar` lock.

The implementation is done in [logging.h](../src/include/logging.h) and
[logging.c](../src/libpgmoneta/logging.c).

## Protocol

The protocol interactions can be debugged using [Wireshark](https://www.wireshark.org/) or
[pgprtdbg](https://github.com/jesperpedersen/pgprtdbg).
