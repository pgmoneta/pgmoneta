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
| Store | `src/libpgmoneta/walbridge/wal_store.c` | Writes the downstream 19 stream (segment layout, LSN positioning, padding, fsync) and recomputes full-page-image block checksums |
| Config | `src/libpgmoneta/configuration.c` | Parses the upstream (per-server) sections and the `[pgmoneta-walbridge]` section (`host`, `port`) |
| LSN map | `src/libpgmoneta/walbridge/lsn_map.c` | Persists the `(upstream LSN -> downstream LSN)` mapping per record |
| Sender | `src/libpgmoneta/walbridge/wal_sender.c` | Raw physical replication server (IDENTIFY_SYSTEM / READ_REPLICATION_SLOT / START_REPLICATION / XLogData / keepalive); streams the archived downstream segments as raw bytes |

## How it works

1. **Startup**: `pgmoneta-walbridge` creates shared memory, loads and validates the main configuration and user files, then locates the server WAL directory. The downstream store lives in `<wal dir>/walbridge` and the LSN map in `<wal dir>/walbridge.lsnmap`.

2. **Restart policy**: downstream segments, the LSN map, and receiver state are retained across a clean restart. The receiver resumes at the persisted `(last_up, done_segno)` point and appends to the existing downstream stream; it does not rebuild or reset the map.

3. **Receiver**: runs in a child process and spawns the existing pgmoneta WAL client (which itself forks) so the upstream stream is written to the server WAL directory using existing, tested logic. The client authenticates to the PostgreSQL 18 primary with **SCRAM-SHA-256** using the configured per-server `user` and the credentials in `pgmoneta_users.conf`. The receiver polls that directory, sorts pending segments and, for each completed upstream segment:
   - parses it with `pgmoneta_wal_parse_wal_file`,
   - translates every non-partial record through the migration engine,
   - writes the translated record to the downstream store and **flushes the partial page immediately** so the sender can stream it without waiting for a full 8 KiB page,
   - flushes (`fsync`) the store segment once the whole upstream segment is translated,
   - records `(upstream LSN, downstream LSN)` for each record in the LSN map.

   Scan gating and crash-safe resume state:
   - A completed upstream segment is eligible for translation only if its `segno` is at or before the persisted resume segment (`segno < resume_segno_gate`) **and** it is not yet translated (`segno > done_segno`, a high-water mark set when a segment fully translates). Without the `done_segno` watermark the last complete segment (whose `segno == gate`) is re-scanned forever: its records all lie at or below `last_up` so no persist fires inside the translate loop, the persisted state never advances, and the segment is re-parsed every poll cycle.
   - Resume state (`segno xl_prev next_lsn last_up done_segno`) is written at most once per second while records are translating, and **always forced at segment end**, so the persisted `last_up` reaches the true end of the last translated segment instead of lagging behind the in-memory value. `done_segno` is persisted with it, so a restart skips already-translated segments entirely and resumes at the exact downstream position without re-delivering (or re-translating) bytes already served to a replica. The loader accepts the legacy 4-field file (reconstructing `done_segno` from `last_up`) for a seamless upgrade.

The receiver creates the downstream store with `pgmoneta_wal_store_create` and always enables block checksum recomputation on it via `pgmoneta_wal_store_set_checksums(store, true)`.

4. **Migration engine**: translates 18.x records to 19.x. Notable translations:
   - `XLOG_CHECKPOINT_REDO`: 4-byte `{wal_level}` payload becomes 8-byte `{wal_level, data_checksum_version}`. Data checksums are always supported, so the downstream checkpoint advertises data checksums enabled.
   - `XLOG_CHECKPOINT_ONLINE` carries the checkpoint record; its checksum state is likewise passed through.
   - `XLOG_HEAP2_PRUNE_VACUUM_CLEANUP`: 8-byte `{oldest_xid, new_relfilenumber, flags, nredirected, ndead}` layout becomes the 19.x 16-byte `{oldest_xid, new_relfilenumber, flags, nredirected, ndead, 0, 0, 0}` layout.
   - `XLOG_HEAP2_VISIBLE`, `XLOG_MULTIXACT_CREATE_ID`, `XLOG_MULTIXACT_TRUNCATE_ID` and `XLOG_GIST_ASSIGN_LSN` are handled so that the downstream chaining (`xl_prev`) and record CRCs stay correct.
   - Every translated record gets a recomputed CRC-32C (Castagnoli) since the payload changed.

5. **Downstream stream layout**: records are laid out at 8-byte aligned positions starting at LSN 40 (after the 40-byte long page header with 19.x magic `0xD121`), chained via `xl_prev`, padded with `MAXALIGN`, and finished with a segment switch + tail pad. Exactly like PostgreSQL, a record that does not fit in the remaining page space continues across the page boundary (partial header or data, then a continuation page carrying `XLP_FIRST_IS_CONTRECORD` and `xlp_rem_len`), so no zero gap is ever left between records. The downstream LSNs are entirely independent of the upstream LSNs.

6. **LSN map and retention**: for each translated record the map stores `upstream_lsn downstream_lsn` lines. The replica speaks downstream LSNs. On a standby status update, the sender maps the durable downstream flush position backwards to an upstream LSN and atomically persists the pair in `<wal dir>/walbridge.lsnmap.feedback`. The watermark is monotonic: late status messages cannot move it backwards. The receiver prunes raw upstream WAL and translated downstream WAL below this floor, retaining the segment that contains the acknowledged position. `READ_REPLICATION_SLOT` reports this retained downstream floor, not the current stream head.

7. **Sender**: a raw-socket physical replication server (no TLS, no metrics) bound to the `host`/`port` from the `[pgmoneta-walbridge]` section. On connection it performs the startup handshake and authenticates the replica with **SCRAM-SHA-256** (default; `trust` and `md5` methods are also accepted) against the user entries from `pgmoneta_users.conf`, replies `AuthenticationOk` + `ReadyForQuery`, answers `IDENTIFY_SYSTEM` and `READ_REPLICATION_SLOT`, and on `START_REPLICATION … <downstream-lsn>` replies the 3-byte `CopyBothResponse` body `{0, 0, 0}` and streams the downstream segments as raw bytes.

   The wire format follows the walsender `XLogData` layout: the first byte of every `CopyData` body is the message type byte — `'w'` (XLogData, `0x77`), `'k'` (keepalive, `0x6b`) or `'r'`/`'d'` from the replica (standby status). An `XLogData` message is 25 header bytes plus payload — `['w'][8-byte dataStart][8-byte walEnd][8-byte sendTime][raw WAL bytes]`; a keepalive is 18 bytes — `['k'][8-byte walEnd][8-byte sendTime][1-byte reply-requested]`.

   PostgreSQL's `pg_receivewal`/replica core requires a **raw, contiguous** byte stream: each `XLogData` starts exactly where the previous payload ended. The sender begins at the requested downstream LSN (which may be inside a segment), reads the corresponding offset with `pread`, and sends 8 KiB chunks with `dataStart` set to that stream position. At the translated frontier it stays in keepalive mode, reloads the LSN map, and resumes when more bytes arrive.

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
void pgmoneta_wal_store_set_checksums(struct wal_store* store, bool checksums);
void pgmoneta_wal_store_recompute_page_checksums(struct wal_store* store,
                                                  struct decoded_xlog_record* record);
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
                                         struct lsn_map* map, bool data_checksums);
```

### Receiver / sender (`wal_receiver.h`, `wal_sender.h`)

```c
int pgmoneta_walbridge_run_receiver(int srv, char** argv, struct lsn_map* map);
int pgmoneta_walbridge_run_sender(int srv, char* downstream_dir, char* map_path);
```

### Orchestrator (`walbridge.h`)

```c
int pgmoneta_walbridge_start(char* configuration_path, char** argv);
```

## Cross-segment record reassembly

A single PostgreSQL record can physically span a segment boundary: the tail is written at the end of segment N and continued on the first page of segment N+1, whose long page header carries `XLP_FIRST_IS_CONTRECORD` and `xlp_rem_len`. The reader (`pgmoneta_wal_parse_wal_file`) reassembles such records:

- `struct partial_xlog_record` gains `from_seg` (logical segment number the tail came from) and `lsn` (absolute start LSN of the partial record). Both save sites record them: the split-header site and the cross-EOF data tail site.
- A continuity guard runs at the top of a parse: a saved partial is consumed only when parsing the immediate successor segment (`from_seg == logSegNo - 1`); otherwise it is stale state (unrelated stream or a torn, still-growing segment) and is discarded, so a record is never spliced from unrelated bytes.
- Reconstruction: when the successor channel is a continuation, the saved header + saved body are merged with the remaining `xlp_rem_len` bytes on page 0. The decoded record uses the saved absolute start LSN (`partial_lsn`) for its `lsn`, `xl_prev`/CRC integrity and correct downstream chaining.
- A second stale-guard protects the data buffer: if `data_buffer_bytes_read` exceeds the current record's `data_length`, the saved bytes cannot be this record's continuation and are discarded before reading fresh.

A real bug surfaced while testing this path. The continuation offset was computed from `ftell(file)`; but `read_all_page_headers()` has already moved the file cursor (to the segment end when a torn page is present), so the position-relative offset was `MAXALIGN(32768 + …)` instead of `MAXALIGN(40 + …)`, and every record following the continuation on page 0 was silently skipped. The offset is now anchored to `SIZE_OF_XLOG_LONG_PHD` (the fixed page-0 data start). The `test_walbridge_reader_cross_segment` unit test crafts two 32 KiB segments (a 4-page tail stream ending exactly at the segment EOF, and its continuation with a full set of fillers) and verifies the reassembled record bytes, LSN, the recovered records after the continuation, and the stale-discard behaviour.

## PG18/PG19 WAL compatibility matrix

Verified against the PostgreSQL 17/18/19 STABLE headers:

| Concern | PG17 | PG18 | PG19 |
| :------ | :--- | :--- | :--- |
| `XLogRecord`, block/data headers | identical | identical | identical |
| `XLogRecordBlockHeader.fork_flags` layout (fork low 4 bits, `HAS_IMAGE 0x10`, `HAS_DATA 0x20`, `WILL_INIT 0x40`, `SAME_REL 0x80`) | identical | identical | identical |
| Block IDs (`DATA_SHORT 255`, `DATA_LONG 254`, `ORIGIN 253`, `TOPLEVEL_XID 252`) | identical | identical | identical |
| `xl_heap_prune` | `{reason, flags}` (`XLHP_*`) | `{reason, flags}` (`XLHP_*`) | `{uint16 flags}` (`XLHP_*`) |
| `xlp_info` flags (`XLP_ALL_FLAGS`) | 0x0001\|0x0002\|0x0004 = 0x0007 | `XLP_BKP_REMOVABLE 0x0008` added (`0x000F`) | 0x0007 (0x0008 dropped) |
| WAL magic | 0xD116 | 0xD118 | 0xD121 |

Consequences enforced in pgmoneta:

- The block-header format is identical between 18 and 19, so raw 18.x blocks decode with the same constants; no flag remap is needed for `fork_flags`.
- The 18.x page-header flag `XLP_BKP_REMOVABLE (0x0008)` does not exist in 19.x; `pgmoneta_wal_store_remap_page_flags` strips it (the reader accepts it; the store never emits it).
- `XLH_PRUNE_NO_LOGICAL` does **not** appear in PostgreSQL 17/18/19 prune records. It is a PG15-era flag (`no_untouched_dead_items`) removed in PG17 when the `{reason, flags}` prune format was introduced; nothing in the 18→19 bridge has to handle it.
- The heap2 prune redo payload differs only in width: 17/18 use `{reason, flags}` and 19 uses a 16-bit `flags`; `pgmoneta_wal_heap2_desc` dispatches on `server_config->version >= 19` to the v19 layout and uses the shared deserializer for the block-0 sub-records (`xlhp_freeze_plans`, redirected/dead/now-unused arrays, `frz_offsets`), whose stored order matches PostgreSQL exactly.

## Testing

Unit tests live in `test/testcases/test_walbridge.c` and cover the LSN map lookup, the migration engine translations (heap2 prune, heap2 visible, checkpoint, multixact create/truncate, gist), the full-page-image checksum recompute helper, a full store round-trip, a store run where a record header straddles a page boundary and a store run where a record ends exactly at a page boundary (write translated records, parse the downstream segment back, verify LSNs, chaining and CRCs):

```bash
cmake -S . -B build -Dcheck=ON
cmake --build build --target pgmoneta-test
build/test/pgmoneta-test -m walbridge
```

A live end-to-end run streams a PostgreSQL 18 primary through the receiver into the downstream store, serves it with the sender and downloads it with the PostgreSQL 19 `pg_receivewal` (using the `walbridge` slot). The downloaded segments are byte-identical to the downstream segments and `pg_waldump` parses them with its own decoder and CRC checks with zero errors:

A **byte-continuity / reconnect-resume** test additionally proves that a client which disconnects mid-segment and reconnects receives exactly the bytes a continuous capture would: a reference capture via `pg_receivewal` (pulled through the store's translated frontier into the live-tail zone) is byte-identical to the combined output of a capture cut mid-segment plus a second capture resumed from that point, across the full overlap including the store/live-tail boundary. Restart-based resume is validated end-to-end too: killing and relaunching the daemon resumes the receiver exactly at the persisted `last_up`/`done_segno` and appends new upstream segments without re-translating old ones — the pre-resume store region is md5-identical (`769d34d58bd823739a5da58f2d802ce7`) and a fresh full-segment capture matches the store (`dfadc9d0b29dbfd73c8573a55768397f`):

```bash
# continuous reference
PGPASSWORD='<password>' /usr/lib/postgresql/19/bin/pg_receivewal \
  -h 127.0.0.1 -p 9970 -U <replication_user> -D /tmp/ref -v -E 0/66400000
# cut mid-segment ...
PGPASSWORD='<password>' /usr/lib/postgresql/19/bin/pg_receivewal \
  -h 127.0.0.1 -p 9970 -U <replication_user> -D /tmp/dl -v -E 0/66350000
# ... then resume in the same directory to the same end position
PGPASSWORD='<password>' /usr/lib/postgresql/19/bin/pg_receivewal \
  -h 127.0.0.1 -p 9970 -U <replication_user> -D /tmp/dl -v -E 0/66400000
```

> Replace `<password>`, `<replication_user>`, and the `-E` target LSN if needed to match your environment before running the commands.

One nuance discovered while validating restart resume: `DEFAULT_WAL_SEGZ_BYTES` is used in the legacy 4-field state fallback as a divisor, and because it was defined without parentheses (`16 * 1024 * 1024`) the fallback evaluated `last_up / 16 * 1024 * 1024`, producing a poisoned `done_segno` that skipped every pending segment after restart. The macro is now parenthesized (`(16 * 1024 * 1024)`).

Note that `pg_receivewal` itself floors a resume to a segment boundary (a `.partial` must be 0 or a whole segment), so the exact mid-segment resumed bytes are proven two ways: the archived (store-served) prefix matches the store md5, and the client-side concatenation matches the continuous capture.

```bash
PGPASSWORD='<password>' /usr/lib/postgresql/19/bin/pg_receivewal \
  -h 127.0.0.1 -p 9970 -U <replication_user> -D /tmp/dl --slot=<slot_name> -v
/usr/lib/postgresql/19/bin/pg_waldump /tmp/dl/000000010000000000000000
```

> Replace `<password>`, `<replication_user>`, and `<slot_name>` with your actual PostgreSQL values before executing these commands.

The unit suite passes 28/28 (`build/test/pgmoneta-test -m walbridge`).
