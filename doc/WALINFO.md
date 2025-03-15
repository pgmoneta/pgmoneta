# pgmoneta_walinfo configuration
The `pgmoneta_walinfo` configuration defines the info needed for `walinfo` to work.

The configuration is loaded from either the path specified by the `-c` flag or `/etc/pgmoneta/pgmoneta_walinfo.conf` if -c wasn't provided.

## [pgmoneta_walinfo]

| Property | Default | Unit | Required | Description |
| :------- | :------ | :--- | :------- | :---------- |
| log_type | console | String | No | The logging type (console, file, syslog) |
| log_level | info | String | No | The logging level, any of the (case insensitive) strings `FATAL`, `ERROR`, `WARN`, `INFO` and `DEBUG` (that can be more specific as `DEBUG1` thru `DEBUG5`). Debug level greater than 5 will be set to `DEBUG5`. Not recognized values will make the log_level be `INFO` |
| log_path | pgmoneta.log | String | No | The log file location. Can be a strftime(3) compatible string. |

## Server section

| Property | Default | Unit | Required | Description |
| :------- | :------ | :--- | :------- | :---------- |
| host | | String | Yes | The address of the PostgreSQL instance |
| port | | Int | Yes | The port of the PostgreSQL instance |
| user | | String | Yes | The replication user name |