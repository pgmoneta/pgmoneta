# GCS Storage Engine Configuration

pgmoneta supports Google Cloud Storage (GCS) as remote storage for backups.

## Google Cloud Storage

```ini
storage_engine = gcs

# Storage location
gcs_bucket = <bucket_name>
gcs_base_dir = <base_directory>

# Service account JSON key; pgmoneta exchanges it for a short-lived access
# token (RFC 7523 JWT Bearer flow) and refreshes it automatically
gcs_credentials_file = <path_to_service_account_key.json>

# Custom endpoint (optional; default: storage.googleapis.com)
gcs_endpoint = <endpoint_host>
gcs_port = <port>

# Enable or disable TLS for GCS (default: off, but enabled for port 443)
gcs_use_tls = <on|off>
```

### Example

```ini
[pgmoneta]
storage_engine = gcs
gcs_bucket = <bucket_name>
gcs_base_dir = <base_directory>
gcs_credentials_file = <path_to_service_account_key.json>
```

### Prerequisites

1. Create a Cloud Storage bucket in the [Google Cloud Console](https://console.cloud.google.com/).
2. Create a service account and grant it the `Storage Object Admin` role on the bucket (or a narrower custom role covering `storage.objects.create`, `storage.objects.get`, `storage.objects.list` and `storage.objects.delete`).
3. Create and download a JSON key for that service account, and point `gcs_credentials_file` at it.

## GCS-Compatible Emulator (fake-gcs-server)

pgmoneta supports [fake-gcs-server](https://github.com/fsouza/fake-gcs-server), a local Google Cloud Storage emulator, for trying out or testing the GCS backend without a real GCP project.

```ini
storage_engine = gcs

# Storage location
gcs_bucket = <bucket_name>
gcs_base_dir = <base_directory>

# Emulator endpoint
gcs_endpoint = <endpoint_host>
gcs_port = <port>

# Enable or disable TLS for GCS (default: off, but enabled for port 443)
gcs_use_tls = <on|off>
```

**Note:** Leave `gcs_credentials_file` unset when using the emulator. fake-gcs-server does not validate the `Authorization` header content, matching how the official GCS client libraries behave when pointed at an emulator — pgmoneta sends requests unauthenticated in that case. Talking to real GCS without a credentials file will fail with 401.

### Example

```ini
[pgmoneta]
storage_engine = gcs
gcs_bucket = <bucket_name>
gcs_base_dir = <base_directory>
gcs_endpoint = <endpoint_host>
gcs_port = <port>
gcs_use_tls = off
```

### fake-gcs-server Tutorial

If you already have fake-gcs-server running with a bucket created, the flow is:

1. Start fake-gcs-server so the JSON API is reachable.
2. Configure pgmoneta to use the emulator endpoint.
3. Start pgmoneta and run a backup.

Start the emulator (plain HTTP, in-memory backend):

```sh
docker run -d --name fake-gcs-server --network host fsouza/fake-gcs-server \
  -scheme http -port 4443 -public-host 127.0.0.1:4443
```

Create the bucket:

```sh
curl -X POST http://127.0.0.1:4443/storage/v1/b \
  -H "Content-Type: application/json" \
  -d '{"name": "<bucket_name>"}'
```

Minimal configuration example:

```ini
[pgmoneta]
storage_engine = gcs
gcs_bucket = <bucket_name>
gcs_base_dir = pgmoneta
gcs_endpoint = 127.0.0.1
gcs_port = 4443
gcs_use_tls = off
```

Start pgmoneta and back up:

```sh
pgmoneta -c pgmoneta.conf -u pgmoneta_users.conf
pgmoneta-cli backup primary
```

The backup's data and metadata files are uploaded to the configured bucket
under `<gcs_base_dir>/<server>/backup/<label>/`.

## Per-Server Configuration

All GCS configuration settings can be specified within a server's configuration section. This allows you to use different GCS buckets or credentials for different PostgreSQL servers.

When a server section has GCS settings, they override the global settings found in the `[pgmoneta]` section. If a server section does not specify a particular GCS setting, it will fall back to the value in the `[pgmoneta]` section.

If the `[pgmoneta]` section has no GCS configuration, then all required GCS settings must be specified in each server section that uses the GCS storage engine.

### Example with Per-Server Override

In this example, `server1` uses the global GCS bucket, but `server2` overrides it with its own bucket and credentials.

```ini
[pgmoneta]
host = localhost
unix_socket_dir = /tmp/
storage_engine = gcs

# Global GCS configuration
gcs_bucket = global-backup-bucket
gcs_base_dir = pgmoneta
gcs_credentials_file = /etc/pgmoneta/global-key.json

[server1]
host = pg1.example.com
port = 5432
user = repl

# server1 uses the global GCS configuration.

[server2]
host = pg2.example.com
port = 5432
user = repl

# server2 overrides some GCS settings.
gcs_bucket = server2-backup-bucket
gcs_credentials_file = /etc/pgmoneta/server2-key.json
```
