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
  ClientVersion: 0.19.1
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
  ServerVersion: 0.19.1
```

## View backups

We can list all backups for a server with the following command

```
pgmoneta-cli list-backup primary
```

and you will get output like

```
Header:
  ClientVersion: 0.19.1
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
  ServerVersion: 0.19.1
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
  ClientVersion: 0.19.1
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
  ServerVersion: 0.19.1
```

Incremental backups are supported when using [PostgreSQL 17+](https://www.postgresql.org). Note that currently
branching is not allowed for incremental backup -- a backup can have at most 1
incremental backup child.

## Backup information

You can list the information about a backup

```
pgmoneta-cli -c pgmoneta.conf info primary newest
```

and you will get output like

```
Header:
  ClientVersion: 0.19.1
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
  ServerVersion: 0.19.1
  StartHiLSN: 0
  StartLoLSN: 4F000060
  StartTimeline: 1
  Tablespaces: {}
  Valid: yes
  WAL: 00000001000000000000004F
```

## Verify a backup

You can use the command line interface to verify a backup by

```
pgmoneta-cli verify primary oldest /tmp
```

which will verify the oldest backup of the `[primary]` host.

[**pgmoneta**][pgmoneta] creates a SHA512 checksum file(`backup.sha512`) for each backup at the backup root directory, which can be used to verify the integrity of the files.

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

## Encryption

By default, the encryption is disabled. To enable this feature, modify `pgmoneta.conf`:

```
encryption = aes-256-cbc
```

Many encryption modes are supported, see the documentation for the `encryption` property for details.

### Encryption and Decryption Commands

[**pgmoneta**][pgmoneta] use the same key created by `pgmoneta-admin master-key` to encrypt and decrypt files.

Encrypt a file with `pgmoneta-cli encrypt`, the file will be encrypted in place and remove unencrypted file on success.

```sh
pgmoneta-cli -c pgmoneta.conf encrypt '<path-to-your-file>/file.tar.zstd'
```

Decrypt a file with `pgmoneta-cli decrypt`, the file will be decrypted in place and remove encrypted file on success.

```sh
pgmoneta-cli -c pgmoneta.conf decrypt '<path-to-your-file>/file.tar.zstd.aes'
```

`pgmoneta-cli encrypt` and `pgmoneta-cli decrypt` are built to deal with files created by `pgmoneta-cli archive`. It can be used on other files though.

## Annotate

**Add a comment**

You can add a comment by

```
pgmoneta-cli -c pgmoneta.conf annotate primary newest add mykey mycomment
```

**Update a comment**

You can update a comment by

```
pgmoneta-cli -c pgmoneta.conf annotate primary newest update mykey mynewcomment
```

**Remove a comment**

You can remove a comment by

```
pgmoneta-cli -c pgmoneta.conf annotate primary newest remove mykey
```

**View comments**

You can view the comments by

```
pgmoneta-cli -c pgmoneta.conf info primary newest
```

## Archive

In order to create an archive of a backup use

```
pgmoneta-cli -c pgmoneta.conf archive primary newest current /tmp/
```

which will take the latest backup and all Write-Ahead Log (WAL) segments and create
an archive named `/tmp/primary-<timestamp>.tar.zstd`. This archive will contain
an up-to-date copy.

## Crontab

Lets create a `crontab` such that a backup is made every day,

First, take a full backup if you are using PostgreSQL 17+,

```
pgmoneta-cli backup primary
```

then you can use incremental backup for your daily jobs,

```
crontab -e
```

and insert

```
0 6 * * * pgmoneta-cli backup primary latest
```

for taking an incremental backup every day at 6 am.

Otherwise use the full backup in the cron job.
