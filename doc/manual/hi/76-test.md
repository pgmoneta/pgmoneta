
## Test

### Local Environment

To ensure the test suite works well, please make sure you have installed PostgreSQL 17.x version installed

For RPM based distributions such as Fedora and RHEL you can add the
[PostgreSQL YUM repository](https://yum.postgresql.org/) and do the install via

```
dnf -qy module disable postgresql
dnf install -y postgresql17 postgresql17-server pgmoneta
```

also make sure that the `initdb`, `pg_ctl` and `psql` are in PATH variable.

**Add Path variable**

Add the `initdb`, `pg_ctl` and `psql` binaries into the environment path.

```
export PATH=$PATH:$(dirname $(which initdb))
export PATH=$PATH:$(dirname $(which psql))
```

**Note:** `initdb` and `pg_ctl` belongs to same binary directory

**Install check library**

Before you test, you need to install the `check` library. If there is no package for `check`, the `CMakeLists.txt` will not compile the test suite. Only after you have installed `check` will it compile the test suite.

``` sh
dnf install -y check check-devel check-static
```

**Build the project**

Make sure to execute the test script inside the project build. Run the following commands if project is not already built.

```
git clone https://github.com/pgmoneta/pgmoneta.git
cd pgmoneta
mkdir build
cd build
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug ..
make
```

You can do

```
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-DCORE_DEBUG" ..
```

in order to get information from the core libraries too.

**Run test suite**

To run the testsuite get inside your build and just execute -

```
./testsuite.sh
```

The script creates the PostgreSQL and pgmoneta environment inside the build itself for example -
- the PostgreSQL related files like the data directory and PostgreSQL configuration will be stored in `pgmoneta-postgres`
- the pgmoneta related files like pgmoneta configuration and users file will be stored in `pgmoneta-testsiute`


It will be the responsibility of the script to clean up the setup environment.

**Note:** You can however view the PostgreSQL and pgmoneta server logs in a separate `log` directory inside build.

In case you see those setup directories like `pgmoneta-postgres` and `pgmoneta-testsiute` in build after successfully executing the script, you should probably run

```
./testsuite.sh clean
```

before running the script again to avoid any inconsistency or errors. The clean subcommand will however clean the logs as well.


**Add testcases**

To add an additional testcase go to [testcases](https://github.com/pgmoneta/pgmoneta/tree/main/test/testcases) directory inside the `pgmoneta` project.

Create a `.c` file that contains the test suite and define the suite inside `/test/include/tssuite.sh`. Add the above created suite to the test runner in [runner.c](https://github.com/pgmoneta/pgmoneta/tree/main/test/runner.c)

**Running Containerized Tests**

The test suite supports containerized testing environments. When you run the C tests in the `build` directory, a Docker (or Podman) container is automatically started, and the test script is executed inside it. This ensures a consistent and isolated environment for testing.

> **Note:** The containerized test option (`ctest`) is only available if Docker or Podman is installed on your system. The CMake configuration will detect this and enable the container test target accordingly.

You have two main options to run the tests:

**1. Using CTest (with Docker/Podman)**

From the `build` directory, simply run:

```sh
ctest -V
```

This will:

* Spin up the Docker/Podman container
* Execute all test scripts inside the container
* Collect logs, coverage, and test outputs

**2. Using coverage.sh**

Alternatively, you can run the coverage script directly:

```sh
./coverage.sh
```

This will:

* Run all tests in the container
* Generate code coverage reports

**Artifacts**

After running the tests, you will find:

* **Test logs:** `build/log/`
* **Coverage reports:** `build/coverage/`
* **CTest logs:** `build/testing/`

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
  START_TEST(test_check_point_shutdown_v17)
  {
    test_walfile(pgmoneta_test_generate_check_point_shutdown_v17);
  }
  END_TEST
```
and replace `pgmoneta_test_generate_check_point_shutdown_v17);` with the function you implemented in step 1.

`test_walfile` is a function that will take care of the reading, writing and comparing of the wal file generated against the one read from the disk.

If the record type you are adding has differences between versions of PostgreSQL (13-17), you will need to implement a generate function per version (`generate_rec_x` -> `generate_rec_x_v16`, `generate_rec_x_v17`, etc.).

For the sake of simplicity, please create one test suite per postgres version where the implementation resides in `test/libpgmonetatest/tswalutils/tswalutils_<version>.c` and the testcases in `test/testcases/test_wal_utils.c` and add testcase per record type within this version. You can take a look at [this testcase](../../../../../test/testcases/test_wal_utils.c) for reference.