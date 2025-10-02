=====================
pgmoneta-walfilter
=====================

---------------------------------------------------------------------------------------
Command line utility to filter Write-Ahead Log (WAL) files based on user-defined rules.
---------------------------------------------------------------------------------------

SYNOPSIS
========

**pgmoneta-walfilter** [*OPTIONS*] *yaml_config_file*

DESCRIPTION
===========

**pgmoneta-walfilter** is a command-line utility that reads PostgreSQL Write-Ahead Log (WAL) files from a source directory, filters them based on user-defined rules, recalculates CRC checksums, and writes the filtered WAL files to a target directory.

**Filtering Rules:**

The tool supports two types of filtering:

1. **Transaction ID (XID) filtering**: Filter out specific transaction IDs
   - Specify a list of XIDs to remove from the WAL stream

2. **Operation-based filtering**: Filter out specific database operations
   - **DELETE**: Removes all DELETE operations and their associated transactions

ARGUMENTS
=========

*yaml_config_file*
  Path to the YAML configuration file that specifies source and target directories and settings.

CONFIGURATION
=============

The tool uses a YAML configuration file with the following structure:

.. code-block:: yaml

   source_dir: /path/to/source/backup/directory
   target_dir: /path/to/target/directory
   configuration_file: /etc/pgmoneta/pgmoneta_walfilter.conf
   rules:                             # Optional: filtering rules
     - xids:                          # Filter by transaction IDs
       - 752
       - 753

**Configuration Parameters:**

*source_dir* (required)
  Source directory containing the backup and WAL files

*target_dir* (required)
  Target directory where filtered WAL files will be written

*configuration_file* (optional)
  Path to pgmoneta_walfilter.conf file

*rules* (optional)
  Filtering rules to apply to WAL files

*rules.xids* (optional)
  Array of transaction IDs (XIDs) to filter out

*rules.operations* (optional)
  Array of operations to filter out

HOW IT WORKS
============

1. **Read Configuration**: Parses the YAML configuration file
2. **Load WAL Files**: Reads all WAL files from the source directory
3. **Apply Filters**: Applies the specified filtering rules:

   - Filters out records matching specified operations if specified
   - Filters out records with specified transaction IDs if specified (XIDs)
   - Converts filtered records to NOOP operations

4. **Recalculate CRCs**: Updates checksums for modified records
5. **Write Output**: Saves filtered WAL files to the target directory

EXAMPLES
========

Basic usage with a configuration file:

.. code-block:: bash

   pgmoneta-walfilter [<OPTIONS>] filter_config.yaml

Example with filtering rules:

.. code-block:: yaml

   source_dir: /path/to/source/backup/directory
   target_dir: /path/to/target/directory
   configuration_file: /etc/pgmoneta/pgmoneta_walfilter.conf
   rules:
     - xids:
       - 752
       - 753

**Log Files:**

The tool uses the logging configuration from *pgmoneta_walfilter.conf*. Check the log file specified in the configuration for detailed error messages and processing information.

SEE ALSO
========

**pgmoneta-walinfo** (1), **pgmoneta** (1), **pgmoneta-cli** (1)

For more detailed information, see the pgmoneta documentation at https://pgmoneta.github.io/
