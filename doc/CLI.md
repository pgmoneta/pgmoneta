# pgmoneta-cli user guide

The **pgmoneta-cli** command line interface controls your interaction with [**pgmoneta**](https://pgmoneta.github.io/).

**It is important that you only use the pgmoneta-cli command line interface to operate on your backup directory**

Using other commands on the backup directory could cause problems.

```
pgmoneta-cli
  Command line utility for pgmoneta

Usage:
  pgmoneta-cli [ -c CONFIG_FILE ] [ COMMAND ]

Options:
  -c, --config CONFIG_FILE                        Set the path to the pgmoneta_cli.conf file
  -h, --host HOST                                 Set the host name
  -p, --port PORT                                 Set the port number
  -U, --user USERNAME                             Set the user name
  -P, --password PASSWORD                         Set the password
  -L, --logfile FILE                              Set the log file
  -v, --verbose                                   Output text string of result
  -V, --version                                   Display version information
  -F, --format text|json|raw                      Set the output format
  -C, --compress none|gz|zstd|lz4|bz2             Compress the wire protocol
  -E, --encrypt none|aes|aes256|aes192|aes128     Encrypt the wire protocol
  -s, --sort asc|desc                             Sort result (for list-backup)
      --cascade                                   Cascade a retain/expunge backup
  -?, --help                                      Display help

Commands:
  annotate                 Annotate a backup with comments
  archive                  Archive a backup from a server
  backup                   Backup a server
  clear <what>             Clear data, with:
                           - 'prometheus' to reset the Prometheus statistics
  compress                 Compress a file using configured method
  conf <action>            Manage the configuration, with one of subcommands:
                           - 'get' to obtain information about a runtime configuration value
                             conf get <parameter_name>
                           - 'ls' to print the configurations used
                           - 'reload' to reload the configuration
                           - 'set' to modify a configuration value;
                             conf set <parameter_name> <parameter_value>;
  decompress               Decompress a file using configured method
  decrypt                  Decrypt a file using master-key
  delete                   Delete a backup from a server
  encrypt                  Encrypt a file using master-key
  expunge                  Expunge a backup from a server
  info                     Information about a backup
  list-backup              List the backups for a server
  mode                     Switch the mode for a server
  ping                     Check if pgmoneta is alive
  restore                  Restore a backup from a server
  retain                   Retain a backup from a server
  shutdown                 Shutdown pgmoneta
  status [details]         Status of pgmoneta, with optional details
  verify                   Verify a backup from a server
```

## backup

Backup a server

The command for a full backup is

```sh
pgmoneta-cli backup <server>
```

Example

```sh
pgmoneta-cli backup primary
```

The command for an incremental backup is

```sh
pgmoneta-cli backup <server> <identifier>
```

where the `identifier` is the identifier for a backup.

Example

```sh
pgmoneta-cli backup primary 20250101120000
```

## list-backup

List the backups for a server

Command

```sh
pgmoneta-cli list-backup <server>
```

Example

```sh
pgmoneta-cli list-backup primary
```

## restore

Restore a backup from a server

Command

```sh
pgmoneta-cli restore <server> [<timestamp>|oldest|newest] [[current|name=X|xid=X|lsn=X|time=X|inclusive=X|timeline=X|action=X|primary|replica],*] <directory>
```

where

* `current` means copy the Write-Ahead Log (WAL ), and restore to first stable checkpoint
* `name=X` means copy the Write-Ahead Log (WAL ), and restore to the label specified
* `xid=X` means copy the Write-Ahead Log (WAL ), and restore to the XID specified
* `time=X` means copy the Write-Ahead Log (WAL ), and restore to the timestamp specified
* `lsn=X` means copy the Write-Ahead Log (WAL ), and restore to the Log Sequence Number (LSN) specified
* `inclusive=X` means that the restore is inclusive of the specified information
* `timeline=X` means that the restore is done to the specified information timeline
* `action=X` means which action should be executed after the restore (pause, shutdown)
* `primary` means that the cluster is setup as a primary
* `replica` means that the cluster is setup as a replica

[More information](https://www.postgresql.org/docs/current/runtime-config-wal.html#RUNTIME-CONFIG-WAL-RECOVERY-TARGET)

Example

```sh
pgmoneta-cli restore primary newest name=MyLabel,primary /tmp
```

### Automatic Backup Selection

When using recovery targets (`lsn=X`, `time=X`, or `timeline=X`), pgmoneta automatically selects the appropriate backup:

```sh
# Restore to a specific LSN (backup auto-selected)
pgmoneta-cli restore primary newest lsn=0/16B0938 /tmp

# Restore to a specific time (backup auto-selected)
pgmoneta-cli restore primary newest time=2025-01-15\ 10:30:00 /tmp

# Restore from a specific timeline (backup auto-selected)
pgmoneta-cli restore primary newest timeline=2 /tmp
```

## verify

Verify a backup from a server

Command

```sh
pgmoneta-cli verify <server> [<timestamp>|oldest|newest] <directory> [failed|all]
```

Example

```sh
pgmoneta-cli verify primary oldest /tmp
```

## archive

Archive a backup from a server

Command

```sh
pgmoneta-cli archive <server> [<timestamp>|oldest|newest] [[current|name=X|xid=X|lsn=X|time=X|inclusive=X|timeline=X|action=X|primary|replica],*] <directory>
```

Example

```sh
pgmoneta-cli archive primary newest current /tmp
```

## delete

Delete a backup from a server

Command

```sh
pgmoneta-cli delete <server> [<timestamp>|oldest|newest]
```

Example

```sh
pgmoneta-cli delete primary oldest
```

## retain

Retain a backup from a server. The backup will not be deleted by the retention policy

Command

```sh
pgmoneta-cli retain [--cascade] <server> [<timestamp>|oldest|newest]
```

Example

```sh
pgmoneta-cli retain primary oldest
```

## expunge

Expunge a backup from a server. The backup will be deleted by the retention policy

Command

```sh
pgmoneta-cli expunge [--cascade] <server> [<timestamp>|oldest|newest]
```

Example

```sh
pgmoneta-cli expunge primary oldest
```

## encrypt

Encrypt the file in place, remove unencrypted file after successful encryption.

Command

```sh
pgmoneta-cli encrypt <file>
```

## decrypt

Decrypt the file in place, remove encrypted file after successful decryption.

Command

```sh
pgmoneta-cli decrypt <file>
```

## compress

Compress the file in place, remove uncompressed file after successful compression.

Command

```sh
pgmoneta-cli compress <file>
```

## decompress

Decompress the file in place, remove compressed file after successful decompression.

Command

```sh
pgmoneta-cli decompress <file>
```

## info

Information about a backup.

Command

```sh
pgmoneta-cli info <server> <timestamp|oldest|newest>
```

## annotate

Annotate a backup with comments

```sh
pgmoneta-cli annotate <server> <backup> add <key> <comment>
pgmoneta-cli annotate <server> <backup> update <key> <comment>
pgmoneta-cli annotate <server> <backup> remove <key>
```

## ping

Verify if [**pgmoneta**](https://pgmoneta.github.io/) is alive

Command

```sh
pgmoneta-cli ping
```

Example

```sh
pgmoneta-cli ping
```

## mode

[**pgmoneta**](https://pgmoneta.github.io/) detects when a server is down. You can bring a server online or offline
using the mode command.

Command

```
pgmoneta-cli mode <server> <online|offline>
```

Example

```
pgmoneta-cli mode primary offline
```

or

```
pgmoneta-cli mode primary online
```

[**pgmoneta**](https://pgmoneta.github.io/) will keep basic services running for an offline server such that
you can verify a backup or do a restore.

## shutdown

Shutdown [**pgmoneta**](https://pgmoneta.github.io/)

Command

```sh
pgmoneta-cli shutdown
```

Example

```sh
pgmoneta-cli shutdown
```

## status

Status of [**pgmoneta**](https://pgmoneta.github.io/), with a `details` option

Command

```sh
pgmoneta-cli status [details]
```

Example

```sh
pgmoneta-cli status details
```

## conf

Manage the configuration

Command

```sh
pgmoneta-cli conf [reload | ls | get | set]
```

Subcommand

- `reload`: Reload configuration
- `ls` : To print the configurations used
- `get <config_key>` : To obtain information about a runtime configuration value
- `set <config_key> <config_value>` : To modify the runtime configuration value

Example

```sh
pgmoneta-cli conf reload
pgmoneta-cli conf ls
pgmoneta-cli conf get server.primary.host
pgmoneta-cli conf set encryption aes-256-cbc
```
### conf get

Get the value of a runtime configuration key, or the entire configuration.

- If you provide a `<config_key>`, you get the value for that key.
  - For main section keys, you can use either just the key (e.g., `host`) or with the section (e.g., `pgmoneta.host`).
  - For server section keys, use the server name as the section (e.g., `server.primary.host`, `server.myserver.port`).
- If you run `pgmoneta-cli conf get` without any key, the complete configuration will be output.

Examples

```sh
pgmoneta-cli conf get
pgmoneta-cli conf get host
pgmoneta-cli conf get pgmoneta.host
pgmoneta-cli conf get server.primary.host
pgmoneta-cli conf get server.myserver.port
```

### conf set

Set runtime configuration values with immediate application or restart notification.

**Syntax:**
```sh
pgmoneta-cli conf set <config_key> <config_value>
```

**Key Formats:**
- Main configuration: `parameter` or `pgmoneta.parameter`
- Server configuration: `server.server_name.parameter` (only format supported)

**Examples:**
```sh
# Immediate application
pgmoneta-cli conf set log_level debug5
pgmoneta-cli conf set metrics 5001
pgmoneta-cli conf set compression zstd
pgmoneta-cli conf set server.primary.workers 4

# Restart required (shows restart message)
pgmoneta-cli conf set host 192.168.1.100
pgmoneta-cli conf set server.primary.port 5433

# Service control
pgmoneta-cli conf set metrics 0      # Disable metrics
pgmoneta-cli conf set management 0   # Disable management
```

**Output Examples:**
```
# Immediate success
Configuration change applied successfully
   Parameter: log_level
   Old value: info
   New value: debug5
   Status: Active (applied to running instance)

# Restart required
Configuration change requires manual restart
   Parameter: host
   Current value: localhost (unchanged in running instance)
   Requested value: 192.168.1.100 (cannot be applied to live instance)
   Status: Requires full service restart
```

## clear

Clear data/statistics

Command

```sh
pgmoneta-cli clear [prometheus]
```

Subcommand

- `prometheus`: Reset the Prometheus statistics

Example

```sh
pgmoneta-cli clear prometheus
```

## Shell completions

There is a minimal shell completion support for `pgmoneta-cli`.

Please refer to the manual for detailed information about how to enable and use shell completions.
