# walbridge end-to-end harness

`run.sh` runs `pgmoneta-walbridge` between a real PostgreSQL 18 primary and
a real PostgreSQL 19 `pg_receivewal` and checks the WAL that comes out. It
complements the MCTF unit tests in `test/testcases/test_walbridge.c`, which do
not start a server.

```
PG18 primary --(physical, slot "walbridge")--> pgmoneta-walbridge --(physical)--> PG19 pg_receivewal
```

## Requirements

- Linux, bash, `ss` (iproute2), `od`, `awk`
- a pgmoneta build with `pgmoneta-walbridge` and `pgmoneta-admin` (default
  `build/` in this checkout; an ASAN Debug build is fine)
- PostgreSQL 18 (`postgres`, `initdb`, `pg_ctl`, `psql`, `pg_waldump`) and
  PostgreSQL 19 (`postgres`, `psql`, `pg_receivewal`, `pg_waldump`)

Set `PG18BIN` and `PG19BIN` to the two bin directories. If a variable is not
set, the harness takes the first directory on `PATH` whose `postgres --version`
reports that major version. It stops with an error if it finds none.
Distribution packages usually keep the binaries off `PATH`, for example in
`/usr/lib/postgresql/18/bin`.

## Usage

```
PG18BIN=/usr/lib/postgresql/18/bin PG19BIN=/usr/lib/postgresql/19/bin \
    test/walbridge/run.sh [-o DIR] [-k] [-l] [WORKLOAD | stream ...]
```

| Argument | Meaning |
| :-- | :-- |
| `stream` | every workload (the default) |
| `WORKLOAD` | one workload by name (`-l` lists them) |
| `-o DIR` | artifact directory (default `<work dir>/artifacts`) |
| `-k` | keep the data directories (normally deleted after each workload) |

| Environment | Default |
| :-- | :-- |
| `PG18BIN`, `PG19BIN` | found on `PATH` (see above) |
| `PGMONETA_BUILD` | `<checkout>/build` |
| `WB_E2E_TIMEOUT` | `120` seconds to wait for the stream to deliver the end marker |
| `WB_E2E_LIVE_TAIL_WAIT` | `10` seconds to wait for a record without a segment switch |
| `WB_E2E_STRICT_XPASS` | `1`: an expected failure that passes fails the run |

Everything goes into a fresh `mktemp -d` directory under `$TMPDIR` (or
`/tmp`). The ports of PostgreSQL and walbridge are picked at run time, so the
harness can run next to other clusters. `HOME` points into the work directory
for pgmoneta, so the master key is created there and the user's own key is
not touched.

`pgmoneta-walbridge` and `pgmoneta-admin` are copied into the work directory
and run from there. This works around pgmoneta's backtrace helper, which
passes the executable path to `addr2line` unquoted and hangs when the path
contains a space (#1718).

The exit status is 0 when every check is `PASS`, `XFAIL`, `FLAKY-PASS`,
`FLAKY-FAIL` or `SKIP`. It is 1 when any check is `FAIL`, or `XPASS` while
`WB_E2E_STRICT_XPASS=1`. It is 2 when the harness itself cannot run, for
example because a binary is missing.

## How a workload runs

Each workload gets a fresh PG18 primary: `initdb`, `wal_level = replica`, a
replication user and a physical slot. It also gets its own walbridge and a
PG19 `pg_receivewal` that reads from walbridge. The workload runs between two
`pg_logical_emit_message()` markers. After the end marker the harness calls
`pg_switch_wal()` (see the live-tail expected failure) and waits for the end
marker to arrive.

Then PG19 `pg_waldump` reads the received WAL, and PG18 `pg_waldump` reads the
primary's `pg_wal` over the same range. Both are reduced to the ordered list
of record types (`rmgr type`) between the markers, and the two lists are
compared. The comparison applies walbridge's translations. An upstream
`XLOG SWITCH` or `Gist ASSIGN_LSN` is dropped. `Heap2 VISIBLE` no longer
exists in PostgreSQL 19 and becomes `Heap2 PRUNE_VACUUM_CLEANUP`.

## Checks

Every workload runs these:

| Check | Passes when |
| :-- | :-- |
| `stream_reaches_end_marker` | the end marker arrives within `WB_E2E_TIMEOUT` |
| `waldump_clean` | PG19 `pg_waldump` reads the received WAL through the end marker; the only error allowed is an ordinary end of WAL after it |
| `record_sequence_matches_upstream` | the received records between the markers have the same types, in the same order, as the primary's |
| `walbridge_alive` | walbridge is still running |
| `walbridge_sanitizer_clean` | walbridge's stderr has no ASAN or UBSAN report |
| `walbridge_log_no_errors` | `walbridge.log` has no `ERROR` or `FATAL` line other than the known ones (below) |
| `identify_system_sysid` | `IDENTIFY_SYSTEM` reports a nonzero system identifier, equal to the one in the long page header of the received WAL |
| `identify_system_sysid_no_plain_segment` | the same check, used instead when no sealed raw segment in walbridge's archive is plain (uncompressed and unencrypted) |

`walbridge_log_no_errors` does not fail on lines listed in the
`KNOWN_LOG_ERRORS` table at the top of `run.sh`: each one belongs to an open
issue. Its detail counts them per issue. The table also covers the
disconnect, "starting up" and "Authentication failed" lines that every
primary restart produces in the crash workloads; those are expected
behaviour, not a bug. The WAL client logs "Authentication failed" when the
restarting primary answers its startup packet with "the database system is
starting up". These lines are accepted only inside a restart window, from
the immediate stop until 10 s after `pg_ctl start` returns. The same line at
any other time is reported as an unknown error.

The compression and encryption workloads have two more checks. Compression
and encryption run in the background after a segment is sealed, so before
any archive check the harness waits until every sealed segment is done:

| Check | Passes when |
| :-- | :-- |
| `raw_archive_fully_processed` | within 30 s, every sealed raw segment carries the configured suffix (`.gz`, `.zstd.aes`, ...) |
| `lsnmap_not_compressed` | after a walbridge restart, `walbridge.lsnmap` and its feedback file are still plain. The restart makes the WAL client run its startup scan of the raw directory |

The system identifier check runs after that restart, when the WAL client
has processed everything it is going to process.

The workloads and their own checks:

| Workload | What it does | Its own checks |
| :-- | :-- | :-- |
| `heap` | INSERT, HOT and non-HOT UPDATE, DELETE, VACUUM, VACUUM (FREEZE) | `heap_workload_generated`: the primary wrote the record types the workload is meant to produce. `live_tail_delivered_without_switch`: a record arrives while its upstream segment is still being written |
| `multixact` | concurrent `SELECT ... FOR SHARE` and `FOR KEY SHARE` | `multixact_workload_generated`. `multixact_members_and_offsets_match`: every received MultiXact `CREATE_ID` has the same members and offsets as upstream |
| `gist` | GiST index build, inserts with page splits, deletes, VACUUM | `gist_workload_generated` |
| `checkpoint` | two `CHECKPOINT`s | `checkpoint_workload_generated`. `checkpoint_redo_lsn_translated`: the redo pointer of every received online checkpoint is the LSN of a received record, at or before the checkpoint |
| `bigmsg` | one 20 MiB `pg_logical_emit_message()`, a record that spans segments | `big_logical_message_intact`: the message arrives whole |
| `switch` | three `pg_switch_wal()` calls with records between them, plus one on an empty segment | `switch_workload_generated`. `records_after_each_switch_received` |
| `crash` | `pg_ctl stop -m immediate` once 24 MiB of a 128 MiB record are flushed, then restart. Retried (up to 3 times) until the primary has written `XLOG_OVERWRITE_CONTRECORD` | `crash_tore_a_record`: the primary wrote it. `walbridge_reconnected_upstream`: the slot is active again. `overwrite_contrecord_not_forwarded`: the received WAL has no `XLOG_OVERWRITE_CONTRECORD`, because walbridge only sends complete records |
| `crash_repeat` | the same, three torn records in a row | as `crash` |
| `reconnect` | `kill -9` of `pg_receivewal` partway through the stream, more WAL, restart on the same directory | `receivewal_resumed_after_kill`: streaming resumes and the record written while it was down arrives exactly once |
| `seg1mb` | `initdb --wal-segsize=1`, heap work, a 600 KiB message and wide rows that cross segment boundaries | `seg1mb_wal_segment_size_reported`: `SHOW wal_segment_size` is `1MB`. `seg1mb_received_segment_size`: every received file is 1 MiB |
| `compress_gzip`, `compress_zstd`, `compress_lz4`, `compress_bzip2` | walbridge `compression = <type>`, several segments of WAL | `raw_archive_compressed`: walbridge's archive really holds compressed segments |
| `encrypt_aes`, `compress_zstd_aes` | `encryption = aes-256-gcm`, with and without zstd | `raw_archive_encrypted` / `raw_archive_compressed_encrypted` |

## Expected failures

Checks that fail on the current code are listed in the `EXPECTED_FAILURES`
table at the top of `run.sh`, each with a pattern (`workload/check`), a
reason, its issue and a kind:

- `xfail`: the check fails on every run. It reports `XFAIL` and does not fail
  the run. If it starts to pass, it reports `XPASS` and the run fails until its
  line is removed.
- `flaky`: a race in walbridge or the WAL client decides the outcome. The
  check reports `FLAKY-PASS` or `FLAKY-FAIL` and never fails the run. Only real
  races in the code are marked flaky; where the harness's timing was the
  cause, the harness waits instead.

With the current code, `run.sh stream` exits 0.

## Artifacts

```
<artifacts>/
  environment.txt  results.tsv  summary.txt  run.log
  <workload>/      (<workload>.2, .3, ... when a workload is named more than once)
    logs/          pg18.log, walbridge.log, walbridge.out, pg_receivewal.N.log, initdb18.log
    received/      the WAL exactly as pg_receivewal wrote it (.partial included)
    waldump.txt    PG19 pg_waldump of the received WAL (lines cut at 400 chars)
    waldump.err    its stderr
    waldump_stats.txt
    upstream.txt   PG18 pg_waldump of the primary over the same range
    seq_up.txt, seq_down.txt, seq.diff
    raw_archive.ls walbridge's raw archive listing
```

`results.tsv` has one line per check: workload, check, status, detail.
