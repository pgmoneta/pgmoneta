\newpage

# Backup

## Create a full backup

We can take a full backup from the primary with the following command

```
pgmoneta-cli backup primary
```

and you will get output like

```
Header:
  ClientVersion: 0.21.0
  Command: 1
  Output: 0
  Timestamp: 20240928065644
Outcome:
  Status: true
  Time: 00:00:20
Request:
  Server: primary
Response:
  Backup: 20240928065644
  BackupSize: 8531968
  Compression: 2
  Encryption: 0
  MajorVersion: 17
  MinorVersion: 0
  RestoreSize: 48799744
  Server: primary
  ServerVersion: 0.21.0
```

## View backups

We can list all backups for a server with the following command

```
pgmoneta-cli list-backup primary
```

and you will get output like

```
Header:
  ClientVersion: 0.21.0
  Command: 2
  Output: 0
  Timestamp: 20240928065812
Outcome:
  Status: true
  Time: 00:00:00
Request:
  Server: primary
Response:
  Backups:
    - Backup: 20240928065644
      BackupSize: 8531968
      Comments: ''
      Compression: 2
      Encryption: 0
      Incremental: false
      Keep: false
      RestoreSize: 48799744
      Server: primary
      Valid: 1
      WAL: 0
  MajorVersion: 17
  MinorVersion: 0
  Server: primary
  ServerVersion: 0.21.0
```

## Sorting backups

You can sort the backup list by timestamp using the `--sort` option:

```
pgmoneta-cli list-backup primary --sort asc
```

for ascending order (oldest first), or 

```
pgmoneta-cli list-backup primary --sort desc
```

for descending order (newest first).

## Create an incremental backup

We can take an incremental backup from the primary with the following command

```
pgmoneta-cli backup primary 20240928065644
```

and you will get output like

```
Header:
  ClientVersion: 0.21.0
  Command: 1
  Output: 0
  Timestamp: 20240928065730
Outcome:
  Status: true
  Time: 00:00:20
Request:
  Server: primary
Response:
  Backup: 20240928065750
  BackupSize: 124312
  Compression: 2
  Encryption: 0
  Incremental: true
  MajorVersion: 17
  MinorVersion: 0
  RestoreSize: 48799744
  Server: primary
  ServerVersion: 0.21.0
```

Incremental backups are supported when using [PostgreSQL 17+](https://www.postgresql.org). Note that currently branching is not allowed for incremental backup -- a backup can have at most 1
incremental backup child. 

Note: Support for incremental backups on PostgreSQL versions 14–16 is also provided, at preview level.

## Incremental backup for PostgreSQL 14-16

### Working

This section will provide a brief idea of how `pgmoneta` performs incremental backups

* Fetch the modified blocks within a range of WAL LSN (typically from the checkpoint of preceding backup to the start LSN of current backup) and generate a summary of it
* Fetch all the server files in the data directory of the server
* Iterate the server files -
    * If the file is not in 'base'/'global', perform full file backup (as files not under 'base'/'global' are not WAL-logged properly)
    * Otherwise, if file was found to be modified using the summary, perform incremental backup of this file.
    * Otherewise, the file is unchanged, now if the file size is 0 or its limit block intersect the segment (meaning the file is truncated fully/partially)
        * Perform full file backup
        * Otherwise, perform empty incremental backup with only a header
* Copy all the WAL segments after and including the WAL segment in which start LSN is present
* Generate manifest file over the incremental backup data directory

### Dependencies

For PostgreSQL version 14-16, we rely on `pgmoneta` native block-level incremental solutions for backups. To facilitate this solution `pgmoneta` highly depends on [pgmoneta_ext](https://github.com/pgmoneta/pgmoneta_ext) extension and PostgreSQL's system administration functions. Following are the list of admin functions `pgmoneta` depends on:

| Function                    | System administration function | Minimum Privilege | Parameters | Description                                            |
|-----------------------------|---|--------|------------|--------------------------------------------------------|
| `pgmoneta_server_backup_start`    |   pg_start_backup/pg_backup_start |    EXECUTE    | label | Returns a row with the backup start lsn |
| `pgmoneta_server_backup_stop`    |   pg_stop_backup/pg_backup_stop |    EXECUTE    | None | Returns a row with two columns - backup stop lsn and the backup label file contents  |
| `pgmoneta_server_read_binary`    |   pg_read_binary_file |    pg_read_server_files & EXECUTE    | (offset, length, path/to/file) | Returns the contents of the file provided from the server of particular length at a particular offset |
| `pgmoneta_server_file_stat`    |   pg_stat_file |    pg_read_server_files & EXECUTE    | path/to/file | Returns the metadata of the file provided from the server like file size, modification time etc |

For complete information on the server api refer [this](https://github.com/pgmoneta/pgmoneta/blob/main/doc/manual/en/80-server-api.md)

The extension functions on which the native solution depends are:

- `pgmoneta_ext_get_file('<path/to/file>')`
- `pgmoneta_ext_get_files('<path/to/dir>')`

You can read more about these functions and their required privileges over [here](https://github.com/pgmoneta/pgmoneta_ext/blob/main/doc/manual/en/04-functions.md)

### Setup

Let's assume we want to make setup for PostgreSQL 14. Make sure PostgreSQL 14 is installed on your system. Since our solution depends on `pgmoneta_ext`, build and install the extension for version 14 using the following commands:

```
git clone https://github.com/pgmoneta/pgmoneta_ext
cd pgmoneta_ext
mkdir build
cd build
cmake ..
make
sudo make install
```

If multiple PostgreSQL versions are present modify the `PATH` variable such that binary path of version 14 appears first.

**Initialize a PostgreSQL cluster**

```
initdb -D <path>
```

**Modify the `postgresql.conf` to enable the following parameters**

```
password_encryption = scram-sha-256
shared_preload_libraries = 'pgmoneta_ext'
```

**Add the following entry to `pg_hba.conf` file**

```
host    replication     repl           127.0.0.1/32            scram-sha-256
```

**Start the cluster using the command**

```
pg_ctl -D <path> -l logfile start
```

**Lastly, perform the following commands to create the extension and grant required permissions to the connection user**

```
cat > init-permissions.sh << 'OUTER_EOF'
#!/bin/bash

# Check input parameters
if [ $# -lt 2 ]; then
    echo "Usage: $0 <connection_user_name> <postgres_version>"
    exit 1
fi

CONN_USER_NAME="$1"
PG_VERSION="$2"

SCRIPT_BASE_NAME=$(basename -s .sh "$0")
OUTPUT_SQL_FILE="${SCRIPT_BASE_NAME}.sql"

# Check if pgmoneta_ext extension exists
EXT_EXISTS=$(psql -d postgres -Atc \
    "SELECT 1 FROM pg_extension WHERE extname = 'pgmoneta_ext';")

if [ $? -ne 0 ]; then
    echo "Failed checking for pgmoneta_ext extension"
    exit 1
fi

# -------------------------------------------------------------------
# Start writing SQL into the output file
# -------------------------------------------------------------------

cat <<EOF > "$OUTPUT_SQL_FILE"
SET password_encryption = 'scram-sha-256';

DO \$\$
BEGIN
    -- Create new replication user if not present
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = '$CONN_USER_NAME') THEN
        EXECUTE 'CREATE ROLE $CONN_USER_NAME WITH LOGIN REPLICATION PASSWORD '"$CONN_USER_NAME"'';
    END IF;

    -- Create replication slot if missing
    IF NOT EXISTS (
        SELECT 1 FROM pg_replication_slots WHERE slot_name = '$CONN_USER_NAME'
    ) THEN
        PERFORM pg_create_physical_replication_slot('$CONN_USER_NAME', true, false);
    END IF;
END
\$\$;

-- Create extension if missing
EOF

if [ "$EXT_EXISTS" != "1" ]; then
  echo "DROP EXTENSION IF EXISTS pgmoneta_ext;" >> "$OUTPUT_SQL_FILE"
  echo "CREATE EXTENSION pgmoneta_ext;" >> "$OUTPUT_SQL_FILE"
fi

cat <<EOF >> "$OUTPUT_SQL_FILE"

GRANT EXECUTE ON FUNCTION pgmoneta_ext_get_file(text) TO $CONN_USER_NAME;
GRANT EXECUTE ON FUNCTION pgmoneta_ext_get_files(text) TO $CONN_USER_NAME;

-- Privilege to read server files
GRANT pg_read_server_files TO $CONN_USER_NAME;

GRANT EXECUTE ON FUNCTION pg_read_binary_file(text, bigint, bigint, boolean) TO $CONN_USER_NAME;
GRANT EXECUTE ON FUNCTION pg_stat_file(text, boolean) TO $CONN_USER_NAME;

-- Backup function privileges depending on version
EOF

# Version-based backup grants
if [ "$PG_VERSION" -ge 15 ]; then
  cat <<EOF >> "$OUTPUT_SQL_FILE"
  GRANT EXECUTE ON FUNCTION pg_backup_start(text, boolean) TO $CONN_USER_NAME;
  GRANT EXECUTE ON FUNCTION pg_backup_stop(boolean) TO $CONN_USER_NAME;
  EOF
else
  cat <<EOF >> "$OUTPUT_SQL_FILE"
  GRANT EXECUTE ON FUNCTION pg_start_backup(text, boolean, boolean) TO $CONN_USER_NAME;
  GRANT EXECUTE ON FUNCTION pg_stop_backup(boolean, boolean) TO $CONN_USER_NAME;
  EOF
fi

OUTER_EOF

chmod 755 ./init-permissions.sh
./init-permissions.sh repl 14
psql -f init-permissions.sql postgres
```

This will generate an sql file `init-permissions.sql` use this to initialize the cluster:

```
psql -f init-permissions.sql postgres
```

Once this setup is done you can go ahead and create incremental backups without hassles.

## Backup information

You can list the information about a backup

```
pgmoneta-cli -c pgmoneta.conf info primary newest
```

and you will get output like

```
Header:
  ClientVersion: 0.21.0
  Command: info
  Output: text
  Timestamp: 20241025163541
Outcome:
  Status: true
  Time: 00:00:00
Request:
  Backup: newest
  Server: primary
Response:
  Backup: 20241019163516
  BackupSize: 6.54MB
  CheckpointHiLSN: 0
  CheckpointLoLSN: 4F0000B8
  Comments: ''
  Compression: zstd
  Elapsed: 4
  Encryption: none
  EndHiLSN: 0
  EndLoLSN: 4F000158
  EndTimeline: 1
  Incremental: false
  Keep: true
  MajorVersion: 17
  MinorVersion: 0
  NumberOfTablespaces: 0
  RestoreSize: 45.82MB
  Server: primary
  ServerVersion: 0.21.0
  StartHiLSN: 0
  StartLoLSN: 4F000060
  StartTimeline: 1
  Tablespaces: {}
  Valid: yes
  WAL: 00000001000000000000004F
```

## Verify a backup

You can use the command line interface to verify a backup by

```
pgmoneta-cli verify primary oldest /tmp
```

which will verify the oldest backup of the `[primary]` host.

[**pgmoneta**][pgmoneta] creates a SHA512 checksum file(`backup.sha512`) for each backup at the backup root directory, which can be used to verify the integrity of the files.

Using `sha512sum`:
```
cd <path-to-specific-backup-directory>

sha512sum --check backup.sha512
```

The `verification` parameter can be use to control how frequently pgmoneta verifies the integrity of backup files. You can configure this in `pgmoneta.conf`:

```
[pgmoneta]
.
.
.
verification = 3600
```
For example, setting `verification = 3600` or `verification = 1H` will perform integrity checks every hour.

## Encryption

By default, the encryption is disabled. To enable this feature, modify `pgmoneta.conf`:

```
encryption = aes-256-cbc
```

Many encryption modes are supported, see the documentation for the `encryption` property for details.

### Encryption and Decryption Commands

[**pgmoneta**][pgmoneta] use the same key created by `pgmoneta-admin master-key` to encrypt and decrypt files.

Encrypt a file with `pgmoneta-cli encrypt`, the file will be encrypted in place and remove unencrypted file on success.

```sh
pgmoneta-cli -c pgmoneta.conf encrypt '<path-to-your-file>/file.tar.zstd'
```

Decrypt a file with `pgmoneta-cli decrypt`, the file will be decrypted in place and remove encrypted file on success.

```sh
pgmoneta-cli -c pgmoneta.conf decrypt '<path-to-your-file>/file.tar.zstd.aes'
```

`pgmoneta-cli encrypt` and `pgmoneta-cli decrypt` are built to deal with files created by `pgmoneta-cli archive`. It can be used on other files though.

## Annotate

**Add a comment**

You can add a comment by

```
pgmoneta-cli -c pgmoneta.conf annotate primary newest add mykey mycomment
```

**Update a comment**

You can update a comment by

```
pgmoneta-cli -c pgmoneta.conf annotate primary newest update mykey mynewcomment
```

**Remove a comment**

You can remove a comment by

```
pgmoneta-cli -c pgmoneta.conf annotate primary newest remove mykey
```

**View comments**

You can view the comments by

```
pgmoneta-cli -c pgmoneta.conf info primary newest
```

## Archive

In order to create an archive of a backup use

```
pgmoneta-cli -c pgmoneta.conf archive primary newest current /tmp/
```

which will take the latest backup and all Write-Ahead Log (WAL) segments and create
an archive named `/tmp/primary-<timestamp>.tar.zstd`. This archive will contain
an up-to-date copy.

## Crontab

Lets create a `crontab` such that a backup is made every day,

First, take a full backup if you are using PostgreSQL 17+,

```
pgmoneta-cli backup primary
```

then you can use incremental backup for your daily jobs,

```
crontab -e
```

and insert

```
0 6 * * * pgmoneta-cli backup primary latest
```

for taking an incremental backup every day at 6 am.

Otherwise use the full backup in the cron job.
