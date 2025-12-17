\newpage

# Write-Ahead Log (WAL) Tools

pgmoneta provides two powerful command-line utilities for working with PostgreSQL Write-Ahead Log (WAL) files:

- **pgmoneta-walinfo**: Read and display information about WAL files
- **pgmoneta-walfilter**: Filter WAL files based on user-defined rules.

## pgmoneta-walinfo

`pgmoneta-walinfo` is a command-line utility designed to read and display information about PostgreSQL Write-Ahead Log (WAL) files. The tool provides output in either raw or JSON format, making it easy to analyze WAL files for debugging, auditing, or general information purposes.

In addition to standard WAL files, `pgmoneta-walinfo` also supports encrypted (**aes**) and compressed WAL files in the following formats: **zstd**, **gz**, **lz4**, and **bz2**.

### Usage

```bash
pgmoneta-walinfo 0.20.0
  Command line utility to read and display Write-Ahead Log (WAL) files

Usage:
  pgmoneta-walinfo <file>

Options:
  -c,  --config      Set the path to the pgmoneta_walinfo.conf file
  -u,  --users       Set the path to the pgmoneta_users.conf file
  -RT, --tablespaces Filter on tablspaces
  -RD, --databases   Filter on databases
  -RT, --relations   Filter on relations
  -R,  --filter      Combination of -RT, -RD, -RR
  -o,  --output      Output file
  -F,  --format      Output format (raw, json)
  -L,  --logfile     Set the log file
  -q,  --quiet       No output only result
       --color       Use colors (on, off)
  -r,  --rmgr        Filter on a resource manager
  -s,  --start       Filter on a start LSN
  -e,  --end         Filter on an end LSN
  -x,  --xid         Filter on an XID
  -l,  --limit       Limit number of outputs
  -v,  --verbose     Output result
  -S,  --summary     Show a summary of WAL record counts grouped by resource manager
  -V,  --version     Display version information
  -m,  --mapping     Provide mappings file for OID translation
  -t,  --translate   Translate OIDs to object names in XLOG records
  -?,  --help        Display help
```

### Output Formats

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

### Examples

1. **View WAL file details in JSON format:**

```bash
pgmoneta-walinfo -F json /path/to/walfile
```

2. **View WAL file details with OIDs translated to object names using database connection:**

```bash
pgmoneta-walinfo -c pgmoneta_walinfo.conf -t -u /path/to/pgmoneta_user.conf /path/to/walfile
```

3. **View WAL file details with OIDs translated using a mapping file:**

```bash
pgmoneta-walinfo -c pgmoneta_walinfo.conf -t -m /path/to/mapping.json /path/to/walfile
```

4. **Show summary of WAL record counts by resource manager:**

```bash
pgmoneta-walinfo -S /path/to/walfile
```

5. **Filter records by resource manager and limit output:**

```bash
pgmoneta-walinfo -r Heap -l 10 /path/to/walfile
```

### OID Translation

`pgmoneta-walinfo` supports translating OIDs in WAL records to human-readable object names in two ways:

#### Method 1: Database Connection

If you provide a `pgmoneta_user.conf` file, the tool will connect to the database cluster and fetch object names directly:

```bash
pgmoneta-walinfo -c pgmoneta_walinfo.conf -t -u /path/to/pgmoneta_user.conf /path/to/walfile
```

#### Method 2: Mapping File

If you provide a mapping file containing OIDs and corresponding object names:

```bash
pgmoneta-walinfo -c pgmoneta_walinfo.conf -t -m /path/to/mapping.json /path/to/walfile
```

**Example mapping.json file:**

```json
{
    "tablespaces": [
        {"pg_default": "1663"},
        {"my_tablespace": "16399"}
    ],
    "databases": [
        {"mydb": "16733"},
        {"postgres": "5"}
    ],
    "relations": [
        {"public.test_table": "16734"},
        {"public.users": "16735"}
    ]
}
```

You can generate the mapping data using these SQL queries:

```sql
SELECT spcname, oid FROM pg_tablespace;
SELECT datname, oid FROM pg_database;
SELECT nspname || '.' || relname, c.oid FROM pg_class c JOIN pg_namespace n ON c.relnamespace = n.oid;
```

**Notes:**
- Use the `-t` flag to enable translation
- If both `pgmoneta_users.conf` and `mappings.json` are provided, the mapping file takes precedence
- OIDs not found in the server/mapping will be displayed as-is

## pgmoneta-walfilter

`pgmoneta-walfilter` is a command-line utility that reads PostgreSQL Write-Ahead Log (WAL) files from a source directory, filters them based on user-defined rules, recalculates CRC checksums, and writes the filtered WAL files to a target directory.

### Filtering Rules

The tool supports two types of filtering rules:

1. **Transaction ID (XID) filtering**: Filter out specific transaction IDs
   - Specify a list of XIDs to remove from the WAL stream
   - All records associated with these XIDs will be filtered out

2. **Operation-based filtering**: Filter out specific database operations
   - `DELETE`: Removes all DELETE operations and their associated transactions from the WAL stream

### Usage

```bash
pgmoneta-walfilter <yaml_config_file>
```

### Configuration

The tool uses a YAML configuration file to specify source and target directories and other settings.

#### Configuration File Structure

```yaml
source_dir: /path/to/source/backup/directory
target_dir: /path/to/target/directory
configuration_file: /etc/pgmoneta/pgmoneta_walfilter.conf
rules:                             # Optional: filtering rules
  - xids:                          # Filter by transaction IDs
    - 752
    - 753
  - operations:                    # Filter by operations
    - DELETE
```

#### Configuration Parameters

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `source_dir` | String | Yes | Source directory containing the backup and WAL files |
| `target_dir` | String | Yes | Target directory where filtered WAL files will be written |
| `configuration_file` | String | No | Path to pgmoneta_walfilter.conf file |
| `rules` | Array | No | Filtering rules to apply to WAL files |
| `rules.xids` | Array of Integers | No | List of transaction IDs (XIDs) to filter out |
| `rules.operations` | Array of Strings | No | List of operations to filter out |

### How It Works

1. **Read Configuration**: Parses the YAML configuration file
2. **Load WAL Files**: Reads all WAL files from the source directory
3. **Apply Filters**: Applies the specified filtering rules:
   - Filters out records matching specified operations (e.g., DELETE)
   - Filters out records with specified transaction IDs (XIDs)
   - Converts filtered records to NOOP operations
4. **Recalculate CRCs**: Updates checksums for modified records
5. **Write Output**: Saves filtered WAL files to the target directory

### Examples

#### Basic Usage

Create a configuration file `config.yaml`:

```yaml
source_dir: /path/to/source/directory
target_dir: /path/to/target/directory
configuration_file: /etc/pgmoneta/pgmoneta_walfilter.conf
```

Run the tool:

```bash
pgmoneta-walfilter config.yaml
```

#### Filtering Example

Create a configuration file with filtering rules:

```yaml
source_dir: /path/to/source/directory
target_dir: /path/to/target/directory
configuration_file: /etc/pgmoneta/pgmoneta_walfilter.conf
rules:
  - xids:
    - 752
    - 753
  - operations:
    - DELETE
```

This configuration will:
- Filter out all DELETE operations and their associated transactions
- Filter out all records with transaction IDs 752 and 753

Run the tool:

```bash
pgmoneta-walfilter filter_config.yaml
```

**Log Files:**

The tool uses the logging configuration from `pgmoneta_walfilter.conf`. Check the log file specified in the configuration for detailed error messages and processing information.

### Additional Information
For more detailed information about the internal APIs and developer documentation, see the [WAL Developer Guide](78-wal.md).
