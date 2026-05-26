# Prometheus metrics

## pgmoneta_state

The state of pgmoneta

## pgmoneta_version

The version of pgmoneta

| Attribute | Description |
| :-------- | :---------- |
| version | The version of pgmoneta |

## pgmoneta_fips

Is pgmoneta running in FIPS mode

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

## pgmoneta_server_online

Is the server in an online state

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_server_primary

Is the server a primary ?

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

## pgmoneta_server_fips_mode

Is FIPS mode enabled on the PostgreSQL server

**Detection method varies by PostgreSQL version:**

**PostgreSQL 14-17:**
- Uses `pgmoneta_ext_fips()` from the pgmoneta_ext extension
- Requires pgmoneta_ext to be installed (optional, returns 0 if not installed)

**PostgreSQL 18+:**
- Uses the `fips_mode()` function from pgcrypto extension
- Requires pgcrypto extension (optional, returns 0 if not installed)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_server_summarize_wal

Is summarize_wal enabled

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_server_extensions_detected

The number of extensions detected on server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_server_extension

Information about installed extensions on server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| extension | The name of the extension |
| version | The version of the extension |
| comment | Description of the extension's functionality |

## pgmoneta_extension_pgmoneta_ext

Status of the pgmoneta extension

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| version | The version of the pgmoneta extension (or "not_installed" if not present) |

## pgmoneta_alert_server_down

> **Note:** All `pgmoneta_alert_*` metrics are opt-in. They are only emitted when `alerts = on` is set in the `[pgmoneta]` section or `alerts = 1` is set on a specific server section.

Alert: server is not online (1 = down, 0 = up)


| Attribute | Description |
| :-------- | :---------- |
| server | The server identifier |
| alert | The alert name (`server_down`) |
| type | The alert type (`state`) |

## pgmoneta_alert_wal_streaming_down

Alert: WAL streaming is not active (1 = down, 0 = streaming)

| Attribute | Description |
| :-------- | :---------- |
| server | The server identifier |
| alert | The alert name (`wal_streaming_down`) |
| type | The alert type (`state`) |

## pgmoneta_alert_no_valid_backup

Alert: no valid backup exists for the server (1 = no valid backup, 0 = ok)

| Attribute | Description |
| :-------- | :---------- |
| server | The server identifier |
| alert | The alert name (`no_valid_backup`) |
| type | The alert type (`state`) |

## pgmoneta_alert_backup_stale

Alert: newest valid backup is older than the retention period (1 = stale, 0 = fresh)

| Attribute | Description |
| :-------- | :---------- |
| server | The server identifier |
| alert | The alert name (`backup_stale`) |
| type | The alert type (`state`) |

## pgmoneta_alert_disk_space_critical

Alert: free disk space below 10% of total (1 = critical, 0 = ok)

| Attribute | Description |
| :-------- | :---------- |
| server | The server identifier |
| alert | The alert name (`disk_space_critical`) |
| type | The alert type (`state`) |

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

## pgmoneta_backup_valid

The number of valid backups for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

## pgmoneta_backup_invalid

The number of invalid backups for a server

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

## pgmoneta_progress_percentage

The workflow progress percentage (0-100) for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| workflow | The current workflow type (e.g. Backup, Restore, Archive) |
| phase | The current workflow phase name |

## pgmoneta_progress_elapsed_time

The elapsed seconds since the current workflow started

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| workflow | The current workflow type (e.g. Backup, Restore, Archive) |
| phase | The current workflow phase name |

## pgmoneta_progress_total

The total units of work in the current workflow phase

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| workflow | The current workflow type (e.g. Backup, Restore, Archive) |
| phase | The current workflow phase name |

## pgmoneta_progress_done

The units of work completed in the current workflow phase

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| workflow | The current workflow type (e.g. Backup, Restore, Archive) |
| phase | The current workflow phase name |

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

# Alerts

`pgmoneta` includes a set of built-in alert metrics that monitor the health of your
[PostgreSQL](https://www.postgresql.org) servers and expose alert states on the Prometheus
`/metrics` endpoint.

Alerts let you detect problems — such as a server going down, WAL streaming stopping, or disk
space running out — early, before they impact your backup operations or data safety.

Each alert evaluates a condition and publishes the result as a Prometheus gauge
(`1` = alert firing, `0` = OK). You can query them directly at the `/metrics` endpoint
or visualize them in a Grafana dashboard. To receive notifications when an alert fires
(for example, via a Slack webhook), see [Alerting with Grafana](#alerting-with-grafana).

## Enabling Alerts

Alert metrics are **opt-in** and can be controlled globally or per server.

Set `alerts = on` in the `[pgmoneta]` section to enable alerts for **all** servers by default.
You can then override that default for individual servers using `alerts = off` in their own
section.

```ini
[pgmoneta]
alerts = on      # enable alerts globally

[server1]
...             # inherits alerts = on → alerts are fired for server1

[server2]
...
alerts = off     # override → alerts are disabled for server2
```

In this example alerts fire for `server1` but are suppressed for `server2`.

## Built-in Alerts

| Metric | Alert Name | Fires when (`value = 1`) |
|--------|------------|--------------------------|
| `pgmoneta_alert_server_down` | `server_down` | The server is not online |
| `pgmoneta_alert_wal_streaming_down` | `wal_streaming_down` | WAL streaming is not active |
| `pgmoneta_alert_no_valid_backup` | `no_valid_backup` | No valid backup exists for the server |
| `pgmoneta_alert_backup_stale` | `backup_stale` | The newest valid backup is older than the retention period |
| `pgmoneta_alert_disk_space_critical` | `disk_space_critical` | Free disk space is below 10% of total |

All alert metrics carry three labels: `server` (the server identifier), `alert` (the alert
name), and `type` (the alert type, currently `state`).

# Alerting with Grafana

In addition to dashboards, Grafana can monitor pgmoneta alert metrics and send notifications
when something goes wrong — for example, when a PostgreSQL server goes down, WAL streaming
stops, or disk space runs critically low.

## Setting Up a Contact Point

Before creating alerts you need to configure where notifications are sent. We use Slack as an
example here, but Grafana also supports Email, PagerDuty, Microsoft Teams, and others (see the
[Grafana Contact Points documentation](https://grafana.com/docs/grafana/latest/alerting/fundamentals/notifications/contact-points/)).

First, create a Slack Incoming Webhook for the channel you want to receive alerts in
(Slack -> Apps -> Incoming Webhooks -> Add New Webhook) (see the
[Slack Incoming Webhooks documentation](https://docs.slack.dev/messaging/sending-messages-using-incoming-webhooks/)).

Then in Grafana, click Alerts & IRM -> Contact points -> "Create contact point".

**Click on "Contact points".**

![image](doc/images/pgmoneta_grafana_alerting_contact_point.png)

**Click on "Create contact point".**

![image](doc/images/pgmoneta_grafana_alerting_create_contact_point.png)

**Set contact point**

Set the name (e.g., `pgmoneta-slack`), choose "Slack" as the integration type, and paste
your Webhook URL. Click "Test" to verify the connection, then "Save contact point".

![image](doc/images/pgmoneta_grafana_alerting_set_contact_point.png)

**Successful connection**

![image](doc/images/pgmoneta_grafana_alerting_slack_success.png)

## Creating an Alert Rule

We will create a `PgMonetaServerDown` alert as an example. This alert fires when a PostgreSQL
server monitored by pgmoneta becomes unreachable.

Click Alerts & IRM -> Alert rules -> "+ New alert rule".

Enter the rule name `PgMonetaServerDown`.

Then define the query and condition. Select your Prometheus datasource (the one scraping
pgmoneta) and enter the following query:

```promql
pgmoneta_alert_server_down == 1
```

Set the condition to fire when the query result is above `0` (i.e., at least one server is
down). Click "Preview" to verify the expression.

Then set the folder and labels. Choose or create a folder (e.g., `pgmoneta-alerts`) and add
labels such as `severity = critical`.

![image](doc/images/pgmoneta_grafana_alerting_new_rule.png)

Next, configure the evaluation behavior. Select or create an evaluation group (e.g.,
`pgmoneta` with interval `1m`). Set the pending period to `1m` — this means the condition
must be true for 1 minute before the alert fires, which avoids false alarms from brief scrape
gaps.

![image](doc/images/pgmoneta_grafana_alerting_evaluation.png)

Then configure the notifications. Select the contact point you created earlier
(e.g., `pgmoneta-slack`).

![image](doc/images/pgmoneta_grafana_alerting_notifications.png)

You can also add annotations to include useful information in the notification message:

*   **Summary**: `pgmoneta server {{ $labels.server }} is down`
*   **Description**: `The PostgreSQL instance {{ $labels.server }} has been unreachable for more than 1 minute.`

![image](doc/images/pgmoneta_grafana_alerting_annotations.png)

Click "Save rule and exit".

![image](doc/images/pgmoneta_grafana_alerting_rule_saved.png)

Verify that the new rule appears in the Grafana Managed Alert Rules list.

![image](doc/images/pgmoneta_grafana_alerting_rule_list.png)

You can repeat these steps for the additional recommended alerts below.

## Recommended Alerts

The following alerts cover the most critical conditions for pgmoneta monitoring. For each
alert, use the same steps described above — only the rule name, query, severity label, and
pending period change.

> **Note:** All `pgmoneta_alert_*` metrics require `alerts = on` to be set in `pgmoneta.conf`.
> See [Enabling Alerts](#enabling-alerts).

### Availability

| Alert Name | Severity | PromQL Expression | Pending Period |
|------------|----------|-------------------|----------------|
| `PgMonetaServerDown` | critical | `pgmoneta_alert_server_down == 1` | 1m |
| `PgMonetaWALStreamingDown` | critical | `pgmoneta_alert_wal_streaming_down == 1` | 2m |

### Backup Health

| Alert Name | Severity | PromQL Expression | Pending Period |
|------------|----------|-------------------|----------------|
| `PgMonetaNoValidBackup` | critical | `pgmoneta_alert_no_valid_backup == 1` | 5m |
| `PgMonetaBackupStale` | warning | `pgmoneta_alert_backup_stale == 1` | 10m |

### Disk

| Alert Name | Severity | PromQL Expression | Pending Period |
|------------|----------|-------------------|----------------|
| `PgMonetaDiskSpaceCritical` | critical | `pgmoneta_alert_disk_space_critical == 1` | 2m |

## Notes

- Always add a `severity` label (`critical` or `warning`) to each rule. This allows you to
  route critical alerts to Slack or PagerDuty and warnings to email using Grafana notification
  policies.
- pgmoneta labels alert metrics with a `server` label. Use `{{ $labels.server }}` in
  annotations to identify which server triggered the alert.
- The pending periods above are sensible defaults. Use longer periods (5m+) for
  backup-health checks where brief transitions are expected, and shorter ones (1m) for
  critical availability checks.
- If you monitor multiple PostgreSQL servers, consider adding `by (server)` to your PromQL
  expressions so each server fires an independent alert:
  ```promql
  pgmoneta_alert_server_down == 1
  ```
  Grafana will automatically split the result into one alert instance per `server` label value.
