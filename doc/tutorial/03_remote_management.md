# Remote administration for pgmoneta

This tutorial will show you how to do setup remote management for pgmoneta.

## Preface

This tutorial assumes that you have an installation of PostgreSQL 10+ and pgmoneta.

See [Install pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/01_install.md)
for more detail.

## Change the pgmoneta configuration

Change `pgmoneta.conf` to add

```
management = 5002
```

under the `[pgmoneta]` setting, like

```
[pgmoneta]
...
management = 5002
```

(`pgmoneta` user)

## Add pgmoneta admin

```
pgmoneta-admin -f pgmoneta_admins.conf -U admin -P admin1234 add-user
```

(`pgmoneta` user)

## Restart pgmoneta

Stop pgmoneta and start it again with

```
pgmoneta-cli -c pgmoneta.conf stop
pgmoneta -c pgmoneta.conf -u pgmoneta_users.conf -A pgmoneta_admins.conf
```

(`pgmoneta` user)

## Connect via remote administration interface

```
pgmoneta-cli -h localhost -p 5002 -U admin details
```

and use `admin1234` as the password

(`pgmoneta` user)
