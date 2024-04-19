/*
 * Copyright (C) 2024 The pgmoneta community
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PGMONETA_UTILS_H
#define PGMONETA_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>
#include <info.h>
#include <message.h>
#include <workers.h>

#include <stdlib.h>

#define SHORT_TIME_LENGHT 8 + 1
#define LONG_TIME_LENGHT  16 + 1
#define UTC_TIME_LENGTH   29 + 1

/** @struct
 * Defines the signal structure
 */
struct signal_info
{
   struct ev_signal signal; /**< The libev base type */
   int slot;                /**< The slot */
};

/** @struct
 * Defines pgmoneta commands.
 * The necessary fields are marked with an ">".
 *
 * Fields:
 * > command: The primary name of the command.
 * > subcommand: The subcommand name. If there is no subcommand, it should be filled with an empty literal string.
 * > accepted_argument_count: An array defining all the number of arguments this command accepts.
 *    Each entry represents a valid count of arguments, allowing the command to support overloads.
 * - default_argument: A default value for the command argument, used when no explicit argument is provided.
 * - log_message: A template string for logging command execution, which can include placeholders for dynamic values.
 * > action: A value indicating the specific action.
 * - mode: A value specifying the mode of operation or context in which the command applies.
 * > deprecated: A flag indicating whether this command is deprecated.
 * - deprecated_by: A string naming the command that replaces the deprecated command.
 *
 * This struct is key to extending and maintaining the command processing functionality in pgmoneta,
 * allowing for clear definition and handling of all supported commands.
 */
struct pgmoneta_command
{
   const char* command;
   const char* subcommand;
   const int accepted_argument_count[MISC_LENGTH];

   const int action;
   const char* default_argument;
   const char* log_message;

   /* Deprecation information */
   bool deprecated;
   unsigned int deprecated_since_major;
   unsigned int deprecated_since_minor;
   const char* deprecated_by;
};

/** @struct
 * Holds parsed command data.
 *
 * Fields:
 * - cmd: A pointer to the command struct that was parsed.
 * - args: An array of pointers to the parsed arguments of the command (points to argv).
 */
struct pgmoneta_parsed_command
{
   const struct pgmoneta_command* cmd;
   char* args[MISC_LENGTH];
};

/**
 * Utility function to parse the command line
 * and search for a command.
 *
 * The function tries to be smart, in helping to find out
 * a command with the possible subcommand.
 *
 * @param argc the command line counter
 * @param argv the command line as provided to the application
 * @param offset the position at which the next token out of `argv`
 * has to be read. This is usually the `optind` set by getopt_long().
 * @param parsed an `struct pgmoneta_parsed_command` to hold the parsed
 * data. It is modified inside the function to be accessed outside.
 * @param command_table array containing one `struct pgmoneta_command` for
 * every possible command.
 * @param command_count number of commands in `command_table`.
 * @return true if the parsing of the command line was succesful, false
 * otherwise
 *
 */
bool
parse_command(int argc,
              char** argv,
              int offset,
              struct pgmoneta_parsed_command* parsed,
              const struct pgmoneta_command command_table[],
              size_t command_count);

/**
 * Get the request identifier
 * @param msg The message
 * @return The identifier
 */
int32_t
pgmoneta_get_request(struct message* msg);

/**
 * Extract the user name and database from a message
 * @param msg The message
 * @param username The resulting user name
 * @param database The resulting database
 * @param appname The resulting application_name
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_extract_username_database(struct message* msg, char** username, char** database, char** appname);

/**
 * Extract a message from a message
 * @param type The message type to be extracted
 * @param msg The message
 * @param extracted The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_extract_message(char type, struct message* msg, struct message** extracted);

/**
 * Extract a error message field from a message
 * @param type The error message field type to be extracted
 * @param msg The error message
 * @param extracted The resulting error message field
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_extract_error_fields(char type, struct message* msg, char** extracted);

/**
 * Extract a message based on an offset
 * @param offset The offset
 * @param data The data segment
 * @param extracted The resulting message
 * @return The next offset
 */
size_t
pgmoneta_extract_message_offset(size_t offset, void* data, struct message** extracted);

/**
 * Extract a message based on a type
 * @param type The type
 * @param data The data segment
 * @param data_size The data size
 * @param extracted The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_extract_message_from_data(char type, void* data, size_t data_size, struct message** extracted);

/**
 * Read a byte
 * @param data Pointer to the data
 * @return The byte
 */
signed char
pgmoneta_read_byte(void* data);

/**
 * Read an int16
 * @param data Pointer to the data
 * @return The int16
 */
int16_t
pgmoneta_read_int16(void* data);

/**
 * Read an int32
 * @param data Pointer to the data
 * @return The int32
 */
int32_t
pgmoneta_read_int32(void* data);

/**
 * Read an int64
 * @param data Pointer to the data
 * @return The int64
 */
int64_t
pgmoneta_read_int64(void* data);

/**
 * Write a byte
 * @param data Pointer to the data
 * @param b The byte
 */
void
pgmoneta_write_byte(void* data, signed char b);

/**
 * Write an int32
 * @param data Pointer to the data
 * @param i The int32
 */
void
pgmoneta_write_int32(void* data, int32_t i);

/**
 * Write an int64
 * @param data Pointer to the data
 * @param i The int64
 */
void
pgmoneta_write_int64(void* data, int64_t i);

/**
 * Read a string
 * @param data Pointer to the data
 * @return The string
 */
char*
pgmoneta_read_string(void* data);

/**
 * Write a string
 * @param data Pointer to the data
 * @param s The string
 */
void
pgmoneta_write_string(void* data, char* s);

/**
 * Compare two strings
 * @param str1 The first string
 * @param str2 The second string
 * @return true if the strings are the same, otherwise false
 */
bool
pgmoneta_compare_string(const char* str1, const char* str2);

/**
 * Is the machine big endian ?
 * @return True if big, otherwise false for little
 */
bool
pgmoneta_bigendian(void);

/**
 * Swap
 * @param i The value
 * @return The swapped value
 */
unsigned int
pgmoneta_swap(unsigned int i);

/**
 * Print the available libev engines
 */
void
pgmoneta_libev_engines(void);

/**
 * Get the constant for a libev engine
 * @param engine The name of the engine
 * @return The constant
 */
unsigned int
pgmoneta_libev(char* engine);

/**
 * Get the name for a libev engine
 * @param val The constant
 * @return The name
 */
char*
pgmoneta_libev_engine(unsigned int val);

/**
 * Get the home directory
 * @return The directory
 */
char*
pgmoneta_get_home_directory(void);

/**
 * Get the user name
 * @return The user name
 */
char*
pgmoneta_get_user_name(void);

/**
 * Get a password from stdin
 * @return The password
 */
char*
pgmoneta_get_password(void);

/**
 * BASE64 encode a string
 * @param raw The string
 * @param raw_length The length of the raw string
 * @param encoded The encoded string
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_base64_encode(char* raw, int raw_length, char** encoded);

/**
 * BASE64 decode a string
 * @param encoded The encoded string
 * @param encoded_length The length of the encoded string
 * @param raw The raw string
 * @param raw_length The length of the raw string
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_base64_decode(char* encoded, size_t encoded_length, char** raw, int* raw_length);

/**
 * Set process title.
 *
 * The function will autonomously check the update policy set
 * via the configuration option `update_process_title` and
 * will do nothing if the setting is `never`.
 * In the case the policy is set to `strict`, the process title
 * will not overflow the initial command line length (i.e., strlen(argv[*]))
 * otherwise it will do its best to set the title to the desired string.
 *
 * The policies `strict` and `minimal` will be honored only on Linux platforms
 * where a native call to set the process title is not available.
 *
 *
 * The resulting process title will be set to either `s1` or `s1/s2` if there
 * both strings and the length is allowed by the policy.
 *
 * @param argc The number of arguments
 * @param argv The argv pointer
 * @param s1 The first string
 * @param s2 The second string
 */
void
pgmoneta_set_proc_title(int argc, char** argv, char* s1, char* s2);

/**
 * Provide the application version number as a unique value composed of the three
 * specified parts. For example, when invoked with (1,5,0) it returns 10500.
 * Every part of the number must be between 0 and 99, and the function
 * applies a restriction on the values. For example passing 1 or 101 as one of the part
 * will produce the same result.
 *
 * @param major the major version number
 * @param minor the minor version number
 * @param patch the patch level
 * @returns a number made by (patch + minor * 100 + major * 10000 )
 */
unsigned int
pgmoneta_version_as_number(unsigned int major, unsigned int minor, unsigned int patch);

/**
 * Provides the current version number of the application.
 * It relies on `pgmoneta_version_as_number` and invokes it with the
 * predefined constants.
 *
 * @returns the current version number
 */
unsigned int
pgmoneta_version_number(void);

/**
 * Checks if the currently running version number is
 * greater or equal than the specied one.
 *
 * @param major the major version number
 * @param minor the minor version number
 * @param patch the patch level
 * @returns true if the current version is greater or equal to the specified one
 */
bool
pgmoneta_version_ge(unsigned int major, unsigned int minor, unsigned int patch);

/**
 * Create directories
 * @param dir The directory
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_mkdir(char* dir);

/**
 * Append a string
 * @param orig The original string
 * @param s The string
 * @return The resulting string
 */
char*
pgmoneta_append(char* orig, char* s);

/**
 * Append a char
 * @param orig The original string
 * @param s The string
 * @return The resulting string
 */
char*
pgmoneta_append_char(char* orig, char c);

/**
 * Append an integer
 * @param orig The original string
 * @param i The integer
 * @return The resulting string
 */
char*
pgmoneta_append_int(char* orig, int i);

/**
 * Append a long
 * @param orig The original string
 * @param l The long
 * @return The resulting string
 */
char*
pgmoneta_append_ulong(char* orig, unsigned long l);

/**
 * Append a double
 * @param orig The original string
 * @param d The double
 * @return The resulting string
 */
char*
pgmoneta_append_double(char* orig, double d);

/**
 * Append a bool
 * @param orig The original string
 * @param b The bool
 * @return The resulting string
 */
char*
pgmoneta_append_bool(char* orig, bool b);

/**
 * Remove whitespace from a string
 * @param orig The original string
 * @return The resulting string
 */
char*
pgmoneta_remove_whitespace(char* orig);

/**
 * Calculate the directory size
 * @param directory The directory
 * @return The size in bytes
 */
unsigned long
pgmoneta_directory_size(char* directory);

/**
 * Get directories
 * @param base The base directory
 * @param number_of_directories The number of directories
 * @param dirs The directories
 * @return The result
 */
int
pgmoneta_get_directories(char* base, int* number_of_directories, char*** dirs);

/**
 * Remove a directory
 * @param path The directory
 * @return The result
 */
int
pgmoneta_delete_directory(char* path);

/**
 * Get files
 * @param base The base directory
 * @param number_of_files The number of files
 * @param files The files
 * @return The result
 */
int
pgmoneta_get_files(char* base, int* number_of_files, char*** files);

/**
 * Get WAL files
 * @param base The base directory
 * @param number_of_files The number of files
 * @param files The files
 * @return The result
 */
int
pgmoneta_get_wal_files(char* base, int* number_of_files, char*** files);

/**
 * Remove a file
 * @param file The file
 * @param workers The optional workers
 * @return The result
 */
int
pgmoneta_delete_file(char* file, struct workers* workers);

/**
 * Copy a PostgreSQL installation
 * @param from The from directory
 * @param to The to directory
 * @param base The base directory
 * @param server The server name
 * @param id The identifier
 * @param backup The backup
 * @param workers The optional workers
 * @return The result
 */
int
pgmoneta_copy_postgresql(char* from, char* to, char* base, char* server, char* id, struct backup* backup, struct workers* workers);

/**
 * Copy a directory
 * @param from The from directory
 * @param to The to directory
 * @param restore_last_paths The string array of file names that should be excluded from being copied in this round
 * @param workers The workers
 * @return The result
 */
int
pgmoneta_copy_directory(char* from, char* to, char** restore_last_paths, struct workers* workers);

/**
 * Copy a file
 * @param from The from file
 * @param to The to file
 * @param workers The workers
 * @return The result
 */
int
pgmoneta_copy_file(char* from, char* to, struct workers* workers);

/**
 * Move a file
 * @param from The from file
 * @param to The to file
 * @return The result
 */
int
pgmoneta_move_file(char* from, char* to);

/**
 * Get basename of a file
 * @param s The string
 * @param basename The basename of the file
 * @return The result
 */
int
pgmoneta_basename_file(char* s, char** basename);

/**
 * File/directory exists
 * @param f The file/directory
 * @return The result
 */
bool
pgmoneta_exists(char* f);

/**
 * Is the path a directory
 * @param directory The directory
 * @return The result
 */
bool
pgmoneta_is_directory(char* directory);

/**
 * Is the path a file
 * @param file The file
 * @return The result
 */
bool
pgmoneta_is_file(char* file);

/**
 * Compare files
 * @param f1 The first file path
 * @param f2 The second file path
 * @return The result
 */
bool
pgmoneta_compare_files(char* f1, char* f2);

/**
 * Symlink files
 * @param from The from file
 * @param to The to file
 * @return The result
 */
int
pgmoneta_symlink_file(char* from, char* to);

/**
 * Symlinkat file
 * @param from The from file
 * @param to The to file (relative path to where the symlink is located)
 * @return The result
 */
int
pgmoneta_symlink_at_file(char* from, char* to);

/**
 * Check for symlink
 * @param file The file
 * @return The result
 */
bool
pgmoneta_is_symlink(char* file);

/**
 * Get symlink
 * @param symlink The symlink
 * @return The result
 */
char*
pgmoneta_get_symlink(char* symlink);

/**
 * Copy WAL files
 * @param from The from directory
 * @param to The to directory
 * @param start The start file
 * @param workers The optional workers
 * @return The result
 */
int
pgmoneta_copy_wal_files(char* from, char* to, char* start, struct workers* workers);

/**
 * Get the number of WAL files
 * @param directory The directory
 * @param from The from WAL file
 * @param to The to WAL file; can be NULL
 * @return The result
 */
int
pgmoneta_number_of_wal_files(char* directory, char* from, char* to);

/**
 * Get the free space for a path
 * @param path The path
 * @return The result
 */
unsigned long
pgmoneta_free_space(char* path);

/**
 * Get the total space for a path
 * @param path The path
 * @return The result
 */
unsigned long
pgmoneta_total_space(char* path);

/**
 * Does a string start with another string
 * @param str The string
 * @param prefix The prefix
 * @return The result
 */
bool
pgmoneta_starts_with(char* str, char* prefix);

/**
 * Does a string end with another string
 * @param str The string
 * @param suffix The suffix
 * @return The result
 */
bool
pgmoneta_ends_with(char* str, char* suffix);

/**
 * Does a string contain another string
 * @param str The string
 * @param s The search string
 * @return The result
 */
bool
pgmoneta_contains(char* str, char* s);

/**
 * Sort a string array
 * @param size The size of the array
 * @param array The array
 * @return The result
 */
void
pgmoneta_sort(size_t size, char** array);

/**
 * Bytes to string
 * @param bytes The number of bytes
 * @return The result
 */
char*
pgmoneta_bytes_to_string(uint64_t bytes);

/**
 * Read version number
 * @param directory The base directory
 * @param version The version
 * @return The result
 */
int
pgmoneta_read_version(char* directory, char** version);

/**
 * Read the first WAL file name
 * @param directory The base directory
 * @param wal The WAL
 * @return The result
 */
int
pgmoneta_read_wal(char* directory, char** wal);

/**
 * Read the checkpoint WAL location from a backup_label file
 * @param directory The base directory
 * @param chkptpos [out] The checkpoint WAL position
 * @return 0 on success, 1 if otherwise
 */
int
pgmoneta_read_checkpoint_info(char* directory, char** chkptpos);

/**
 * Get the directory for a server
 * @param server The server
 * @return The directory
 */
char*
pgmoneta_get_server(int server);

/**
 * Get the backup directory for a server
 * @param server The server
 * @return The backup directory
 */
char*
pgmoneta_get_server_backup(int server);

/**
 * Get the wal directory for a server
 * @param server The server
 * @return The wal directory
 */
char*
pgmoneta_get_server_wal(int server);

/**
 * Get the wal shipping directory for a server
 * @param server The server
 * @return The wal shipping directory
 */
char*
pgmoneta_get_server_wal_shipping(int server);

/**
 * Get the wal address of the wal shipping directory for a server
 * @param server The server
 * @return The wal subdirectory under wal shipping directory
 */
char*
pgmoneta_get_server_wal_shipping_wal(int server);

/**
 * Get the backup directory for a server with an identifier
 * @param server The server
 * @param identifier The identifier
 * @return The backup directory
 */
char*
pgmoneta_get_server_backup_identifier(int server, char* identifier);

/**
 * Get the data directory for a server with an identifier
 * @param server The server
 * @param identifier The identifier
 * @return The backup directory
 */
char*
pgmoneta_get_server_backup_identifier_data(int server, char* identifier);

/**
 * Get the tablespace directory for a server with an identifier
 * @param server The server
 * @param identifier The identifier
 * @param name The tablespace name
 * @return The backup directory
 */
char*
pgmoneta_get_server_backup_identifier_tablespace(int server, char* identifier, char* name);

/**
 * Get the pg_wal directory for a server with an identifier
 * @param server The server
 * @param identifier The identifier
 * @return The backup directory
 */
char*
pgmoneta_get_server_backup_identifier_data_wal(int server, char* identifier);

/**
 * Recurive permissions (700 for directories, 600 for files)
 * @param d The top-level directory
 * @return The status
 */
int
pgmoneta_permission_recursive(char* d);

/**
 * Permission
 * @param e The entry
 * @param user The user (0; nothing, 4; read, 6; read/write)
 * @param group The group (0; nothing, 4; read, 6; read/write)
 * @param all All (0; nothing, 4; read, 6; read/write)
 * @return The status
 */
int
pgmoneta_permission(char* e, int user, int group, int all);

/**
 * Get file permission.
 * @param path The file path.
 * @return The mode of file.
 */
mode_t
pgmoneta_get_permission(char* path);

/**
 * Get short date and long date in ISO8601_format.
 * @param short_date The short date <yymmdd>.
 * @param long_date The long date <yymmddThhmmssZ>.
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_get_timestamp_ISO8601_format(char* short_date, char* long_date);

/**
 * Get the Coordinated Universal Time (UTC) timestamp.
 * @param utc_date The date.
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_get_timestamp_UTC_format(char* utc_date);

/**
 * Get the current local time
 * @return The microseconds
 */
int64_t
pgmoneta_get_current_timestamp(void);

/**
 * Get the local time since 2000-01-01 at midnight
 * @return The microseconds
 */
int64_t
pgmoneta_get_y2000_timestamp(void);

/**
 * Convert base32 to hexadecimal.
 * @param base32 The base32.
 * @param base32_length The base32 length.
 * @param hex The hexadecimal.
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_convert_base32_to_hex(unsigned char* base32, int base32_length, unsigned char** hex);

/**
 * Get the file size of a given file
 * @param file_path The file path
 * @return The file size, 0 if error occurred
 */
size_t
pgmoneta_get_file_size(char* file_path);

/**
 * Is the file is compressed and/or encrypted
 * @param file_path The file path
 * @return True if archive, otherwise false
 */
bool
pgmoneta_is_file_archive(char* file_path);

/** @struct
 * Defines token bucket structure
 */
struct token_bucket
{
   unsigned long burst; /**< default value is 0, no limit */
   atomic_ulong cur_tokens;
   long max_rate;
   int every;
   atomic_ulong last_time;
};

/**
 * Init a token bucket
 * @param tb The token bucket
 * @param max_rate The number of bytes of tokens added every one second
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_token_bucket_init(struct token_bucket* tb, long max_rate);

/**
 * Free the memory of the token bucket
 * @param tb The token bucket
 */
void
pgmoneta_token_bucket_destroy(struct token_bucket* tb);

/**
 * Add new token into the bucket
 * @param tb The token bucket
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_token_bucket_add(struct token_bucket* tb);

/**
 * Get tokens from token bucket wrapper
 * @param tb The token bucket
 * @param tokens Needed tokens
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_token_bucket_consume(struct token_bucket* tb, unsigned long tokens);

/**
 * Get tokens from token bucket once
 * @param tb The token bucket
 * @param tokens Needed tokens
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_token_bucket_once(struct token_bucket* tb, unsigned long tokens);

#ifdef DEBUG

/**
 * Generate a backtrace in the log
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_backtrace(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
