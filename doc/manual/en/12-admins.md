\newpage

# Adminstration access

You can access [**pgmoneta**][pgmoneta] from a remote machine if you enable access.

## Configuration

First, you need to enable remote access by adding

```
management = 5002
```

in `pgmoneta.conf` in the `[pgmoneta]` section.

## Administrators

Next, you will need to add one or more administrators in `pgmoneta_admins.conf` through

```
pgmoneta-admin -f /etc/pgmoneta/pgmoneta_admins.conf user add
```

for example with a user name of `admin` and `secretpassword` as the password.

## Restart pgmoneta

You have to restart [**pgmoneta**][pgmoneta] to make the changes take effect.

## Connect to pgmoneta

Then you will use the `pgmoneta-cli` tool to access [**pgmoneta**][pgmoneta] with

```
pgmoneta-cli -h myhost -p 5002 -U admin status
```

to execute the `status` command after have entered the password.

## Transport Level Security support

You can security the administration level interface by using Transport Level Security (TLS).

It is done by setting the following options,

```
[pgmoneta]
tls_cert_file=/path/to/server.crt
tls_key_file=/path/to/server.key
tls_ca_file=/path/to/root.crt

...
```

in `pgmoneta.conf`.

The client side setup must go into `~/.pgmoneta/` with the following files

```
~/.pgmoneta/pgmoneta.key
~/.pgmoneta/pgmoneta.crt
~/.pgmoneta/root.crt
```

They must have 0600 permission.
