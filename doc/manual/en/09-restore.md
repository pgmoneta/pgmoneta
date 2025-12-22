\newpage

# Restore

## Restore a backup

We can restore a backup from the primary with the following command

```
pgmoneta-cli restore primary newest current /tmp
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

## Automatic Backup Selection

When specifying a recovery target (`lsn=X`, `time=X`, or `timeline=X`), pgmoneta can automatically
select the appropriate backup that contains the target. Instead of specifying a backup timestamp,
use `newest` and pgmoneta will find the latest backup that can be used for recovery to the specified target.

### Target LSN

Restore to a specific LSN, with automatic backup selection:

```
pgmoneta-cli restore primary newest lsn=0/16B0938 /tmp
```

pgmoneta will select the latest valid backup whose start LSN is less than or equal to `0/16B0938`.

### Target Time

Restore to a specific point in time:

```
pgmoneta-cli restore primary newest time=2025-01-15\ 10:30:00 /tmp
```

pgmoneta will select the latest valid backup that started before or at the specified timestamp.
The timestamp format is `YYYY-MM-DD HH:MM:SS`.

### Target Timeline

Restore from a specific timeline:

```
pgmoneta-cli restore primary newest timeline=2 /tmp
```

pgmoneta will select the latest valid backup from the specified timeline.

And, you will get output like

```
Header:
  ClientVersion: 0.21.0
  Command: 3
  Output: 0
  Timestamp: 20240928130406
Outcome:
  Status: true
  Time: 00:00:00
Request:
  Backup: newest
  Directory: /tmp
  Position: current
  Server: primary
Response:
  Backup: 20240928065644
  BackupSize: 8531968
  Comments: ''
  Compression: 2
  Encryption: 0
  MajorVersion: 17
  MinorVersion: 0
  RestoreSize: 48799744
  Server: primary
  ServerVersion: 0.21.0
```


This command take the latest backup and all Write-Ahead Log (WAL) segments and restore it into the `/tmp/primary-20240928065644` directory for an up-to-date copy.

## Hot standby

In order to use hot standby, simply add

```
hot_standby = /your/local/hot/standby/directory
```

to the corresponding server section of `pgmoneta.conf`. [**pgmoneta**][pgmoneta] will create the directory if it doesn't exist,
and keep the latest backup in the defined directory.

You can also configure muptiple hot standby directories (up to 8) by providing comma-separated paths:
```
/path/to/hot/standby1,/path/to/hot/standby2,/path/to/hot/standby3
```
[**pgmoneta**][pgmoneta] will maintain identical copies of the hot standby in all specified directories.

You can use

```
hot_standby_overrides = /your/local/hot/standby/overrides/
```

to override files in the `hot_standby` directories. The overrides will be applied to all hot_standby directories.

### Tablespaces

By default tablespaces will be mapped to a similar path than the original one, for example `/tmp/mytblspc` becomes `/tmp/mytblspchs`.

However, you can use the directory name to map it to another directory, like

```
hot_standby_tablespaces = /tmp/mytblspc->/tmp/mybcktblspc
```

You can also use the `OID` for the key part, like

```
hot_standby_tablespaces = 16392->/tmp/mybcktblspc
```

Multiple tablespaces can be specified using a `,` between them.
