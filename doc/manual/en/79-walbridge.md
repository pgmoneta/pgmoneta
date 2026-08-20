\newpage

# pgmoneta-walbridge

**Proof of concept** : a WAL protocol proxy that translates a PostgreSQL 18 write-ahead log stream into a PostgreSQL 19 stream, served to a 19.x replica over physical replication.

## Overview

`pgmoneta-walbridge` connects to a PostgreSQL 18 primary as a physical WAL
receiver, retains raw WAL locally, and translates it into PostgreSQL 19 format
only when serving a downstream physical replication client.

```
+-------------------+     WAL (physical)   +---------------------+   WAL (physical)  +-------------------+
| PostgreSQL 18     | --------------->    | pgmoneta-walbridge  | ------------->    | PostgreSQL 19     |
| (primary)         |   physical stream   |  receiver -> sender |   physical stream | (replica)         |
+-------------------+                     +---------------------+                   +-------------------+
```

## Requirements

- PostgreSQL **18** primary and PostgreSQL **19 beta3** client tools.
- **Data checksums are always supported.** The primary may be initialized with data checksums enabled or disabled; the migration engine recomputes the block/page checksums of full-page images and propagates the data-checksum state, so no `--no-data-checksums` is needed. Record CRCs are always recomputed.
- The upstream `wal_slot` must be created on the PostgreSQL 18 primary before `pgmoneta-walbridge` starts. The reference downstream client is PostgreSQL 19 `pg_receivewal`; it speaks the bridge's independent downstream LSN space. The sender currently implements one durable downstream retention watermark, not PostgreSQL slot catalog management.
- Both links of the chain use **SCRAM-SHA-256**: the upstream connection from the receiver to the PostgreSQL 18 primary, and the downstream connection from the PostgreSQL 19 client to the sender. Credentials on both links come from the per-server `user` and `pgmoneta_users.conf`.
- The raw upstream WAL archive is `<base_dir>/<server-section>/wal/`. The LSN
  map and durable feedback watermark are `walbridge.lsnmap` and
  `walbridge.lsnmap.feedback` in that directory.

## Usage

```
pgmoneta-walbridge -c CONFIG_FILE -u USERS_FILE
```

| Option | Description |
| :----- | :---------- |
| `-c, --config` | Path to the `pgmoneta.conf` file |
| `-u, --users` | Path to the `pgmoneta_users.conf` file (default: `/etc/pgmoneta/pgmoneta_users.conf`) |
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

Each non-main section describes the upstream primary. It must provide `host`,
`port`, `user`, and `wal_slot`. The first configured server is used for WAL
bridging. Its raw archive location is derived from `base_dir` and the section
name; `wal` is not a server configuration key.

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
pgmoneta-walbridge -c /path/to/pgmoneta.conf -u /path/to/pgmoneta_users.conf

PGPASSWORD='<password>' /usr/lib/postgresql/19/bin/pg_receivewal \
  -h 127.0.0.1 -p 9970 -U <replication_user> -D /tmp/walbridge-download -v

/usr/lib/postgresql/19/bin/pg_waldump -q -p /tmp/walbridge-download \
  -s 0/28 000000010000000000000000
```

> Replace `<password>`, `<replication_user>`, and `<slot_name>` with the values used in your PostgreSQL setup before executing the examples above.

`pg_waldump -q` exits successfully without reporting WAL or CRC errors. A new
translated stream begins at downstream LSN `0/28`; use the first mapped LSN if
validating a resumed stream. The successful result is a PG19-readable WAL archive.
Reconnecting `pg_receivewal` with the same output directory exercises resume
from its downstream LSN. It is not a bootable PG19 standby.

## Known limitations

- Proof of concept only: no TLS, metrics, multi-downstream fan-out, or
  downstream replication-slot catalog management.
- The raw upstream archive is the sole durable source. Delivery always follows
  raw WAL → decode → translate → encode.
- A replica reconnect after the map was reset (restart) requires the replica to be re-based on the rebuilt stream.
- Only physical replication is supported; `BASE_BACKUP`/extended-protocol requests are rejected.
- Timeline changes during a stream are not supported.
- If the LSN map is removed or rebuilt, existing downstream LSNs are no
  longer usable and the downstream client must be re-based.
- Upstream WAL is fetched via the existing pgmoneta WAL client; the primary must be at `wal_level = replica` or `logical`.
- **Retention:** downstream flush feedback is translated through the LSN map and persisted monotonically. It is used to prune both raw upstream archive segments and translated downstream segments, retaining the segment that contains the confirmed flush LSN so reconnect can resume within it. `READ_REPLICATION_SLOT` reports this retained downstream floor rather than the translated-stream head.
