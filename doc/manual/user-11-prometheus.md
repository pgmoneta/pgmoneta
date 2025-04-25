\newpage

# Prometheus metrics

[**pgmoneta**][pgmoneta] has the following [Prometheus][prometheus] metrics.

## pgmoneta_state

The state of pgmoneta

## pgmoneta_version

The version of pgmoneta

| Attribute | Description |
| :-------- | :---------- |
| version | The version of pgmoneta |

## pgmoneta_logging_info

The number of INFO logging statements

## pgmoneta_logging_warn

The number of WARN logging statements

## pgmoneta_logging_error

The number of ERROR logging statements

## pgmoneta_logging_fatal

The number of FATAL logging statements

## pgmoneta_retention_days

The retention days of pgmoneta

## pgmoneta_retention_weeks

The retention weeks of pgmoneta

## pgmoneta_retention_months

The retention months of pgmoneta

## pgmoneta_retention_years

The retention years of pgmoneta

## pgmoneta_retention_server

The retention of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| parameter | The day, week, month or year |

## pgmoneta_compression

The compression used

## pgmoneta_used_space

The disk space used for pgmoneta

## pgmoneta_free_space

The free disk space for pgmoneta

## pgmoneta_total_space

The total disk space for pgmoneta

## pgmoneta_wal_shipping

The disk space used for WAL shipping for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_wal_shipping_used_space

The disk space used for WAL shipping of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_wal_shipping_free_space

The free disk space for WAL shipping of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_wal_shipping_total_space

The total disk space for WAL shipping of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_workspace

The disk space used for workspace for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_workspace_free_space

The free disk space for workspace of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_workspace_total_space

The total disk space for workspace of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_hot_standby

The disk space used for hot standby for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_hot_standby_free_space

The free disk space for hot standby of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_hot_standby_total_space

The total disk space for hot standby of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_server_timeline

The current timeline a server is on

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_server_parent_tli

The parent timeline of a timeline on a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| tli | |

## pgmoneta_server_timeline_switchpos

The WAL switch position of a timeline on a server (showed in hex as a parameter)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| tli | |
| walpos | |

## pgmoneta_server_workers

The numbeer of workers for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_server_valid

Is the server in a valid state

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_wal_streaming

The WAL streaming status of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_server_operation_count

The count of client operations of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_server_failed_operation_count

The count of failed client operations of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_server_last_operation_time

The time of the latest client operation of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_server_last_failed_operation_time

The time of the latest failed client operation of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_server_checksums

Are checksums enabled

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_server_summarize_wal

Is summarize_wal enabled

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_extension

The version of pgmoneta extension

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| version | The version of the extension |

## pgmoneta_backup_oldest

The oldest backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_backup_newest

The newest backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_backup_count

The number of valid backups for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_backup

Is the backup valid for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_version

The version of postgresql for a backup

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |
| major | The PostgreSQL major version |
| minor | The PostgreSQL minor version |

## pgmoneta_backup_total_elapsed_time

The backup in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_basebackup_elapsed_time

The duration for basebackup in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_manifest_elapsed_time

The duration for manifest in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_compression_zstd_elapsed_time

The duration for zstd compression in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_compression_gzip_elapsed_time

The duration for gzip compression in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_compression_bzip2_elapsed_time

The duration for bzip2 compression in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_compression_lz4_elapsed_time

The duration for lz4 compression in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_encryption_elapsed_time

The duration for encryption in seconds for a server

| Attribute | Description |
|:----------|:------------|
| name      | The server identifier |
| label     | The backup label |

## pgmoneta_backup_linking_elapsed_time

The duration for linking in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_remote_ssh_elapsed_time

The duration for remote ssh in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_remote_s3_elapsed_time

The duration for remote_s3 in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_remote_azure_elapsed_time

The duration for remote_azure in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_start_timeline

The starting timeline of a backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_end_timeline

The ending timeline of a backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_start_walpos

The starting WAL position of a backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label|
| walpos | The WAL position |

## pgmoneta_backup_checkpoint_walpos

The checkpoint WAL position of a backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |
| walpos | The WAL position |

## pgmoneta_backup_end_walpos

The ending WAL position of a backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |
| walpos | The WAL position |

## pgmoneta_restore_newest_size

The size of the newest restore for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_backup_newest_size

The size of the newest backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_restore_size

The size of a restore for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_restore_size_increment

The size increment of a restore for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_size

The size of a backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_compression_ratio

The ratio of backup size to restore size for each backup

| Attribute | Description           |
|:----------|:----------------------|
| name      | The server identifier |
| label     | The backup label      |

## pgmoneta_backup_throughput

The throughput of the backup for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_basebackup_mbs

The throughput of the basebackup for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_manifest_mbs

The throughput of the manifest for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_compression_zstd_mbs

The throughput of the zstd compression for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_compression_gzip_mbs

The throughput of the gzip compression for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_compression_bzip2_mbs

The throughput of the bzip2 compression for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_compression_lz4_mbs

The throughput of the lz4 compression for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_encryption_mbs

The throughput of the encryption for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_linking_mbs

The throughput of the linking for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_remote_ssh_mbs

The throughput of the remote_ssh for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_remote_s3_mbs

The throughput of the remote_s3 for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_remote_azure_mbs

The throughput of the remote_azure for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

## pgmoneta_backup_retain

Retain backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier|
| label | The backup label |

## pgmoneta_backup_total_size

The total size of the backups for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_wal_total_size

The total size of the WAL for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_total_size

The total size for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_active_backup

Is there an active backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_active_restore

Is there an active restore for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_active_archive

Is there an active archiving for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_active_delete

Is there an active delete for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_active_retention

Is there an active archiving for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_current_wal_file

The current streaming WAL filename of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| file | The WAL file name |

## pgmoneta_current_wal_lsn

The current WAL log sequence number

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| lsn | The Logical Sequence Number |
