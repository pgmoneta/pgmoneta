## Backup and restore

This tutorial will show you how to do a backup and a restore using [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

### Preface

This tutorial assumes that you have an installation of PostgreSQL 13+ and [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

See [Install pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/01_install.md)
for more detail.

### Backup

```
pgmoneta-cli -c pgmoneta.conf backup primary
```

will take a backup of the `[primary]` host.

(`pgmoneta` user)

### List backups

```
pgmoneta-cli -c pgmoneta.conf list-backup primary
```

(`pgmoneta` user)

### Restore

```
pgmoneta-cli -c pgmoneta.conf restore primary newest current /tmp/ 
```

will take the latest backup and all Write-Ahead Log (WAL) segments and restore it
into the `/tmp/primary-<timestamp>` directory for an up-to-date copy.

(`pgmoneta` user)
