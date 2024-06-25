## Hot standby

This tutorial will show you how to configure a hot standby instance.

### Preface

This tutorial assumes that you have an installation of PostgreSQL 13+ and [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

See [Install pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/01_install.md)
for more detail.

### Configuration
In order to use hot standby, simply add

```
hot_standby = /your/local/hot/standby/directory
```

to the corresponding server section of `pgmoneta.conf`. [**pgmoneta**](https://github.com/pgmoneta/pgmoneta) will create the directory if it doesn't exist,
and keep the latest backup in the defined directory.

You can use

```
hot_standby_overrides = /your/local/hot/standby/overrides/
```

to override files in the `hot_standby` directory.

### Tablespaces

By default tablespaces will be mapped to a similar path than the original one, for example `/tmp/mytblspc` becomes `/tmp/mytblspchs`.

However, you can use

```
hot_standby_tablespaces = /tmp/mytblspc->/tmp/mytblspchs
```

to map it to another directory. You can either use the `OID` or the directory name for the key part.

Multiple tablespaces can be specified using a `,` between them.

