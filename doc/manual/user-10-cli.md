\newpage

# Command line interface

``` sh
pgmoneta-cli [ -c CONFIG_FILE ] [ COMMAND ]

-c, --config CONFIG_FILE Set the path to the pgmoneta.conf file
-h, --host HOST          Set the host name
-p, --port PORT          Set the port number
-U, --user USERNAME      Set the user name
-P, --password PASSWORD  Set the password
-L, --logfile FILE       Set the log file
-v, --verbose            Output text string of result
-V, --version            Display version information
-F, --format text|json   Set the output format
-?, --help               Display help
```

## backup

Backup a server

Command

``` sh
pgmoneta-cli backup [<server>|all]
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

## info

Information about a backup.

Command

``` sh
pgmoneta-cli info <server> <backup>
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

Please refer to the [Install pgmoneta][t_install] tutorial for detailed information about how to enable and use shell completions.
