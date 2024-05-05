## Retention Policy

This tutorial will show you how to configure retention to retain backups.

### Preface

This tutorial assumes that you have an installation of PostgreSQL 13+ and [**pgmoneta**](https://github.com/pgmoneta/pgmoneta).

See [Install pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/01_install.md)
for more detail.

### Retention Setup

In `pgmoneta.conf`, you can use `retention = 7, 4, 12, 5` to configure [**pgmoneta**](https://github.com/pgmoneta/pgmoneta) to retain backups
within the nearest 7 days, 4 weeks, 12 months and 5 years. Specifically, [**pgmoneta**](https://github.com/pgmoneta/pgmoneta) will retain
all the backups within the nearest 7 days, the latest backup on each Monday within the nearest 4 weeks,
the latest backup on the first day of each month in the last 12 months and the latest backup on the first
day of each year in the last 5 years. If you input more than 4 values, [**pgmoneta**](https://github.com/pgmoneta/pgmoneta) will only read the first 4.

There are a lot of ways to leave a parameter unspecified. For trailing parameters, you can simply omit them. 
And for parameters in between, you can use placeholders. Currently, placeholders we allow are: `-`, `X`, `x`, `0` 
or whitespaces (spaces or tabs). 

Please note that you should always configure `days` to retain the nearest backups.
If you don't configure retention, by default [**pgmoneta**](https://github.com/pgmoneta/pgmoneta) keeps backups within the nearest 7 days and other parameters 
(weeks, months, years) are unspecified. 
Additionally, if you are using prometheus, unspecified values will be shown as `0`.

### Retention Validation Rule

Current validation rule is:

1. Retention days >= 1
2. If retention months is specified, then 1 <= weeks <= 4, otherwise weeks >= 1
3. If retention years is specified, then 1 <= months <= 12, otherwise months >= 1
4. Retention years >= 1
Please note that the rule above only checks specified parameters, except for days, which should always be specified
