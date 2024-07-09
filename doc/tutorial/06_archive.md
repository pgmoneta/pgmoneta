## Archive

This tutorial will show you how to do an archive using [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

### Preface

This tutorial assumes that you have an installation of PostgreSQL 13+ and [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

See [Install pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/01_install.md)
for more detail.

### Creating an archive

```
pgmoneta-cli -c pgmoneta.conf archive primary newest current /tmp/ 
```

will take the latest backup and all Write-Ahead Log (WAL) segments and create
an archive named `/tmp/primary-<timestamp>.tar.zstd`. This archive will contain
an up-to-date copy.

(`pgmoneta` user)
