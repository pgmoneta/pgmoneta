## Server APIs

### Overview

`pgmoneta` offers a collection of APIs designed to interact with a PostgreSQL cluster. Access to some these APIs is not open to all users by default; instead, the connecting role must possess certain predefined PostgreSQL privileges or roles. These roles ensure that only users with the appropriate level of authorization can invoke sensitive operations, maintaining both security and proper access control within the cluster.

The document will mostly focus on functionalities and things to be cautious about. It may
offer some examples as to how to use the APIs.

### Dependencies

The Server APIs depends only on the installation and configuration of `pgmoneta`. This requires you to [install](https://github.com/pgmoneta/pgmoneta/blob/main/doc/manual/en/02-installation.md) the software and to create a role, provide this role authentication to access the cluster and configure it with a server in the `pgmoneta` configurations.

To simplify this setup and ensure that all prerequisites are met, we recommend following the official [quickstart guide](https://github.com/pgmoneta/pgmoneta/blob/main/doc/manual/en/03-quickstart.md). 


### Additional Grants

Some server API requires the conection user to have additional privileges (see in Server Functions section). To grant a specific predefined role (say <predefined_rolename>) to a user (say 'repl'), execute the following SQL command:

```sh
GRANT <predefined_rolename> TO repl;
```

You can checkout the list of all pre-defined roles available in PostgreSQL 17 [here](https://www.postgresql.org/docs/17/predefined-roles.html).

### APIs

**pgmoneta_server_info**

Fetch trivial information about a configured PostgreSQL server like:
- PostgreSQL version of the server
- status of checksums (enabled/disabled)
- type of server (primary/non-primary)
- configured wal level, wal size, segment size, block size
- status of wal summarize (enabled/disabled)

Privilege:
- Default (all PostgreSQL versions)

**pgmoneta_server_valid**

Check if the server is valid by validating if the given properties are not initialized:
- version
- wal size
- segment size
- block size

Privilege:
- Default (all PostgreSQL versions)

**pgmoneta_server_is_online**

Check if the server is active/online to make connections. `pgmoneta` server structure contains a field: `online`. This API functions as a getter for that field. This function is used in event loops of background processes to ensure that the bg process runs as long as server is online.

Privilege:
- Default (all PostgreSQL versions)

**pgmoneta_server_set_online**

Make a specific server as active/online. Sets the `online` field of server struct to either `true`/`false`.

Privilege:
- Default (all PostgreSQL versions)

**pgmoneta_server_verify_connection**

Verifies if a server connection is possible by simply connecting to the server's configured `<host>:<port>` and disconneting just after connecting.

Privilege: 
- Default (all PostgreSQL versions)

**pgmoneta_server_read_binary_file**

Request a binary chunk of relation file from the server. It takes as an argument:
- path to relational file (data file)
- offset (starting position)
- length (the size of bytes to be retrieved)

Under the hood, this API calls `pg_read_binary_file(text, bigint, bigint, boolean)` admin function which returns the data in `bytea` format. The API is also responsible for serializing incoming `bytea` to binary.

Privileges: 
- pg_read_server_files | SUPERUSER (PostgreSQL +11)
- SUPERUSER (otherwise)

Note: connection user must be granted `EXECUTE` privilege on the function `pg_read_binary_file(text, bigint, bigint, boolean)`

**pgmoneta_server_checkpoint**

Force a checkpoint into the cluster. Under the hood, it first calls `CHECKPOINT;` command and then retrieves the checkpoint LSN by executing `SELECT checkpoint_lsn, timeline_id FROM pg_control_checkpoint();`. Useful while performing backups/incremental backups.

Privileges:
- pg_checkpoint | SUPERUSER (PostgreSQL 15+)
- SUPERUSER (PostgreSQL 13/14)

**pgmoneta_server_file_stat**

Fetch metadata of a data file like size, modification time, creation time etc. Exact meta data structure is as follows:

```c
struct file_stats
{
   size_t size;
   bool is_dir;
   struct tm timetamps[4];
};
```

* `size`: The size of the file at the time of request
* `is_dir`: if the file is a directory (Note: doesn't differentiate between a regular file and a symlink)
* `timestamps`: An array (length 4) of timestamp information related to the file
    * `access time`: time the file was last accessed
    * `modification time`: time the contents of the file was last changed
    * `changed time`: time the perms/ownership of a file was last changed
    * `creation time`: time when the file was created

The timestamps are represented in the format `%Y-%m-%d %H:%M:%S` by the server

Privileges:
- Default (all PostgreSQL versions)

Note: connection user must be granted `EXECUTE` privilege on the function `pg_stat_file(text, boolean)`

**pgmoneta_server_backup_start**

Accepts a backup label as input and invokes `do_pg_backup_start()` internally, this marks the onset of a backup. It returns a LSN which represents the starting point of backup.

Privileges:
- Default (for all PostgreSQL 14+)

Note: connection user must be granted `EXECUTE` privilege on the function `pg_backup_start(text, boolean)` for PostgreSQL version 15+ and `pg_start_backup(text, boolean, boolean)` for PostgreSQL version < 15.

**pgmoneta_server_backup_stop**

Invokes `do_pg_backup_stop()` internally, this marks the end of a backup, that was previously started. It returns a LSN which represents the ending point of backup and also the contents of a `backup_label` file.

Label file contents (returned by this API):
- START WAL LOCATION
- CHECKPOINT LOCATION
- BACKUP METHOD
- BACKUP FROM
- LABEL
- START TIMELINE
- START TIME

Privileges:
- Default (for all PostgreSQL 14+)

Note: connection user must be granted `EXECUTE` privilege on the function `pg_backup_stop(boolean)` for PostgreSQL version 15+ and `pg_stop_backup(boolean, boolean)` for PostgreSQL version < 15.