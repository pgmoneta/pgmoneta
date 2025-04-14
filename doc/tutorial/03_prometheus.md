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