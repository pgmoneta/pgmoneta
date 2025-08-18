=====================
pgmoneta-walfilter
=====================

--------------------------------------------------------------------
Command line utility to read and process Write-Ahead Log (WAL) files
--------------------------------------------------------------------

SYNOPSIS
========

**pgmoneta-walfilter** *yaml_config_file*

DESCRIPTION
===========

**pgmoneta-walfilter** is a command-line utility that reads PostgreSQL Write-Ahead Log (WAL) files from a source directory, parses them, recalculates CRC checksums, and writes the processed WAL files to a target directory.

The tool supports encrypted and compressed WAL files in various formats including **zstd**, **gz**, **lz4**, and **bz2**.

ARGUMENTS
=========

*yaml_config_file*
  Path to the YAML configuration file that specifies source and target directories, encryption/compression, and settings.

CONFIGURATION
=============

The tool uses a YAML configuration file with the following structure:

.. code-block:: yaml

   source_dir: /path/to/source/backup/directory
   target_dir: /path/to/target/directory
   encryption: aes                    # Optional: encryption method
   compression: gz                    # Optional: compression method
   configuration_file: /etc/pgmoneta/pgmoneta_walfilter.conf

**Configuration Parameters:**

*source_dir* (required)
  Source directory containing the backup and WAL files

*target_dir* (required)
  Target directory where generated WAL files will be written

*encryption* (optional)
  Encryption method used to encrypt the generated WAL files (e.g., "aes")

*compression* (optional)
  Compression method used to compress the generated WAL files (e.g., "gz", "zstd", "lz4", "bz2")

*configuration_file* (optional)
  Path to pgmoneta_walfilter.conf file for logging configuration

HOW IT WORKS
============

1. **Read Configuration**: Parses the YAML configuration file
2. **Load WAL Files**: Reads all WAL files from the source directory
3. **Recalculate CRCs**: Updates checksums for modified records
4. **Write Output**: Saves generated WAL files to the target directory

EXAMPLES
========

Basic usage with a configuration file:

.. code-block:: bash

   pgmoneta-walfilter filter_config.yaml

Where *filter_config.yaml* contains:

.. code-block:: yaml

   source_dir: /home/user/backup/primary/backup/20250913014258
   target_dir: /home/user/generated_wal
   encryption: aes
   compression: gz
   configuration_file: /etc/pgmoneta/pgmoneta_walfilter.conf

**Log Files:**

The tool uses the logging configuration from *pgmoneta_walfilter.conf*. Check the log file specified in the configuration for detailed error messages and processing information.

SEE ALSO
========

**pgmoneta-walinfo** (1), **pgmoneta** (1), **pgmoneta-cli** (1)

For more detailed information, see the pgmoneta documentation at https://pgmoneta.github.io/
