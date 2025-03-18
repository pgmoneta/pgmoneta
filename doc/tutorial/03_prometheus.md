## Prometheus metrics for pgmoneta

This tutorial will show you how to do setup [Prometheus](https://prometheus.io/) metrics for [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

### Preface

This tutorial assumes that you have an installation of PostgreSQL 13+ and [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

See [Install pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/01_install.md)
for more detail.

### Change the pgmoneta configuration

Change `pgmoneta.conf` to add

```
metrics = 5001
```

under the `[pgmoneta]` setting, like

```
[pgmoneta]
...
metrics = 5001
```

(`pgmoneta` user)

### Restart pgmoneta

Shutdown pgmoneta and start it again with

```
pgmoneta-cli -c pgmoneta.conf shutdown
pgmoneta -c pgmoneta.conf -u pgmoneta_users.conf
```

(`pgmoneta` user)

### Get Prometheus metrics

You can now access the metrics via

```
http://localhost:5001/metrics
```

(`pgmoneta` user)

### TLS support
To add TLS support for Prometheus metrics, first we need a self-signed certificate.

Generate the private key and self-signed certificate (valid for 3650 days)
```
openssl req -x509 -nodes -days 3650 -newkey rsa:2048 \
  -keyout localhost.key -out localhost.crt \
  -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
```

Now install the certificate on your system:
- For Fedora:
```
# Create directory for certificates if it doesn't exist
sudo mkdir -p /etc/pki/ca-trust/source/anchors/

# Copy your certificate
sudo cp localhost.crt /etc/pki/ca-trust/source/anchors/

# Update CA certificates store
sudo update-ca-trust
```

- For Ubuntu:
```
# Create directory for certificates if it doesn't exist
sudo mkdir -p /usr/local/share/ca-certificates/extra

# Copy your certificate
sudo cp localhost.crt /usr/local/share/ca-certificates/extra/

# Update CA certificates store
sudo update-ca-certificates
```

- For MacOS:
    - Open Keychain Access and import the certificate file
    - Set the certificate to "Always Trust"

- For browsers like Chrome/Chromium
```
# Install in Chrome/Chromium's certificate store
certutil -d sql:$HOME/.pki/nssdb -A -t "P,," -n "localhost" -i localhost.crt
```

Edit `pgmoneta.conf` to add the following keys under pgmoneta section:
```
[pgmoneta]
.
.
.
metrics_cert_file=<path-to-cert-file>
metrics_key_file=<path-to-key-file>
```

You can now access metrics at `https://localhost:5001`