\newpage

# Backup

## Create a full backup

We can take a full backup from the primary with the following command

```
pgmoneta-cli backup primary
```

and you will get output like

```
Header:
  ClientVersion: 0.18.0
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
  ServerVersion: 0.18.0
```

## View backups

We can list all backups for a server with the following command

```
pgmoneta-cli list-backup primary
```

and you will get output like

```
Header:
  ClientVersion: 0.18.0
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
      Incremental: false
      Keep: false
      RestoreSize: 48799744
      Server: primary
      Valid: 1
      WAL: 0
  MajorVersion: 17
  MinorVersion: 0
  Server: primary
  ServerVersion: 0.18.0
```

## Sorting backups

You can sort the backup list by timestamp using the `--sort` option:

```
pgmoneta-cli list-backup primary --sort asc
```

for ascending order (oldest first), or 

```
pgmoneta-cli list-backup primary --sort desc
```

for descending order (newest first).

## Create an incremental backup

We can take an incremental backup from the primary with the following command

```
pgmoneta-cli backup primary 20240928065644
```

and you will get output like

```
Header:
  ClientVersion: 0.18.0
  Command: 1
  Output: 0
  Timestamp: 20240928065730
Outcome:
  Status: true
  Time: 00:00:20
Request:
  Server: primary
Response:
  Backup: 20240928065750
  BackupSize: 124312
  Compression: 2
  Encryption: 0
  Incremental: true
  MajorVersion: 17
  MinorVersion: 0
  RestoreSize: 48799744
  Server: primary
  ServerVersion: 0.18.0
```

Incremental backups are supported when using [PostgreSQL 17+](https://www.postgresql.org). Note that currently
branching is not allowed for incremental backup -- a backup can have at most 1
incremental backup child.

## View backups

We can list all backups for a server with the following command

```
pgmoneta-cli list-backup primary
```

and you will get output like

```
Header:
  ClientVersion: 0.18.0
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
      Incremental: false
      Keep: false
      RestoreSize: 48799744
      Server: primary
      Valid: 1
      WAL: 0
  MajorVersion: 17
  MinorVersion: 0
  Server: primary
  ServerVersion: 0.18.0
```

## Backup information

You can list the information about a backup

```
pgmoneta-cli -c pgmoneta.conf info primary newest
```

and you will get output like

```
Header:
  ClientVersion: 0.18.0
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
  Incremental: false
  Keep: true
  MajorVersion: 17
  MinorVersion: 0
  NumberOfTablespaces: 0
  RestoreSize: 45.82MB
  Server: primary
  ServerVersion: 0.18.0
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

## Verify backup integrity

pgmoneta creates a SHA512 checksum file(`backup.sha512`) for each backup at the backup root directory, which can be used to verify the integrity of the files.

Using `sha512sum`:
```
cd <path-to-specific-backup-directory>

sha512sum --check backup.sha512
```

The `verification` parameter can be use to control how frequently pgmoneta verifies the integrity of backup files. You can configure this in `pgmoneta.conf`:

```
[pgmoneta]
.
.
.
verification = 3600
```
For example, setting `verification = 3600` or `verification = 1H` will perform integrity checks every hour.
