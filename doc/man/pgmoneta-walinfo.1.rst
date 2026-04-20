=====================
pgmoneta-walinfo
=====================

Command line utility to read and display Write-Ahead Log (WAL) files

:Manual section: 1

SYNOPSIS
========

pgmoneta-walinfo <file|directory|tar_archive>

DESCRIPTION
===========

pgmoneta-walinfo is a command line utility to read and display information about PostgreSQL Write-Ahead Log (WAL) files. It supports individual WAL files, directories containing WAL files, TAR archives (optionally compressed with .gz, .lz4, .zstd, or .bz2), and encrypted files (.aes). It provides details of the WAL file in either raw or JSON format.

In addition to standard WAL files, pgmoneta-walinfo also supports:

- Encrypted WAL files (**.aes**)
- Compressed WAL files: **.zstd**, **.gz**, **.lz4**, and **.bz2**
- TAR archives containing WAL files (**.tar**)
- Directories containing WAL files

OPTIONS
=======

-I, --interactive 
  Interactive mode for WAL files with ncurses (see **INTERACTIVE MODE** below).

-c, --config CONFIG_FILE
  Set the path to the pgmoneta.conf file

-u, --users USERS_FILE
  Set the path to the pgmoneta_users.conf file

-o, --output FILE
  Output file

-F, --format raw|json
  Set the output format. Default is ``raw``.

-L, --logfile FILE
  Set the log file

-q, --quiet
  No output only result

--color
  Use colors (on, off)

-v, --verbose
  Output result

-S, --summary
  Show a summary of WAL record counts grouped by resource manager

-V, --version
  Display version information

-t, --translate
  Translate the OIDs in the XLOG records to the corresponding object (database/tablespace/relation) names

-m, --mapping
  The JSON file that contains the mapping of the OIDs to the corresponding object names

-RT, --tablespaces
  Filter on tablspaces

-RD, --databases
  Filter on databases

-RT, --relations
  Filter on relations

-R,   --filter
  Combination of -RT, -RD, -RR

-?, --help
  Display help and usage information.

ARGUMENTS
=========

<file|directory|tar_archive>
  The path to the WAL file, directory containing WAL files, or TAR archive to be analyzed.

INTERACTIVE MODE
================

With **-I** (**--interactive**), pgmoneta-walinfo runs a full-screen **ncurses** viewer. You can browse WAL records in a table, search, filter, mark rows, and export XIDs for **pgmoneta-walfilter**.

**Navigation**

- **Up / Down** -- Move to previous/next record (at file boundaries, moves to adjacent WAL files in the same directory when applicable).
- **PgUp / PgDn** -- Scroll by page.
- **Home / End** -- First / last record in the current list.
- **Enter** -- Open detailed view for the current record.

**File browser**

- Press **l** to open the file browser.
- **Up / Down** moves between directory entries.
- **PgUp / PgDn** jumps by one visible page in the file browser list.
- **Enter** opens a directory or loads the selected WAL file.
- **q** closes the file browser without loading a file.

**Display**

- **t** -- Text mode (human-readable columns).
- **b** -- Binary mode (hex dump).

**Search** (separate from filtering)

- **s** -- Open the search dialog (resource manager, LSN fields, XID, description). **Tab** cycles through known values for RMGR, Start LSN, and End LSN fields.
- **n** / **p** (or **Right** / **Left** when search is active) -- Next / previous search match; **Right** / **Left** can cycle multiple matches in a field.
- **Esc** -- Clear **search** highlights and results (does not clear **filters**).

**Filtering**

- **f** -- Open the **filter** dialog. Criteria you set restrict which rows remain in the list. The dialog is **pre-filled** with the current rules so you can edit or remove them. **Tab** cycles through known values for RMGR, Start LSN, and End LSN fields.
- **u** -- **Clear all filters** and reload the full record list from the WAL file.
- **Ctrl+U** (in the filter dialog only) -- Clear all filter fields.

**Filter semantics**

- **AND across fields**: Every non-empty field you set must match (RMGR, Start LSN range, End LSN range, XID, Relation).
- **OR within a field**: For **RMGR**, **XID**, and **Relation**, you may enter **comma-separated** values; a record matches if it matches **any** token in that field.
- **Start LSN** and **End LSN** in the filter dialog define a **range** together: the record’s start LSN must be greater than or equal to the filter start (if set) and the record’s end LSN must be less than or equal to the filter end (if set). If only one bound is set, only that bound applies.

**Visibility**

- While filters are active, the **header** shows a short summary of the rules; the **status** line shows how many records are shown versus the total loaded (e.g. filtered vs full file).

**Marks and YAML export**

- **m** -- Mark or unmark the current row (for export).
- **g** -- Write a **walfilter** YAML file listing **XIDs** from **marked** rows (for use with **pgmoneta-walfilter**).

**Other**

- **v** -- Verify records (stub in current viewer).
- **l** -- Load a different WAL file or browse directories.
- **?** -- Help overlay listing shortcuts.
- **q** -- Quit.

USAGE
=====

To display information about a WAL file in raw format:

    pgmoneta-walinfo /path/to/walfile

To display information about a WAL file in interactive mode using ncurses
    
    pgmoneta-walinfo -I /path/to/walfile

To display information in JSON format:

    pgmoneta-walinfo -F json /path/to/walfile

To analyze WAL files from a TAR archive:

    pgmoneta-walinfo /path/to/wal_backup.tar.gz

To display information and translate the OIDs to the corresponding object names:

    pgmoneta-walinfo -c pgmoneta_walinfo.conf -t -m /path/to/mapping.json /path/to/walfile
    or
    pgmoneta-walinfo -c pgmoneta_walinfo.conf -t -u /path/to/pgmoneta_users.conf /path/to/walfile

OID TRANSLATION
===============

The `-t` flag enables translation of WAL record OIDs to object names. Use either:

- `-u /path/to/pgmoneta_users.conf` to resolve names from a live PostgreSQL server
- `-m /path/to/mapping.json` to resolve names from a local JSON mapping file

The mapping JSON file must contain `tablespaces`, `databases`, and `relations` sections with object names mapped to OIDs.

If both mappings and server credentials are supplied, the mapping file takes precedence.

CREATING THE MAPPING FILE
=========================

The mapping file can be created manually or generated from a PostgreSQL database.

Each section is an array of objects where the key is the object name and the value is the OID as a string.

**Generating from PostgreSQL:**

Tablespaces:
    SELECT '{' || string_agg('"' || spcname || '": "' || oid || '"', ', ') || '}' FROM pg_tablespace;

Databases:
    SELECT '{' || string_agg('"' || datname || '": "' || oid || '"', ', ') || '}' FROM pg_database WHERE datname NOT IN ('template0', 'template1');

Relations:
    SELECT '{' || string_agg('"' || nspname || '.' || relname || '": "' || c.oid || '"', ', ') || '}'
    FROM pg_class c
    JOIN pg_namespace n ON c.relnamespace = n.oid
    WHERE c.relkind IN ('r', 'p', 'v', 'm', 'f') AND n.nspname NOT IN ('pg_catalog', 'information_schema');

Combine into a JSON file with "tablespaces", "databases", and "relations" arrays.

REPORTING BUGS
==============

pgmoneta is maintained on GitHub at https://github.com/pgmoneta/pgmoneta

COPYRIGHT
=========

pgmoneta-walinfo is licensed under the 3-clause BSD License.

SEE ALSO
========

pgmoneta.conf(5), pgmoneta(1), pgmoneta-admin(1)
