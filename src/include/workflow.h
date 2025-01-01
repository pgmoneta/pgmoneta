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

#include <deque.h>
#include <info.h>

#include <stdlib.h>
#include <stdbool.h>

#define WORKFLOW_TYPE_BACKUP        0
#define WORKFLOW_TYPE_RESTORE       1
#define WORKFLOW_TYPE_ARCHIVE       2
#define WORKFLOW_TYPE_DELETE_BACKUP 3
#define WORKFLOW_TYPE_RETENTION     4
#define WORKFLOW_TYPE_WAL_SHIPPING  5
#define WORKFLOW_TYPE_VERIFY        6

#define PERMISSION_TYPE_BACKUP  0
#define PERMISSION_TYPE_RESTORE 1
#define PERMISSION_TYPE_ARCHIVE 2

#define CLEANUP_TYPE_RESTORE 0

#define NODE_ALL           "all"
#define NODE_BACKUP        "backup"
#define NODE_BACKUP_BASE   "backup_base"
#define NODE_BACKUP_DATA   "backup_data"
#define NODE_DESTINATION   "destination"
#define NODE_DIRECTORY     "directory"
#define NODE_FAILED        "failed"
#define NODE_FILES         "files"
#define NODE_IDENTIFIER    "identifier"
#define NODE_LABEL         "label"
#define NODE_OUTPUT        "output"
#define NODE_POSITION      "position"
#define NODE_PRIMARY       "primary"
#define NODE_RECOVERY_INFO "recovery_info"
#define NODE_SERVER_BACKUP "server_backup"
#define NODE_SERVER_BASE   "server_base"
#define NODE_TARFILE       "tarfile"

typedef int (* setup)(int, char*, struct deque*);
typedef int (* execute)(int, char*, struct deque*);
typedef int (* teardown)(int, char*, struct deque*);

/** @struct workflow
 * Defines a workflow
 */
struct workflow
{
   setup setup;           /**< The setup  function pointer */
   execute execute;       /**< The execute function pointer */
   teardown teardown;     /**< The taerdown function pointer */

   struct workflow* next; /**< The next workflow */
};

/**
 * Create a workflow
 * @param workflow_type The workflow type
 * @param backup The backup
 * @return The workflow
 */
struct workflow*
pgmoneta_workflow_create(int workflow_type, struct backup* backup);

/**
 * Create standard workflow nodes
 * @param server The server
 * @param identifier The identifier
 * @param nodes The nodes
 * @param backup The backup
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_workflow_nodes(int server, char* identifier, struct deque* nodes, struct backup** backup);

/**
 * Destroy the workflow
 * @param workflow The workflow
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_workflow_destroy(struct workflow* workflow);

#ifdef __cplusplus
}
#endif

#endif
