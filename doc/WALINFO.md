# pgmoneta_walinfo configuration
The `pgmoneta_walinfo` configuration defines the info needed for `walinfo` to work.

The configuration is loaded from either the path specified by the `-c` flag or `/etc/pgmoneta/pgmoneta_walinfo.conf` if `-c` was not provided.

## Features

`pgmoneta-walinfo` provides multiple ways to analyze WAL files:

- **Command-line mode**: Raw text or JSON output with filtering options (`-r`, `-s`, `-e`, `-x`, `-l`, etc.)
- **Interactive mode**: Full-screen ncurses UI for browsing, searching, filtering, marking rows, and exporting walfilter YAML
- **Format support**: Plain, compressed (zstd, gz, lz4, bz2), encrypted (aes), and combined compression+encryption
- **OID translation**: Convert OIDs to human-readable object names (`-t` with `-m` or `-u`). Use `-m /path/to/mapping.json` to translate using a local JSON mapping file with `tablespaces`, `databases`, and `relations` sections.
- **Summary statistics**: Analyze WAL record distribution by resource manager (`-S`)

## OID translation

`pgmoneta-walinfo` can translate OIDs to object names using either:

- `-u /path/to/pgmoneta_users.conf` to fetch object names from a live PostgreSQL server
- `-m /path/to/mapping.json` to load object name mappings from a local JSON file

Example `mapping.json` format:

```json
{
    "tablespaces": [
        {"pg_default": "1663"}
    ],
    "databases": [
        {"postgres": "16384"}
    ],
    "relations": [
        {"public.test_table": "16734"}
    ]
}
```

If both a mapping file and a user credentials file are provided, the mapping file takes precedence.

### Creating the mapping file

The mapping file can be created manually or generated from a PostgreSQL database using SQL queries. Each section (`tablespaces`, `databases`, `relations`) is an array of objects where the key is the object name and the value is the OID as a string.

#### Generating from PostgreSQL

Connect to your PostgreSQL database and run these queries to generate the JSON structure:

**Tablespaces:**
```sql
SELECT '{' || string_agg('"' || spcname || '": "' || oid || '"', ', ') || '}' FROM pg_tablespace;
```

**Databases:**
```sql
SELECT '{' || string_agg('"' || datname || '": "' || oid || '"', ', ') || '}' FROM pg_database WHERE datname NOT IN ('template0', 'template1');
```

**Relations:**
```sql
SELECT '{' || string_agg('"' || nspname || '.' || relname || '": "' || c.oid || '"', ', ') || '}'
FROM pg_class c
JOIN pg_namespace n ON c.relnamespace = n.oid
WHERE c.relkind IN ('r', 'p', 'v', 'm', 'f') AND n.nspname NOT IN ('pg_catalog', 'information_schema');
```

Combine the results into a full JSON file:

```json
{
    "tablespaces": {
        /* paste tablespaces query result */ 
    },
    "databases": {
        /* paste databases query result */ 
    },
    "relations": {
        /* paste relations query result */ 
    }
}
```

#### Manual creation

You can also create the file manually by looking up OIDs in system catalogs or using tools like `pg_dump` output. Ensure OIDs are strings and object names are fully qualified (schema.table for relations).

## Interactive mode

Launch the interactive viewer with the `-I` flag:

```bash
pgmoneta-walinfo -I <file>
pgmoneta-walinfo -I <directory>
```

**Capabilities**

- **File browser**: Choose WAL files from a directory
- **Record table**: RMGR, LSNs, lengths, XID, description; **t** / **b** toggles text vs binary (hex) view
- **Detail view**: **Enter** opens the current record
- **Search** (**s**): Find records by RMGR, LSN fields, XID, or description (KMP-based). **n** / **p** step results; **Esc** clears search (not filters)
- **Filter** (**f**): Restrict the visible rows by criteria; **u** clears all filters and reloads the full file. Active rules are summarized in the **header**; the **status** line shows *showing N of M records* when filtered
- **Marks & export** (**m** / **g**): Mark rows, then **g** writes a **pgmoneta-walfilter** YAML from marked XIDs

### Filter dialog semantics

- **AND across fields**: Each non-empty field you set must match.
- **OR within a field**: For **RMGR**, **XID**, and **Relation**, use **comma-separated** values; a row matches if it matches **any** token.
- **Start LSN** / **End LSN**: Together they bound the visible span: record start **>=** filter start (if set) and record end **<=** filter end (if set). Only one bound may be set.
- Reopening **f** **pre-fills** the dialog with current rules. **Ctrl+U** inside the dialog clears all fields.

### Keyboard reference (interactive)

| Key | Action |
|-----|--------|
| **↑** / **↓** | Previous / next record (may move to adjacent WAL files at boundaries) |
| **PgUp** / **PgDn** | Page scroll |
| **Home** / **End** | First / last record |
| **Enter** | Detail view for current record |
| **t** / **b** | Text mode / Binary (hex) mode |
| **s** | Open **search** dialog |
| **n** / **p** | Next / previous search match (when search is active) |
| **f** | Open **filter** dialog |
| **u** | **Clear all filters** and reload full record list |
| **m** | Mark / unmark row (for YAML export) |
| **g** | Generate **walfilter** YAML from marked XIDs |
| **v** | Verify (viewer integration) |
| **l** | Load another WAL file / browse |
| **?** | Help overlay |
| **Esc** | Clear **search** state |
| **q** | Quit |

For the full list, see **doc/man/pgmoneta-walinfo.1.rst** (INTERACTIVE MODE) or the built-in **?** help.

## [pgmoneta_walinfo]

| Property | Default | Unit | Required | Description |
| :------- | :------ | :--- | :------- | :---------- |
| log_type | console | String | No | The logging type (console, file, syslog) |
| log_level | info | String | No | The logging level, any of the (case insensitive) strings `FATAL`, `ERROR`, `WARN`, `INFO` and `DEBUG` (that can be more specific as `DEBUG1` thru `DEBUG5`). Debug level greater than 5 will be set to `DEBUG5`. Not recognized values will make the log_level be `INFO` |
| log_path | pgmoneta.log | String | No | The log file location. Can be a strftime(3) compatible string. |
| encryption | aes-256-gcm | String | No | The encryption mode for encrypt wal and data<br/> `none`: No encryption <br/> `aes \| aes-256 \| aes-256-gcm`: AES GCM (Galois/Counter Mode) mode with 256 bit key length (Recommended)<br/> `aes-192 \| aes-192-gcm`: AES GCM mode with 192 bit key length<br/> `aes-128 \| aes-128-gcm`: AES GCM mode with 128 bit key length |

## Server section

| Property | Default | Unit | Required | Description |
| :------- | :------ | :--- | :------- | :---------- |
| host | | String | Yes | The address of the PostgreSQL instance |
| port | | Int | Yes | The port of the PostgreSQL instance |
| user | | String | Yes | The replication user name |