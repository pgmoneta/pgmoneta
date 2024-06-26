# Test

## Important

Before starting, please make sure you create two users: `postgres` and `pgmoneta`.

``` sh
sudo adduser postgres
sudo adduser pgmoneta
```

You can simply use `CTest` to test all PostgreSQL versions from 13 to 16. It will automatically run `testsuite.sh` to test `pgmoneta` and `pgmoneta_ext` for each version. The script will automatically install the specified PostgreSQL version, install `pgmoneta` and `pgmoneta_ext`, and use the `check` framework to test their functions. After that, it will automatically clean up everything for you.

``` sh
mkdir build
cd build
cmake ..
make test
```

CTest will output logs into `build/Testing/Temporary/LastTest.log`. If you want to check the specific process, you can review that log file.

`testsuite.sh` accepts three variables. The first one is `dir`, which specifies the `/test` directory location, with a default value of `./`. The second one is `cleanup`, with a default value of `y`. The third one is the PostgreSQL `version`, with a default value of `13`.

## Installation

Enter into the `/test/script` directory.

``` sh
chmod +x installation.sh
```

Run `installation.sh` to install any necessary dependencies and configuration.

``` sh
./installation.sh
```

The script will install PostgreSQL 13 by default. If you prefer to install a specific version, please input the version as shown below. It will automatically download and use the latest patch release number. You can find the source versions at [PostgreSQL GIT repository](https://github.com/postgres/postgres).

``` sh
./installation.sh 14
```

If you want to test different versions, please make sure you have deleted all previous version data, removed the role 'repl', and stopped the PostgreSQL server. Otherwise, it will raise some compatibility errors when you install and configure a new version. It is better to kill all current PostgreSQL processes.

You can use `cleanup.sh` to do this. Make sure to specify the PostgreSQL version when using the script.

> NOTICE:
> The `installation.sh` script will check if `PostgreSQL` and `pgmoneta` are already installed using `pg_config -v` and `pgmoneta -v`. If they exist, the script will skip their installation and configuration, and only install `pgmoneta_ext`. Therefore, if you have already installed them, please ensure they are configured correctly.

## Test pgmoneta

If you installed `pgmoneta` yourself, you need to remove it and use `installation.sh` to install it. Our `test_pgmoneta.sh` script only works when `pgmoneta` is configured through `installation.sh`; otherwise, it will not function correctly.

Enter into the `/test/script` directory.

``` sh
chmod +x test_pgmoneta.sh
```

Run `test_pgmoneta.sh` to test `pgmoneta`'s backup and restore functions.

``` sh
./test_pgmoneta.sh
```

This will automatically use the `check` framework to test if `pgmoneta`'s backup and restore functions work well. Please maintain the directory structure to ensure it functions correctly.

## Test pgmoneta_ext

If you have already installed `pgmoneta_ext` successfully and completed all configurations described in [DEVELOPERS.md](https://github.com/pgmoneta/pgmoneta_ext/blob/main/doc/DEVELOPERS.md#developer-guide), you can skip the **Installation** step above and directly follow the steps below to test the functions in the API extension.

Enter into the `/test/script` directory.

``` sh
chmod +x test_pgmoneta_ext.sh
```

Run `test_pgmoneta_ext.sh` to test the functions in the API extension.

``` sh
./test_pgmoneta_ext.sh
```

The default password for the role `repl` is `secretpassword`. If you have a specific password, please provide that value when running `test_pgmoneta_ext.sh`.

``` sh
./test_pgmoneta_ext.sh 'yourpassword'
```

If you encounter the following error when creating the extension, you may need to inform the system where to find the `libpq.so.5` library. Use the command `echo "/usr/local/pgsql/lib" | sudo tee -a /etc/ld.so.conf` and then update the cache using `sudo ldconfig`. After this, you need to use `test_pgmoneta_ext.sh` to test `pgmoneta_ext` independently.

``` console
ERROR:  could not load library "/usr/local/pgsql/lib/pgmoneta_ext.so": libpq.so.5: cannot open shared object file: No such file or directory
```

If you see the error `could not change directory to "/home/pgmoneta/pgmoneta_ext/test/build": Permission denied`, it doesn't matter. This will not affect the execution of the command; it is just because you used `sudo`, which tries to change the current working directory.
