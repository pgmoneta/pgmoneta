## Feature extra

This tutorial will show you how to configure the `extra` feature to retrieve extra files and directories (recursively) from the PostgreSQL server instance.

### Preface

This tutorial assumes that you have an installation of PostgreSQL 13+, [**pgmoneta_ext**](https://github.com/pgmoneta/pgmoneta_ext), and [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

See [Install pgmoneta_ext](https://github.com/pgmoneta/pgmoneta_ext/blob/main/doc/manual/dev-01-install.md) and [Install pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/01_install.md)
for more detail.

### Configuration
In order to use `exta`, simply add

```ini
extra = /tmp/myfile, /tmp/mydir, /tmp/mydirs
```

to the corresponding server section of `pgmoneta.conf`. [**pgmoneta**](https://github.com/pgmoneta/pgmoneta) will retrieve `extra` files and directories (recursively) from the PostgreSQL server instance when performing a backup.

You can use

```sh
pgmoneta-cli -c pgmoneta.conf backup primary
```

to trigger the client to get `extra` files from the server side.

All the files will be stored in the `/extra` directory under the backup directory. For our configuration, they will be stored in the `/tmp/primary/<timestamp>/extra` directory.

### Info

You can use

```sh
pgmoneta-cli -c pgmoneta.conf info primary newest
```

to show all copied `extra` files, something like this (only showing successfully copied files):

```console
Extra                : myfile, ...
```
