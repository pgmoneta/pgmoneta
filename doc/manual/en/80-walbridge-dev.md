\newpage

# pgmoneta-walbridge developer guide

This document covers the internals, architecture, testing and public API of the WAL bridge subsystem.

For installation, configuration and usage see **21-walbridge.md**.

## Components

| Component | File | Purpose |
| :-------- | :--- | :------ |
| Orchestrator | `src/libpgmoneta/walbridge/walbridge.c` | Startup, configuration, and receiver/sender process management |
| Receiver | `src/libpgmoneta/walbridge/wal_receiver.c` | Spawns the existing WAL client (`pgmoneta_wal`) and retains/prunes raw upstream segments only |
| Migration engine | `src/libpgmoneta/walbridge/migration_engine.c` | Translates individual 18.x records to 19.x (per-record handlers, CRC recomputation) |
| Encoder | `src/libpgmoneta/walbridge/wal_encode.c` | Defines the downstream 19 delivery layout produced from raw records, including CRC and full-page-image checksums |
| Config | `src/libpgmoneta/configuration.c` | Parses the upstream (per-server) sections and the `[pgmoneta-walbridge]` section (`host`, `port`) |
| LSN map | `src/libpgmoneta/walbridge/lsn_map.c` | Persists the `(upstream LSN -> downstream LSN)` mapping per record |
| Sender | `src/libpgmoneta/walbridge/wal_sender.c` | Physical replication server that decodes, translates, and encodes retained raw WAL for delivery |

## How it works

1. **Startup**: `pgmoneta-walbridge` loads configuration and credentials, then opens the configured server WAL directory. The LSN map is `<wal dir>/walbridge.lsnmap`.

2. **Receiver**: runs the existing pgmoneta WAL client to fetch and durably retain raw PostgreSQL 18 WAL. It does not translate or create a target-version WAL cache. It supervises the client and prunes raw segments only after persisted downstream feedback permits it.

3. **Sender**: on `START_REPLICATION`, resolves the requested downstream LSN with the persisted map, reads the relevant raw WAL records, translates them, encodes PostgreSQL 19 WAL, and sends contiguous `XLogData`. It records and flushes new LSN-map entries before publishing the associated stream bytes.

4. **Migration engine**: translates 18.x records to 19.x. Notable translations:
   - `XLOG_CHECKPOINT_REDO`: 4-byte `{wal_level}` payload becomes 8-byte `{wal_level, data_checksum_version}`. Data checksums are always supported, so the downstream checkpoint advertises data checksums enabled.
   - `XLOG_CHECKPOINT_ONLINE` carries the checkpoint record; its checksum state is likewise passed through.
   - `XLOG_HEAP2_PRUNE_VACUUM_CLEANUP`: 8-byte `{oldest_xid, new_relfilenumber, flags, nredirected, ndead}` layout becomes the 19.x 16-byte `{oldest_xid, new_relfilenumber, flags, nredirected, ndead, 0, 0, 0}` layout.
   - `XLOG_HEAP2_VISIBLE`, `XLOG_MULTIXACT_CREATE_ID`, `XLOG_MULTIXACT_TRUNCATE_ID` and `XLOG_GIST_ASSIGN_LSN` are handled so that the downstream chaining (`xl_prev`) and record CRCs stay correct.
   - Every translated record gets a recomputed CRC-32C (Castagnoli) since the payload changed.

5. **Downstream stream layout**: records are laid out at 8-byte aligned positions starting at LSN 40 (after the 40-byte long page header with 19.x magic `0xD121`), chained via `xl_prev`, padded with `MAXALIGN`, and finished with a segment switch + tail pad. Exactly like PostgreSQL, a record that does not fit in the remaining page space continues across the page boundary (partial header or data, then a continuation page carrying `XLP_FIRST_IS_CONTRECORD` and `xlp_rem_len`), so no zero gap is ever left between records. The downstream LSNs are entirely independent of the upstream LSNs.

6. **LSN map and retention**: the map stores `upstream_lsn downstream_lsn` pairs. On a standby status update, the sender maps durable downstream flush feedback backwards and atomically persists it in `<wal dir>/walbridge.lsnmap.feedback`. The receiver retains the raw segment containing that upstream LSN and prunes only older raw segments. `READ_REPLICATION_SLOT` reports the retained downstream floor.

7. **Protocol**: the sender is a raw-socket physical replication server (no TLS or metrics). It authenticates against `pgmoneta_users.conf`, answers `IDENTIFY_SYSTEM` and `READ_REPLICATION_SLOT`, and accepts `START_REPLICATION`.

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
int pgmoneta_walbridge_run_receiver(int srv, char** argv);
int pgmoneta_walbridge_run_sender(int srv, char* downstream_dir, char* map_path);
```

### Orchestrator (`walbridge.h`)

```c
int pgmoneta_walbridge_start(char* configuration_path, char* users_path, char** argv);
```

## Cross-segment record reassembly

A single PostgreSQL record can physically span a segment boundary: the tail is written at the end of segment N and continued on the first page of segment N+1, whose long page header carries `XLP_FIRST_IS_CONTRECORD` and `xlp_rem_len`. The reader (`pgmoneta_wal_parse_wal_file`) reassembles such records:

- `struct partial_xlog_record` gains `from_seg` (logical segment number the tail came from) and `lsn` (absolute start LSN of the partial record). Both save sites record them: the split-header site and the cross-EOF data tail site.
- A continuity guard runs at the top of a parse: a saved partial is consumed only when parsing the immediate successor segment (`from_seg == logSegNo - 1`); otherwise it is stale state (unrelated stream or a torn, still-growing segment) and is discarded, so a record is never spliced from unrelated bytes.
- Reconstruction: when the successor channel is a continuation, the saved header + saved body are merged with the remaining `xlp_rem_len` bytes on page 0. The decoded record uses the saved absolute start LSN (`partial_lsn`) for its `lsn`, `xl_prev`/CRC integrity and correct downstream chaining.
- A second stale-guard protects the data buffer: if `data_buffer_bytes_read` exceeds the current record's `data_length`, the saved bytes cannot be this record's continuation and are discarded before reading fresh.

A real bug surfaced while testing this path. The continuation offset was computed from `ftell(file)`; but `read_all_page_headers()` has already moved the file cursor (to the segment end when a torn page is present), so the position-relative offset was `MAXALIGN(32768 + …)` instead of `MAXALIGN(40 + …)`, and every record following the continuation on page 0 was silently skipped. The offset is now anchored to `SIZE_OF_XLOG_LONG_PHD` (the fixed page-0 data start). The `test_walbridge_reader_cross_segment` unit test crafts two 32 KiB segments (a 4-page tail stream ending exactly at the segment EOF, and its continuation with a full set of fillers) and verifies the reassembled record bytes, LSN, the recovered records after the continuation, and the stale-discard behaviour.

## PG18/PG19 WAL compatibility matrix

Verified against the PostgreSQL 18 and PostgreSQL 19 beta3 headers:

| Concern | PG17 | PG18 | PG19 |
| :------ | :--- | :--- | :--- |
| `XLogRecord`, block/data headers | identical | identical | identical |
| `XLogRecordBlockHeader.fork_flags` layout (fork low 4 bits, `HAS_IMAGE 0x10`, `HAS_DATA 0x20`, `WILL_INIT 0x40`, `SAME_REL 0x80`) | identical | identical | identical |
| Block IDs (`DATA_SHORT 255`, `DATA_LONG 254`, `ORIGIN 253`, `TOPLEVEL_XID 252`) | identical | identical | identical |
| `xl_heap_prune` | `{reason, flags}` (`XLHP_*`) | `{reason, flags}` (`XLHP_*`) | `{uint16 flags}` (`XLHP_*`) |
| `xlp_info` flags (`XLP_ALL_FLAGS`) | 0x0001\|0x0002\|0x0004\|0x0008 = 0x000F (BKP_REMOVABLE 0x0004, OVERWRITE_CONTRECORD 0x0008) | identical to 17 (0x000F) | 0x0001\|0x0002\|0x0004 = 0x0007 (BKP_REMOVABLE removed; OVERWRITE_CONTRECORD re-used 0x0004) |
| WAL magic | 0xD116 | 0xD118 | 0xD121 (PostgreSQL 19 beta3) |

Consequences enforced in pgmoneta:

- The block-header format is identical between 18 and 19, so raw 18.x blocks decode with the same constants; no flag remap is needed for `fork_flags`.
- The page-header flag `XLP_FIRST_IS_OVERWRITE_CONTRECORD` moved from 0x0008 (17/18) to 0x0004 (19), and `XLP_BKP_REMOVABLE` (0x0004 in 17/18) was removed in 19. `pgmoneta_wal_store_remap_page_flags` maps the 18.x overwrite-contrecord bit 0x0008 → 0x0004 and drops 18.x BKP_REMOVABLE. The reader accepts the 18.x flags; the store never emits them (it builds fresh page headers).
- The only PG19 target is beta3: it uses magic `0xD121`, carries CheckPoint checksum state, and writes the eight-byte `{int wal_level, uint32 data_checksum_version}` `xl_checkpoint_redo` payload.
- `XLH_PRUNE_NO_LOGICAL` does **not** appear in PostgreSQL 17/18/19 prune records. It is a PG15-era flag (`no_untouched_dead_items`) removed in PG17 when the `{reason, flags}` prune format was introduced; nothing in the 18→19 bridge has to handle it.
- The heap2 prune redo payload differs only in width: 17/18 use `{reason, flags}` and 19 uses a 16-bit `flags`; `pgmoneta_wal_heap2_desc` dispatches on `server_config->version >= 19` to the v19 layout and uses the shared deserializer for the block-0 sub-records (`xlhp_freeze_plans`, redirected/dead/now-unused arrays, `frz_offsets`), whose stored order matches PostgreSQL exactly.

## Testing

Unit tests live in `test/testcases/test_walbridge.c`. They cover LSN-map lookup and persistence, downstream-feedback retention and raw-archive pruning, record translations, CRC recomputation, WAL encoding, protocol messages, and reconnect/resume decisions.

```bash
cmake -S . -B build -Dcheck=ON
cmake --build build --target pgmoneta-test
build/test/pgmoneta-test -m walbridge
```

The supported live validation uses a PostgreSQL 18 primary and PostgreSQL 19 beta3 `pg_receivewal`/`pg_waldump`; it is documented in `79-walbridge.md`. It validates a received archive, not PostgreSQL 19 recovery from a PostgreSQL 18 data directory.
