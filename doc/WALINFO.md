#pgmoneta_walinfo configuration
The `pgmoneta_walinfo` configuration defines the info needed for `walinfo` to work.

The configuration is loaded from either the path specified by the `-c` flag or `/etc/pgmoneta/pgmoneta_walinfo.conf` if -c wasn't provided.

## Features

`pgmoneta-walinfo` provides multiple ways to analyze WAL files:

- **Command-Line Mode**: Raw text or JSON output with filtering options
- **Interactive Mode**: Full-featured ncurses UI for browsing and searching WAL records
- **Format Support**: Plain, compressed (zstd, gz, lz4, bz2), encrypted (aes), and combined compression+encryption
- **OID Translation**: Convert OIDs to human-readable object names
- **Summary Statistics**: Analyze WAL record distribution by resource manager

## Interactive Mode

Launch the interactive viewer with the `-I` flag:

```bash
pgmoneta-walinfo -I <file>
pgmoneta-walinfo -I <directory>
```

**Features:**

- **File Browser**: Navigate through directories to select WAL files
- **Record Viewer**: Display detailed information about each WAL record in a table format
- **Search**: Find records by resource manager, LSN range, transaction ID (XID), or description
- **Color-Coded UI**: Different colors highlight different record types and record components
- **Easy Navigation**: Use keyboard shortcuts for navigation and searching

**Keyboard Controls:**

| Key | Action |
|-----|--------|
| Arrow Keys ↑↓ | Navigate records |
| Arrow Keys ←→ | Navigate columns |
| Enter | Select file or confirm |
| `/` | Start search |
| `n` / `p` | Next / Previous search result |
| `?` | Show help |
| `q` | Quit |

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