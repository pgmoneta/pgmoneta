# Prometheus metrics

## pgmoneta_state

The state of pgmoneta

1 = Running

## pgmoneta_version

The version of pgmoneta

## pgmoneta_logging_info

The number of INFO statements

## pgmoneta_logging_warn

The number of WARN statements

## pgmoneta_logging_error

The number of ERROR statements

## pgmoneta_logging_fatal

The number of FATAL statements

## pgmoneta_retention_days

The retention of pgmoneta in days

## pgmoneta_retention_weeks

The retention of pgmoneta in weeks

## pgmoneta_retention_months

The retention of pgmoneta in months

## pgmoneta_retention_years

The retention of pgmoneta in years

## pgmoneta_retention_server

The retention of a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |
|parameter 	| days weeks months years            |

## pgmoneta_extension

The version of pgmoneta extension

## pgmoneta_compression

The compression used

0 = None

1 = GZip

2 = ZSTD

3 = LZ4

4 = BZIP2

## pgmoneta_used_space

The disk space used for pgmoneta

## pgmoneta_free_space

The free disk space for pgmoneta

## pgmoneta_total_space

The total disk space for pgmoneta

## pgmoneta_server_valid

Is the server in a valid state

## pgmoneta_wal_streaming

The WAL streaming status of a server

## pgmoneta_server_operation_count

The count of client operations of a server

## pgmoneta_server_failed_operation_count

The count of failed client operations of a server

## pgmoneta_server_last_operation_time

The time of the latest client operation of a server

## pgmoneta_server_last_failed_operation_time

The time of the latest failed client operation of a server

## pgmoneta_wal_shipping

The disk space used for WAL shipping for a server

## pgmoneta_wal_shipping_used_space

The disk space used for everything under the WAL shipping directory of a server

## pgmoneta_wal_shipping_free_space

The free disk space for the WAL shipping directory of a server

## pgmoneta_wal_shipping_total_space

The total disk space for the WAL shipping directory of a server

## pgmoneta_workspace

The disk space used for workspace for a server

## pgmoneta_workspace_free_space

The free disk space for the workspace directory of a server

## pgmoneta_workspace_total_space

The total disk space for the workspace directory of a server

## pgmoneta_hot_standby

The disk space used for hot standby for a server

## pgmoneta_hot_standby_free_space

The free disk space for the hot standby directory of a server

## pgmoneta_hot_standby_total_space

The total disk space for the hot standby directory of a server

## pgmoneta_server_timeline

The current timeline a server is on

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |

## pgmoneta_server_parent_tli

The parent timeline of a timeline on a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |
|tli 	    |The current/previous timeline ID in the server history|

## pgmoneta_server_timeline_switchpos

The WAL switch position of a timeline on a server (showed in hex as a parameter)

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |
|tli  	    |The current/previous timeline ID in the server history|
|walpos 	|The WAL switch position of this timeline |

## pgmoneta_server_workers

The number of workers for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |

## pgmoneta_backup_oldest

The oldest backup for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |

## pgmoneta_backup_newest

The newest backup for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |

## pgmoneta_backup_count

The number of valid backups for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |

## pgmoneta_backup

Is the backup valid for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |
|label 	    |The backup label                    |

## pgmoneta_backup_version

The version of PostgreSQL for a backup

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |
|label 	    |The backup label                    |
|major 	    |The backup PostgreSQL major version |
|minor 	    |The backup PostgreSQL minor version |

## pgmoneta_backup_throughput

The throughput of the backup for a server (bytes/s)

name 	The identifier for the server
label 	The backup label

## pgmoneta_backup_elapsed_time

The backup in seconds for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |
|label 	    |The backup label                    |

## pgmoneta_backup_start_timeline

The starting timeline of a backup for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |
|label 	    |The backup label                    |

## pgmoneta_backup_end_timeline

The ending timeline of a backup for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |
|label 	    |The backup label                    |

## pgmoneta_backup_start_walpos

The starting WAL position of a backup for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |
|label 	    |The backup label                    |
|walpos 	|The backup starting WAL position    |

## pgmoneta_backup_checkpoint_walpos

The checkpoint WAL pos of a backup for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |
|label 	    |The backup label                    |
|walpos 	|The backup checkpoint WAL position  |

## pgmoneta_backup_end_walpos

The ending WAL pos of a backup for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |
|label 	    |The backup label                    |
|walpos 	|The backup ending WAL position      |

## pgmoneta_restore_newest_size

The size of the newest restore for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |

## pgmoneta_backup_newest_size

The size of the newest backup for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |

## pgmoneta_restore_size

The size of a restore for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |
|label 	    |The backup label                    |

## pgmoneta_restore_size_increment

The increment size of a restore for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |
|label 	    |The backup label                    |

## pgmoneta_backup_size

The size of a backup for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |
|label 	    |The backup label                    |

## pgmoneta_backup_compression_ratio

The ratio of backup size to restore size for each backup

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |
|label 	    |The backup label                    |

## pgmoneta_backup_retain

Retain a backup for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |
|label 	    |The backup label                    |

## pgmoneta_backup_total_size

The total size of the backups for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |

## pgmoneta_wal_total_size

The total size of the WAL for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |

## pgmoneta_total_size

The total size for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |

## pgmoneta_active_backup

Is there an active backup for a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |

## pgmoneta_current_wal_file

The current streaming WAL filename of a server

| Attribute | Description |
| :-------- | :--------------------------------- |
|name 	    |The identifier for the server       |
|file 	    |The current WAL filename for this server|

## pgmoneta_current_wal_lsn

The current WAL log sequence number

| Attribute | Description |
| :-------- | :--------------------------------- |
|name       |The identifier for the server       |
|lsn        |The current WAL log sequence number |
