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

## Components

| Component | File | Purpose |
| :-------- | :--- | :------ |
| Orchestrator | `src/libpgmoneta/walbridge/walbridge.c` | Startup, config, reset, fork of receiver / sender |
| Receiver | `src/libpgmoneta/walbridge/wal_receiver.c` | Spawns the existing WAL client (`pgmoneta_wal`), monitors the upstream WAL directory, translates completed segments |
| Migration engine | `src/libpgmoneta/walbridge/migration_engine.c` | Translates individual 18.x records to 19.x (per-record handlers, CRC recomputation) |
| Store | `src/libpgmoneta/walbridge/wal_store.c` | Writes the downstream 19 stream (segment layout, LSN positioning, padding, fsync) |
| LSN map | `src/libpgmoneta/walbridge/lsn_map.c` | Persists the `(upstream LSN -> downstream LSN)` mapping per record |
| Sender | `src/libpgmoneta/walbridge/wal_sender.c` | Raw physical replication server (IDENTIFY_SYSTEM / START_REPLICATION / XLogData / keepalive) |

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

## How it works

1. **Startup**: `pgmoneta-walbridge` creates shared memory, loads and validates the main configuration and user files, then locates the server WAL directory. The downstream store lives in `<wal dir>/walbridge` and the LSN map in `<wal dir>/walbridge.lsnmap`.

2. **Reset (restart policy)**: at start, all previously written downstream segments and the LSN map file are removed. The stream is rebuilt by re-translating every upstream segment currently present. This keeps the downstream stream and the map consistent with a fresh store.

3. **Receiver**: runs in a child process and spawns the existing pgmoneta WAL client (which itself forks) so the upstream stream is written to the server WAL directory using existing, tested logic. The receiver polls that directory, sorts pending segments and, for each completed upstream segment:
   - parses it with `pgmoneta_wal_parse_wal_file`,
   - translates every non-partial record through the migration engine,
   - writes the translated record to the downstream store and **flushes the partial page immediately** so the sender can stream it without waiting for a full 8 KiB page,
   - flushes (`fsync`) the store segment once the whole upstream segment is translated,
   - records `(upstream LSN, downstream LSN)` for each record in the LSN map.

4. **Migration engine**: translates 18.x records to 19.x. Notable translations:
   - `XLOG_CHECKPOINT_REDO`: 4-byte `{wal_level}` payload becomes 8-byte `{wal_level, data_checksum_version}` (with the checksum version forced to 0 so the replica does not expect block checksums).
   - `XLOG_HEAP2_PRUNE_VACUUM_CLEANUP`: 8-byte `{oldest_xid, new_relfilenumber, flags, nredirected, ndead}` layout becomes the 19.x 16-byte `{oldest_xid, new_relfilenumber, flags, nredirected, ndead, 0, 0, 0}` layout.
   - `XLOG_HEAP2_VISIBLE`, `XLOG_MULTIXACT_CREATE_ID`, `XLOG_MULTIXACT_TRUNCATE_ID` and `XLOG_GIST_ASSIGN_LSN` are handled so that the downstream chaining (`xl_prev`) and record CRCs stay correct.
   - Every translated record gets a recomputed CRC-32C (Castagnoli) since the payload changed.

5. **Downstream stream layout**: records are laid out at 8-byte aligned positions starting at LSN 40 (after the 40-byte long page header with 19.x magic `0xD121`), chained via `xl_prev`, padded with `MAXALIGN`, and finished with a segment switch + tail pad. Exactly like PostgreSQL, a record that does not fit in the remaining page space continues across the page boundary (partial header or data, then a continuation page carrying `XLP_FIRST_IS_CONTRECORD` and `xlp_rem_len`), so no zero gap is ever left between records. The downstream LSNs are entirely independent of the upstream LSNs.

6. **LSN map**: for each translated record the map stores `upstream_lsn downstream_lsn` lines. The sender uses `lsn_map_get_downstream_at_or_before(start_lsn)` to convert the LSN the replica requests (an upstream LSN from its control data) into the correct downstream position.

7. **Sender**: a raw-socket physical replication server (no TLS, no metrics). On connection it handles the startup handshake, replies `AuthenticationOk` + `ReadyForQuery`, answers `IDENTIFY_SYSTEM` (system identifier, timeline and WAL position from the lowest downstream segment), and on `START_REPLICATION PHYSICAL <upstream-lsn>` replies `CopyBothResponse` and streams `XLogData` messages with the translated 19.x records. It sends keepalives roughly once a second and honours client status/copy-done/terminate messages.

## Testing

Unit tests live in `test/testcases/test_walbridge.c` and cover the LSN map lookup, the migration engine translations (heap2 prune, heap2 visible, checkpoint, multixact create/truncate, gist), a full store round-trip, a store run where a record header straddles a page boundary and a store run where a record ends exactly at a page boundary (write translated records, parse the downstream segment back, verify LSNs, chaining and CRCs):

```bash
cmake -S . -B build -Dcheck=ON
cmake --build build --target pgmoneta-test
build/test/pgmoneta-test -m walbridge
```

The live downstream stream is validated with the PostgreSQL 19 `pg_waldump` tool, which uses the server's own decoder and CRC checks. The last full-page run parsed 48,291 records with no errors:

```bash
cp /tmp/wb/wal/pg18/wal/walbridge/000000010000000000000000 /tmp/dswal/
cd /tmp/dswal
/usr/lib/postgresql/19/bin/pg_waldump --start=0/28 --end=0/43C000 000000010000000000000000
```

## Known limitations

- Proof of concept only: no TLS, metrics, retry/reconnect of the replica, or multi-slot support.
- Records are translated eagerly per completed upstream segment; there is no passthrough of partially received records across segment boundaries.
- A replica reconnect after the map was reset (restart) requires the replica to be re-based on the rebuilt stream.
- Only physical replication is supported; `BASE_BACKUP`/extended-protocol requests are rejected.
- **End-to-end result: PARTIAL.** A real 19.x replica cannot be attached to this PoC because PostgreSQL 19 refuses to start on a data directory initialized by PostgreSQL 18 (`FATAL: database files are incompatible with server`); a replica started from a 19.x backup would not be able to reach the 18.x timeline's base state. The upstream side and the full downstream stream are exercised live (a PostgreSQL 18 primary streams into the receiver; the produced stream is parsed by the PostgreSQL 19 `pg_waldump` with zero errors). What is not exercised is the final step: a 19.x instance replaying the stream.
- Upstream WAL is fetched via the existing pgmoneta WAL client; the primary must be at `wal_level = replica` or `logical`.
