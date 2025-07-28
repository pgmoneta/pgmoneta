\newpage

# Prometheus / Grafana

[**pgmoneta**][pgmoneta] support [Prometheus][prometheus] metrics.

We enabled the [Prometheus][prometheus] metrics in the original configuration by setting

```
metrics = 5001
```

in `pgmoneta.conf`.

## Access Prometheus metrics

You can now access the metrics via

```
http://localhost:5001/metrics
```

## Metrics

The following metrics are available.

**pgmoneta_state**

The state of pgmoneta

**pgmoneta_version**

The version of pgmoneta

| Attribute | Description |
| :-------- | :---------- |
| version | The version of pgmoneta |

**pgmoneta_logging_info**

The number of INFO logging statements

**pgmoneta_logging_warn**

The number of WARN logging statements

**pgmoneta_logging_error**

The number of ERROR logging statements

**pgmoneta_logging_fatal**

The number of FATAL logging statements

**pgmoneta_retention_days**

The retention days of pgmoneta

**pgmoneta_retention_weeks**

The retention weeks of pgmoneta

**pgmoneta_retention_months**

The retention months of pgmoneta

**pgmoneta_retention_years**

The retention years of pgmoneta

**pgmoneta_retention_server**

The retention of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| parameter | The day, week, month or year |

**pgmoneta_compression**

The compression used

**pgmoneta_used_space**

The disk space used for pgmoneta

**pgmoneta_free_space**

The free disk space for pgmoneta

**pgmoneta_total_space**

The total disk space for pgmoneta

**pgmoneta_wal_shipping**

The disk space used for WAL shipping for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_wal_shipping_used_space**

The disk space used for WAL shipping of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_wal_shipping_free_space**

The free disk space for WAL shipping of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_wal_shipping_total_space**

The total disk space for WAL shipping of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_workspace**

The disk space used for workspace for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_workspace_free_space**

The free disk space for workspace of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_workspace_total_space**

The total disk space for workspace of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_hot_standby**

The disk space used for hot standby for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_hot_standby_free_space**

The free disk space for hot standby of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_hot_standby_total_space**

The total disk space for hot standby of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_server_timeline**

The current timeline a server is on

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_server_parent_tli**

The parent timeline of a timeline on a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| tli | |

**pgmoneta_server_timeline_switchpos**

The WAL switch position of a timeline on a server (showed in hex as a parameter)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| tli | |
| walpos | |

**pgmoneta_server_workers**

The numbeer of workers for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_server_online**

Is the server online ?

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_server_primary**

Is the server a primary ?

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_server_valid**

Is the server in a valid state

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_wal_streaming**

The WAL streaming status of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_server_operation_count**

The count of client operations of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_server_failed_operation_count**

The count of failed client operations of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_server_last_operation_time**

The time of the latest client operation of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_server_last_failed_operation_time**

The time of the latest failed client operation of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_server_checksums**

Are checksums enabled

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_server_summarize_wal**

Is summarize_wal enabled

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_server_extensions_detected**

The number of extensions detected on server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_server_extension**

Information about installed extensions on server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| extension | The name of the extension |
| version | The version of the extension |
| comment | Description of the extension's functionality |

**pgmoneta_extension_pgmoneta_ext**

Status of the pgmoneta extension

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| version | The version of the pgmoneta extension (or "not_installed" if not present) |

**pgmoneta_backup_oldest**

The oldest backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_backup_newest**

The newest backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_backup_valid**

The number of valid backups for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_backup_invalid**

The number of invalid backups for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_backup**

Is the backup valid for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_version**

The version of postgresql for a backup

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |
| major | The PostgreSQL major version |
| minor | The PostgreSQL minor version |

**pgmoneta_backup_total_elapsed_time**

The backup in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_basebackup_elapsed_time**

The duration for basebackup in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_manifest_elapsed_time**

The duration for manifest in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_compression_zstd_elapsed_time**

The duration for zstd compression in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_compression_gzip_elapsed_time**

The duration for gzip compression in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_compression_bzip2_elapsed_time**

The duration for bzip2 compression in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_compression_lz4_elapsed_time**

The duration for lz4 compression in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_encryption_elapsed_time**

The duration for encryption in seconds for a server

| Attribute | Description |
|:----------|:------------|
| name      | The server identifier |
| label     | The backup label |

**pgmoneta_backup_linking_elapsed_time**

The duration for linking in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_remote_ssh_elapsed_time**

The duration for remote ssh in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_remote_s3_elapsed_time**

The duration for remote_s3 in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_remote_azure_elapsed_time**

The duration for remote_azure in seconds for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_start_timeline**

The starting timeline of a backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_end_timeline**

The ending timeline of a backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_start_walpos**

The starting WAL position of a backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label|
| walpos | The WAL position |

**pgmoneta_backup_checkpoint_walpos**

The checkpoint WAL position of a backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |
| walpos | The WAL position |

**pgmoneta_backup_end_walpos**

The ending WAL position of a backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |
| walpos | The WAL position |

**pgmoneta_restore_newest_size**

The size of the newest restore for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_backup_newest_size**

The size of the newest backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_restore_size**

The size of a restore for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_restore_size_increment**

The size increment of a restore for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_size**

The size of a backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_compression_ratio**

The ratio of backup size to restore size for each backup

| Attribute | Description           |
|:----------|:----------------------|
| name      | The server identifier |
| label     | The backup label      |

**pgmoneta_backup_throughput**

The throughput of the backup for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_basebackup_mbs**

The throughput of the basebackup for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_manifest_mbs**

The throughput of the manifest for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_compression_zstd_mbs**

The throughput of the zstd compression for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_compression_gzip_mbs**

The throughput of the gzip compression for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_compression_bzip2_mbs**

The throughput of the bzip2 compression for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_compression_lz4_mbs**

The throughput of the lz4 compression for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_encryption_mbs**

The throughput of the encryption for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_linking_mbs**

The throughput of the linking for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_remote_ssh_mbs**

The throughput of the remote_ssh for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_remote_s3_mbs**

The throughput of the remote_s3 for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_remote_azure_mbs**

The throughput of the remote_azure for a server (MB/s)

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| label | The backup label |

**pgmoneta_backup_retain**

Retain backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier|
| label | The backup label |

**pgmoneta_backup_total_size**

The total size of the backups for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_wal_total_size**

The total size of the WAL for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_total_size**

The total size for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_active_backup**

Is there an active backup for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_active_restore**

Is there an active restore for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_active_archive**

Is there an active archiving for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_active_delete**

Is there an active delete for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_active_retention**

Is there an active archiving for a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |

**pgmoneta_current_wal_file**

The current streaming WAL filename of a server

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| file | The WAL file name |

**pgmoneta_current_wal_lsn**

The current WAL log sequence number

| Attribute | Description |
| :-------- | :---------- |
| name | The server identifier |
| lsn | The Logical Sequence Number |

## Transport Level Security support

To add TLS support for Prometheus metrics, first we need a self-signed certificate.
1. Generate CA key and certificate
```bash
openssl genrsa -out ca.key 2048
openssl req -x509 -new -nodes -key ca.key -sha256 -days 3650 -out ca.crt -subj "/CN=My Local CA"
```

2. Generate server key and CSR
```bash
openssl genrsa -out server.key 2048
openssl req -new -key server.key -out server.csr -subj "/CN=localhost"
```

3. Create a config file for Subject Alternative Name
```bash
cat > server.ext << EOF
authorityKeyIdentifier=keyid,issuer
basicConstraints=CA:FALSE
keyUsage = digitalSignature, nonRepudiation, keyEncipherment, dataEncipherment
subjectAltName = @alt_names

[alt_names]
DNS.1 = localhost
IP.1 = 127.0.0.1
EOF
```

4. Sign the server certificate with our CA
```bash
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out server.crt -days 3650 -sha256 -extfile server.ext
```

5. Generate client key and certificate
```bash
openssl genrsa -out client.key 2048
openssl req -new -key client.key -out client.csr -subj "/CN=Client Certificate"
openssl x509 -req -in client.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out client.crt -days 3650 -sha256
```

6. Create PKCS#12 file (Optional, needed for browser import)
```bash
openssl pkcs12 -export -out client.p12 -inkey client.key -in client.crt -certfile ca.crt -passout pass:<your_password>
```

Edit `pgmoneta.conf` to add the following keys under pgmoneta section:
```
[pgmoneta]
.
.
.
metrics_cert_file=<path_to_server_cert_file>
metrics_key_file=<path_to_server_key_file>
metrics_ca_file=<path_to_ca_file>
```

You can now access the metrics at `https://localhost:5001` using curl as follows:
```
curl -v -L "https://localhost:5001" --cacert <path_to_ca_file> --cert <path_to_client_cert_file> --key <path_to_client_key_file>
```

(Optional) If you want to access the page through the browser:
- First install the certificates on your system
    - For Fedora:
    ```
    # Create directory if it doesn't exist
    sudo mkdir -p /etc/pki/ca-trust/source/anchors/

    # Copy CA cert to the trust store
    sudo cp ca.crt /etc/pki/ca-trust/source/anchors/

    # Update the CA trust store
    sudo update-ca-trust extract
    ```

    - For Ubuntu:
    ```
    # Copy the CA certificate to the system certificate store
    sudo cp ca.crt /usr/local/share/ca-certificates/

    # Update the CA certificate store
    sudo update-ca-certificates
    ```

    - For MacOS:
        - Open Keychain Access and import the certificate file
        - Set the certificate to "Always Trust"

- For browsers like Firefox
    - Go to Menu → Preferences → Privacy & Security
    - Scroll down to "Certificates" section and click "View Certificates"
    - Go to "Authorities" tab and click "Import"
    - Select your `ca.crt` file
    - Check "Trust this CA to identify websites" and click OK
    - Go to "Your Certificates" tab
    - Click "Import" and select the `client.p12` file
    - Enter the password you set when creating the PKCS#12 file

- For browsers like Chrome/Chromium
    - For client certificates, go to Settings → Privacy and security → Security → Manage certificates
    - Click on "Import" and select your `client.p12` file
    - Enter the password you set when creating it

You can now access metrics at `https://localhost:5001`

## Grafana

Enable the endpoint by adding

```yml
scrape_configs:
  - job_name: 'pgmoneta'
    metrics_path: '/metrics'
    static_configs:
      - targets: ['localhost:5001']
```

to the Grafana configuration.

Then the Prometheus service will query your [**pgmoneta**][pgmoneta] metrics every 15 seconds and package them as time-series data. You can query your [**pgmoneta**][pgmoneta] metrics and watch their changes as time passed in Prometheus web page (default port is `9090`).

![](../images/prometheus_console.jpg)

![](../images/prometheus_graph.jpg)

### Import a Grafana dashboard

Although Prometheus provides capacity of querying and monitoring metrics, we can not customize graphs for each metric and provide a unified view.

As a result, we use Grafana to help us manage all graphs together. First of all, we should install Grafana in the computer you need to monitor [**pgmoneta**][pgmoneta] metrics. You can browse Grafana web page with default port `3000`, default user `admin` and default password `admin`. Then you can create Prometheus data source of [**pgmoneta**][pgmoneta].

![](../images/grafana_datasource.jpg)

Finally you can create dashboard by importing `contrib/grafana/dashboard.json` and monitor metrics about [**pgmoneta**][pgmoneta].

![](../images/grafana_dashboard.jpg)
