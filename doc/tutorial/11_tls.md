## Use of Transport Level Security (TLS)

This tutorial is about using Transport Level Security (TLS) in PostgreSQL, and how it affects [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

**Note, that this tutorial is an example on how to setup a PostgreSQL TLS environment for development use only !**

### Preface

This tutorial assumes that you have an installation of PostgreSQL 13+, OpenSSL and [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

See [Install pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/01_install.md)
for more detail.

### PostgreSQL

Generate the server key

```
openssl genrsa -aes256 8192 > server.key 
```

Remove the passphase

```
openssl rsa -in server.key -out server.key
```

Set the server key permission

```
chmod 400 server.key
```

Generate the server certificate

```
openssl req -new -key server.key -days 3650 -out server.crt -x509 
```

Use the server certificate as the root certificate (self-signed)

```
cp server.crt root.crt
```

In `postgresql.conf` change the following settings

```
listen_addresses = '*'
ssl = on
ssl_ca_file = '/path/to/root.crt'
ssl_cert_file = '/path/to/server.crt'
ssl_key_file = '/path/to/server.key'
ssl_prefer_server_ciphers = on
```

In `pg_hba.conf` change

```
host       all           all           0.0.0.0/0          scram-sha-256
```

to

```
hostssl    all           all           0.0.0.0/0          scram-sha-256
```

In this scenario there are no changes to the `pgmoneta.conf` configuration file.

### Using client certificate

Create the client key
```
openssl ecparam -name prime256v1 -genkey -noout -out client.key
```

Create the client request - remember that the `CN` has to have the name of the replication user
```
openssl req -new -sha256 -key client.key -out client.csr -subj "/CN=repl"
```

Generate the client certificate
```
openssl x509 -req -in client.csr -CA root.crt -CAkey server.key -CAcreateserial -out client.crt -days 3650 -sha256
```

You can test your setup by copying the files into the default PostgreSQL client directory, like

```
mkdir ~/.postgresql
cp client.crt ~/.postgresql/postgresql.crt
cp client.key ~/.postgresql/postgresql.key
cp root.crt ~/.postgresql/ca.crt
chmod 0600 ~/.postgresql/postgresql.crt ~/.postgresql/postgresql.key ~/.postgresql/ca.crt
```

and then test with the `psql` command.

In `pg_hba.conf` change

```
hostssl    all           all           0.0.0.0/0          scram-sha-256
```

to

```
hostssl    all           all           0.0.0.0/0          scram-sha-256 clientcert=verify-ca
```

In `pgmoneta.conf` add the paths to the server in question, like

```
[pgmoneta]
...

[primary]
host=...
port=...
user=repl
tls_cert_file=/path/to/home/.postgresql/postgresql.crt
tls_key_file=/path/to/home/.postgresql/postgresql.key
tls_ca_file=/path/to/home/.postgresql/ca.crt
```


### More information

* [Secure TCP/IP Connections with SSL](https://www.postgresql.org/docs/12/ssl-tcp.html)
* [The pg_hba.conf File](https://www.postgresql.org/docs/12/auth-pg-hba-conf.html)
