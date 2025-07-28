## Code Coverage

### Automatic Code Coverage Detection

If both `gcov` and `gcovr` are installed on your system **and the compiler is set to GCC** (regardless of the build type), code coverage will be automatically enabled during the build process. The build scripts will detect these tools and set the appropriate flags. If either tool is missing, or if the compiler is not GCC, code coverage will be skipped and a message will indicate that coverage tools were not found or the compiler is not supported.

### Generating Coverage Reports

After building the project with coverage enabled and running your testsuite, coverage reports will be generated automatically in the `build/coverage` directory if `gcov` and `gcovr` are available.

The following commands are used to generate the reports (executed automatically by the test scripts):

```sh
# Make sure the coverage directory exists
mkdir -p ./coverage

gcovr -r ../src --object-directory . --html --html-details -o ./coverage/index.html
gcovr -r ../src --object-directory . > ./coverage/summary.txt
```

**Important:**
These commands must be run from inside the `build` directory.

- The HTML report will be available at `build/coverage/index.html`
- A summary text report will be available at `build/coverage/summary.txt`

If you want to generate these reports manually after running your own tests, simply run the above commands from your `build` directory.

> **Note:** If the `coverage` directory does not exist, create it first using `mkdir -p ./coverage` before running the coverage commands.
>
> **Important:** `gcovr` only works with GCC builds. If you build the project with Clang, coverage reports will not be generated with `gcovr`.

### Notes

- Make sure you have both `gcov` and `gcovr` installed before building the project to enable coverage.
- If coverage tools are not found, or the compiler is not GCC, coverage generation will be skipped automatically and a message will be shown.
- You can always re-run the coverage commands manually if needed.

---
