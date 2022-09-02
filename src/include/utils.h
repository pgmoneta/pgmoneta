/*
 * Copyright (C) 2022 Red Hat
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

#include <stdlib.h>

/** @struct
 * Defines the signal structure
 */
struct signal_info
{
   struct ev_signal signal; /**< The libev base type */
   int slot;                /**< The slot */
};

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
 * Read a byte
 * @param data Pointer to the data
 * @return The byte
 */
signed char
pgmoneta_read_byte(void* data);

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
 * Append a bool
 * @param orig The original string
 * @param b The bool
 * @return The resulting string
 */
char*
pgmoneta_append_bool(char* orig, bool b);

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
 * @return The result
 */
int
pgmoneta_delete_file(char* file);

/**
 * Copy a directory
 * @param from The from directory
 * @param to The to directory
 * @return The result
 */
int
pgmoneta_copy_directory(char* from, char* to);

/**
 * Copy a file
 * @param from The from file
 * @param to The to file
 * @return The result
 */
int
pgmoneta_copy_file(char* from, char* to);

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
 * Check for file
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
 * @return The result
 */
int
pgmoneta_copy_wal_files(char* from, char* to, char* start);

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
