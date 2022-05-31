# Replication slot for pgmoneta

This tutorial will show you how to do use a replication slot for pgmoneta.

## Preface

This tutorial assumes that you have an installation of PostgreSQL 10+ and pgmoneta.

See [Install pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/01_install.md)
for more detail.

## Add replication slot

```
psql postgres
SELECT pg_create_physical_replication_slot('repl');
\q
```

(`postgres` user)

## Change the pgmoneta configuration

Change `pgmoneta.conf` to add

```
wal_slot = repl
```

under the `[primary]` server setting, like

```
[primary]
host = localhost
port = 5432
user = repl
wal_slot = repl
```

(`pgmoneta` user)

## Restart pgmoneta

Stop pgmoneta and start it again with

```
pgmoneta-cli -c pgmoneta.conf stop
pgmoneta -c pgmoneta.conf -u pgmoneta_users.conf
```

(`pgmoneta` user)
