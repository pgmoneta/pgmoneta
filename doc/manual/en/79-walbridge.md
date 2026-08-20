\newpage

# pgmoneta-walbridge

**Proof of concept** : a WAL protocol proxy that translates a PostgreSQL 18 write-ahead log stream into a PostgreSQL 19 stream, served to a 19.x replica over physical replication.

## Overview

`pgmoneta-walbridge` connects to a PostgreSQL 18 primary as a normal physical WAL receiver, translates the fetched WAL into PostgreSQL 19 format, stores it in a local downstream WAL directory, and then serves that translated stream to a PostgreSQL 19 replica that connects to it as if it were a physical walsender.

```
+-------------------+     WAL (physical)   +---------------------+   WAL (physical)  +-------------------+
| PostgreSQL 18     | --------------->    | pgmoneta-walbridge  | ------------->    | PostgreSQL 19     |
| (primary)         |   physical stream   |  receiver -> sender |   physical stream | (replica)         |
+-------------------+                     +---------------------+                   +-------------------+
```

## Requirements

- PostgreSQL **18** primary and PostgreSQL **19** replica.
- **Data checksums are always supported.** The primary may be initialized with data checksums enabled or disabled; the migration engine recomputes the block/page checksums of full-page images and propagates the data-checksum state, so no `--no-data-checksums` is needed. Record CRCs are always recomputed.
- The upstream `wal_slot` must be created on the PostgreSQL 18 primary before `pgmoneta-walbridge` starts. The reference downstream client is PostgreSQL 19 `pg_receivewal`; it speaks the bridge's independent downstream LSN space. The sender currently implements one durable downstream retention watermark, not PostgreSQL slot catalog management.
- Both links of the chain authenticate with **SCRAM-SHA-256**: the upstream connection from the receiver to the PostgreSQL 18 primary, and the downstream connection from the PostgreSQL 19 replica to the sender. Credentials on the upstream side come from the per-server `user` and the `pgmoneta_users.conf` file; the downstream side authenticates the replica against the same users file. (`trust` and `md5` are also accepted by the sender.)
- Downstream timeline equals the upstream timeline (parsed from the upstream segment file names).

## Usage

```
pgmoneta-walbridge -c CONFIG_FILE
```

| Option | Description |
| :----- | :---------- |
| `-c, --config` | Path to the `pgmoneta.conf` file |
| `-?, --help` | Display help |

### Configuration

The `[pgmoneta-walbridge]` section under `CONFIG_FILE` selects the TCP address and port the downstream sender listens on:

```ini
[pgmoneta-walbridge]
host = *
port = 9970
```

| Key | Description |
| :--- | :---------- |
| `host` | TCP address to bind the downstream sender to (default `*`) |
| `port` | TCP port the downstream sender listens on (default 9970) |

Each non-main (per-server) section in the same file must describe the upstream primary: `host`, `port`, `user`, `wal` directory and a replication-capable user, so the WAL client can fetch the upstream stream. The first configured server is used for WAL bridging.

## Testing

Run the focused unit suite first:

```bash
cmake -S . -B build -Dcheck=ON
cmake --build build --target pgmoneta-test
ASAN_OPTIONS=detect_leaks=1 build/test/pgmoneta-test -m walbridge
```

For archive-level integration validation, use a PostgreSQL 18 primary and a PostgreSQL 19 client. Configure the primary with `wal_level = replica`, a replication-capable user, and an upstream physical slot matching the server section's `wal_slot`. Replace the placeholders below with your own values before running the commands:

```sql
CREATE ROLE <replication_user> WITH LOGIN REPLICATION PASSWORD '<password>';
SELECT pg_create_physical_replication_slot('<slot_name>');
```

Add the same user and password to `pgmoneta_users.conf`, start the bridge, generate WAL on the primary, then capture and validate the translated stream:

```bash
pgmoneta-walbridge -c /path/to/pgmoneta.conf

PGPASSWORD='<password>' /usr/lib/postgresql/19/bin/pg_receivewal \
  -h 127.0.0.1 -p 9970 -U <replication_user> -D /tmp/walbridge-download -v

/usr/lib/postgresql/19/bin/pg_waldump /tmp/walbridge-download/000000010000000000000000
```

> Replace `<password>`, `<replication_user>`, and `<slot_name>` with the values used in your PostgreSQL setup before executing the examples above.

The successful integration result is a PG19-readable WAL archive with valid record CRCs. It is not a bootable PG19 standby; see the limitation below.

## Known limitations

- Proof of concept only: no TLS, metrics, retry/reconnect of the replica, or multi-slot support.
- Records are translated eagerly per completed upstream segment. The reader preserves and reassembles records that span a segment boundary: the final record tail from segment N is saved in `partial_xlog_record`, and the immediate successor segment N+1 merges the continuation (`xlp_rem_len`) to reconstruct the full record. A stale partial from a non-successor segment is discarded automatically.
- A replica reconnect after the map was reset (restart) requires the replica to be re-based on the rebuilt stream.
- Only physical replication is supported; `BASE_BACKUP`/extended-protocol requests are rejected.
- **End-to-end result: ARCHIVE VALIDATED.** A real 19.x replica cannot be attached to this PoC because PostgreSQL 19 refuses to start on a data directory initialized by PostgreSQL 18 (`FATAL: database files are incompatible with server`); a replica started from a 19.x backup would not be able to reach the 18.x timeline's base state. The full chain is instead exercised with the PostgreSQL 19 `pg_receivewal`: a PostgreSQL 18 primary streams into the receiver, is translated into the downstream store, served by the sender and downloaded by `pg_receivewal` (which completes and renames whole segments, including graceful `.partial` shutdown); the downloaded segments are byte-identical to the store and are parsed by the PostgreSQL 19 `pg_waldump` with zero errors. The final step — a 19.x instance replaying the stream — cannot be demonstrated, so checksum recomputation is validated through unit tests and `pg_waldump`.

- **Crash-safe, byte-continuous resume VALIDATED.** Every completed upstream segment is translated exactly once: since the `done_segno` high-water-mark fix, the live-tail scan no longer re-translates the last complete segment in a loop, and the persisted resume state reaches the true end of the last translated segment (previously the persisted `last_up` lagged and a restart would re-translate). A restart resumes exactly at the persisted `last_up`/`done_segno` pair and appends new segments without touching previously translated bytes — verified by md5 identity of the pre-resume store region (`769d34d58bd823739a5da58f2d802ce7`) and of a fresh full-segment `pg_receivewal` capture against the store (`dfadc9d0b29dbfd73c8573a55768397f`). One bug found while validating restart is worth noting: `DEFAULT_WAL_SEGZ_BYTES` was an unparenthesized macro (`16 * 1024 * 1024`), so the legacy 4-field state fallback computed `last_up / 16 * 1024 * 1024` instead of `last_up / 16777216`, poisoning `done_segno` and causing a restarted receiver to skip every pending segment; the macro is now parenthesized.
- Upstream WAL is fetched via the existing pgmoneta WAL client; the primary must be at `wal_level = replica` or `logical`.
- **Retention:** downstream flush feedback is translated through the LSN map and persisted monotonically. It is used to prune both raw upstream archive segments and translated downstream segments, retaining the segment that contains the confirmed flush LSN so reconnect can resume within it. `READ_REPLICATION_SLOT` reports this retained downstream floor rather than the translated-stream head.
