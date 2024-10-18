\newpage

# Backup

## Create backup

We can take a backup from the primary with the following command

```
pgmoneta-cli backup primary
```

and you will get output like

```
Header: 
  ClientVersion: 0.15.0
  Command: 1
  Output: 0
  Timestamp: 20240928065644
Outcome: 
  Status: true
  Time: 00:00:20
Request: 
  Server: primary
Response: 
  Backup: 20240928065644
  BackupSize: 8531968
  Compression: 2
  Encryption: 0
  MajorVersion: 17
  MinorVersion: 0
  RestoreSize: 48799744
  Server: primary
  ServerVersion: 0.15.0
```

## View backups

We can list all backups for a server with the following command

```
pgmoneta-cli list-backup primary
```

and you will get output like

```
Header: 
  ClientVersion: 0.15.0
  Command: 2
  Output: 0
  Timestamp: 20240928065812
Outcome: 
  Status: true
  Time: 00:00:00
Request: 
  Server: primary
Response: 
  Backups: 
    - Backup: 20240928065644
      BackupSize: 8531968
      Comments: ''
      Compression: 2
      Encryption: 0
      Keep: false
      RestoreSize: 48799744
      Server: primary
      Valid: 1
      WAL: 0
  MajorVersion: 17
  MinorVersion: 0
  Server: primary
  ServerVersion: 0.15.0
```

## Backup information

You can list the information about a backup

```
pgmoneta-cli -c pgmoneta.conf info primary newest
```

and you will get output like

```
Header: 
  ClientVersion: 0.15.0
  Command: 18
  Output: 0
  Timestamp: 20240928065958
Outcome: 
  Status: true
  Time: 00:00:00
Request: 
  Backup: newest
  Server: primary
Response: 
  Backup: 20240928065644
  BackupSize: 8531968
  CheckpointHiLSN: 0
  CheckpointLoLSN: 67108960
  Comments: ''
  Compression: 2
  Elapsed: 20
  Encryption: 0
  EndHiLSN: 0
  EndLoLSN: 67109176
  EndTimeline: 1
  Keep: false
  MajorVersion: 17
  MinorVersion: 0
  NumberOfTablespaces: 0
  RestoreSize: 48799744
  Server: primary
  ServerVersion: 0.15.0
  StartHiLSN: 0
  StartLoLSN: 67108904
  StartTimeline: 1
  Tablespaces: 

  Valid: 1
  WAL: 000000010000000000000004
```

## Create a crontab

Lets create a `crontab` such that a backup is made every day,

```
crontab -e
```

and insert

```
0 6 * * * pgmoneta-cli backup primary
```

for taking a backup every day at 6 am.
