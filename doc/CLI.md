# pgmoneta-cli user guide

```
pgmoneta-cli [ -c CONFIG_FILE ] [ COMMAND ]

-c, --config CONFIG_FILE Set the path to the pgmoneta.conf file
-h, --host HOST          Set the host name
-p, --port PORT          Set the port number
-U, --user USERNAME      Set the user name
-P, --password PASSWORD  Set the password
-L, --logfile FILE       Set the log file
-v, --verbose            Output text string of result
-V, --version            Display version information
-?, --help               Display help
```

## backup
Backup a server

Command

```
pgmoneta-cli backup [<server>|all]
```

Example

```
pgmoneta-cli backup primary
```

## list-backup
List the backups for a server

Command

```
pgmoneta-cli list-backup <server>
```

Example

```
pgmoneta-cli list-backup primary
```

## restore
Restore a backup from a server

Command

```
pgmoneta-cli restore <server> [<timestamp>|oldest|newest] [[current|name=X|xid=X|lsn=X|time=X|inclusive=X|timeline=X|action=X|primary|replica],*] <directory>
```

Example

```
pgmoneta-cli restore primary newest name=MyLabel,primary /tmp
```

## archive
Archive a backup from a server

Command

```
pgmoneta-cli archive <server> [<timestamp>|oldest|newest] [[current|name=X|xid=X|lsn=X|time=X|inclusive=X|timeline=X|action=X|primary|replica],*] <directory>
```

Example

```
pgmoneta-cli archive primary newest current /tmp
```

## delete
Delete a backup from a server

Command

```
pgmoneta-cli delete <server> [<timestamp>|oldest|newest]
```

Example

```
pgmoneta-cli delete primary oldest
```

## retain
Retain a backup from a server. The backup will not be deleted by the retention policy

Command

```
pgmoneta-cli retain <server> [<timestamp>|oldest|newest]
```

Example

```
pgmoneta-cli retain primary oldest
```

## expunge
Expunge a backup from a server. The backup will be deleted by the retention policy

Command

```
pgmoneta-cli expunge <server> [<timestamp>|oldest|newest]
```

Example

```
pgmoneta-cli expunge primary oldest
```

## ping
Verify if pgmoneta is alive

Command

```
pgmoneta-cli ping
```

Example

```
pgmoneta-cli ping
```

## stop
Stop pgmoneta

Command

```
pgmoneta-cli stop
```

Example

```
pgmoneta-cli stop
```

## status
Status of pgmoneta, with a `details` option

Command

```
pgmoneta-cli status [details]
```

Example

```
pgmoneta-cli status details
```

## conf
Manage the configuration

Command

```
pgmoneta-cli conf [reload]
```

Subcommand

- `reload`: Reload configuration

Example

```
pgmoneta-cli conf reload
```

## clear
Clear data/statistics

Command

```
pgmoneta-cli clear [prometheus]
```

Subcommand

- `prometheus`: Reset the Prometheus statistics

Example

```
pgmoneta-cli clear prometheus
```

## decrypt
Decrypt the file in place, remove encrypted file after successful decryption.

Command

```
pgmoneta-cli decrypt <file>
```

## encrypt
Encrypt the file in place, remove unencrypted file after successful encryption.

Command

```
pgmoneta-cli encrypt <file>
```

## Shell completions

There is a minimal shell completion support for `pgmoneta-cli`.
Please refer to the [Install pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/master/doc/tutorial/01_install.md) tutorial for detailed information about how to enable and use shell completions.
