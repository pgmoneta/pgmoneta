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

1. Generate the private key and self-signed certificate (valid for 3650 days)
```
# Generate a private key
openssl genrsa -out server.key 2048

# Create a Certificate Signing Request (CSR)
openssl req -new -key server.key -out server.csr -subj "/CN=localhost"

# Create a self-signed certificate valid for 365 days
openssl x509 -req -days 3650 -in server.csr -signkey server.key -out server.crt

# Use the server certificate as the root certificate (self-signed)
cp server.crt ca.crt
```

2. Edit `pgmoneta.conf` to add the following keys under pgmoneta section:
```
[pgmoneta]
.
.
.
metrics_cert_file=<path-to-cert-file>
metrics_key_file=<path-to-key-file>
metrics_ca_file=<path-to-ca-file>
```

3. You can now access the metrics at `https://localhost:5001` using curl as follows:
```
curl -v -L "https://localhost:5001" --cacert <path_to_ca_file> --cert <path_to_cert_file> --key <path_to_key_file>
```

4. (Optional) If you want to install the certificate on your system:
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

You can now access metrics at `https://localhost:5001`