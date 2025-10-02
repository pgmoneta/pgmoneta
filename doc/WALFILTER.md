# pgmoneta-walfilter

`pgmoneta-walfilter` is a command-line utility that filters PostgreSQL Write-Ahead Log (WAL) files based on user-defined rules.

## Overview

The tool reads WAL files from a source directory, applies filtering rules to remove unwanted operations or transactions, recalculates the CRC checksums, and writes the filtered WAL files to a target directory.

## Filtering Rules

The tool supports two types of filtering:

1. **Transaction ID (XID) filtering**: Filter out specific transaction IDs
   - Specify a list of XIDs to remove from the WAL stream
   - All records associated with these XIDs will be filtered out

2. **Operation-based filtering**: Filter out specific database operations
   - `DELETE`: Removes all DELETE operations and their associated transactions from the WAL stream

## Configuration

The tool uses two configuration files:
1. A YAML configuration file (required) that specifies source/target directories, filtering rules, and other settings
2. A `pgmoneta_walfilter.conf` file (optional) for logging configuration

### YAML Configuration

The YAML file is the main configuration file that specifies what to filter and where to write the output.

Example with filtering rules:

```yaml
source_dir: /path/to/source/backup/directory
target_dir: /path/to/target/directory
configuration_file: /etc/pgmoneta/pgmoneta_walfilter.conf
rules:                             # Optional: filtering rules
  - xids:                          # Filter by transaction IDs
    - 752
    - 753
```

### pgmoneta_walfilter.conf

The `pgmoneta_walfilter.conf` file is used for logging configuration and is loaded from either the path specified in the YAML configuration, or `/etc/pgmoneta/pgmoneta_walfilter.conf` if not provided.

## [pgmoneta_walfilter]

| Property | Default | Unit | Required | Description |
| :------- | :------ | :--- | :------- | :---------- |
| log_type | console | String | No | The logging type (console, file, syslog) |
| log_level | info | String | No | The logging level, any of the (case insensitive) strings `FATAL`, `ERROR`, `WARN`, `INFO` and `DEBUG` (that can be more specific as `DEBUG1` thru `DEBUG5`). Debug level greater than 5 will be set to `DEBUG5`. Not recognized values will make the log_level be `INFO` |
| log_path | pgmoneta.log | String | No | The log file location. Can be a strftime(3) compatible string. |

## Server section

| Property | Default | Unit | Required | Description |
| :------- | :------ | :--- | :------- | :---------- |
| host | | String | Yes | The address of the PostgreSQL instance |
| port | | Int | Yes | The port of the PostgreSQL instance |
| user | | String | Yes | The replication user name |

## Demo: Filtering by Transaction IDs (XIDs)

This demo shows how to filter specific transactions from WAL files using `pgmoneta-walfilter`.

### Step 1: Create a YAML configuration file

Create a file named `~/filter_config.yaml` with the following content:

```yaml
source_dir: ~/demo/source
target_dir: ~/demo/target
configuration_file: ~/pgmoneta_walfilter.conf
rules:
  - xids:
    - 840
```

### Step 2: (Optional) Create a pgmoneta_walfilter.conf file

If you want custom logging configuration, create a `pgmoneta_walfilter.conf` file:

```ini
[pgmoneta-walfilter]
log_type = file
log_level = debug5
log_path = /tmp/pgmoneta.log
```

### Step 3: Prepare the source WAL files

Ensure you have WAL files in your source directory:

```bash
➜  ~ ls ~/demo/source
000000010000000000000021
```

### Step 4: Inspect the original WAL file

Use `pgmoneta-walinfo` to inspect the original WAL file and identify XIDs to filter:

```bash
➜  ~ pgmoneta-walinfo ~/demo/source/000000010000000000000021
XLOG        | 0/20000120 | 0/21000028 | 49   | 8185 | 0   | FPI_FOR_HINT  blkref #0: rel 1663/16389/2610 forknum 0 blk 2 (FPW); hole: offset: 296, length: 56, compression saved: 0, method: unknown
Heap2       | 0/21000028 | 0/21002040 | 58   | 58   | 0   | snapshot_conflict_horizon_id: 835, is_catalog_rel: F, nplans: 0, nredirected: 0, ndead: 2, nunused: 0, dead: [67, 68] blkref #0: rel 1663/16389/2610 forknum 0 blk 2
XLOG        | 0/21002040 | 0/21002080 | 49   | 3897 | 0   | FPI_FOR_HINT  blkref #0: rel 1663/16389/1259 forknum 0 blk 0 (FPW); hole: offset: 232, length: 4344, compression saved: 0, method: unknown
XLOG        | 0/21002080 | 0/21002FC0 | 49   | 7233 | 0   | FPI_FOR_HINT  blkref #0: rel 1663/16389/1259 forknum 0 blk 1 (FPW); hole: offset: 264, length: 1008, compression saved: 0, method: unknown
XLOG        | 0/21002FC0 | 0/21004C20 | 49   | 7461 | 0   | FPI_FOR_HINT  blkref #0: rel 1663/16389/1259 forknum 0 blk 5 (FPW); hole: offset: 236, length: 780, compression saved: 0, method: unknown
Heap2       | 0/21004C20 | 0/21006960 | 58   | 58   | 0   | snapshot_conflict_horizon_id: 835, is_catalog_rel: F, nplans: 0, nredirected: 0, ndead: 2, nunused: 0, dead: [49, 52] blkref #0: rel 1663/16389/1259 forknum 0 blk 5
Standby     | 0/21006960 | 0/210069A0 | 50   | 50   | 0   | RUNNING_XACTS next_xid 836 latest_completed_xid 835 oldest_running_xid 836
XLOG        | 0/210069A0 | 0/210069D8 | 30   | 30   | 0   | 
Standby     | 0/210069D8 | 0/210069F8 | 50   | 50   | 0   | RUNNING_XACTS next_xid 836 latest_completed_xid 835 oldest_running_xid 836
XLOG        | 0/210069F8 | 0/21006A30 | 114  | 114  | 0   | CHECKPOINT_ONLINE redo 0/210069D8; tli 1; prev tli 1; fpw true; wal_level replica; xid 0:836; oid 81940; multi 1; offset 0; oldest xid 731 in DB 1; oldest multi 1 in DB 1; oldest/newest commit timestamp xid: 0/0; oldest running xid 836 online
Standby     | 0/21006A30 | 0/21006AA8 | 50   | 50   | 0   | RUNNING_XACTS next_xid 836 latest_completed_xid 835 oldest_running_xid 836
XLOG        | 0/21006AA8 | 0/21006AE0 | 114  | 114  | 0   | CHECKPOINT_SHUTDOWN redo 0/21006AE0; tli 1; prev tli 1; fpw true; wal_level replica; xid 0:836; oid 81940; multi 1; offset 0; oldest xid 731 in DB 1; oldest multi 1 in DB 1; oldest/newest commit timestamp xid: 0/0; oldest running xid 0 shutdown
Standby     | 0/21006AE0 | 0/21006B58 | 50   | 50   | 0   | RUNNING_XACTS next_xid 836 latest_completed_xid 835 oldest_running_xid 836
XLOG        | 0/21006B58 | 0/21006B90 | 49   | 8237 | 0   | FPI_FOR_HINT  blkref #0: rel 1663/16389/2610 forknum 0 blk 0 (FPW); hole: offset: 268, length: 4, compression saved: 0, method: unknown
Heap2       | 0/21006B90 | 0/21008BD8 | 58   | 58   | 0   | snapshot_conflict_horizon_id: 835, is_catalog_rel: F, nplans: 0, nredirected: 0, ndead: 2, nunused: 0, dead: [60, 61] blkref #0: rel 1663/16389/2610 forknum 0 blk 0
XLOG        | 0/21008BD8 | 0/21008C18 | 49   | 7409 | 0   | FPI_FOR_HINT  blkref #0: rel 1663/16389/2610 forknum 0 blk 3 (FPW); hole: offset: 248, length: 832, compression saved: 0, method: unknown
Standby     | 0/21008C18 | 0/2100A928 | 50   | 50   | 0   | RUNNING_XACTS next_xid 836 latest_completed_xid 835 oldest_running_xid 836
XLOG        | 0/2100A928 | 0/2100A960 | 30   | 30   | 0   | 
Standby     | 0/2100A960 | 0/2100A980 | 50   | 50   | 0   | RUNNING_XACTS next_xid 836 latest_completed_xid 835 oldest_running_xid 836
XLOG        | 0/2100A980 | 0/2100A9B8 | 114  | 114  | 0   | CHECKPOINT_ONLINE redo 0/2100A960; tli 1; prev tli 1; fpw true; wal_level replica; xid 0:836; oid 81940; multi 1; offset 0; oldest xid 731 in DB 1; oldest multi 1 in DB 1; oldest/newest commit timestamp xid: 0/0; oldest running xid 836 online
Standby     | 0/2100A9B8 | 0/2100AA30 | 50   | 50   | 0   | RUNNING_XACTS next_xid 836 latest_completed_xid 835 oldest_running_xid 836
XLOG        | 0/2100AA30 | 0/2100AA68 | 114  | 114  | 0   | CHECKPOINT_SHUTDOWN redo 0/2100AA68; tli 1; prev tli 1; fpw true; wal_level replica; xid 0:836; oid 81940; multi 1; offset 0; oldest xid 731 in DB 1; oldest multi 1 in DB 1; oldest/newest commit timestamp xid: 0/0; oldest running xid 0 shutdown
Standby     | 0/2100AA68 | 0/2100AAE0 | 50   | 50   | 0   | RUNNING_XACTS next_xid 836 latest_completed_xid 835 oldest_running_xid 836
XLOG        | 0/2100AAE0 | 0/2100AB18 | 30   | 30   | 0   | NEXTOID 90132
Standby     | 0/2100AB18 | 0/2100AB38 | 42   | 42   | 836 | LOCK xid 836 db 16389 rel 1450416 
Storage     | 0/2100AB38 | 0/2100AB68 | 42   | 42   | 836 | base/16389/81940
Heap        | 0/2100AB68 | 0/2100AB98 | 54   | 5814 | 836 | INSERT off 54 flags 0x00 blkref #0: rel 1663/16389/1247 forknum 0 blk 14 (FPW); hole: offset: 240, length: 2432, compression saved: 0, method: unknown
Btree       | 0/2100AB98 | 0/2100C268 | 53   | 5893 | 836 | INSERT_LEAF  off: 290 blkref #0: rel 1663/16389/2703 forknum 0 blk 2 (FPW); hole: offset: 1184, length: 2352, compression saved: 0, method: unknown
Btree       | 0/2100C268 | 0/2100D970 | 53   | 6529 | 836 | INSERT_LEAF  off: 110 blkref #0: rel 1663/16389/2704 forknum 0 blk 2 (FPW); hole: offset: 764, length: 1716, compression saved: 0, method: unknown
...
Heap2       | 0/2101F768 | 0/2101F7A8 | 85   | 85   | 836 | 1 tuples flags 0x02 blkref #0: rel 1663/16389/2608 forknum 0 blk 13
Btree       | 0/2101F7A8 | 0/2101F800 | 53   | 4677 | 836 | INSERT_LEAF  off: 99 blkref #0: rel 1663/16389/2673 forknum 0 blk 10 (FPW); hole: offset: 568, length: 3568, compression saved: 0, method: unknown
Btree       | 0/2101F800 | 0/21020A60 | 72   | 72   | 836 | INSERT_LEAF  off: 141 blkref #0: rel 1663/16389/2674 forknum 0 blk 8
Transaction | 0/21020A60 | 0/21020AA8 | 565  | 565  | 836 | COMMIT 2025-07-15 17:28:20.716006 EEST; inval msgs: catcache 80 catcache 79 catcache 80 catcache 79 catcache 55 catcache 54 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 snapshot 2608 relcache 81940
Standby     | 0/21020AA8 | 0/21020CE0 | 42   | 42   | 837 | LOCK xid 837 db 16389 rel 1461328 
Storage     | 0/21020CE0 | 0/21020D10 | 42   | 42   | 837 | base/16389/81943
Heap        | 0/21020D10 | 0/21020D40 | 211  | 211  | 837 | INSERT off 56 flags 0x00 blkref #0: rel 1663/16389/1247 forknum 0 blk 14
...
Btree       | 0/21021E18 | 0/21021E70 | 72   | 72   | 837 | INSERT_LEAF  off: 100 blkref #0: rel 1663/16389/2673 forknum 0 blk 10
Btree       | 0/21021E70 | 0/21021EB8 | 72   | 72   | 837 | INSERT_LEAF  off: 143 blkref #0: rel 1663/16389/2674 forknum 0 blk 8
Transaction | 0/21021EB8 | 0/21021F00 | 501  | 501  | 837 | COMMIT 2025-07-15 17:28:20.716661 EEST; inval msgs: catcache 80 catcache 79 catcache 80 catcache 79 catcache 55 catcache 54 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 snapshot 2608 relcache 81943
Standby     | 0/21021F00 | 0/21022110 | 42   | 42   | 838 | LOCK xid 838 db 16389 rel 1471216 
...
Btree       | 0/210231C0 | 0/21023208 | 64   | 64   | 838 | INSERT_LEAF  off: 391 blkref #0: rel 1663/16389/2659 forknum 0 blk 11
Heap2       | 0/21023208 | 0/21023248 | 85   | 85   | 838 | 1 tuples flags 0x02 blkref #0: rel 1663/16389/2608 forknum 0 blk 13
Btree       | 0/21023248 | 0/210232A0 | 72   | 72   | 838 | INSERT_LEAF  off: 101 blkref #0: rel 1663/16389/2673 forknum 0 blk 10
Btree       | 0/210232A0 | 0/210232E8 | 72   | 72   | 838 | INSERT_LEAF  off: 145 blkref #0: rel 1663/16389/2674 forknum 0 blk 8
Transaction | 0/210232E8 | 0/21023330 | 501  | 501  | 838 | COMMIT 2025-07-15 17:28:20.717062 EEST; inval msgs: catcache 80 catcache 79 catcache 80 catcache 79 catcache 55 catcache 54 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 snapshot 2608 relcache 81946
Standby     | 0/21023330 | 0/21023528 | 42   | 42   | 839 | LOCK xid 839 db 16389 rel 1481104 
Storage     | 0/21023528 | 0/21023558 | 42   | 42   | 839 | base/16389/81949
Heap        | 0/21023558 | 0/21023588 | 211  | 211  | 839 | INSERT off 60 flags 0x00 blkref #0: rel 1663/16389/1247 forknum 0 blk 14
...
Standby     | 0/21026B58 | 0/21026D30 | 42   | 42   | 840 | LOCK xid 840 db 16389 rel 1492720 
Standby     | 0/21026D30 | 0/21026D60 | 42   | 42   | 840 | LOCK xid 840 db 16389 rel 1492848 
Standby     | 0/21026D60 | 0/21026D90 | 42   | 42   | 840 | LOCK xid 840 db 16389 rel 1492976 
Standby     | 0/21026D90 | 0/21026DC0 | 42   | 42   | 840 | LOCK xid 840 db 16389 rel 1493104 
Storage     | 0/21026DC0 | 0/21026DF0 | 42   | 42   | 840 | base/16389/81952
...
Heap2       | 0/21FF4F10 | 0/21FF68A0 | 59   | 59   | 840 | cutoff xid 0 flags 0x03 blkref #0: rel 1663/16389/81952 forknum 2 blk 0 blkref #1: rel 1663/16389/81952 forknum 0 blk 2469
Heap2       | 0/21FF9C68 | 0/21FFB5F8 | 59   | 59   | 840 | cutoff xid 0 flags 0x03 blkref #0: rel 1663/16389/81952 forknum 2 blk 0 blkref #1: rel 1663/16389/81952 forknum 0 blk 2472
Heap2       | 0/21FFB5F8 | 0/21FFB638 | 6515 | 6515 | 840 | 61 tuples flags 0x20 blkref #0: rel 1663/16389/81952 forknum 0 blk 2473
Heap2       | 0/21FFB638 | 0/21FFCFC8 | 59   | 59   | 840 | cutoff xid 0 flags 0x03 blkref #0: rel 1663/16389/81952 forknum 2 blk 0 blkref #1: rel 1663/16389/81952 forknum 0 blk 2473
Heap2       | 0/21FFCFC8 | 0/21FFD008 | 6515 | 6515 | 840 | 61 tuples flags 0x20 blkref #0: rel 1663/16389/81952 forknum 0 blk 2474
Heap2       | 0/21FFD008 | 0/21FFE998 | 59   | 59   | 840 | cutoff xid 0 flags 0x03 blkref #0: rel 1663/16389/81952 forknum 2 blk 0 blkref #1: rel 1663/16389/81952 forknum 0 blk 2474
Heap2       | 0/21FFE998 | 0/21FFE9D8 | 2699 | 2699 | 840 | 25 tuples flags 0x22 blkref #0: rel 1663/16389/81952 forknum 0 blk 2475
Heap2       | 0/21FFE9D8 | 0/21FFF468 | 59   | 59   | 840 | cutoff xid 0 flags 0x03 blkref #0: rel 1663/16389/81952 forknum 2 blk 0 blkref #1: rel 1663/16389/81952 forknum 0 blk 2475
```

### Step 5: Run pgmoneta-walfilter

Execute the filtering command:

```bash
pgmoneta-walfilter ~/filter_config.yaml
```

### Step 6: Verify the filtered WAL file

Inspect the filtered WAL file to confirm the specified XIDs have been removed:

```bash
➜  ~ pgmoneta-walinfo ~/demo/target/000000010000000000000021
XLOG        | 0/20000120 | 0/21000028 | 49   | 8185 | 0   | FPI_FOR_HINT  blkref #0: rel 1663/16389/2610 forknum 0 blk 2 (FPW); hole: offset: 296, length: 56, compression saved: 0, method: unknown
Heap2       | 0/21000028 | 0/21002040 | 58   | 58   | 0   | snapshot_conflict_horizon_id: 835, is_catalog_rel: F, nplans: 0, nredirected: 0, ndead: 2, nunused: 0, dead: [67, 68] blkref #0: rel 1663/16389/2610 forknum 0 blk 2
XLOG        | 0/21002040 | 0/21002080 | 49   | 3897 | 0   | FPI_FOR_HINT  blkref #0: rel 1663/16389/1259 forknum 0 blk 0 (FPW); hole: offset: 232, length: 4344, compression saved: 0, method: unknown
XLOG        | 0/21002080 | 0/21002FC0 | 49   | 7233 | 0   | FPI_FOR_HINT  blkref #0: rel 1663/16389/1259 forknum 0 blk 1 (FPW); hole: offset: 264, length: 1008, compression saved: 0, method: unknown
XLOG        | 0/21002FC0 | 0/21004C20 | 49   | 7461 | 0   | FPI_FOR_HINT  blkref #0: rel 1663/16389/1259 forknum 0 blk 5 (FPW); hole: offset: 236, length: 780, compression saved: 0, method: unknown
Heap2       | 0/21004C20 | 0/21006960 | 58   | 58   | 0   | snapshot_conflict_horizon_id: 835, is_catalog_rel: F, nplans: 0, nredirected: 0, ndead: 2, nunused: 0, dead: [49, 52] blkref #0: rel 1663/16389/1259 forknum 0 blk 5
Standby     | 0/21006960 | 0/210069A0 | 50   | 50   | 0   | RUNNING_XACTS next_xid 836 latest_completed_xid 835 oldest_running_xid 836
XLOG        | 0/210069A0 | 0/210069D8 | 30   | 30   | 0   | 
Standby     | 0/210069D8 | 0/210069F8 | 50   | 50   | 0   | RUNNING_XACTS next_xid 836 latest_completed_xid 835 oldest_running_xid 836
XLOG        | 0/210069F8 | 0/21006A30 | 114  | 114  | 0   | CHECKPOINT_ONLINE redo 0/210069D8; tli 1; prev tli 1; fpw true; wal_level replica; xid 0:836; oid 81940; multi 1; offset 0; oldest xid 731 in DB 1; oldest multi 1 in DB 1; oldest/newest commit timestamp xid: 0/0; oldest running xid 836 online
Standby     | 0/21006A30 | 0/21006AA8 | 50   | 50   | 0   | RUNNING_XACTS next_xid 836 latest_completed_xid 835 oldest_running_xid 836
XLOG        | 0/21006AA8 | 0/21006AE0 | 114  | 114  | 0   | CHECKPOINT_SHUTDOWN redo 0/21006AE0; tli 1; prev tli 1; fpw true; wal_level replica; xid 0:836; oid 81940; multi 1; offset 0; oldest xid 731 in DB 1; oldest multi 1 in DB 1; oldest/newest commit timestamp xid: 0/0; oldest running xid 0 shutdown
Standby     | 0/21006AE0 | 0/21006B58 | 50   | 50   | 0   | RUNNING_XACTS next_xid 836 latest_completed_xid 835 oldest_running_xid 836
XLOG        | 0/21006B58 | 0/21006B90 | 49   | 8237 | 0   | FPI_FOR_HINT  blkref #0: rel 1663/16389/2610 forknum 0 blk 0 (FPW); hole: offset: 268, length: 4, compression saved: 0, method: unknown
Heap2       | 0/21006B90 | 0/21008BD8 | 58   | 58   | 0   | snapshot_conflict_horizon_id: 835, is_catalog_rel: F, nplans: 0, nredirected: 0, ndead: 2, nunused: 0, dead: [60, 61] blkref #0: rel 1663/16389/2610 forknum 0 blk 0
XLOG        | 0/21008BD8 | 0/21008C18 | 49   | 7409 | 0   | FPI_FOR_HINT  blkref #0: rel 1663/16389/2610 forknum 0 blk 3 (FPW); hole: offset: 248, length: 832, compression saved: 0, method: unknown
Standby     | 0/21008C18 | 0/2100A928 | 50   | 50   | 0   | RUNNING_XACTS next_xid 836 latest_completed_xid 835 oldest_running_xid 836
XLOG        | 0/2100A928 | 0/2100A960 | 30   | 30   | 0   | 
Standby     | 0/2100A960 | 0/2100A980 | 50   | 50   | 0   | RUNNING_XACTS next_xid 836 latest_completed_xid 835 oldest_running_xid 836
XLOG        | 0/2100A980 | 0/2100A9B8 | 114  | 114  | 0   | CHECKPOINT_ONLINE redo 0/2100A960; tli 1; prev tli 1; fpw true; wal_level replica; xid 0:836; oid 81940; multi 1; offset 0; oldest xid 731 in DB 1; oldest multi 1 in DB 1; oldest/newest commit timestamp xid: 0/0; oldest running xid 836 online
Standby     | 0/2100A9B8 | 0/2100AA30 | 50   | 50   | 0   | RUNNING_XACTS next_xid 836 latest_completed_xid 835 oldest_running_xid 836
XLOG        | 0/2100AA30 | 0/2100AA68 | 114  | 114  | 0   | CHECKPOINT_SHUTDOWN redo 0/2100AA68; tli 1; prev tli 1; fpw true; wal_level replica; xid 0:836; oid 81940; multi 1; offset 0; oldest xid 731 in DB 1; oldest multi 1 in DB 1; oldest/newest commit timestamp xid: 0/0; oldest running xid 0 shutdown
Standby     | 0/2100AA68 | 0/2100AAE0 | 50   | 50   | 0   | RUNNING_XACTS next_xid 836 latest_completed_xid 835 oldest_running_xid 836
XLOG        | 0/2100AAE0 | 0/2100AB18 | 30   | 30   | 0   | NEXTOID 90132
Standby     | 0/2100AB18 | 0/2100AB38 | 42   | 42   | 836 | LOCK xid 836 db 16389 rel 1450416 
Storage     | 0/2100AB38 | 0/2100AB68 | 42   | 42   | 836 | base/16389/81940
Heap        | 0/2100AB68 | 0/2100AB98 | 54   | 5814 | 836 | INSERT off 54 flags 0x00 blkref #0: rel 1663/16389/1247 forknum 0 blk 14 (FPW); hole: offset: 240, length: 2432, compression saved: 0, method: unknown
Btree       | 0/2100AB98 | 0/2100C268 | 53   | 5893 | 836 | INSERT_LEAF  off: 290 blkref #0: rel 1663/16389/2703 forknum 0 blk 2 (FPW); hole: offset: 1184, length: 2352, compression saved: 0, method: unknown
Btree       | 0/2100C268 | 0/2100D970 | 53   | 6529 | 836 | INSERT_LEAF  off: 110 blkref #0: rel 1663/16389/2704 forknum 0 blk 2 (FPW); hole: offset: 764, length: 1716, compression saved: 0, method: unknown
...
Heap2       | 0/2101F768 | 0/2101F7A8 | 85   | 85   | 836 | 1 tuples flags 0x02 blkref #0: rel 1663/16389/2608 forknum 0 blk 13
Btree       | 0/2101F7A8 | 0/2101F800 | 53   | 4677 | 836 | INSERT_LEAF  off: 99 blkref #0: rel 1663/16389/2673 forknum 0 blk 10 (FPW); hole: offset: 568, length: 3568, compression saved: 0, method: unknown
Btree       | 0/2101F800 | 0/21020A60 | 72   | 72   | 836 | INSERT_LEAF  off: 141 blkref #0: rel 1663/16389/2674 forknum 0 blk 8
Transaction | 0/21020A60 | 0/21020AA8 | 565  | 565  | 836 | COMMIT 2025-07-15 17:28:20.716006 EEST; inval msgs: catcache 80 catcache 79 catcache 80 catcache 79 catcache 55 catcache 54 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 snapshot 2608 relcache 81940
Standby     | 0/21020AA8 | 0/21020CE0 | 42   | 42   | 837 | LOCK xid 837 db 16389 rel 1461328 
Storage     | 0/21020CE0 | 0/21020D10 | 42   | 42   | 837 | base/16389/81943
Heap        | 0/21020D10 | 0/21020D40 | 211  | 211  | 837 | INSERT off 56 flags 0x00 blkref #0: rel 1663/16389/1247 forknum 0 blk 14
...
Btree       | 0/21021E18 | 0/21021E70 | 72   | 72   | 837 | INSERT_LEAF  off: 100 blkref #0: rel 1663/16389/2673 forknum 0 blk 10
Btree       | 0/21021E70 | 0/21021EB8 | 72   | 72   | 837 | INSERT_LEAF  off: 143 blkref #0: rel 1663/16389/2674 forknum 0 blk 8
Transaction | 0/21021EB8 | 0/21021F00 | 501  | 501  | 837 | COMMIT 2025-07-15 17:28:20.716661 EEST; inval msgs: catcache 80 catcache 79 catcache 80 catcache 79 catcache 55 catcache 54 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 snapshot 2608 relcache 81943
Standby     | 0/21021F00 | 0/21022110 | 42   | 42   | 838 | LOCK xid 838 db 16389 rel 1471216 
...
Btree       | 0/210231C0 | 0/21023208 | 64   | 64   | 838 | INSERT_LEAF  off: 391 blkref #0: rel 1663/16389/2659 forknum 0 blk 11
Heap2       | 0/21023208 | 0/21023248 | 85   | 85   | 838 | 1 tuples flags 0x02 blkref #0: rel 1663/16389/2608 forknum 0 blk 13
Btree       | 0/21023248 | 0/210232A0 | 72   | 72   | 838 | INSERT_LEAF  off: 101 blkref #0: rel 1663/16389/2673 forknum 0 blk 10
Btree       | 0/210232A0 | 0/210232E8 | 72   | 72   | 838 | INSERT_LEAF  off: 145 blkref #0: rel 1663/16389/2674 forknum 0 blk 8
Transaction | 0/210232E8 | 0/21023330 | 501  | 501  | 838 | COMMIT 2025-07-15 17:28:20.717062 EEST; inval msgs: catcache 80 catcache 79 catcache 80 catcache 79 catcache 55 catcache 54 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 catcache 7 catcache 6 snapshot 2608 relcache 81946
Standby     | 0/21023330 | 0/21023528 | 42   | 42   | 839 | LOCK xid 839 db 16389 rel 1481104 
Storage     | 0/21023528 | 0/21023558 | 42   | 42   | 839 | base/16389/81949
Heap        | 0/21023558 | 0/21023588 | 211  | 211  | 839 | INSERT off 60 flags 0x00 blkref #0: rel 1663/16389/1247 forknum 0 blk 14
```

### Expected Results

- The filtered WAL file should have all records with the specified XIDs marked as NOOP
- The CRC checksums are automatically recalculated for the modified WAL files
- The filtered WAL files are written to the target directory specified in the YAML configuration
