\newpage

# pgmoneta-walbridge developer guide

This document covers the internals, architecture, testing and public API of the WAL bridge subsystem.

For installation, configuration and usage see **21-walbridge.md**.

## Components

| Component | File | Purpose |
| :-------- | :--- | :------ |
| Orchestrator | `src/libpgmoneta/walbridge/walbridge.c` | Startup, config, reset, fork of receiver / sender |
| Receiver | `src/libpgmoneta/walbridge/wal_receiver.c` | Spawns the existing WAL client (`pgmoneta_wal`), monitors the upstream WAL directory, translates completed segments |
| Migration engine | `src/libpgmoneta/walbridge/migration_engine.c` | Translates individual 18.x records to 19.x (per-record handlers, CRC recomputation) |
| Store | `src/libpgmoneta/walbridge/wal_store.c` | Writes the downstream 19 stream (segment layout, LSN positioning, padding, fsync) |
| LSN map | `src/libpgmoneta/walbridge/lsn_map.c` | Persists the `(upstream LSN -> downstream LSN)` mapping per record |
| Sender | `src/libpgmoneta/walbridge/wal_sender.c` | Raw physical replication server (IDENTIFY_SYSTEM / START_REPLICATION / XLogData / keepalive) |

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

6. **LSN map**: for each translated record the map stores `upstream_lsn downstream_lsn` lines. The sender uses `pgmoneta_lsn_map_get_downstream_at_or_before(start_lsn)` to convert the LSN the replica requests (an upstream LSN from its control data) into the correct downstream position.

7. **Sender**: a raw-socket physical replication server (no TLS, no metrics). On connection it handles the startup handshake, replies `AuthenticationOk` + `ReadyForQuery`, answers `IDENTIFY_SYSTEM` (system identifier, timeline and WAL position from the lowest downstream segment), and on `START_REPLICATION PHYSICAL <upstream-lsn>` replies `CopyBothResponse` and streams `XLogData` messages with the translated 19.x records. It sends keepalives roughly once a second and honours client status/copy-done/terminate messages.

## Public API

All public symbols are prefixed with `pgmoneta_`. Internal (static) helpers within each `.c` file are not part of the API.

### WAL store (`wal_store.h`)

```c
int pgmoneta_wal_store_create(const char* downstream_dir,
                              struct lsn_map* map,
                              uint64_t sysid,
                              uint32_t wal_seg_size,
                              uint32_t xlog_blksz,
                              uint32_t tli,
                              struct wal_store** store);
int pgmoneta_wal_store_write_record(struct wal_store* store,
                                    struct decoded_xlog_record* record);
int pgmoneta_wal_store_flush(struct wal_store* store);
int pgmoneta_wal_store_sync_partial_page(struct wal_store* store);
void pgmoneta_wal_store_destroy(struct wal_store* store);
uint32_t pgmoneta_wal_store_compute_crc(const char* buffer, uint32_t total_len);
```

### LSN map (`lsn_map.h`)

```c
int pgmoneta_lsn_map_create(const char* path, struct lsn_map** map);
int pgmoneta_lsn_map_put(struct lsn_map* map, uint64_t upstream, uint64_t downstream);
int pgmoneta_lsn_map_get_downstream(struct lsn_map* map, uint64_t upstream, uint64_t* downstream);
int pgmoneta_lsn_map_get_downstream_at_or_before(struct lsn_map* map, uint64_t upstream,
                                                  uint64_t* upstream_found, uint64_t* downstream);
int pgmoneta_lsn_map_get_upstream(struct lsn_map* map, uint64_t downstream, uint64_t* upstream);
void pgmoneta_lsn_map_destroy(struct lsn_map* map);
```

### Migration engine (`migration_engine.h`)

```c
int pgmoneta_migration_engine_translate(struct decoded_xlog_record* record,
                                         uint16_t src_magic, uint16_t tgt_magic,
                                         struct lsn_map* map);
```

### Receiver / sender (`wal_receiver.h`, `wal_sender.h`)

```c
int pgmoneta_walbridge_run_receiver(int srv, char** argv, struct lsn_map* map);
int pgmoneta_walbridge_run_sender(int srv, char* downstream_dir, char* map_path);
```

### Orchestrator (`walbridge.h`)

```c
int pgmoneta_walbridge_start(int srv, char* configuration_path, char* users_path,
                              char* admins_path, char* directory_path, char** argv);
```

## Testing

Unit tests live in `test/testcases/test_walbridge.c` and cover the LSN map lookup, the migration engine translations (heap2 prune, heap2 visible, checkpoint, multixact create/truncate, gist), a full store round-trip, a store run where a record header straddles a page boundary and a store run where a record ends exactly at a page boundary (write translated records, parse the downstream segment back, verify LSNs, chaining and CRCs):

```bash
cmake -S . -B build -Dcheck=ON
cmake --build build --target pgmoneta-test
build/test/pgmoneta-test -m walbridge
```

The live downstream stream is validated with the PostgreSQL 19 `pg_waldump` tool, which uses the server's own decoder and CRC checks:

```bash
cp /tmp/wb/wal/pg18/wal/walbridge/000000010000000000000000 /tmp/dswal/
cd /tmp/dswal
/usr/lib/postgresql/19/bin/pg_waldump --start=0/28 --end=0/43C000 000000010000000000000000
```

The last full-page run parsed 83,260 records with zero errors.
