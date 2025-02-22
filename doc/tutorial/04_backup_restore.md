## Backup and restore

This tutorial will show you how to do a backup and a restore using [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

### Preface

This tutorial assumes that you have an installation of PostgreSQL 13+ and [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

See [Install pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/01_install.md)
for more detail.

### Backup

```
pgmoneta-cli -c pgmoneta.conf backup primary
```

will take a backup of the `[primary]` host.

(`pgmoneta` user)

### List backups

```
pgmoneta-cli -c pgmoneta.conf list-backup primary
```

(`pgmoneta` user)

### Restore

```
pgmoneta-cli -c pgmoneta.conf restore primary newest current /tmp/
```

will take the latest backup and all Write-Ahead Log (WAL) segments and restore it
into the `/tmp/primary-<timestamp>` directory for an up-to-date copy.

The 2nd to last parameter allows

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

(`pgmoneta` user)
