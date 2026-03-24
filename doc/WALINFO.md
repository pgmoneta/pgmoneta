# pgmoneta_walinfo configuration
The `pgmoneta_walinfo` configuration defines the info needed for `walinfo` to work.

The configuration is loaded from either the path specified by the `-c` flag or `/etc/pgmoneta/pgmoneta_walinfo.conf` if `-c` was not provided.

## Features

`pgmoneta-walinfo` provides multiple ways to analyze WAL files:

- **Command-line mode**: Raw text or JSON output with filtering options (`-r`, `-s`, `-e`, `-x`, `-l`, etc.)
- **Interactive mode**: Full-screen ncurses UI for browsing, searching, filtering, marking rows, and exporting walfilter YAML
- **Format support**: Plain, compressed (zstd, gz, lz4, bz2), encrypted (aes), and combined compression+encryption
- **OID translation**: Convert OIDs to human-readable object names (`-t` with `-m` or `-u`)
- **Summary statistics**: Analyze WAL record distribution by resource manager (`-S`)

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

## Server section

| Property | Default | Unit | Required | Description |
| :------- | :------ | :--- | :------- | :---------- |
| host | | String | Yes | The address of the PostgreSQL instance |
| port | | Int | Yes | The port of the PostgreSQL instance |
| user | | String | Yes | The replication user name |