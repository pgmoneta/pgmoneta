/*
 * Copyright (C) 2026 The pgmoneta community
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
 *
 */

#ifndef PGMONETA_TSCLIENT_HELPERS_H
#define PGMONETA_TSCLIENT_HELPERS_H

#include <json.h>
#include <stdbool.h>

/**
 * Get backup count from LIST_BACKUP response
 * @param response The JSON response from list_backup
 * @return Number of backups, or -1 on error
 */
int
pgmoneta_tsclient_get_backup_count(struct json* response);

/**
 * Get backup at specific index from LIST_BACKUP response
 * @param response The JSON response from list_backup
 * @param index The backup index
 * @return JSON object for backup, or NULL on error
 */
struct json*
pgmoneta_tsclient_get_backup(struct json* response, int index);

/**
 * Get backup label from backup JSON object
 * @param backup The backup JSON object
 * @return Label string, or NULL on error
 */
char*
pgmoneta_tsclient_get_backup_label(struct json* backup);

/**
 * Get backup type (FULL/INCREMENTAL) from backup JSON object
 * @param backup The backup JSON object
 * @return Type string ("FULL" or "INCREMENTAL"), or NULL on error
 */
char*
pgmoneta_tsclient_get_backup_type(struct json* backup);

/**
 * Get backup parent label from backup JSON object
 * @param backup The backup JSON object
 * @return Parent label string, or NULL if FULL backup or error
 */
char*
pgmoneta_tsclient_get_backup_parent(struct json* backup);

/**
 * Verify parent-child relationship between two backups
 * @param parent The parent backup JSON object
 * @param child The child backup JSON object
 * @return true if child's parent matches parent's label
 */
bool
pgmoneta_tsclient_verify_backup_chain(struct json* parent, struct json* child);

#endif
