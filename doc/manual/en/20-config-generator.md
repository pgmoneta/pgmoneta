\newpage

# Configuration generator

`pgmoneta-config` is a standalone command line tool for generating and managing pgmoneta
configuration files. It works directly on the configuration file without requiring a
running pgmoneta instance.

## Usage

``` sh
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

Generate a new `pgmoneta.conf` configuration file interactively. The user will be prompted
for required values such as host, base directory, compression, logging, and PostgreSQL
server connection details.

Command

``` sh
pgmoneta-config init
```

### Quiet mode

Use the `-q` flag to generate a template with default values without prompts.

``` sh
pgmoneta-config -q init
```

### Output path

Use `-o` to set the output file path.

``` sh
pgmoneta-config -o /etc/pgmoneta/pgmoneta.conf init
```

### Force overwrite

If the output file already exists, use `-F` to force overwrite.

``` sh
pgmoneta-config -q -F -o /etc/pgmoneta/pgmoneta.conf init
```

### Generated file

The generated configuration file contains a `[pgmoneta]` section with the main settings
and one or more server sections with PostgreSQL connection details.

Example output

``` ini
[pgmoneta]
host = *
metrics = 5001

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

``` sh
pgmoneta-config get <file> <section> <key>
```

Example

``` sh
pgmoneta-config get pgmoneta.conf pgmoneta compression
```

## set

Set a configuration value in a file. Creates the section and key if they do not exist.
Preserves existing comments and formatting.

Command

``` sh
pgmoneta-config set <file> <section> <key> <value>
```

Example

``` sh
pgmoneta-config set pgmoneta.conf pgmoneta compression lz4
```

Adding a new server section

``` sh
pgmoneta-config set pgmoneta.conf replica host 192.168.1.10
pgmoneta-config set pgmoneta.conf replica port 5432
pgmoneta-config set pgmoneta.conf replica user repl
```

## del

Delete a specific key from a section, or delete an entire section.

Command

``` sh
pgmoneta-config del <file> <section> [key]
```

Delete a key

``` sh
pgmoneta-config del pgmoneta.conf pgmoneta compression
```

Delete an entire section

``` sh
pgmoneta-config del pgmoneta.conf replica
```

## ls

List all sections within a configuration file, or list all keys within a specific section.

Command

``` sh
pgmoneta-config ls <file> [section]
```

List all sections

``` sh
pgmoneta-config ls pgmoneta.conf
```

List keys in a section

``` sh
pgmoneta-config ls pgmoneta.conf pgmoneta
```

## Safety features

### Atomic writes

All write operations use atomic file replacement. Changes are first written to a
temporary file, flushed to disk with `fsync`, and then atomically renamed over the
target file.

### File permissions

Generated and modified configuration files are set to `0600` permissions to protect
sensitive credentials.

### Comparison with pgmoneta-cli conf

`pgmoneta-cli conf set/get` manages the live runtime configuration of a running pgmoneta
daemon over the management socket.

`pgmoneta-config set/get` manages the configuration file on disk without requiring a
running daemon. This is useful for initial setup, provisioning, and offline editing.
