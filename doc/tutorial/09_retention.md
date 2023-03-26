# Retention Policy

This tutorial will show you how to configure retention to retain backups.

## Preface

This tutorial assumes that you have an installation of PostgreSQL 10+ and pgmoneta.

See [Install pgmoneta](https://github.com/pgmoneta/pgmoneta/blob/main/doc/tutorial/01_install.md)
for more detail.

## Retention Setup

In `pgmoneta.conf`, you can use `retention = 7, 4, 12, 5` to configure pgmoneta to retain backups
within the nearest 7 days, 4 weeks, 12 months and 5 years. Specifically, pgmoneta will retain 
all the backups within the nearest 7 days, the latest backup on each Monday within the nearest 4 weeks,
the latest backup on the first day of each month in the last 12 months and the latest backup on the first 
day of each year in the last 5 years.


You can leave trailing parameters unspecified, for example, you can configure retention as `retention = 7, 4`,
then pgmoneta will only keep all the backups within the nearest 7 days and the latest backup on each Monday 
within the nearest 4 weeks. The retention months and years are set to 0 and will not be subject to configuration validation.
If you want to leave some parameter in between unspecified, you can set it to 0, e.g. if `retention = 7, 0, 12`, 
pgmoneta will treat `weeks` unspecified. However, you should always configure `days` to retain the nearest backups. 
If you don't configure retention, by default pgmoneta keeps backups within the nearest 7 days.

## Retention Validation Rule
Current validation rule is:
1. Retention days >= 1
2. If retention months is specified, then 1 <= weeks <= 4, otherwise weeks >= 1
3. If retention years is specified, then 1 <= months <= 12, otherwise months >= 1
4. Retention years >= 1
Please note that the rule above only checks non-zero parameters, except for days, which should always be specified