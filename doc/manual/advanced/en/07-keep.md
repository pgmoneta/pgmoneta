\newpage

# Keeping backups

## List backups

First, we can list our current backups using

```
pgmoneta-cli list-backup primary
```

you will get output like

```
Header:
  ClientVersion: 0.18.1
  Command: list-backup
  Output: text
  Timestamp: 20241018092853
Outcome:
  Status: true
  Time: 00:00:00
Request:
  Server: primary
Response:
  Backups:
    - Backup: 20241012091219
      BackupSize: 6.11MB
      Comments: ''
      Compression: zstd
      Encryption: none
      Keep: false
      RestoreSize: 39.13MB
      Server: primary
      Valid: yes
      WAL: 0
  MajorVersion: 17
  MinorVersion: 0
  Server: primary
  ServerVersion: 0.18.1
```

As you can see backup `20241012091219` has a `Keep` flag of `false`.

## Keep a backup

Now, in order to keep the backup which means that it won't be deleted by the retention policy you
can issue the following command,

```
pgmoneta-cli retain primary 20241012091219
```

and get output like,

```
Header:
  ClientVersion: 0.18.1
  Command: retain
  Output: text
  Timestamp: 20241018094129
Outcome:
  Status: true
  Time: 00:00:00
Request:
  Backup: 20241012091219
  Server: primary
Response:
  Backups:
    - 20241012091219
  Cascade: false
  Comments: ''
  Compression: none
  Encryption: none
  Keep: true
  MajorVersion: 17
  MinorVersion: 0
  Server: primary
  ServerVersion: 0.18.1
  Valid: yes
```

and you can see that the backup has a `Keep` flag of `true`.

## Describe a backup

Now, you may want to add a description to your backup, and as you can see

```
Header:
  ClientVersion: 0.18.1
  Command: retain
  Output: text
  Timestamp: 20241018094129
Outcome:
  Status: true
  Time: 00:00:00
Request:
  Backup: 20241012091219
  Server: primary
Response:
  Backups:
    - 20241012091219
  Cascade: false  
  Comments: ''
  Compression: none
  Encryption: none
  Keep: true
  MajorVersion: 17
  MinorVersion: 0
  Server: primary
  ServerVersion: 0.18.1
  Valid: yes
```

there is a `Comments` field to do that.

You can use the command,

```
pgmoneta-cli annotate primary 20241012091219 add Type "Main fall backup"
```

which will give

```
Header:
  ClientVersion: 0.18.1
  Command: annotate
  Output: text
  Timestamp: 20241018095906
Outcome:
  Status: true
  Time: 00:00:00
Request:
  Action: add
  Backup: 20241012091219
  Comment: Main fall backup
  Key: Type
  Server: primary
Response:
  Backup: 20241012091219
  BackupSize: 6.11MB
  CheckpointHiLSN: 0
  CheckpointLoLSN: 33554560
  Comments: Type|Main fall backup
  Compression: zstd
  Elapsed: 1
  Encryption: none
  EndHiLSN: 0
  EndLoLSN: 33554776
  EndTimeline: 1
  Keep: true
  MajorVersion: 17
  MinorVersion: 0
  NumberOfTablespaces: 0
  RestoreSize: 39.13MB
  Server: primary
  ServerVersion: 0.18.1
  StartHiLSN: 0
  StartLoLSN: 33554472
  StartTimeline: 1
  Tablespaces:

  Valid: yes
  WAL: 000000010000000000000002
```

As you can see the `Comments` field with the `Type` key.

The `annotate` command has `add`, `update` and `remove` commands to modify the `Comments` field.

## Put a backup back into retention

When you don't need a backup anymore you can put into retention again by,

```
pgmoneta-cli expunge primary 20241012091219
```

will give,

```
Header:
  ClientVersion: 0.18.1
  Command: expunge
  Output: text
  Timestamp: 20241018101839
Outcome:
  Status: true
  Time: 00:00:00
Request:
  Backup: 20241012091219
  Server: primary
Response:
  Backup: 20241012091219
  Comments: Type|Main fall backup
  Compression: none
  Encryption: none
  Keep: false
  MajorVersion: 17
  MinorVersion: 0
  Server: primary
  ServerVersion: 0.18.1
  Valid: yes
```

and now, the `Keep` flag is back to `false`.

## Cascade mode
You can retain/expunge the entire incremental backup chain using the `--cascade` option. 
This will retain/expunge all backups along the line until the root full backup.

Say you have an incremental backup chain: `20250625055547 (incremental)` -> `20250625055528 (incremental)` -> `20250625055517(full)`.
Running `pgmoneta-cli retain --cascade primary 20250625055547` will also retain backup `20250625055528` and backup `20250625055517`.

This will give
```
Header:
  ClientVersion: 0.18.1
  Command: retain
  Compression: none
  Encryption: none
  Output: text
  Timestamp: 20250625055654
Outcome:
  Status: true
  Time: 00:00:0.1032
Request:
  Backup: newest
  Cascade: true
  Server: primary
Response:
  BackupSize: 0.00B
  Backups:
    - 20250625055547
    - 20250625055528
    - 20250625055517
  BiggestFileSize: 0.00B
  Cascade: true
  Comments: ''
  Compression: none
  Delta: 0.00B
  Encryption: none
  Keep: true
  MajorVersion: 17
  MinorVersion: 0
  RestoreSize: 0.00B
  Server: primary
  ServerVersion: 0.18.1
  Valid: yes
  WAL: 0.00B
```
