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

#ifndef PGMONETA_INFO_H
#define PGMONETA_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

#define INFO_STATUS  "STATUS"
#define INFO_LABEL   "LABEL"
#define INFO_WAL     "WAL"
#define INFO_ELAPSED "ELAPSED"
#define INFO_VERSION "VERSION"
#define INFO_KEEP    "KEEP"
#define INFO_BACKUP  "BACKUP"
#define INFO_RESTORE "RESTORE"

#define VALID_UNKNOWN -1
#define VALID_FALSE    0
#define VALID_TRUE     1

/** @struct
 * Defines a backup
 */
struct backup
{
   char label[MISC_LENGTH];    /**< The label of the backup */
   char wal[MISC_LENGTH];      /**< The name of the WAL file */
   unsigned long backup_size;  /**< The backup size */
   unsigned long restore_size; /**< The restore size */
   int elapsed_time;           /**< The elapsed time in seconds */
   int version;                /**< The version */
   bool keep;                  /**< Keep the backup */
   char valid;                 /**< Is the backup valid */
} __attribute__ ((aligned (64)));

/**
 * Create a backup information file
 * @param directory The backup directory
 * @param label The label
 * @param status The status
 */
void
pgmoneta_create_info(char* directory, char* label, int status);

/**
 * Update backup information: unsigned long
 * @param directory The backup directory
 * @param key The key
 * @param value The value
 */
void
pgmoneta_update_info_unsigned_long(char* directory, char* key, unsigned long value);

/**
 * Update backup information: string
 * @param directory The backup directory
 * @param key The key
 * @param value The value
 */
void
pgmoneta_update_info_string(char* directory, char* key, char* value);

/**
 * Update backup information: bool
 * @param directory The backup directory
 * @param key The key
 * @param value The value
 */
void
pgmoneta_update_info_bool(char* directory, char* key, bool value);

/**
 * Get the backups
 * @param directory The directory
 * @param number_of_backups The number of backups
 * @param backups The backups
 * @return The result
 */
int
pgmoneta_get_backups(char* directory, int* number_of_backups, struct backup*** backups);

/**
 * Get a backup
 * @param directory The directory
 * @param label The label
 * @param backup The backup
 * @return The result
 */
int
pgmoneta_get_backup(char* directory, char* label, struct backup** backup);

/**
 * Get the number of valid backups
 * @param i The server
 * @return The result
 */
int
pgmoneta_get_number_of_valid_backups(int server);

#ifdef __cplusplus
}
#endif

#endif
