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
  ClientVersion: 0.15.1
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
  ServerVersion: 0.15.1
```

## View backups

We can list all backups for a server with the following command

```
pgmoneta-cli list-backup primary
```

and you will get output like

```
Header: 
  ClientVersion: 0.15.1
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
  ServerVersion: 0.15.1
```

## Backup information

You can list the information about a backup

```
pgmoneta-cli -c pgmoneta.conf info primary newest
```

and you will get output like

```
Header:
  ClientVersion: 0.15.1
  Command: info
  Output: text
  Timestamp: 20241025163541
Outcome:
  Status: true
  Time: 00:00:00
Request:
  Backup: newest
  Server: primary
Response:
  Backup: 20241019163516
  BackupSize: 6.54MB
  CheckpointHiLSN: 0
  CheckpointLoLSN: 4F0000B8
  Comments: ''
  Compression: zstd
  Elapsed: 4
  Encryption: none
  EndHiLSN: 0
  EndLoLSN: 4F000158
  EndTimeline: 1
  Keep: true
  MajorVersion: 17
  MinorVersion: 0
  NumberOfTablespaces: 0
  RestoreSize: 45.82MB
  Server: primary
  ServerVersion: 0.15.1
  StartHiLSN: 0
  StartLoLSN: 4F000060
  StartTimeline: 1
  Tablespaces: {}
  Valid: yes
  WAL: 00000001000000000000004F
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
