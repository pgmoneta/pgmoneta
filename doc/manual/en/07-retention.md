\newpage

# Retention

The retention policy decide for how long a backup should be kept.

## Retention configuration

The configuration is done in the main configuration section, or by a server basis with

| Property | Default | Unit | Required | Description |
| :------- | :------ | :--- | :------- | :---------- |
| retention | 7, - , - , - | Array | No | The retention time in days, weeks, months, years |

which means by default that backups are kept for 7 days.

Defining a retention policy is very important because it defines how you will be able to restore your system
from the backups.

The key is to decide what your policy is, for example

```
7, 4, 12, 5
```

will keep backups for 7 days, one backup each Monday for 4 weeks, one backup for each month, and backups for  5 years.

There are a lot of ways to leave a parameter unspecified. For trailing parameters, you can simply omit them.
And for parameters in between, you can use placeholders. Currently, placeholders we allow are: `-`, `X`, `x`, `0`
or whitespaces (spaces or tabs).

If you want to restore from the latest backup plus the Write-Ahead Log (WAL) then the default [**pgmoneta**][pgmoneta] policy maybe is enough.

Note, that if a backup has an incremental backup child that depends on it, its data will be rolled up to its child before getting deleted.

Current validation rule is:

1. Retention days >= 1
2. If retention months is specified, then 1 <= weeks <= 4, otherwise weeks >= 1
3. If retention years is specified, then 1 <= months <= 12, otherwise months >= 1
4. Retention years >= 1

Please note that the rule above only checks specified parameters, except for days, which should always be specified

The retention check runs every 5 minutes, and will delete one backup per run.

You can change this to every 30 minutes by

```
retention_interval = 1800
```

under the `[pgmoneta]` configuration.

## Delete a backup

```
pgmoneta-cli -c pgmoneta.conf delete [--force] primary oldest
```

will delete the oldest backup on `[primary]`.

When `--force` is used, the backup is deleted immediately, ignoring the configured retention policy.

Note, that if the backup has an incremental backup child that depends on it,
its data will be rolled up to its child before getting deleted.

## Write-Ahead Log shipping

In order to use WAL shipping, simply add

```
wal_shipping = your/local/wal/shipping/directory
```

to the corresponding server section of `pgmoneta.conf`, [**pgmoneta**][pgmoneta] will create the directory if it doesn't exist,
and ship a copy of WAL segments under the subdirectory `your/local/wal/shipping/directory/server_name/wal`.
