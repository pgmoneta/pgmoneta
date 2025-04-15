# Test

## Local Environment

To ensure the test suite works well, please make sure you have installed PostgreSQL 17.x version installed

For RPM based distributions such as Fedora and RHEL you can add the
[PostgreSQL YUM repository](https://yum.postgresql.org/) and do the install via

```
dnf -qy module disable postgresql
dnf install -y postgresql17 postgresql17-server pgmoneta
```

also make sure that the `initdb`, `pg_ctl` and `psql` are in PATH variable.

### Add Path variable

Add the `initdb`, `pg_ctl` and `psql` binaries into the environment path.

```
export PATH=$PATH:$(dirname $(which initdb))
export PATH=$PATH:$(dirname $(which psql))
```

**Note:** `initdb` and `pg_ctl` belongs to same binary directory

### Install check library

Before you test, you need to install the `check` library. If there is no package for `check`, the `CMakeLists.txt` will not compile the test suite. Only after you have installed `check` will it compile the test suite.

``` sh
dnf install -y check check-devel check-static
```

### Build the project

Make sure to execute the test script inside the project build. Run the following commands if project is not already built.

```
git clone https://github.com/pgmoneta/pgmoneta.git
cd pgmoneta
mkdir build
cd build
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug ..
make
```

### Run test suites

To run the testsuites get inside your build and just execute -

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
./testsuite clean
```

before running the script again to avoid any inconsistency or errors. The clean subcommand will however clean the logs as well.


### Add testcases

To add an additional testcase go to [testcases](https://github.com/pgmoneta/pgmoneta/tree/main/test/testcases) directory inside the `pgmoneta` project.

Create a `.c` file that contains the test suite and its corresponding `.h` file (see [pgmoneta_test_1.c](https://github.com/pgmoneta/pgmoneta/tree/main/test/testcases/pgmoneta_test_1.c) or [pgmoneta_test_2.c](https://github.com/pgmoneta/pgmoneta/tree/main/test/testcases/pgmoneta_test_2.c) for reference). Add the above created suite to the test runner in [runner.c](https://github.com/pgmoneta/pgmoneta/tree/main/test/testcases/runner.c)

Also remember to link the new test suite in [CMakeLists](https://github.com/pgmoneta/pgmoneta/blob/main/test/CMakeLists.txt) file inside test directory

```
30:  set(SOURCES
31:    testcases/common.c
32:    testcases/pgmoneta_test_1.c
33:    testcases/pgmoneta_test_2.c
34:    testcases/runner.c
35:  )
```
