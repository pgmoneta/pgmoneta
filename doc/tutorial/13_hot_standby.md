## Hot standby

This tutorial will show you how to configure hot standby instances.

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

You can also configure muptiple hot standby directories (up to 8) by providing comma-separated paths:
```
/path/to/hot/standby1,/path/to/hot/standby2,/path/to/hot/standby3
```
[**pgmoneta**](https://github.com/pgmoneta/pgmoneta) will maintain identical copies of the hot standby in all specified directories.

You can use

```
hot_standby_overrides = /your/local/hot/standby/overrides/
```

to override files in the `hot_standby` directories. The overrides will be applied to all hot_standby directories.

### Tablespaces

By default tablespaces will be mapped to a similar path than the original one, for example `/tmp/mytblspc` becomes `/tmp/mytblspchs`.

However, you can use the directory name to map it to another directory, like

```
hot_standby_tablespaces = /tmp/mytblspc->/tmp/mybcktblspc
```

You can also use the `OID` for the key part, like

```
hot_standby_tablespaces = 16392->/tmp/mybcktblspc
```

Multiple tablespaces can be specified using a `,` between them.
