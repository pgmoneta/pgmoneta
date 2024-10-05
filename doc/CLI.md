# pgmoneta-cli user guide

The **pgmoneta-cli** command line interface controls your interaction with **pgmoneta**.

**It is important that you only use the pgmoneta-cli command line interface to operate on your backup directory**

Using other commands on the backup directory could cause problems.

```
pgmoneta-cli
  Command line utility for pgmoneta

Usage:
  pgmoneta-cli [ -c CONFIG_FILE ] [ COMMAND ] 

Options:
  -c, --config CONFIG_FILE              Set the path to the pgmoneta.conf file
  -h, --host HOST                       Set the host name
  -p, --port PORT                       Set the port number
  -U, --user USERNAME                   Set the user name
  -P, --password PASSWORD               Set the password
  -L, --logfile FILE                    Set the log file
  -v, --verbose                         Output text string of result
  -V, --version                         Display version information
  -F, --format text|json|raw            Set the output format
  -C, --compress none|gz|zstd|lz4|bz2   Compress the wire protocol
  -?, --help                            Display help

Commands:
  backup                   Backup a server
  list-backup              List the backups for a server
  restore                  Restore a backup from a server
  verify                   Verify a backup from a server
  archive                  Archive a backup from a server
  delete                   Delete a backup from a server
  retain                   Retain a backup from a server
  expunge                  Expunge a backup from a server
  encrypt                  Encrypt a file using master-key
  decrypt                  Decrypt a file using master-key
  compress                 Compress a file using configured method
  decompress               Decompress a file using configured method
  info                     Information about a backup
  annotate                 Annotate a backup with comments
  ping                     Check if pgmoneta is alive
  stop                     Stop pgmoneta
  status [details]         Status of pgmoneta, with optional details
  conf <action>            Manage the configuration, with one of subcommands:
                           - 'reload' to reload the configuration
  clear <what>             Clear data, with:
                           - 'prometheus' to reset the Prometheus statistics
```

## backup

Backup a server

Command

``` sh
pgmoneta-cli backup <server>
```

Example

``` sh
pgmoneta-cli backup primary
```

## list-backup

List the backups for a server

Command

``` sh
pgmoneta-cli list-backup <server>
```

Example

``` sh
pgmoneta-cli list-backup primary
```

## restore

Restore a backup from a server

Command

``` sh
pgmoneta-cli restore <server> [<timestamp>|oldest|newest] [[current|name=X|xid=X|lsn=X|time=X|inclusive=X|timeline=X|action=X|primary|replica],*] <directory>
```

Example

``` sh
pgmoneta-cli restore primary newest name=MyLabel,primary /tmp
```

## verify

Verify a backup from a server

Command

``` sh
pgmoneta-cli verify <server> [<timestamp>|oldest|newest] <directory> [failed|all]
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
pgmoneta-cli info <server> <backup>
```

## annotate

Annotate a backup with comments

```sh
pgmoneta-cli annotate <server> <backup> add <key> <comment>
pgmoneta-cli annotate <server> <backup> update <key> <comment>
pgmoneta-cli annotate <server> <backup> remove <key>
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

## stop

Stop [**pgmoneta**][pgmoneta]

Command

``` sh
pgmoneta-cli stop
```

Example

``` sh
pgmoneta-cli stop
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
Please refer to the [Install pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/master/doc/tutorial/01_install.md) tutorial for detailed information about how to enable and use shell completions.
