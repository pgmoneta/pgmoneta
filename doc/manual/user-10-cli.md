\newpage

# Command line interface

The **pgmoneta-cli** command line interface controls your interaction with **pgmoneta**.

**It is important that you only use the pgmoneta-cli command line interface to operate on your backup directory**

Using other commands on the backup directory could cause problems.

``` sh
pgmoneta-cli 0.17.1
  Command line utility for pgmoneta

Usage:
  pgmoneta-cli [ -c CONFIG_FILE ] [ COMMAND ]

Options:
  -c, --config CONFIG_FILE                        Set the path to the pgmoneta.conf file
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
  ping                     Check if pgmoneta is alive
  restore                  Restore a backup from a server
  retain                   Retain a backup from a server
  shutdown                 Shutdown pgmoneta
  status [details]         Status of pgmoneta, with optional details
  verify                   Verify a backup from a server

pgmoneta: https://pgmoneta.github.io/
Report bugs: https://github.com/pgmoneta/pgmoneta/issues
```

## backup

Backup a server

The command for a full backup is

``` sh
pgmoneta-cli backup <server>
```

Example

``` sh
pgmoneta-cli backup primary
```

The command for an incremental backup is

``` sh
pgmoneta-cli backup <server> <identifier>
```

where the `identifier` is the identifier for a backup.

Example

``` sh
pgmoneta-cli backup primary 20250101120000
```

## list-backup

List the backups for a server

Command

``` sh
pgmoneta-cli list-backup <server> [--sort asc|desc]
```

The `--sort` option allows sorting backups by timestamp:
- `asc` for ascending order (oldest first)
- `desc` for descending order (newest first)

Example

``` sh
pgmoneta-cli list-backup primary
```

Example with sorting

``` sh
pgmoneta-cli list-backup primary --sort desc
```

## restore

Restore a backup from a server

Command

``` sh
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

[More information](https://www.postgresql.org/docs/current/runtime-config-wal.html#RUNTIME-CONFIG-WAL-RECOVERY-TARGET)

Example

``` sh
pgmoneta-cli restore primary newest name=MyLabel,primary /tmp
```

## verify

Verify a backup from a server

Command

``` sh
pgmoneta-cli verify <server> <directory> [failed|all]
```

Example

``` sh
pgmoneta-cli verify primary oldest /tmp
```

## archive

Archive a backup from a server

Command

``` sh
pgmoneta-cli archive <server> [<timestamp>|oldest|newest] [[current|name=X|xid=X|lsn=X|time=X|inclusive=X|timeline=X|action=X|primary|replica],*] <directory>
```

Example

``` sh
pgmoneta-cli archive primary newest current /tmp
```

## delete

Delete a backup from a server

Command

``` sh
pgmoneta-cli delete <server> [<timestamp>|oldest|newest]
```

Example

``` sh
pgmoneta-cli delete primary oldest
```

## retain

Retain a backup from a server. The backup will not be deleted by the retention policy

Command

``` sh
pgmoneta-cli retain <server> [<timestamp>|oldest|newest]
```

Example

``` sh
pgmoneta-cli retain primary oldest
```

## expunge

Expunge a backup from a server. The backup will be deleted by the retention policy

Command

``` sh
pgmoneta-cli expunge <server> [<timestamp>|oldest|newest]
```

Example

``` sh
pgmoneta-cli expunge primary oldest
```

## encrypt

Encrypt the file in place, remove unencrypted file after successful encryption.

Command

``` sh
pgmoneta-cli encrypt <file>
```

## decrypt

Decrypt the file in place, remove encrypted file after successful decryption.

Command

``` sh
pgmoneta-cli decrypt <file>
```

## compress

Compress the file in place, remove uncompressed file after successful compression.

Command

``` sh
pgmoneta-cli compress <file>
```

## decompress

Decompress the file in place, remove compressed file after successful decompression.

Command

``` sh
pgmoneta-cli decompress <file>
```

## info

Information about a backup.

Command

``` sh
pgmoneta-cli info <server> <timestamp|oldest|newest>
```

## ping

Verify if [**pgmoneta**][pgmoneta] is alive

Command

``` sh
pgmoneta-cli ping
```

Example

``` sh
pgmoneta-cli ping
```

## shutdown

Shutdown [**pgmoneta**][pgmoneta]

Command

``` sh
pgmoneta-cli shutdown
```

Example

``` sh
pgmoneta-cli shutdown
```

## status

Status of [**pgmoneta**][pgmoneta], with a `details` option

Command

``` sh
pgmoneta-cli status [details]
```

Example

``` sh
pgmoneta-cli status details
```

## conf

Manage the configuration

Command

``` sh
pgmoneta-cli conf [reload]
```

Subcommand

- `reload`: Reload configuration

Example

``` sh
pgmoneta-cli conf reload
```

## clear

Clear data/statistics

Command

``` sh
pgmoneta-cli clear [prometheus]
```

Subcommand

- `prometheus`: Reset the Prometheus statistics

Example

``` sh
pgmoneta-cli clear prometheus
```

## Shell completions

There is a minimal shell completion support for `pgmoneta-cli`.

Please refer to the [Install pgmoneta][t_install] tutorial for detailed information about how to enable and use shell completions.
