\newpage

# Architecture

## Overview

[**pgmoneta**][pgmoneta] use a process model (`fork()`), where each process handles one Write-Ahead Log (WAL) receiver to [PostgreSQL][postgresql].

The main process is defined in [main.c][main_c].

Backup is handled in [backup.h][backup_h] ([backup.c][backup_c]).

Restore is handled in [restore.h][restore_h] ([restore.c][restore_c]) with linking handled in [link.h][link_h] ([link.c][link_c]).

Archive is handled in [achv.h][achv_h] ([archive.c][archive_c]) backed by restore.

Write-Ahead Log is handled in [wal.h][wal_h] ([wal.c][wal_c]).

Backup information is handled in [info.h][info_h] ([info.c][info_c]).

Retention is handled in [retention.h][retention_h] ([retention.c][retention_c]).

Compression is handled in [gzip_compression.h][gzip_compression.h] ([gzip_compression.c][gzip_compression.c]),
[lz4_compression.h][lz4_compression.h] ([lz4_compression.c][lz4_compression.c]),
[zstandard_compression.h][zstandard_compression.h] ([zstandard_compression.c][zstandard_compression.c]),
and [bzip2_compression.h][bzip2_compression.h] ([bzip2_compression.c][bzip2_compression.c]).

Encryption is handled in [aes.h][aes.h] ([aes.c][aes.c]).

## Shared memory

A memory segment ([shmem.h][shmem_h]) is shared among all processes which contains the [**pgmoneta**][pgmoneta] state containing the configuration and the list of servers.

The configuration of [**pgmoneta**][pgmoneta] (`struct configuration`) and the configuration of the servers (`struct server`) is initialized in this shared memory segment. These structs are all defined in [pgmoneta.h][pgmoneta_h].

The shared memory segment is created using the `mmap()` call.

## Network and messages

All communication is abstracted using the `struct message` data type defined in [messge.h][messge_h].

Reading and writing messages are handled in the [message.h][messge_h] ([message.c][message_c]) files.

Network operations are defined in [network.h][network_h] ([network.c][network_c]).

## Memory

Each process uses a fixed memory block for its network communication, which is allocated upon startup of the process.

That way we don't have to allocate memory for each network message, and more importantly free it after end of use.

The memory interface is defined in [memory.h][memory_h] ([memory.c][memory_c]).

## Management

[**pgmoneta**][pgmoneta] has a management interface which defines the administrator abilities that can be performed when it is running.
This include for example taking a backup. The `pgmoneta-cli` program is used for these operations ([cli.c][cli_c]).

The management interface is defined in [management.h][management_h]. The management interface
uses its own protocol which uses JSON as its foundation.

### Write

The client sends a single JSON string to the server,

| Field         | Type   | Description                     |
|---------------|--------|---------------------------------|
| `compression` | Byte   | The compression type            |
| `encryption`  | Byte   | The encryption type             |
| `length`      | Int    | The length of the JSON document |
| `json`        | String | The JSON document               |

The server sends a single JSON string to the client,

| Field         | Type   | Description                     |
|---------------|--------|---------------------------------|
| `compression` | Byte   | The compression type            |
| `encryption`  | Byte   | The encryption type             |
| `length`      | Int    | The length of the JSON document |
| `json`        | String | The JSON document               |

### Read

The server sends a single JSON string to the client,

| Field         | Type   | Description                     |
|---------------|--------|---------------------------------|
| `compression` | Byte   | The compression type            |
| `encryption`  | Byte   | The encryption type             |
| `length`      | Int    | The length of the JSON document |
| `json`        | String | The JSON document               |

The client sends to the server a single JSON documents,

| Field         | Type   | Description                     |
|---------------|--------|---------------------------------|
| `compression` | Byte   | The compression type            |
| `encryption`  | Byte   | The encryption type             |
| `length`      | Int    | The length of the JSON document |
| `json`        | String | The JSON document               |

### Remote management

The remote management functionality uses the same protocol as the standard management method.

However, before the management packet is sent the client has to authenticate using SCRAM-SHA-256 using the
same message format that PostgreSQL uses, e.g. StartupMessage, AuthenticationSASL, AuthenticationSASLContinue,
AuthenticationSASLFinal and AuthenticationOk. The SSLRequest message is supported.

The remote management interface is defined in [remote.h][remote_h] ([remote.c][remote_c]).

## libev usage

[libev][libev] is used to handle network interactions, which is "activated" upon an `EV_READ` event.

Each process has its own event loop, such that the process only gets notified when data related only to that process is ready. The main loop handles the system wide "services" such as idle timeout checks and so on.

## Signals

The main process of [**pgmoneta**][pgmoneta] supports the following signals `SIGTERM`, `SIGINT` and `SIGALRM` as a mechanism for shutting down. The `SIGABRT` is used to request a core dump (`abort()`).

The `SIGHUP` signal will trigger a reload of the configuration.

It should not be needed to use `SIGKILL` for [**pgmoneta**][pgmoneta]. Please, consider using `SIGABRT` instead, and share the core dump and debug logs with the [**pgmoneta**][pgmoneta] community.

## Reload

The `SIGHUP` signal will trigger a reload of the configuration.

However, some configuration settings requires a full restart of [**pgmoneta**][pgmoneta] in order to take effect. These are

* `hugepage`
* `libev`
* `log_path`
* `log_type`
* `unix_socket_dir`
* `pidfile`

The configuration can also be reloaded using `pgmoneta-cli -c pgmoneta.conf conf reload`. The command is only supported over the local interface, and hence doesn't work remotely.

## Prometheus

pgmoneta has support for [Prometheus][prometheus] when the `metrics` port is specified.

The module serves two endpoints

* `/` - Overview of the functionality (`text/html`)
* `/metrics` - The metrics (`text/plain`)

All other URLs will result in a 403 response.

The metrics endpoint supports `Transfer-Encoding: chunked` to account for a large amount of data.

The implementation is done in [prometheus.h][prometheus_h] and
[prometheus.c][prometheus_c].

## Logging

Simple logging implementation based on a `atomic_schar` lock.

The implementation is done in [logging.h][logging_h] and [logging.c][logging_c].

## Protocol

The protocol interactions can be debugged using [Wireshark][wireshark] or [pgprtdbg][pgprtdbg].
