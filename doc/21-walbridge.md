\newpage

# pgmoneta-walbridge

**Proof of concept** : a WAL protocol proxy that translates a PostgreSQL 18 write-ahead log stream into a PostgreSQL 19 stream, served to a 19.x replica over physical replication.

## Overview

`pgmoneta-walbridge` connects to a PostgreSQL 18 primary as a normal physical WAL receiver, translates the fetched WAL into PostgreSQL 19 format, stores it in a local downstream WAL directory, and then serves that translated stream to a PostgreSQL 19 replica that connects to it as if it were a physical walsender.

```
+------------+     WAL (18)      +---------------------+   WAL (19)    +------------+
| PostgreSQL | ---------------> | pgmoneta-walbridge  | -------------> | PostgreSQL |
| 18 primary |   logical/wal    |  receiver -> sender  |    physical   | 19 replica |
+------------+                  +---------------------+                +------------+
```

## Requirements

- PostgreSQL **18** primary and PostgreSQL **19** replica.
- The primary must be initialized **with data checksums disabled** (`--no-data-checksums`), because the migration engine rewrites records in place and recomputes only record CRCs, not block checksums.
- The `walbridge` replication slot must be created on the primary and a **base backup taken before** `pgmoneta-walbridge` is started. The replica is brought up from that backup; its start LSN is mapped onto the downstream stream by the LSN map.
- Auth is `trust` for the replica connection (PoC).
- Downstream timeline equals the upstream timeline (parsed from the upstream segment file names).

## Usage

```
pgmoneta-walbridge [ -c CONFIG_FILE ] [ -u USERS_FILE ] [ -A ADMINS_FILE ] [ -D DIRECTORY ] [ -s SERVER ]
```

| Option | Description |
| :----- | :---------- |
| `-c, --config` | Path to the `pgmoneta.conf` file |
| `-u, --users` | Path to the `pgmoneta_users.conf` file |
| `-A, --admins` | Path to the `pgmoneta_admins.conf` file |
| `-D, --directory` | Directory containing all configuration files |
| `-s, --server` | Server index to use for WAL bridging (default 0) |
| `-V, --version` | Display version information |
| `-?, --help` | Display help |

### Configuration

The `walbridge` key under `[pgmoneta]` selects the TCP port the downstream sender listens on:

```ini
[pgmoneta]
walbridge = 9970
```

The referenced server (`-s`) must be configured with `host`, `port`, `user`, `wal` directory and a replication-capable user as usual, so the WAL client can fetch the upstream stream.

## Known limitations

- Proof of concept only: no TLS, metrics, retry/reconnect of the replica, or multi-slot support.
- Records are translated eagerly per completed upstream segment; there is no passthrough of partially received records across segment boundaries.
- A replica reconnect after the map was reset (restart) requires the replica to be re-based on the rebuilt stream.
- Only physical replication is supported; `BASE_BACKUP`/extended-protocol requests are rejected.
- **End-to-end result: PARTIAL.** A real 19.x replica cannot be attached to this PoC because PostgreSQL 19 refuses to start on a data directory initialized by PostgreSQL 18 (`FATAL: database files are incompatible with server`); a replica started from a 19.x backup would not be able to reach the 18.x timeline's base state. The upstream side and the full downstream stream are exercised live (a PostgreSQL 18 primary streams into the receiver; the produced stream is parsed by the PostgreSQL 19 `pg_waldump` with zero errors). What is not exercised is the final step: a 19.x instance replaying the stream.
- Upstream WAL is fetched via the existing pgmoneta WAL client; the primary must be at `wal_level = replica` or `logical`.
