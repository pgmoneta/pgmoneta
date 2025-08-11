
## Test

**Dependencies**

To install all the required dependencies, simply run `<PATH_TO_PGMONETA>/pgmoneta/test/check.sh setup`. You need to install docker or podman
separately.

**Running Tests**

To run the tests, simply run `<PATH_TO_PGMONETA>/pgmoneta/test/check.sh`. The script will build a PostgreSQL 17 image the first time you run it,
and start a docker/podman container using the image (so make sure you at least have one of them installed and have the corresponding container engine started). 
The containerized postgres server will have a `repl` user with the replication attribute, a normal user `myuser` and a database `mydb`.

The script then starts pgmoneta and runs tests in your local environment. The tests are run locally so that you may leverage stdout to debug and
the testing environment won't run into weird container environment issues, and so that we can reuse the installed dependencies and cmake cache to speed up development
and debugging.

All the configuration, logs, coverage reports and data will be in `/tmp/pgmoneta-test/`, and a cleanup will run whether 
the script exits normally or not. pgmoneta will be force shutdown if it doesn't terminate normally.
So don't worry about your local setup being tampered. The container will be stopped and removed when the script exits or is terminated. 

It recommended that you **ALWAYS** run tests before raising PR.

**Add testcases**

To add an additional testcase go to [testcases](https://github.com/pgmoneta/pgmoneta/tree/main/test/testcases) directory inside the `pgmoneta` project.

Create a `.c` file that contains the test suite and define the suite inside `/test/include/tssuite.sh`. Add the above created suite to the test runner in [runner.c](https://github.com/pgmoneta/pgmoneta/tree/main/test/runner.c)

**Artifacts**

After running the tests, you will find:

* **pgmoneta log:** `/tmp/pgmoneta-test/log/`
* **postgres log:** `/tmp/pgmoneta-test/pg_log/`, the log level is set to debug5 and has the application name (**pgmoneta**) shown in the log.
* **code coverage reports:** `/tmp/pgmoneta-test/coverage/`

**Cleanup**

`<PATH_TO_PGMONETA>/pgmoneta/test/check.sh clean` will remove the testing directory and the built image. If you are using docker, chances are it eats your 
disk space secretly, in that case consider cleaning up using `docker system prune --volume`. Use with caution though as it
nukes all the docker volumes.

**Deprecated**

You may also notice the `testsuite.sh` script inside the test directory. This script will now only be used for CI.
Please leverage `check.sh` for faster testing and debugging.

### Adding wal-related testcases

While moving towards the goal of building a complete test suite to test pgmoneta wal generation and replay mechanisms, we need to add some testcases that will generate wal files and then replay them. Currently we need to add testcases for the following wal record types:

<details>
<summary>Click to expand</summary>

- **XLOG**
  - XLOG_CHECKPOINT_SHUTDOWN
  - XLOG_CHECKPOINT_ONLINE
  - XLOG_NOOP
  - XLOG_NEXTOID
  - XLOG_SWITCH
  - XLOG_BACKUP_END
  - XLOG_PARAMETER_CHANGE
  - XLOG_RESTORE_POINT
  - XLOG_FPI
  - XLOG_FPI_FOR_HINT
  - XLOG_FPW_CHANGE
  - XLOG_END_OF_RECOVERY
  - XLOG_OVERWRITE_CONTRECORD

- **XACT**
  - XLOG_XACT_COMMIT
  - XLOG_XACT_ABORT
  - XLOG_XACT_PREPARE
  - XLOG_XACT_COMMIT_PREPARED
  - XLOG_XACT_ABORT_PREPARED
  - XLOG_XACT_ASSIGNMENT

- **SMGR**
  - XLOG_SMGR_CREATE
  - XLOG_SMGR_TRUNCATE

- **DBASE**
  - XLOG_DBASE_CREATE
  - XLOG_DBASE_DROP

- **TBLSPC**
  - XLOG_TBLSPC_CREATE
  - XLOG_TBLSPC_DROP

- **RELMAP**
  - XLOG_RELMAP_UPDATE

- **STANDBY**
  - XLOG_RUNNING_XACTS
  - XLOG_STANDBY_LOCK

- **HEAP2**
  - XLOG_HEAP2_FREEZE_PAGE
  - XLOG_HEAP2_VACUUM
  - XLOG_HEAP2_VISIBLE
  - XLOG_HEAP2_MULTI_INSERT
  - XLOG_HEAP2_PRUNE

- **HEAP**
  - XLOG_HEAP_INSERT
  - XLOG_HEAP_DELETE
  - XLOG_HEAP_UPDATE
  - XLOG_HEAP_INPLACE
  - XLOG_HEAP_LOCK
  - XLOG_HEAP_CONFIRM

- **BTREE**
  - XLOG_BTREE_INSERT_LEAF
  - XLOG_BTREE_INSERT_UPPER
  - XLOG_BTREE_INSERT_META
  - XLOG_BTREE_SPLIT_L
  - XLOG_BTREE_SPLIT_R
  - XLOG_BTREE_VACUUM
  - XLOG_BTREE_DELETE
  - XLOG_BTREE_UNLINK_PAGE
  - XLOG_BTREE_NEWROOT
  - XLOG_BTREE_REUSE_PAGE

- **HASH**
  - XLOG_HASH_INIT_META_PAGE
  - XLOG_HASH_INIT_BITMAP_PAGE
  - XLOG_HASH_INSERT
  - XLOG_HASH_ADD_OVFL_PAGE
  - XLOG_HASH_DELETE
  - XLOG_HASH_SPLIT_ALLOCATE_PAGE
  - XLOG_HASH_SPLIT_PAGE
  - XLOG_HASH_SPLIT_COMPLETE
  - XLOG_HASH_MOVE_PAGE_CONTENTS
  - XLOG_HASH_SQUEEZE_PAGE

- **GIN**
  - XLOG_GIN_CREATE_PTREE
  - XLOG_GIN_INSERT
  - XLOG_GIN_SPLIT
  - XLOG_GIN_VACUUM_PAGE
  - XLOG_GIN_DELETE_PAGE
  - XLOG_GIN_UPDATE_META_PAGE
  - XLOG_GIN_INSERT_LISTPAGE
  - XLOG_GIN_DELETE_LISTPAGE

- **GIST**
  - XLOG_GIST_PAGE_UPDATE
  - XLOG_GIST_PAGE_SPLIT
  - XLOG_GIST_DELETE

- **SEQ**
  - XLOG_SEQ_LOG

- **SPGIST**
  - XLOG_SPGIST_ADD_LEAF
  - XLOG_SPGIST_MOVE_LEAFS
  - XLOG_SPGIST_ADD_NODE
  - XLOG_SPGIST_SPLIT_TUPLE
  - XLOG_SPGIST_VACUUM_LEAF
  - XLOG_SPGIST_VACUUM_ROOT
  - XLOG_SPGIST_VACUUM_REDIRECT

- **BRIN**
  - XLOG_BRIN_CREATE_INDEX
  - XLOG_BRIN_UPDATE
  - XLOG_BRIN_SAMEPAGE_UPDATE
  - XLOG_BRIN_REVMAP_EXTEND
  - XLOG_BRIN_DESUMMARIZE

- **REPLORIGIN**
  - XLOG_REPLORIGIN_SET
  - XLOG_REPLORIGIN_DROP

- **LOGICALMSG**
  - XLOG_LOGICAL_MESSAGE

</details>

For every record type, we need to add a test case that will generate the wal record and then replay it. For all types, the reading and writing procedures will be the same, but the generation of the wal record will be different. To add testcases for a specific record type, you will need to follow the procedures mentioned in the previous section. To write the testcase itself, do the following:
1. Implement function `generate_<type>_v<version>*` in `test/wal/wal_<version>.c` (add the function prototype in `test/include/wal_utils.h` as well). This function is responsible for generating the wal record of the type you are adding that mimics a real PostgreSQL wal record.
2. Add this in the body of the testcase 
```c
  START_TEST(check_point_shutdown_v17)
  {
    int rc = pgmoneta_test_walfile(generate_check_point_shutdown_v17);
    ck_assert_int_eq(rc, 0);
  }
  END_TEST
```
and replace `generate_xlog_checkpoint_shutdown_v17);` with the function you implemented in step 1.

`pgmoneta_test_walfile` is a function that will take care of the reading, writing and comparing of the wal file generated against the one read from the disk.

If the record type you are adding has differences between versions of PostgreSQL (13-17), you will need to implement a generate function per version (`generate_rec_x` -> `generate_rec_x_v16`, `generate_rec_x_v17`, etc.).

For the sake of simplicity, please create one test suite per postgres version where the implementation resides in `test/wal/wal_<version>.c` and the testcases in `test/testcases/pgmoneta_test_<whatever>` and add testcase per record type within this version. You can take a look at [this testcase](../../../test/testcases/pgmoneta_test_10.c) for reference.