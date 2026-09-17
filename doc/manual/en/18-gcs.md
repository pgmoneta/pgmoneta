\newpage

# GCS

## Prerequisites

First of all, you need to have a Google Cloud project, a Cloud Storage bucket and a service account with access to it.

To create a Cloud Storage bucket:

1. Sign in to the [Google Cloud Console][gcp_console] and open Cloud Storage.

2. Choose Create bucket.

3. Provide a globally-unique bucket name.

4. Choose a location and storage class, keeping the defaults if unsure.

5. Choose Create.

To create a service account and a JSON key for pgmoneta to authenticate with:

1. In the navigation pane, open IAM & Admin, then Service Accounts.

2. Choose Create service account, provide a name, and choose Create and continue.

3. Grant the service account the `Storage Object Admin` role (or a narrower custom role covering `storage.objects.create`, `storage.objects.get`, `storage.objects.list` and `storage.objects.delete` on the bucket), then choose Continue and Done.

4. Open the new service account, go to the Keys tab, choose Add key, then Create new key, and select JSON. A JSON key file downloads to your computer — keep it safe, it grants access to the bucket.

## Modify the pgmoneta configuration

You need to have a storage space for 1 backup on your local computer.

Change `pgmoneta.conf` to add

``` ini
storage_engine = gcs
gcs_bucket = your-gcs-bucket-name
gcs_base_dir = directory-where-backups-will-be-stored-in
gcs_credentials_file = /path/to/service-account-key.json
```

under the `[pgmoneta]` section.

`gcs_credentials_file` points at the downloaded service-account JSON key. pgmoneta exchanges it for a short-lived access token (RFC 7523 JWT Bearer flow) on demand and refreshes it automatically; the key itself never leaves local disk.

`gcs_bucket` and `gcs_base_dir` can also be set per server, overriding the global value for that server only.

## fake-gcs-server tutorial

[fake-gcs-server][fake-gcs-server] is a local Google Cloud Storage emulator, useful for trying out or testing the GCS backend without a real GCP project. If it is already running with a bucket created, the flow is:

1. Start fake-gcs-server.
2. Configure pgmoneta for the emulator endpoint.
3. Start pgmoneta and run a backup.

Start the emulator (plain HTTP, in-memory backend):

``` sh
docker run -d --name fake-gcs-server --network host fsouza/fake-gcs-server \
  -scheme http -port 4443 -public-host 127.0.0.1:4443
```

Create the bucket:

``` sh
curl -X POST http://127.0.0.1:4443/storage/v1/b \
  -H "Content-Type: application/json" \
  -d '{"name": "pgmoneta-test-bucket"}'
```

Example `pgmoneta.conf` entries under `[pgmoneta]`. Leave `gcs_credentials_file` unset — fake-gcs-server does not validate the `Authorization` header, matching how the official GCS client libraries behave when pointed at an emulator:

``` ini
storage_engine = gcs
gcs_bucket = pgmoneta-test-bucket
gcs_base_dir = pgmoneta
gcs_endpoint = 127.0.0.1
gcs_port = 4443
gcs_use_tls = off
```

Start pgmoneta and back up:

``` sh
pgmoneta -c pgmoneta.conf -u pgmoneta_users.conf
pgmoneta-cli backup primary
```

The backup's data and metadata files are uploaded to the configured bucket
under `<gcs_base_dir>/<server>/backup/<label>/`.

[gcp_console]: https://console.cloud.google.com/
[fake-gcs-server]: https://github.com/fsouza/fake-gcs-server
