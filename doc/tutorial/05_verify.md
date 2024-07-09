## Verify

This tutorial will show you how to verify a backup using [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

### Preface

This tutorial assumes that you have an installation of PostgreSQL 13+ and [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

See [Install pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/01_install.md)
for more detail.

### Verify

```
pgmoneta-cli -c pgmoneta.conf verify primary oldest /tmp
```

will verify the oldest backup of the `[primary]` host.

(`pgmoneta` user)
