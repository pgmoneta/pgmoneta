## WAL shipping

This tutorial will show you how to configure WAL shipping, so that if the backup server
crashes, you will be able to use an older archive to do Point-in-Time recovery with WAL segments shipped to another server/local directory.

Note that this feature is still at its early stages, that it currently only ships WAL segments to another LOCAL directory.
You do not need to make sure the directory is unique for each server, the WAL copy will be saved under the subdirectory `server_name/wal/`

### Preface

This tutorial assumes that you have an installation of PostgreSQL 13+ and [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

See [Install pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/01_install.md)
for more detail.

### Configuration

In order to use WAL shipping, simply add

```
wal_shipping = your/local/wal/shipping/directory
```

to the corresponding server section of `pgmoneta.conf`, [**pgmoneta**](https://github.com/pgmoneta/pgmoneta) will create the directory if it doesn't exist, 
and ship a copy of WAL segments under the subdirectory `your/local/wal/shipping/directory/server_name/wal`.

### Prometheus

You can monitor the disk usage regarding WAL shipping using prometheus. Here are some metrics.

```
pgmoneta_wal_shipping(name)  -- size of the WAL shipping for the server (wal/shipping/directory/server_name/wal)
pgmoneta_wal_shipping_used_space -- size of everything under the WAL shipping directory; this could include archives (wal/shipping/directory/server_name/)
pgmoneta_wal_shipping_free_space -- free size of the WAL shipping directory for the server (wal/shipping/directory/server_name/)
pgmoneta_wal_shipping_total_space -- total size of the WAL shipping directory for the server (wal/shipping/directory/server_name/)
```
