# Replication slot for pgmoneta

This tutorial will show you how to add a replication slot for pgmoneta.

## Preface

This tutorial assumes that you have an installation of PostgreSQL 12+ and pgmoneta.

See [Install pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/01_install.md)
for more detail.

## Add replication slot

```
psql postgres
SELECT pg_create_physical_replication_slot('repl', true, false);
\q
```
Note, it is highly recommended to set `immediately_reserve` to true, 
so that the replication slot keeps the WAL segments on creation.

Alternatively, configure pgmoneta to create replication slots for all servers automatically, 
by adding `create_slot = yes` to the `[pgmoneta]` section in `pgmoneta.conf`, like
```
[pgmoneta]
host = *
metrics = 5001
create_slot = yes
...
```
, or corresponding server section, if you don't want to automatically create slots for all servers, like
```
[primary]
host = localhost
port = 5432
user = repl
create_slot = yes
```

Replication slot(s) named according to `wal_slot` will be created automatically on pgmoneta starts.

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
