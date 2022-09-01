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

## is-alive
Is pgmoneta alive

Command

```
pgmoneta-cli is-alive
```

Example

```
pgmoneta-cli is-alive
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
Status of pgmoneta

Command

```
pgmoneta-cli status
```

Example

```
pgmoneta-cli status
```

## details
Detailed status of pgmoneta

Command

```
pgmoneta-cli details
```

Example

```
pgmoneta-cli details
```

## reload
Reload the configuration

Command

```
pgmoneta-cli reload
```

Example

```
pgmoneta-cli reload
```

## reset
Reset the Prometheus statistics
Command

```
pgmoneta-cli reset
```

Example

```
pgmoneta-cli reset
```


## Shell completions

There is a minimal shell completion support for `pgmoneta-cli`.
Please refer to the [Install pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/master/doc/tutorial/01_install.md) tutorial for detailed information about how to enable and use shell completions.
