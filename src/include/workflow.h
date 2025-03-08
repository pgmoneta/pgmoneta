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

#ifndef PGMONETA_WORKFLOW_H
#define PGMONETA_WORKFLOW_H

#ifdef __cplusplus
extern "C" {
#endif

#include <art.h>
#include <info.h>

#include <stdlib.h>
#include <stdbool.h>

#define WORKFLOW_TYPE_BACKUP                0
#define WORKFLOW_TYPE_RESTORE               1
#define WORKFLOW_TYPE_ARCHIVE               2
#define WORKFLOW_TYPE_DELETE_BACKUP         3
#define WORKFLOW_TYPE_RETENTION             4
#define WORKFLOW_TYPE_WAL_SHIPPING          5
#define WORKFLOW_TYPE_VERIFY                6
#define WORKFLOW_TYPE_INCREMENTAL_BACKUP    7
#define WORKFLOW_TYPE_RESTORE_INCREMENTAL   8

#define PERMISSION_TYPE_BACKUP              0
#define PERMISSION_TYPE_RESTORE             1
#define PERMISSION_TYPE_ARCHIVE             2

#define CLEANUP_TYPE_RESTORE                0

#define NODE_ALL               "all"               /* All the files in a manifest */
#define NODE_BACKUP            "backup"            /* The backup structure */
#define NODE_BACKUPS           "backups"           /* A list of backups */
#define NODE_BACKUP_BASE       "backup_base"       /* The base directory of the backup */
#define NODE_BACKUP_DATA       "backup_data"       /* The data directory of the backup */
#define NODE_FAILED            "failed"            /* The failed files in a manifest */
#define NODE_FILES             "files"             /* The files */
#define NODE_IDENTIFIER        "identifier"        /* The backup identifier (oldest, newest, <timestamp>) */
#define NODE_INCREMENTAL_BASE  "incremental_base"  /* The base directory of incremental */
#define NODE_INCREMENTAL_LABEL "incremental_label" /* The label of the incremental backup */
#define NODE_LABEL             "label"             /* The backup label */
#define NODE_MANIFEST          "manifest"          /* The manifest */
#define NODE_POSITION          "position"          /* The recovery positions */
#define NODE_PRIMARY           "primary"           /* Is the server a primary */
#define NODE_RECOVERY_INFO     "recovery_info"     /* The recovery information */
#define NODE_SERVER            "server"            /* The server number */
#define NODE_SERVER_BACKUP     "server_backup"     /* The backup directory of the server */
#define NODE_SERVER_BASE       "server_base"       /* The base directory of the server */
#define NODE_TARGET_BASE       "target_base"       /* The target base directory */
#define NODE_TARGET_FILE       "target_file"       /* The target file */
#define NODE_TARGET_ROOT       "target_root"       /* The target root directory */
#define NODE_WORKSPACE_ROOT    "workspace_root"    /* The workspace root directory */

typedef char* (*name)(void);
typedef int (*setup)(char*, struct art*);
typedef int (*execute)(char*, struct art*);
typedef int (*teardown)(char*, struct art*);

/** @struct workflow
 * Defines a workflow
 */
struct workflow
{
   name name;        /**< The setup  function pointer */
   setup setup;      /**< The setup  function pointer */
   execute execute;  /**< The execute function pointer */
   teardown teardown; /**< The taerdown function pointer */

   struct workflow* next; /**< The next workflow */
};

/**
 * Create a workflow
 * @param workflow_type The workflow type
 * @param server The server
 * @param backup The backup
 * @return The workflow
 */
struct workflow*
pgmoneta_workflow_create(int workflow_type, int server, struct backup* backup);

/**
 * Create standard workflow nodes
 * @param server The server
 * @param identifier The identifier
 * @param nodes The nodes
 * @param backup The backup
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_workflow_nodes(int server, char* identifier, struct art* nodes, struct backup** backup);

/**
 * Destroy the workflow
 * @param workflow The workflow
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_workflow_destroy(struct workflow* workflow);

/**
 * A common minimal setup
 * @param name The name
 * @param nodes The nodes
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_common_setup(char* name, struct art* nodes);

/**
 * A common minimal teardown
 * @param name The name
 * @param nodes The nodes
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_common_teardown(char* name, struct art* nodes);

#ifdef __cplusplus
}
#endif

#endif
