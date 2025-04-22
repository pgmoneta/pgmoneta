
\newpage
# WAL Reader

## Overview

This document provides an overview of the `wal_reader` tool, with a focus on the `parse_wal_file` function, which serves as the main entry point for parsing Write-Ahead Log (WAL) files. Currently, the function only parses the given WAL file and prints the description of each record. In the future, it will be integrated with other parts of the code.


## pgmoneta-walinfo

`pgmoneta-walinfo` is a command line utility designed to read and display information about PostgreSQL Write-Ahead Log (WAL) files. The tool provides output in either raw or JSON format, making it easy to analyze WAL files for debugging, auditing, or general information purposes.

In addition to standard WAL files, `pgmoneta-walinfo` also supports encrypted (**aes**) and compressed WAL files in the following formats: **zstd**, **gz**, **lz4**, and **bz2**.

#### Usage

```bash
pgmoneta-walinfo 0.17.0
  Command line utility to read and display Write-Ahead Log (WAL) files

Usage:
  pgmoneta-walinfo <file>

Options:
  -c,   --config      Set the path to the pgmoneta_walinfo.conf file
  -u,   --users       Set the path to the pgmoneta_users.conf file
  -RT, --tablespaces  Filter on tablspaces
  -RD, --databases    Filter on databases
  -RT, --relations    Filter on relations
  -R,   --filter      Combination of -RT, -RD, -RR
  -o,   --output      Output file
  -F,   --format      Output format (raw, json)
  -L,   --logfile     Set the log file
  -q,   --quiet       No output only result
        --color       Use colors (on, off)
  -r,   --rmgr        Filter on a resource manager
  -s,   --start       Filter on a start LSN
  -e,   --end         Filter on an end LSN
  -x,   --xid         Filter on an XID
  -l,   --limit       Limit number of outputs
  -v,   --verbose     Output result
  -V,   --version     Display version information
  -m,   --mapping     Provide mappings file for OID translation
  -t,   --translate   Translate OIDs to object names in XLOG records
  -?,   --help        Display help
```

#### Raw Output Format

In `raw` format, the default, the output is structured as follows:

```
Resource Manager | Start LSN | End LSN | rec len | tot len | xid | description (data and backup)
```

- **Resource Manager**: The name of the resource manager handling the log record.
- **Start LSN**: The start Log Sequence Number (LSN).
- **End LSN**: The end Log Sequence Number (LSN).
- **rec len**: The length of the WAL record.
- **tot len**: The total length of the WAL record, including the header.
- **xid**: The transaction ID associated with the record.
- **description (data and backup)**: A detailed description of the operation, along with any related backup block information.

Each part of the output is color-coded:

- **Red**: Header information (resource manager, record length, transaction ID, etc.).
- **Green**: Description of the WAL record.
- **Blue**: Backup block references or additional data.

This format makes it easy to visually distinguish different parts of the WAL file for quick analysis.

#### Example

1. To view WAL file details in JSON format:

```bash
pgmoneta-walinfo -F json /path/to/walfile
```

2. To view WAL file details with OIDs in the records translated to object names:

Currently, `pgmoneta-walinfo` supports translating OIDs in two ways,
1. If the user provided `pgmoneta_user.conf` file, the tool will use it to get the needed credentials to connect to the database cluster and fetch the object names. directly from it.
```bash
pgmoneta-walinfo -c pgmoneta_walinfo.conf -t -u /path/to/pgmoneta_user.conf /path/to/walfile
```

2. If the user provided a mapping file that contains the OIDs and the corresponding object names, the tool will use it to translate the OIDs to the object names. This option helps if the user doesn't have the `pgmoneta_user.conf` file or doesn't want to use it.
```bash
pgmoneta-walinfo -c pgmoneta_walinfo.conf -t -m /path/to/mapping.json /path/to/walfile
```

User can get the needed info to create the file using these queries:
```sql
SELECT spcname, oid FROM pg_tablespace
SELECT datname, oid FROM pg_database
SELECT nspname || '.' || relname, c.oid FROM pg_class c JOIN pg_namespace n ON c.relnamespace = n.oid
```

In either ways, the user should use the `-t` flag to enable the translation. If user provided `pgmoneta_user.conf` file or the mapping file, the tool will do nothing if the `-t` flag is not provided. 

User can create the `pgmoneta_user.conf` file by following the instructions in the [`DEVELOPER.md`](https://github.com/pgmoneta/pgmoneta/blob/main/doc/DEVELOPERS.md)


After using this translation feature, the output will change XLOG records from something like this
`
Heap2 | 1/D8FFD1C0 | 1/D8FFEB50 | 59 | 59 | 958 | cutoff xid 0 flags 0x03 blkref #0: rel 1663/16399/16733 forknum 2 blk 0 blkref #1: rel 1663/16399/16733 forknum 0 blk 27597
`

to this
`
Heap2 | 1/D8FFD1C0 | 1/D8FFEB50 | 59 | 59 | 958 | cutoff xid 0 flags 0x03 blkref #0: rel pg_default/mydb/test_tbl forknum 2 blk 0 blkref #1: rel pg_default/mydb/16733 forknum 0 blk 27597
`

Example of `mappings.json` file:
```json
{
    "tablespaces": [
        {"name1": "oid1"},
        {"name2": "oid2"}
    ],
    "databases": [
        {"name1": "oid1"},
        {"name2": "oid2"}
    ],
    "relations": [
        {"name1": "oid1"},
        {"name2": "oid2"}
    ]
}

```

which is basically three sections, each section contains array key value pairs. The key is the object name and the value is the oid.

*Note 1: If both files (`pgmoneta_users.conf` & `mappings.json`) are provided, the tool will use the mapping file.*
*Note 2: If there is an OID that wasn't in the server/mapping (whichever the user choose at that time), the oid will be written as it is.*

e.g. `rel pg_default/mydb/16733` will be written as `rel pg_default/mydb/16733` if the OID `16733` wasn't in the server/mapping.


## High-Level API Overview

The following section provides a high-level overview of how users can interact with the functions and structures defined in the `walfile.h` file. These APIs allow you to read, write, and manage Write-Ahead Log (WAL) files.

### Struct `walfile`

The `walfile` struct represents the core structure used for interacting with WAL files in PostgreSQL. A WAL file stores a log of changes to the database and is used for crash recovery, replication, and other purposes. Each WAL file consists of pages (each 8192 bytes by default), containing records that capture database changes.

#### Fields:
- **magic_number**: Identifies the PostgreSQL version that created the WAL file. You can find more info on supported magic numbers [here](#supporting-various-wal-structures-in-postgresql-versions-13-to-17).
- **long_phd**: A pointer to the extended header (long header) found on the first page of the WAL file. This header contains additional metadata.
- **page_headers**: A deque of headers representing each page in the WAL file, excluding the first page.
- **records**: A deque of decoded WAL records. Each record represents a change made to the database and contains both metadata and the actual data to be applied during recovery or replication.

### Function Overview

The `walfile.h` file provides three key functions for interacting with WAL files: `pgmoneta_read_walfile`, `pgmoneta_write_walfile`, and `pgmoneta_destroy_walfile`. These functions allow users to read from, write to, and destroy WAL file objects, respectively.

#### `pgmoneta_read_walfile`

```c
int pgmoneta_read_walfile(int server, char* path, struct walfile** wf);
```

##### Description:
This function reads a WAL file from a specified path and populates a `walfile` structure with its contents, including the file's headers and records.

##### Parameters:
- **server**: The index of the Postgres server in Pgmoneta configuration.
- **path**: The file path to the WAL file that needs to be read.
- **wf**: A pointer to a pointer to a `walfile` structure that will be populated with the WAL file data.

##### Return:
- Returns `0` on success or `1` on failure.

##### Usage Example:
```c
struct walfile* wf = NULL;
int result = pgmoneta_read_walfile(0, "/path/to/walfile", &wf);
if (result == 0) {
    // Successfully read WAL file
}
```

#### `pgmoneta_write_walfile`

```c
int pgmoneta_write_walfile(struct walfile* wf, int server, char* path);
```

##### Description:
This function writes the contents of a `walfile` structure back to disk, saving it as a WAL file at the specified path.

##### Parameters:
- **wf**: The `walfile` structure containing the WAL data to be written.
- **server**: The index or ID of the server where the WAL file should be saved.
- **path**: The file path where the WAL file should be written.

##### Return:
- Returns `0` on success or `1` on failure.

##### Usage Example:
```c
int result = pgmoneta_write_walfile(wf, 0, "/path/to/output_walfile");
if (result == 0) {
    // Successfully wrote WAL file
}
```

#### `pgmoneta_destroy_walfile`

```c
void pgmoneta_destroy_walfile(struct walfile* wf);
```

##### Description:
This function frees the memory allocated for a `walfile` structure, including its headers and records.

##### Parameters:
- **wf**: The `walfile` structure to be destroyed.

##### Usage Example:
```c
struct walfile* wf = NULL;
int result = pgmoneta_read_walfile(0, "/path/to/walfile", &wf);
if (result == 0) {
    // Successfully read WAL file
}
pgmoneta_destroy_walfile(wf);
```

## Internal API Overview

### parse_wal_file

This function is responsible for reading and parsing a PostgreSQL Write-Ahead Log (WAL) file.

#### Parameters

- **path**: The file path to the WAL file that needs to be parsed.
- **server_info**: A pointer to a `server` structure containing information about the server.

#### Description

The `parse_wal_file` function opens the WAL file specified by the `path` parameter in binary mode and reads the WAL records. It processes these records, handling various cases such as records that cross page boundaries, while ensuring correct memory management throughout the process.

### Usage Example

```c
parse_wal_file("/path/to/wal/file", &my_server);
```


### WAL File Structure
The image illustrates the structure of a WAL (Write-Ahead Logging) file in PostgreSQL, focusing on how XLOG records are organized within WAL segments.

Source: [https://www.interdb.jp/pg/pgsql09/03.html](https://www.interdb.jp/pg/pgsql09/03.html)

![WAL File Structure](https://www.interdb.jp/pg/pgsql09/fig-9-07.png)

A WAL segment, by default, is a 16 MB file, divided into pages of 8192 bytes (8 KB) each. The first page contains a header defined by the XLogLongPageHeaderData structure, while all subsequent pages have headers described by the XLogPageHeaderData structure. XLOG records are written sequentially in each page, starting at the beginning and moving downward.

The figure highlights how the WAL ensures data consistency by sequentially writing XLOG records in pages, structured within larger 16 MB WAL segments.


## Resource Managers

In the context of the WAL reader, resource managers (rm) are responsible for handling different types of records found within a WAL file. Each record in the WAL file is associated with a specific resource manager, which determines how that record is processed.

### Resource Manager Definitions

Each resource manager is defined in the `rm_[name].h` header file and implemented in the corresponding `rm_[name].c` source file.

In the `rmgr.h` header file, the resource managers are declared as an enum, with each resource manager having a unique identifier.

### Resource Manager Functions

Each resource manager implements the `rm_desc` function, which provides a description of the record type associated with that resource manager. In the future, they will be extended to implement the `rm_redo` function to apply the changes to another server.


### Supporting Various WAL Structures in PostgreSQL Versions 13 to 17
The WAL structure has evolved across PostgreSQL versions 13 to 17, requiring different handling for each version. To accommodate these differences, we have implemented a wrapper-based approach, such as the factory pattern, to handle varying WAL structures.

Below are the commit hashes for the officially supported magic values in each PostgreSQL version:

1. PostgreSQL 13 - 0xD106: [https://github.com/postgres/postgres/commit/c6b92041d38512a4176ed76ad06f713d2e6c01a8](https://github.com/postgres/postgres/commit/c6b92041d38512a4176ed76ad06f713d2e6c01a8)
2. PostgreSQL 14 - 0xD10D: [https://github.com/postgres/postgres/commit/08aa89b326261b669648df97d4f2a6edba22d26a](https://github.com/postgres/postgres/commit/08aa89b326261b669648df97d4f2a6edba22d26a)
3. PostgreSQL 15 - 0xD110: [https://github.com/postgres/postgres/commit/8b1dccd37c71ed2ff016294d8f9053a32b02b19e](https://github.com/postgres/postgres/commit/8b1dccd37c71ed2ff016294d8f9053a32b02b19e)
4. PostgreSQL 16 - 0xD113: [https://github.com/postgres/postgres/commit/6af1793954e8c5e753af83c3edb37ed3267dd179](https://github.com/postgres/postgres/commit/6af1793954e8c5e753af83c3edb37ed3267dd179)
5. PostgreSQL 17 - 0xD116: [https://github.com/postgres/postgres/commit/402b586d0a9caae9412d25fcf1b91dae45375833](https://github.com/postgres/postgres/commit/402b586d0a9caae9412d25fcf1b91dae45375833)


`xl_end_of_recovery` is an example of how we handle different versions of structures with a wrapper struct and a factory pattern.

```c
struct xl_end_of_recovery_v16 {
    timestamp_tz end_time;
    timeline_id this_timeline_id;
    timeline_id prev_timeline_id;
};

struct xl_end_of_recovery_v17 {
    timestamp_tz end_time;
    timeline_id this_timeline_id;
    timeline_id prev_timeline_id;
    int wal_level;
};

struct xl_end_of_recovery {
    int pg_version;
    union {
        struct xl_end_of_recovery_v16 v16;
        struct xl_end_of_recovery_v17 v17;
    } data;
    void (*parse)(struct xl_end_of_recovery* wrapper, void* rec);
    char* (*format)(struct xl_end_of_recovery* wrapper, char* buf);
};

xl_end_of_recovery* create_xl_end_of_recovery(int pg_version) {
    xl_end_of_recovery* wrapper = malloc(sizeof(xl_end_of_recovery));
    wrapper->pg_version = pg_version;

    if (pg_version >= 17) {
        wrapper->parse = parse_v17;
        wrapper->format = format_v17;
    } else {
        wrapper->parse = parse_v16;
        wrapper->format = format_v16;
    }

    return wrapper;
}

void parse_v16(xl_end_of_recovery* wrapper, void* rec) {
    memcpy(&wrapper->data.v16, rec, sizeof(struct xl_end_of_recovery_v16));
}

void parse_v17(xl_end_of_recovery* wrapper, void* rec) {
    memcpy(&wrapper->data.v17, rec, sizeof(struct xl_end_of_recovery_v17));
}

char* format_v16(xl_end_of_recovery* wrapper, char* buf) {
    struct xl_end_of_recovery_v16* xlrec = &wrapper->data.v16;
    return pgmoneta_format_and_append(buf, "tli %u; prev tli %u; time %s",
                                      xlrec->this_timeline_id, xlrec->prev_timeline_id,
                                      pgmoneta_wal_timestamptz_to_str(xlrec->end_time));
}

char* format_v17(xl_end_of_recovery* wrapper, char* buf) {
    struct xl_end_of_recovery_v17* xlrec = &wrapper->data.v17;
    return pgmoneta_format_and_append(buf, "tli %u; prev tli %u; time %s; wal_level %d",
                                      xlrec->this_timeline_id, xlrec->prev_timeline_id,
                                      pgmoneta_wal_timestamptz_to_str(xlrec->end_time),
                                      xlrec->wal_level);
}
```


## WAL Change List

This section lists the changes in the WAL format between different versions of PostgreSQL.

### xl_clog_truncate

17

```c
struct xl_clog_truncate
{
   int64 pageno;              /**< The page number of the clog to truncate */
   transaction_id oldestXact;  /**< The oldest transaction ID to retain */
   oid oldestXactDb;        /**< The database ID of the oldest transaction */
};
```

16

```c
struct xl_clog_truncate
{
   int64 pageno;              /**< The page number of the clog to truncate */
   transaction_id oldestXact;  /**< The oldest transaction ID to retain */
   oid oldestXactDb;        /**< The database ID of the oldest transaction */
};
```

### xl_commit_ts_truncate

17:

```c
typedef struct xl_commit_ts_truncate
{
	int64		pageno;
	TransactionId oldestXid;
} xl_commit_ts_truncate;
```

16:

```c
typedef struct xl_commit_ts_truncate
{
	int			pageno;
	TransactionId oldestXid;
} xl_commit_ts_truncate;
```

### xl_heap_prune

17:

```c
typedef struct xl_heap_prune
{
	uint8		reason;
	uint8		flags;

	/*
	 * If XLHP_HAS_CONFLICT_HORIZON is set, the conflict horizon XID follows,
	 * unaligned
	 */
} xl_heap_prune;
#define SizeOfHeapPrune (offsetof(xl_heap_prune, flags) + sizeof(uint8))

```

16:

```c
typedef struct xl_heap_prune
{
	TransactionId snapshotConflictHorizon;
	uint16		nredirected;
	uint16		ndead;
	bool		isCatalogRel;	/* to handle recovery conflict during logical
								 * decoding on standby */
	/* OFFSET NUMBERS are in the block reference 0 */
} xl_heap_prune;
#define SizeOfHeapPrune (offsetof(xl_heap_prune, isCatalogRel) + sizeof(bool))

```

### xlhp_freeze_plan

Removed `xl_heap_freeze_page`

17:

```c
typedef struct xlhp_freeze_plan
{
	TransactionId xmax;
	uint16		t_infomask2;
	uint16		t_infomask;
	uint8		frzflags;

	/* Length of individual page offset numbers array for this plan */
	uint16		ntuples;
} xlhp_freeze_plan;
```

### spgxlogState
(Doesn’t need to be changed)

17:

```c
typedef struct spgxlogState
{
	TransactionId redirectXid;
	bool		isBuild;
} spgxlogState;
```

16:

```c
typedef struct spgxlogState
{
	TransactionId myXid;
	bool		isBuild;
} spgxlogState;
```

### xl_end_of_recovery

```c
typedef struct xl_end_of_recovery
{
	TimestampTz end_time;
	TimeLineID	ThisTimeLineID; /* new TLI */
	TimeLineID	PrevTimeLineID; /* previous TLI we forked off from */
	int			wal_level;
} xl_end_of_recovery;
```

16:

```c
typedef struct xl_end_of_recovery
{
	TimestampTz end_time;
	TimeLineID	ThisTimeLineID; /* new TLI */
	TimeLineID	PrevTimeLineID; /* previous TLI we forked off from */
} xl_end_of_recovery;
```

---

16 → 15

### gingxlogSplit
16: same for `gin_xlog_update_meta`

```c
typedef struct ginxlogSplit
{
	RelFileLocator locator;
	BlockNumber rrlink;			/* right link, or root's blocknumber if root
								 * split */
	BlockNumber leftChildBlkno; /* valid on a non-leaf split */
	BlockNumber rightChildBlkno;
	uint16		flags;			/* see below */
} ginxlogSplit;
```

15:

```c
typedef struct ginxlogSplit
{
	RelFileNode node;
	BlockNumber rrlink;			/* right link, or root's blocknumber if root
								 * split */
	BlockNumber leftChildBlkno; /* valid on a non-leaf split */
	BlockNumber rightChildBlkno;
	uint16		flags;			/* see below */
} ginxlogSplit;
```

### gistxlogDelete

16:

```c
typedef struct gistxlogDelete
{
	TransactionId snapshotConflictHorizon;
	uint16		ntodelete;		/* number of deleted offsets */
	bool		isCatalogRel;	/* to handle recovery conflict during logical
								 * decoding on standby */

	/* TODELETE OFFSET NUMBERS */
	OffsetNumber offsets[FLEXIBLE_ARRAY_MEMBER];
} gistxlogDelete;
#define SizeOfGistxlogDelete	offsetof(gistxlogDelete, offsets)
```

15:

```c
typedef struct gistxlogDelete
{
	TransactionId latestRemovedXid;
	uint16		ntodelete;		/* number of deleted offsets */

	/*
	 * In payload of blk 0 : todelete OffsetNumbers
	 */
} gistxlogDelete;
#define SizeOfGistxlogDelete	(offsetof(gistxlogDelete, ntodelete) + sizeof(uint16))
```

### gistxlogPageReuse

16:

```c
typedef struct gistxlogPageReuse
{
	RelFileLocator locator;
	BlockNumber block;
	FullTransactionId snapshotConflictHorizon;
	bool		isCatalogRel;	/* to handle recovery conflict during logical
								 * decoding on standby */
} gistxlogPageReuse;
#define SizeOfGistxlogPageReuse	(offsetof(gistxlogPageReuse, isCatalogRel) + sizeof(bool))
```

15:

```c
typedef struct gistxlogPageReuse
{
	RelFileNode node;
	BlockNumber block;
	FullTransactionId latestRemovedFullXid;
} gistxlogPageReuse;

#define SizeOfGistxlogPageReuse	(offsetof(gistxlogPageReuse, latestRemovedFullXid) + sizeof(FullTransactionId))
```

### xl_hash_vacuum_one_page

16:

```c
typedef struct xl_hash_vacuum_one_page
{
	TransactionId snapshotConflictHorizon;
	uint16		ntuples;
	bool		isCatalogRel;	/* to handle recovery conflict during logical
								 * decoding on standby */

	/* TARGET OFFSET NUMBERS */
	OffsetNumber offsets[FLEXIBLE_ARRAY_MEMBER];
} xl_hash_vacuum_one_page;
#define SizeOfHashVacuumOnePage offsetof(xl_hash_vacuum_one_page, offsets)
```

15:

```c
typedef struct xl_hash_vacuum_one_page
{
	TransactionId latestRemovedXid;
	int			ntuples;

	/* TARGET OFFSET NUMBERS FOLLOW AT THE END */
} xl_hash_vacuum_one_page;
#define SizeOfHashVacuumOnePage \
	(offsetof(xl_hash_vacuum_one_page, ntuples) + sizeof(int))
```

### xl_heap_prune

16:

```c
typedef struct xl_heap_prune
{
	TransactionId snapshotConflictHorizon;
	uint16		nredirected;
	uint16		ndead;
	bool		isCatalogRel;	/* to handle recovery conflict during logical
								 * decoding on standby */
	/* OFFSET NUMBERS are in the block reference 0 */
} xl_heap_prune;
#define SizeOfHeapPrune (offsetof(xl_heap_prune, isCatalogRel) + sizeof(bool))
```

15:

```c
typedef struct xl_heap_prune
{
	TransactionId latestRemovedXid;
	uint16		nredirected;
	uint16		ndead;
	/* OFFSET NUMBERS are in the block reference 0 */
} xl_heap_prune;
#define SizeOfHeapPrune (offsetof(xl_heap_prune, ndead) + sizeof(uint16))
```

### xl_heap_freeze_plan

16:

```c
typedef struct xl_heap_freeze_plan
{
	TransactionId xmax;
	uint16		t_infomask2;
	uint16		t_infomask;
	uint8		frzflags;

	/* Length of individual page offset numbers array for this plan */
	uint16		ntuples;
} xl_heap_freeze_plan;
```

15:

```c
typedef struct xl_heap_freeze_tuple
{
	TransactionId xmax;
	OffsetNumber offset;
	uint16		t_infomask2;
	uint16		t_infomask;
	uint8		frzflags;
} xl_heap_freeze_tuple;
```

### xl_heap_freeze_page

16:

```c
typedef struct xl_heap_freeze_page
{
	TransactionId snapshotConflictHorizon;
	uint16		nplans;
	bool		isCatalogRel;	/* to handle recovery conflict during logical
								 * decoding on standby */

	/*
	 * In payload of blk 0 : FREEZE PLANS and OFFSET NUMBER ARRAY
	 */
} xl_heap_freeze_page;
```

15:

```c
typedef struct xl_heap_freeze_page
{
	TransactionId cutoff_xid;
	uint16		ntuples;
} xl_heap_freeze_page;
```

### xl_btree_reuse_page

16:

```c
typedef struct xl_btree_reuse_page
{
	RelFileLocator locator;
	BlockNumber block;
	FullTransactionId snapshotConflictHorizon;
	bool		isCatalogRel;	/* to handle recovery conflict during logical
								 * decoding on standby */
} xl_btree_reuse_page;
```

15:

```c
typedef struct xl_btree_reuse_page
{
	RelFileNode node;
	BlockNumber block;
	FullTransactionId latestRemovedFullXid;
} xl_btree_reuse_page;
```

### xl_btree_delete

16:

```c
typedef struct xl_btree_delete
{
	TransactionId snapshotConflictHorizon;
	uint16		ndeleted;
	uint16		nupdated;
	bool		isCatalogRel;	/* to handle recovery conflict during logical
								 * decoding on standby */

	/*----
	 * In payload of blk 0 :
	 * - DELETED TARGET OFFSET NUMBERS
	 * - UPDATED TARGET OFFSET NUMBERS
	 * - UPDATED TUPLES METADATA (xl_btree_update) ARRAY
	 *----
	 */
} xl_btree_delete;
```

15:

```c
typedef struct xl_btree_delete
{
	TransactionId latestRemovedXid;
	uint16		ndeleted;
	uint16		nupdated;

	/* DELETED TARGET OFFSET NUMBERS FOLLOW */
	/* UPDATED TARGET OFFSET NUMBERS FOLLOW */
	/* UPDATED TUPLES METADATA (xl_btree_update) ARRAY FOLLOWS */
} xl_btree_delete;
```

### spgxlogVacuumRedirect

16:

```c
typedef struct spgxlogVacuumRedirect
{
	uint16		nToPlaceholder; /* number of redirects to make placeholders */
	OffsetNumber firstPlaceholder;	/* first placeholder tuple to remove */
	TransactionId snapshotConflictHorizon;	/* newest XID of removed redirects */
	bool		isCatalogRel;	/* to handle recovery conflict during logical
								 * decoding on standby */

	/* offsets of redirect tuples to make placeholders follow */
	OffsetNumber offsets[FLEXIBLE_ARRAY_MEMBER];
} spgxlogVacuumRedirect;
```

15:

```c
typedef struct spgxlogVacuumRedirect
{
	uint16		nToPlaceholder; /* number of redirects to make placeholders */
	OffsetNumber firstPlaceholder;	/* first placeholder tuple to remove */
	TransactionId newestRedirectXid;	/* newest XID of removed redirects */

	/* offsets of redirect tuples to make placeholders follow */
	OffsetNumber offsets[FLEXIBLE_ARRAY_MEMBER];
} spgxlogVacuumRedirect;
```

---

15 → 14

### xl_xact_prepare

15:

```c
ctypedef struct xl_xact_prepare
{
	uint32		magic;			/* format identifier */
	uint32		total_len;		/* actual file length */
	TransactionId xid;			/* original transaction XID */
	Oid			database;		/* OID of database it was in */
	TimestampTz prepared_at;	/* time of preparation */
	Oid			owner;			/* user running the transaction */
	int32		nsubxacts;		/* number of following subxact XIDs */
	int32		ncommitrels;	/* number of delete-on-commit rels */
	int32		nabortrels;		/* number of delete-on-abort rels */
	int32		ncommitstats;	/* number of stats to drop on commit */
	int32		nabortstats;	/* number of stats to drop on abort */
	int32		ninvalmsgs;		/* number of cache invalidation messages */
	bool		initfileinval;	/* does relcache init file need invalidation? */
	uint16		gidlen;			/* length of the GID - GID follows the header */
	XLogRecPtr	origin_lsn;		/* lsn of this record at origin node */
	TimestampTz origin_timestamp;	/* time of prepare at origin node */
} xl_xact_prepare;
```

14:

```c
typedef struct xl_xact_prepare
{
	uint32		magic;			/* format identifier */
	uint32		total_len;		/* actual file length */
	TransactionId xid;			/* original transaction XID */
	Oid			database;		/* OID of database it was in */
	TimestampTz prepared_at;	/* time of preparation */
	Oid			owner;			/* user running the transaction */
	int32		nsubxacts;		/* number of following subxact XIDs */
	int32		ncommitrels;	/* number of delete-on-commit rels */
	int32		nabortrels;		/* number of delete-on-abort rels */
	int32		ninvalmsgs;		/* number of cache invalidation messages */
	bool		initfileinval;	/* does relcache init file need invalidation? */
	uint16		gidlen;			/* length of the GID - GID follows the header */
	XLogRecPtr	origin_lsn;		/* lsn of this record at origin node */
	TimestampTz origin_timestamp;	/* time of prepare at origin node */
} xl_xact_prepare;
```

### xl_xact_parsed_commit

15:

```c
typedef struct xl_xact_parsed_commit
{
	TimestampTz xact_time;
	uint32		xinfo;

	Oid			dbId;			/* MyDatabaseId */
	Oid			tsId;			/* MyDatabaseTableSpace */

	int			nsubxacts;
	TransactionId *subxacts;

	int			nrels;
	RelFileNode *xnodes;

	int			nstats;
	xl_xact_stats_item *stats;

	int			nmsgs;
	SharedInvalidationMessage *msgs;

	TransactionId twophase_xid; /* only for 2PC */
	char		twophase_gid[GIDSIZE];	/* only for 2PC */
	int			nabortrels;		/* only for 2PC */
	RelFileNode *abortnodes;	/* only for 2PC */
	int			nabortstats;		/* only for 2PC */
	xl_xact_stats_item *abortstats; /* only for 2PC */

	XLogRecPtr	origin_lsn;
	TimestampTz origin_timestamp;
} xl_xact_parsed_commit;
```

14:

```c
typedef struct xl_xact_parsed_commit
{
	TimestampTz xact_time;
	uint32		xinfo;

	Oid			dbId;			/* MyDatabaseId */
	Oid			tsId;			/* MyDatabaseTableSpace */

	int			nsubxacts;
	TransactionId *subxacts;

	int			nrels;
	RelFileNode *xnodes;

	int			nmsgs;
	SharedInvalidationMessage *msgs;

	TransactionId twophase_xid; /* only for 2PC */
	char		twophase_gid[GIDSIZE];	/* only for 2PC */
	int			nabortrels;		/* only for 2PC */
	RelFileNode *abortnodes;	/* only for 2PC */

	XLogRecPtr	origin_lsn;
	TimestampTz origin_timestamp;
} xl_xact_parsed_commit;
```

### xl_xact_parsed_abort

15:

```c
typedef struct xl_xact_parsed_abort
{
	TimestampTz xact_time;
	uint32		xinfo;

	Oid			dbId;			/* MyDatabaseId */
	Oid			tsId;			/* MyDatabaseTableSpace */

	int			nsubxacts;
	TransactionId *subxacts;

	int			nrels;
	RelFileNode *xnodes;

	int			nstats;
	xl_xact_stats_item *stats;

	TransactionId twophase_xid; /* only for 2PC */
	char		twophase_gid[GIDSIZE];	/* only for 2PC */

	XLogRecPtr	origin_lsn;
	TimestampTz origin_timestamp;
} xl_xact_parsed_abort;
```

14:

```c
typedef struct xl_xact_parsed_abort
{
	TimestampTz xact_time;
	uint32		xinfo;

	Oid			dbId;			/* MyDatabaseId */
	Oid			tsId;			/* MyDatabaseTableSpace */

	int			nsubxacts;
	TransactionId *subxacts;

	int			nrels;
	RelFileNode *xnodes;

	TransactionId twophase_xid; /* only for 2PC */
	char		twophase_gid[GIDSIZE];	/* only for 2PC */

	XLogRecPtr	origin_lsn;
	TimestampTz origin_timestamp;
} xl_xact_parsed_abort;
```

### xlogrecord.h flags

15:

```c
#define BKPIMAGE_APPLY			0x02	/* page image should be restored
										 * during replay */
/* compression methods supported */
#define BKPIMAGE_COMPRESS_PGLZ	0x04
#define BKPIMAGE_COMPRESS_LZ4	0x08
#define BKPIMAGE_COMPRESS_ZSTD	0x10

#define	BKPIMAGE_COMPRESSED(info) \
	((info & (BKPIMAGE_COMPRESS_PGLZ | BKPIMAGE_COMPRESS_LZ4 | \
			  BKPIMAGE_COMPRESS_ZSTD)) != 0)

```

14:

```c
#define BKPIMAGE_IS_COMPRESSED		0x02	/* page image is compressed */
#define BKPIMAGE_APPLY		0x04	/* page image should be restored during
									 * replay */
```

---

14 → 13

### xl_heap_prune

14:

```c
typedef struct xl_heap_prune
{
	TransactionId latestRemovedXid;
	uint16		nredirected;
	uint16		ndead;
	/* OFFSET NUMBERS are in the block reference 0 */
} xl_heap_prune;
```

13:

```c
typedef struct xl_heap_clean
{
	TransactionId latestRemovedXid;
	uint16		nredirected;
	uint16		ndead;
	/* OFFSET NUMBERS are in the block reference 0 */
} xl_heap_clean;
```

### xl_heap_vacuum

14:

```c
typedef struct xl_heap_vacuum
{
	uint16		nunused;
	/* OFFSET NUMBERS are in the block reference 0 */
} xl_heap_vacuum;
```

13:

```c
typedef struct xl_heap_cleanup_info
{
	RelFileNode node;
	TransactionId latestRemovedXid;
} xl_heap_cleanup_info;
```

### xl_btree_metadata

14:

```c
typedef struct xl_btree_metadata
{
	uint32		version;
	BlockNumber root;
	uint32		level;
	BlockNumber fastroot;
	uint32		fastlevel;
	uint32		last_cleanup_num_delpages;
	bool		allequalimage;
} xl_btree_metadata;
```

13:

```c
typedef struct xl_btree_metadata
{
	uint32		version;
	BlockNumber root;
	uint32		level;
	BlockNumber fastroot;
	uint32		fastlevel;
	TransactionId oldest_btpo_xact;
	float8		last_cleanup_num_heap_tuples;
	bool		allequalimage;
} xl_btree_metadata;
```

### xl_btree_reuse_page

14:

```c
typedef struct xl_btree_reuse_page
{
	RelFileNode node;
	BlockNumber block;
	FullTransactionId latestRemovedFullXid;
} xl_btree_reuse_page;
```

13:

```c
typedef struct xl_btree_reuse_page
{
	RelFileNode node;
	BlockNumber block;
	TransactionId latestRemovedXid;
} xl_btree_reuse_page;
```

### xl_btree_delete

14:

```c
typedef struct xl_btree_delete
{
	TransactionId latestRemovedXid;
	uint16		ndeleted;
	uint16		nupdated;

	/* DELETED TARGET OFFSET NUMBERS FOLLOW */
	/* UPDATED TARGET OFFSET NUMBERS FOLLOW */
	/* UPDATED TUPLES METADATA (xl_btree_update) ARRAY FOLLOWS */
} xl_btree_delete;
```

13:

```c
typedef struct xl_btree_delete
{
	TransactionId latestRemovedXid;
	uint32		ndeleted;

	/* DELETED TARGET OFFSET NUMBERS FOLLOW */
} xl_btree_delete;
```

### xl_btree_unlink_page

14:

```c
typedef struct xl_btree_unlink_page
{
	BlockNumber leftsib;		/* target block's left sibling, if any */
	BlockNumber rightsib;		/* target block's right sibling */
	uint32		level;			/* target block's level */
	FullTransactionId safexid;	/* target block's BTPageSetDeleted() XID */

	/*
	 * Information needed to recreate a half-dead leaf page with correct
	 * topparent link.  The fields are only used when deletion operation's
	 * target page is an internal page.  REDO routine creates half-dead page
	 * from scratch to keep things simple (this is the same convenient
	 * approach used for the target page itself).
	 */
	BlockNumber leafleftsib;
	BlockNumber leafrightsib;
	BlockNumber leaftopparent;	/* next child down in the subtree */

	/* xl_btree_metadata FOLLOWS IF XLOG_BTREE_UNLINK_PAGE_META */
} xl_btree_unlink_page;
```

13:

```c
typedef struct xl_btree_unlink_page
{
	BlockNumber leftsib;		/* target block's left sibling, if any */
	BlockNumber rightsib;		/* target block's right sibling */

	/*
	 * Information needed to recreate the leaf page, when target is an
	 * internal page.
	 */
	BlockNumber leafleftsib;
	BlockNumber leafrightsib;
	BlockNumber topparent;		/* next child down in the branch */

	TransactionId btpo_xact;	/* value of btpo.xact for use in recovery */
	/* xl_btree_metadata FOLLOWS IF XLOG_BTREE_UNLINK_PAGE_META */
} xl_btree_unlink_page;
```

## Additional Information
For more details on the internal workings and additional helper functions used in `parse_wal_file`, refer to the source code in `wal_reader.c`.
