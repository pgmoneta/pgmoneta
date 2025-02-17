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

And, you will get output like

```
Header: 
  ClientVersion: 0.15.3
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
  ServerVersion: 0.15.3
```


This command take the latest backup and all Write-Ahead Log (WAL) segments and restore it into the `/tmp/primary-20240928065644` directory for an up-to-date copy.
