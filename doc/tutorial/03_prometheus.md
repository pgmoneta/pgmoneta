## Prometheus metrics for pgmoneta

This tutorial will show you how to do setup [Prometheus](https://prometheus.io/) metrics for [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

### Preface

This tutorial assumes that you have an installation of PostgreSQL 13+ and [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

See [Install pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/01_install.md)
for more detail.

### Change the pgmoneta configuration

Change `pgmoneta.conf` to add

```
metrics = 5001
```

under the `[pgmoneta]` setting, like

```
[pgmoneta]
...
metrics = 5001
```

(`pgmoneta` user)

### Restart pgmoneta

Stop pgmoneta and start it again with

```
pgmoneta-cli -c pgmoneta.conf stop
pgmoneta -c pgmoneta.conf -u pgmoneta_users.conf
```

(`pgmoneta` user)

### Get Prometheus metrics

You can now access the metrics via

```
http://localhost:5001/metrics
```

(`pgmoneta` user)
