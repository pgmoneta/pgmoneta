\newpage

# Retention policy

The retention policy decide for how long a backup should be kept.

## Retention configuration

The configuration is done in the main configuration section, or by a server basis with

| Property | Default | Unit | Required | Description |
| :------- | :------ | :--- | :------- | :---------- |
| retention | 7, - , - , - | Array | No | The retention time in days, weeks, months, years |

which means by default that backups are kept for 7 days.

## Defining retention

Defining a retention policy is very important because it defines how you will be able to restore your system
from the backups.

The key is to decide what your policy is, for example

```
7, 4, 12, 5
```

to keep backups for 7 days, one backup each Monday for 4 weeks, one backup for each month, and 5 yearly backups.

This is a very hard property to configure since it depends on the size of the database cluster and therefore how big your backups are.

If you want to restore from the latest backup plus the Write-Ahead Log (WAL) then the default [**pgmoneta**](pgmoneta) policy maybe is enough.

Note that currently if a backup has an incremental backup child that depends on it, it will be kept even if it doesn't

fall under retention policy. We will support incremental backup deletion in later releases.

## Retention check

The retention check runs every 5 minutes, and will delete one backup per run.

You can change this to every 30 minutes by

```
retention_interval = 1800
```

under the `[pgmoneta]` configuration.
