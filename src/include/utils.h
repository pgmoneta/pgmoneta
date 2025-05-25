/*
 * Copyright (C) 2025 The pgmoneta community
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
#include <message.h>
#include <workers.h>

#include <stdbool.h>
#include <openssl/asn1.h>
#include <sys/types.h>

#define SHORT_TIME_LENGTH  8 + 1
#define LONG_TIME_LENGTH  16 + 1
#define UTC_TIME_LENGTH   29 + 1

/** Define Windows 20 palette colors as constants using ANSI codes **/
#define COLOR_BLACK         "\033[30m"
#define COLOR_DARK_RED      "\033[31m"
#define COLOR_DARK_GREEN    "\033[32m"
#define COLOR_DARK_YELLOW   "\033[33m"
#define COLOR_DARK_BLUE     "\033[34m"
#define COLOR_DARK_MAGENTA  "\033[35m"
#define COLOR_DARK_CYAN     "\033[36m"
#define COLOR_LIGHT_GREY    "\033[37m"
#define COLOR_MONEY_GREEN   "\033[32m"   /* Close approximation */
#define COLOR_SKY_BLUE      "\033[36m"   /* Close approximation */
#define COLOR_CREAM         "\033[97m"   /* Close approximation */
#define COLOR_MEDIUM_GREY   "\033[90m"
#define COLOR_DARK_GREY     "\033[90m"
#define COLOR_RED           "\033[31m"
#define COLOR_GREEN         "\033[32m"
#define COLOR_YELLOW        "\033[33m"
#define COLOR_BLUE          "\033[34m"
#define COLOR_MAGENTA       "\033[35m"
#define COLOR_CYAN          "\033[36m"
#define COLOR_WHITE         "\033[97m"
#define COLOR_RESET         "\033[0m"    /* Reset to default color */

/** @struct signal_info
 * Defines the signal structure
 */
struct signal_info
{
   struct ev_signal signal; /**< The libev base type */
   int slot;                /**< The slot */
};

/** @struct pgmoneta_command
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
   char* command;                            /**< The command */
   char* subcommand;                         /**< The subcommand if there is one */
   int accepted_argument_count[MISC_LENGTH]; /**< The argument count */

   int action;                               /**< The specific action */
   char* default_argument;                   /**< The default argument */
   char* log_message;                        /**< The log message used */

   /* Deprecation information */
   bool deprecated;                                /**< Is the command deprecated */
   unsigned int deprecated_since_major;            /**< Deprecated since major version */
   unsigned int deprecated_since_minor;            /**< Deprecated since minor version */
   char* deprecated_by;                      /**< Deprecated by this command */
};

/** @struct pgmoneta_parsed_command
 * Holds parsed command data.
 *
 * Fields:
 * - cmd: A pointer to the command struct that was parsed.
 * - args: An array of pointers to the parsed arguments of the command (points to argv).
 */
struct pgmoneta_parsed_command
{
   struct pgmoneta_command* cmd; /**< The command */
   char* args[MISC_LENGTH];            /**< The arguments */
};

/** @struct token_bucket
 * Defines token bucket structure
 */
struct token_bucket
{
   unsigned long burst;     /**< Default value is 0, no limit */
   atomic_ulong cur_tokens; /**< The current tokens */
   long max_rate;           /**< The maximum rate */
   int every;               /**< The every rate */
   atomic_ulong last_time;  /**< The last time updated */
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
              struct pgmoneta_command command_table[],
              size_t command_count);

/**
 * Get the request identifier
 * @param msg The message
 * @return The identifier
 */
int32_t
pgmoneta_get_request(struct message* msg);

/**
 * Get a memory aligned size
 * @param size The requested size
 * @return The aligned size
 */
size_t
pgmoneta_get_aligned_size(size_t size);

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
 * Read an uint8
 * @param data Pointer to the data
 * @return The uint8
 */
uint8_t
pgmoneta_read_uint8(void* data);

/**
 * Read an int16
 * @param data Pointer to the data
 * @return The int16
 */
int16_t
pgmoneta_read_int16(void* data);

/**
 * Read an uint16
 * @param data Pointer to the data
 * @return The uint16
 */
uint16_t
pgmoneta_read_uint16(void* data);

/**
 * Read an int32
 * @param data Pointer to the data
 * @return The int32
 */
int32_t
pgmoneta_read_int32(void* data);

/**
 * Read an uint32
 * @param data Pointer to the data
 * @return The uint32
 */
uint32_t
pgmoneta_read_uint32(void* data);

/**
 * Read an int64
 * @param data Pointer to the data
 * @return The int64
 */
int64_t
pgmoneta_read_int64(void* data);

/**
 * Read an uint64
 * @param data Pointer to the data
 * @return The uint64
 */
uint64_t
pgmoneta_read_uint64(void* data);

/**
 * Read a bool
 * @param data Pointer to the data
 * @return The bool
 */
bool
pgmoneta_read_bool(void* data);

/**
 * Write a byte
 * @param data Pointer to the data
 * @param b The byte
 */
void
pgmoneta_write_byte(void* data, signed char b);

/**
 * Write a uint8
 * @param data Pointer to the data
 * @param b The uint8
 */
void
pgmoneta_write_uint8(void* data, uint8_t b);

/**
 * Write an int16
 * @param data Pointer to the data
 * @param i The int16
 */
void
pgmoneta_write_int16(void* data, int16_t i);

/**
 * Write an uint16
 * @param data Pointer to the data
 * @param i The uint16
 */
void
pgmoneta_write_uint16(void* data, uint16_t i);

/**
 * Write an int32
 * @param data Pointer to the data
 * @param i The int32
 */
void
pgmoneta_write_int32(void* data, int32_t i);

/**
 * Write an uint32
 * @param data Pointer to the data
 * @param i The uint32
 */
void
pgmoneta_write_uint32(void* data, uint32_t i);

/**
 * Write an int64
 * @param data Pointer to the data
 * @param i The int64
 */
void
pgmoneta_write_int64(void* data, int64_t i);

/**
 * Write an uint64
 * @param data Pointer to the data
 * @param i The uint64
 */
void
pgmoneta_write_uint64(void* data, uint64_t i);

/**
 * Write an bool
 * @param data Pointer to the data
 * @param i The bool
 */
void
pgmoneta_write_bool(void* data, bool b);

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
 * BASE64 encode a data
 * @param raw The data
 * @param raw_length The length of the raw data
 * @param encoded The encoded string
 * @param encoded_length The length of the encoded string
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_base64_encode(void* raw, size_t raw_length, char** encoded, size_t* encoded_length);

/**
 * BASE64 decode a string
 * @param encoded The encoded string
 * @param encoded_length The length of the encoded string
 * @param raw The raw data
 * @param raw_length The length of the raw data
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_base64_decode(char* encoded, size_t encoded_length, void** raw, size_t* raw_length);

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
 * Append a double with set precision
 * @param orig The original string
 * @param d The double
 * @param precision The number of digits after decimal
 * @return The resulting string
 */
char*
pgmoneta_append_double_precision(char* orig, double d, int precision);

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
 * Remove the prefix from orig
 * @param orig The original string
 * @param prefix The prefix string
 * @return The resulting string
 */
char*
pgmoneta_remove_prefix(char* orig, char* prefix);

/**
 * Remove the suffix from orig, it makes a copy of orig if the suffix doesn't exist
 * @param orig The original string
 * @param suffix The suffix string
 * @return The resulting string
 */
char*
pgmoneta_remove_suffix(char* orig, char* suffix);

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
 * Delete a file
 * @param file The file
 * @param workers The optional workers
 * @return The result
 */
int
pgmoneta_delete_file(char* file, struct workers* workers);

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
 * List a directory
 * @param directory The directory
 */
void
pgmoneta_list_directory(char* directory);

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
 * Strip the extension of a file
 * @param s The string
 * @param name The name of the file
 * @return The result
 */
int
pgmoneta_strip_extension(char* s, char** name);

/**
 * Get the translated size of a file
 * @param size The size
 * @return The result
 */
char*
pgmoneta_translate_file_size(uint64_t size);

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
 * Is the symlink valid ?
 * @param path The path
 * @return The result
 */
bool
pgmoneta_is_symlink_valid(char* path);

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
 * Get the biggest file size
 * @param directory The directory
 * @return The result
 */
unsigned long
pgmoneta_biggest_file(char* directory);

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
 * Remove the first character of a string
 * @param str The string
 * @return The result
 */
char*
pgmoneta_remove_first(char* str);

/**
 * Remove the last character of a string
 * @param str The string
 * @return The result
 */
char*
pgmoneta_remove_last(char* str);

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
 * Get the workspace directory for a server
 * @param server The server
 * @return The workspace directory
 */
char*
pgmoneta_get_server_workspace(int server);

/**
 * Delete the workspace directory for a server
 * @param server The server
 * @param label An optional label
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_delete_server_workspace(int server, char* label);

/**
 * Get the backup directory for a server with an identifier
 * @param server The server
 * @param identifier The identifier
 * @return The backup directory
 */
char*
pgmoneta_get_server_backup_identifier(int server, char* identifier);

/**
 * Get the extra directory for a server with an identifier
 * @param server The server
 * @param identifier The identifier
 * @return The extra directory
 */
char*
pgmoneta_get_server_extra_identifier(int server, char* identifier);

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
 * Calculate mode
 * @param user The user (0; nothing, 4; read, 6; read/write)
 * @param group The group (0; nothing, 4; read, 6; read/write)
 * @param all All (0; nothing, 4; read, 6; read/write)
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_get_permission_mode(int user, int group, int all, mode_t* mode);

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
 * Get the duration between two points of time
 * @param start_time The start time
 * @param end_time The end time
 * @return The duration in seconds
 */
double
pgmoneta_compute_duration(struct timespec start_time, struct timespec end_time);

/**
 * Get the timestramp difference as a string
 * @param start_time The start time
 * @param end_time The end time
 * @param seconds The number of seconds
 * @return The timestamp string
 */
char*
pgmoneta_get_timestamp_string(struct timespec start_time, struct timespec end_time, double* seconds);

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
 * Is the file encrypted
 * @param file_path The file path
 * @return True if encrypted, otherwise false
 */
bool
pgmoneta_is_encrypted(char* file_path);

/**
 * Is the file compressed
 * @param file_path The file path
 * @return True if compressed, otherwise false
 */
bool
pgmoneta_is_compressed(char* file_path);

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

/**
 * Format a string and append it to the original string
 * @param buf original string
 * @param format The string to be formatted and appended to buf
 * @param ... The arguments to be formatted
 * @return The resulting string
 */
char*
pgmoneta_format_and_append(char* buf, char* format, ...);

/**
 * Wrapper for the atoi() function, which provides NULL input check
 * @param input The string input
 * @return 0 if input is NULL, otherwise what atoi() returns
 */
int
pgmoneta_atoi(char* input);

/**
 * Indent a string
 * @param str The string
 * @param tag [Optional] The tag, which will be applied after indentation if not NULL
 * @param indent The indent
 * @return The indented string
 */
char*
pgmoneta_indent(char* str, char* tag, int indent);

/**
 * Escape a string
 * @param str The original string
 * @return The escaped string
 */
char*
pgmoneta_escape_string(char* str);

/**
 * Generate the lsn string in a %X/%X format given a lsn integer
 * @param  lsn integer value
 * @return The lsn string in the described format
 */
char*
pgmoneta_lsn_to_string(uint64_t lsn);

/**
 * Check if the path to a file is an incremental path
 * @param path The file path
 * @return true if the file is an incremental path, false if otherwise
 */
bool
pgmoneta_is_incremental_path(char* path);

/**
 * Splits a string into an array of strings separated by a delimeter
 *
 * @param string The string to split
 * @param results The array of strings to store the results
 * @param count The number of strings the string splitted into
 * @param delimeter The delimeter to split the string by
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_split(const char* string, char*** results, int* count, char delimiter);

/**
 * Merges an array of strings into a single string
 *
 * @param lists The arrays of strings to merge
 * @param out_list The resulting merged array
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_merge_string_arrays(char** lists[], char*** out_list);

/**
 * Checks if string 'a' is a substring of 'b'.
 *
 * @param a The substring to search for
 * @param b The string to search within
 * @return 1 if found, 0 otherwise
 */
int
pgmoneta_is_substring(char* a, char* b);

/**
 * Resolve path.
 * The function will resolve the path by expanding environment
 * variables (e.g., $HOME) in subpaths that are either surrounded
 * by double quotes (") or not surrounded by any quotes.
 * @param orig_path The original path
 * @param new_path Reference to the resolved path
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_resolve_path(char* orig_path, char** new_path);

/**
 * Generate a backtrace in the log
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_backtrace(void);

/**
 * Get the OS name and kernel version.
 *
 * @param os            Pointer to store the OS name (e.g., "Linux", "FreeBSD", "OpenBSD").
 *                      Memory will be allocated internally and should be freed by the caller.
 * @param kernel_major  Pointer to store the kernel major version.
 * @param kernel_minor  Pointer to store the kernel minor version.
 * @param kernel_patch  Pointer to store the kernel patch version.
 * @return              0 on success, 1 on error.
 */
int
pgmoneta_os_kernel_version(char** os, int* kernel_major, int* kernel_minor, int* kernel_patch);

/**
 * Check if the given filename represents a WAL file.
 *
 * @param file The filename to check
 * @return true if the file is a WAL file, false otherwise
 */
bool
pgmoneta_is_wal_file(char* file);

/**
 * Is the string a number ?
 * @param str The string
 * @param base The base (10 or 16)
 * @return True if number, otherwise false
 */
bool
pgmoneta_is_number(char* str, int base);

#ifdef __cplusplus
}
#endif

#endif
