# pgmoneta-config user guide

`pgmoneta-config` is a standalone command line tool for generating and managing [**pgmoneta**](https://pgmoneta.github.io/) configuration files.

It works directly on the configuration file without requiring a running pgmoneta instance.

```
pgmoneta-config
  Configuration utility for pgmoneta

Usage:
  pgmoneta-config [ -o FILE ] [ COMMAND ]

Options:
  -o, --output FILE   Set the output file path for the init command (default: ./pgmoneta.conf)
  -q, --quiet         Generate default options without prompts (for init)
  -F, --force         Force overwrite if the output file already exists
  -V, --version       Display version information
  -?, --help          Display help

Commands:
  init                              Generate a pgmoneta.conf file interactively
  get  <file> <section> <key>       Get a configuration value
  set  <file> <section> <key> <val> Set a configuration value
  del  <file> <section> [key]       Delete a section or a key
  ls   <file> [section]             List sections or keys
```

## init

Generate a new `pgmoneta.conf` configuration file.

In interactive mode, the user will be prompted for required values such as host, base directory,
compression, logging, and PostgreSQL server connection details.

Command

```sh
pgmoneta-config init
```

### Quiet mode

Use the `-q` flag to generate a template with default values without any prompts. This is useful
for automated deployments, CI/CD pipelines, and scripting.

Command

```sh
pgmoneta-config -q init
```

### Output path

By default the file is written to `./pgmoneta.conf`. Use `-o` to change the output path.

Example

```sh
pgmoneta-config -o /etc/pgmoneta/pgmoneta.conf init
```

### Force overwrite

If the output file already exists, the tool will ask for confirmation in interactive mode,
or refuse to overwrite in quiet mode. Use `-F` to force overwrite.

Example

```sh
pgmoneta-config -q -F -o /etc/pgmoneta/pgmoneta.conf init
```

### Generated file

The generated configuration file will contain a `[pgmoneta]` section with the main settings
and one or more server sections with PostgreSQL connection details.

Example output

```sh
cat pgmoneta.conf
```

```ini
[pgmoneta]
host = *
metrics = 5001
management = 0

base_dir = /home/pgmoneta/backup

compression = zstd

retention = 7

log_type = file
log_level = info
log_path = /tmp/pgmoneta.log

unix_socket_dir = /tmp/

[primary]
host = localhost
port = 5432
user = repl
wal_slot = repl
```

## get

Get a configuration value from a file.

Command

```sh
pgmoneta-config get <file> <section> <key>
```

Example

```sh
pgmoneta-config get pgmoneta.conf pgmoneta compression
```

Output

```
zstd
```

## set

Set a configuration value in a file. Creates the section and key if they do not exist.
Preserves existing comments and formatting.

Command

```sh
pgmoneta-config set <file> <section> <key> <value>
```

Example

```sh
pgmoneta-config set pgmoneta.conf pgmoneta compression lz4
```

To add a new server section

Example

```sh
pgmoneta-config set pgmoneta.conf replica host 192.168.1.10
pgmoneta-config set pgmoneta.conf replica port 5432
pgmoneta-config set pgmoneta.conf replica user repl
```

## del

Delete a specific key from a section, or delete an entire section.

Command

```sh
pgmoneta-config del <file> <section> [key]
```

Delete a key

Example

```sh
pgmoneta-config del pgmoneta.conf pgmoneta compression
```

Delete an entire section

Example

```sh
pgmoneta-config del pgmoneta.conf replica
```

## ls

List all sections within a configuration file, or list all keys within a specific section.

Command

```sh
pgmoneta-config ls <file> [section]
```

List all sections

Example

```sh
pgmoneta-config ls pgmoneta.conf
```

Output

```
pgmoneta
primary
```

List keys in a section

Example

```sh
pgmoneta-config ls pgmoneta.conf pgmoneta
```

Output

```
host
metrics
base_dir
compression
retention
log_type
log_level
log_path
unix_socket_dir
```

## Safety features

### Atomic writes

All write operations (`init`, `set`, `del`) use atomic file replacement. Changes are first
written to a temporary file, flushed to disk with `fsync`, and then atomically renamed over
the target file. This prevents data loss if the process is interrupted.

### File permissions

Generated and modified configuration files are set to `0600` permissions (owner read/write only)
to protect sensitive credentials.

### Root check

The tool refuses to run as the root user to prevent accidental misconfiguration of
system-wide files.

## Comparison with pgmoneta-cli conf

`pgmoneta-cli conf set/get` manages the **live runtime configuration** of a running pgmoneta
daemon over the management socket. Changes take effect immediately in memory.

`pgmoneta-config set/get` manages the **configuration file on disk** without requiring a
running daemon. This is useful for initial setup, provisioning, and offline editing.

Both tools are complementary.
