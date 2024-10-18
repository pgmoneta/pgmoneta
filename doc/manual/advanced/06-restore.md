\newpage

# Restore

## Restore a backup

We can restore a backup from the primary with the following command

```
pgmoneta-cli restore primary newest current /tmp
```

and you will get output like

```
Header: 
  ClientVersion: 0.15.0
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
  ServerVersion: 0.15.0
```


This command take the latest backup and all Write-Ahead Log (WAL) segments and restore it into the `/tmp/primary-20240928065644` directory for an up-to-date copy.
