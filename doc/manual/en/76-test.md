
## Test

**Dependencies**

To install all the required dependencies, simply run `<PATH_TO_PGMONETA>/pgmoneta/test/check.sh setup`. You need to install docker or podman
separately. The script currently only works on Linux system (we recommend Fedora 39+).

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

To run one particular test case or module, use `<PATH_TO_PGMONETA>/build/test/pgmoneta-test -t <test_case_name>` or `<PATH_TO_PGMONETA>/build/test/pgmoneta-test -m <module_name>`. This requires the test environment to already be set up by `check.sh`.

It is recommended that you **ALWAYS** run tests before raising PR.

**MCTF Framework Overview**

MCTF (Minimal C Test Framework) is pgmoneta's custom test framework designed for simplicity and ease of use.

**What MCTF Can Do:**
- **Automatic test registration** - Tests are automatically registered via constructor attributes
- **Module organization** - Module names are automatically extracted from file names (e.g., `test_utils.c` → module `utils`)
- **Flexible assertions** - Assert macros with optional printf-style error messages
- **Test filtering** - Run tests by name pattern (`-t`) or by module (`-m`)
- **Test skipping** - Skip tests conditionally using `MCTF_SKIP()` when prerequisites aren't met
- **Cleanup pattern** - Structured cleanup using goto labels for resource management
- **Error tracking** - Automatic error tracking with line numbers and custom error messages
- **Multiple assertion types** - Various assertion macros (`MCTF_ASSERT`, `MCTF_ASSERT_PTR_NONNULL`, `MCTF_ASSERT_INT_EQ`, `MCTF_ASSERT_STR_EQ`, etc.)

**What MCTF Cannot Do (Limitations):**
- **No test fixtures** - No automatic setup/teardown per test suite (you must call `pgmoneta_test_setup()` and cleanup manually in each test)
- **No parameterized tests** - Tests cannot be parameterized (each variation needs a separate test function)
- **No parallel or async execution** - Tests run sequentially and synchronously
- **No built-in timeouts** - No framework-level test timeouts (rely on OS-level signals or manual timeouts)
- **No test organization beyond modules** - No test suites, groups, tags, or metadata beyond module names extracted from filenames

**Add Testcases**

To add an additional testcase, go to [testcases](https://github.com/pgmoneta/pgmoneta/tree/main/test/testcases) directory inside the `pgmoneta` project.

Create a `.c` file that contains the test and use the `MCTF_TEST()` macro to define your test. Tests are automatically registered and module names are extracted from file names.

Example test structure:
```c
#include <mctf.h>
#include <tscommon.h>

MCTF_TEST(test_my_feature)
{
   pgmoneta_test_setup();

   // Your test code here
   int result = some_function();
   MCTF_ASSERT(result == 0, cleanup, "function should return 0");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}
```

**MCTF_ASSERT Usage:**

The `MCTF_ASSERT` macro supports optional error messages with printf-style formatting:

- **Without message:** `MCTF_ASSERT(condition, cleanup);` - No error message displayed
- **With simple message:** `MCTF_ASSERT(condition, cleanup, "error message");`
- **With formatted message:** `MCTF_ASSERT(condition, cleanup, "got %d, expected 0", value);`
  - Format arguments (like `value`) are optional and only needed when the message contains format specifiers (`%d`, `%s`, etc.)
  - Multiple format arguments: `MCTF_ASSERT(a == b, cleanup, "expected %d but got %d", expected, actual);`

**Test Resource**

If you have resources as test case input, place them under `test/resource/<your-test-case-name>/`. The `check.sh` will copy them
to `TEST_BASE_DIR/resource/<your-test-case-name>/`, i.e. `/tmp/pgmoneta-test/base/resource/<your-test-case-name>/`. And in your
test code you can open the file from there directly.

**Test Directory**

After running the tests, you will find:

* **pgmoneta log:** `/tmp/pgmoneta-test/log/`
* **postgres log:** `/tmp/pgmoneta-test/pg_log/`, the log level is set to debug5 and has the application name (**pgmoneta**) shown in the log.
* **code coverage reports:** `/tmp/pgmoneta-test/coverage/`

If you need to create a directory runtime, create it under `/tmp/pgmoneta-test/base/`, which also contains `backup/`, `restore/`, `conf` and `workspace/`.
Base directory will be cleaned up after tests are done. In `tscommon.h` you will find `TEST_BASE_DIR` and other global variables holding corresponding directories, 
fetched from environment variables.

**Cleanup**

`<PATH_TO_PGMONETA>/pgmoneta/test/check.sh clean` will remove the testing directory and the built image. If you are using docker, chances are it eats your 
disk space secretly, in that case consider cleaning up using `docker system prune --volume`. Use with caution though as it
nukes all the docker volumes.

**Port**

By default, the pod exposes port 6432 for pgmoneta to connect to. This can be changed by `export PGMONETA_TEST_PORT=<your-port>` before running `check.sh`. Or you
may also run `PGMONETA_TEST_PORT=<your-port> ./check.sh`.

**Configuration**

| Name               | Default | Value           | Description                                         |
|--------------------|---------|-----------------|-----------------------------------------------------|
| PGMONETA_TEST_PORT | 6432    | port number     | The port name pgmoneta use to connect to the db pod |


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
1. Implement function `pgmoneta_test_generate_<type>_v<version>` in `test/libpgmonetatest/tswalutils/tswalutils_<version>.c` (add the function prototype in `test/include/tswalutils.h` as well). This function is responsible for generating the wal record of the type you are adding that mimics a real PostgreSQL wal record.
2. Add this in the body of the testcase 
```c
MCTF_TEST(test_check_point_shutdown_v17)
{
   struct walfile* wf = NULL;
   struct walfile* read_wf = NULL;
   char* path = NULL;

   pgmoneta_test_setup();

   wf = pgmoneta_test_generate_check_point_shutdown_v17();
   MCTF_ASSERT_PTR_NONNULL(wf, cleanup, "failed to generate walfile");

   MCTF_ASSERT(!pgmoneta_write_walfile(wf, 0, path), cleanup, "failed to write walfile to disk");
   MCTF_ASSERT(!pgmoneta_read_walfile(0, path, &read_wf), cleanup, "failed to read walfile from disk");
   MCTF_ASSERT_PTR_NONNULL(read_wf, cleanup, "read walfile is null");
   MCTF_ASSERT(!compare_walfile(wf, read_wf), cleanup, "walfile comparison failed");

cleanup:
   destroy_walfile(wf);
   destroy_walfile(read_wf);
   free(path);
   MCTF_FINISH();
}
```
and replace `pgmoneta_test_generate_check_point_shutdown_v17` with the function you implemented in step 1.

If the record type you are adding has differences between versions of PostgreSQL (13-17), you will need to implement a generate function per version (`generate_rec_x` -> `generate_rec_x_v16`, `generate_rec_x_v17`, etc.).

For the sake of simplicity, please create one test suite per postgres version where the implementation resides in `test/libpgmonetatest/tswalutils/tswalutils_<version>.c` and the testcases in `test/testcases/test_wal_utils.c` and add testcase per record type within this version. You can take a look at [this testcase](../../../../../test/testcases/test_wal_utils.c) for reference.