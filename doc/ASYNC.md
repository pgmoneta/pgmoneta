# Asynchronous operations

The `pgmoneta-cli` client can start selected long-running operations without
waiting for them to finish on the management connection. The server returns a
job identifier immediately, and the job commands can then be used to inspect
the operation.

Asynchronous execution is opt-in. Without `--async`, commands retain their
normal blocking behavior.

## Supported operations

The following commands support `--async`:

* `backup`
* `restore`
* `archive`
* `delete`

For example:

```sh
pgmoneta-cli --async backup primary
pgmoneta-cli --async backup primary newest
pgmoneta-cli --async restore primary newest current /tmp/restore
pgmoneta-cli --async archive primary newest current /tmp/archive
pgmoneta-cli --async delete primary oldest
```

The acknowledgement contains a job identifier such as
`s0-backup-20260826143022`. Save this identifier to query the job later.

Only one repository operation can run for a server at a time. Starting an
asynchronous operation does not bypass that restriction.

## Job lifecycle

A job can have one of these states:

* `Starting`: the request was accepted and the operation is starting
* `Running`: the operation is running
* `Completed`: the operation finished successfully
* `Failed`: the operation did not finish successfully

Completed and failed jobs are persisted so they can be queried after the
operation has left the active server slot. Job cancellation is not supported.

## Query a job

Query a specific active or persisted job by its identifier:

```sh
pgmoneta-cli job <job_id>
```

For example:

```sh
pgmoneta-cli job s0-backup-20260826143022
```

Query the current job for a server operation, or its latest persisted job when
no matching operation is running:

```sh
pgmoneta-cli job status <server> <backup|restore|archive|delete>
```

For example:

```sh
pgmoneta-cli job status primary backup
```

When progress tracking is enabled for the server, a running job
response also contains the live fields `ProgressState`, `Done`, `Total`,
`Elapsed`, `Percentage`, and `Remaining`. These fields are not persisted with
completed or failed job records.

## List jobs

List active and persisted jobs:

```sh
pgmoneta-cli job list all
```

List jobs belonging to one server:

```sh
pgmoneta-cli job list server primary
```

List jobs by state:

```sh
pgmoneta-cli job list status Running
pgmoneta-cli job list status Completed
pgmoneta-cli job list status Failed
```

## Remove persisted jobs

Remove one persisted job:

```sh
pgmoneta-cli job remove <job_id>
```

Remove all persisted jobs:

```sh
pgmoneta-cli job remove all
```

An active job cannot be removed. Removing a job deletes its stored job record;
it does not remove a backup or undo the completed operation.
