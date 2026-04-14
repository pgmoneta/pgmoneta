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
 */

#ifndef PGMONETA_WORKFLOW_H
#define PGMONETA_WORKFLOW_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>
#include <art.h>
#include <info.h>

#define WORKFLOW_TYPE_BACKUP             0
#define WORKFLOW_TYPE_RESTORE            1
#define WORKFLOW_TYPE_ARCHIVE            2
#define WORKFLOW_TYPE_DELETE_BACKUP      3
#define WORKFLOW_TYPE_RETENTION          4
#define WORKFLOW_TYPE_WAL_SHIPPING       5
#define WORKFLOW_TYPE_VERIFY             6
#define WORKFLOW_TYPE_INCREMENTAL_BACKUP 7
#define WORKFLOW_TYPE_COMBINE            8
#define WORKFLOW_TYPE_COMBINE_AS_IS      9
#define WORKFLOW_TYPE_POST_ROLLUP        10

#define WORKFLOW_TYPE_S3_LIST            100
#define WORKFLOW_TYPE_S3_DELETE          101
#define WORKFLOW_TYPE_S3_RESTORE         102

#define WORKFLOW_NAME_BACKUP             "Backup"             /**< The name of backup workflow */
#define WORKFLOW_NAME_INCREMENTAL_BACKUP "Incremental backup" /**< The name of incremental backup workflow */
#define WORKFLOW_NAME_RESTORE            "Restore"            /**< The name of restore workflow */
#define WORKFLOW_NAME_ARCHIVE            "Archive"            /**< The name of archive workflow */
#define WORKFLOW_NAME_DELETE_BACKUP      "Delete backup"      /**< The name of delete backup workflow */
#define WORKFLOW_NAME_RETENTION          "Retention"          /**< The name of retention workflow */
#define WORKFLOW_NAME_VERIFY             "Verify"             /**< The name of verify workflow */
#define WORKFLOW_NAME_COMBINE            "Combine"            /**< The name of combine workflow */
#define WORKFLOW_NAME_COMBINE_AS_IS      "Combine as is"      /**< The name of combine as is workflow */
#define WORKFLOW_NAME_POST_ROLLUP        "Post rollup"        /**< The name of post rollup workflow */
#define WORKFLOW_NAME_S3_LIST            "S3 list"            /**< The name of S3 list workflow */
#define WORKFLOW_NAME_S3_DELETE          "S3 delete"          /**< The name of S3 delete workflow */
#define WORKFLOW_NAME_S3_RESTORE         "S3 restore"         /**< The name of S3 restore workflow */
#define WORKFLOW_NAME_UNKNOWN            "Unknown"            /**< The name of unknown workflow */

#define PHASE_NONE                       0 /* No phase */
#define PHASE_BASEBACKUP                 1 /* Base backup phase */
#define PHASE_MANIFEST                   2 /* Manifest phase */
#define PHASE_SHA512                     3 /* SHA512 phase */
#define PHASE_LINKING                    4 /* Link phase */
#define PHASE_COMPRESSION                5 /* Compression phase */
#define PHASE_ENCRYPTION                 6 /* Encryption phase */

#define PHASE_NAME_BASEBACKUP            "Base backup"        /**< The name of base backup phase */
#define PHASE_NAME_INCREMENTAL_BACKUP    "Incremental backup" /**< The name of incremental backup phase */
#define PHASE_NAME_MANIFEST              "Manifest"           /**< The name of manifest phase */
#define PHASE_NAME_SHA512                "SHA512"             /**< The name of SHA512 phase */
#define PHASE_NAME_LINKING               "Linking"            /**< The name of linking phase */
#define PHASE_NAME_LINK                  "Link"               /**< The name of link phase */
#define PHASE_NAME_COMPRESSION           "Compression"        /**< The name of compression phase */
#define PHASE_NAME_ENCRYPTION            "Encryption"         /**< The name of encryption phase */
#define PHASE_NAME_ZSTD                  "ZSTD"               /**< The name of ZSTD phase */
#define PHASE_NAME_GZIP                  "GZip"               /**< The name of GZip phase */
#define PHASE_NAME_LZ4                   "LZ4"                /**< The name of LZ4 phase */
#define PHASE_NAME_BZIP2                 "BZip2"              /**< The name of BZip2 phase */
#define PHASE_NAME_UNKNOWN               "Unknown"            /**< The name of unknown phase */

#define PERMISSION_TYPE_BACKUP           0
#define PERMISSION_TYPE_RESTORE          1
#define PERMISSION_TYPE_ARCHIVE          2

#define CLEANUP_TYPE_RESTORE             0

#define NODE_ALL                         "all"                 /* All the files in a manifest */
#define NODE_BACKUP                      "backup"              /* The backup structure */
#define NODE_COMBINE_AS_IS               "combine_as_is"       /* Whether to combine the backups as is*/
#define NODE_COPY_WAL                    "copy_wal"            /* Whether to copy WAL */
#define NODE_BACKUP_BASE                 "backup_base"         /* The base directory of the backup */
#define NODE_BACKUP_DATA                 "backup_data"         /* The data directory of the backup */
#define NODE_ERROR_CODE                  "error_code"          /* The error code */
#define NODE_FAILED                      "failed"              /* The failed files in a manifest */
#define NODE_FORCE                       "force"               /* force deletion of backup */
#define NODE_INCREMENTAL_BASE            "incremental_base"    /* The base directory of incremental */
#define NODE_INCREMENTAL_COMBINE         "incremental_combine" /* Whether to combine into one incremental backup */
#define NODE_INCREMENTAL_LABEL           "incremental_label"   /* The label of the incremental backup */
#define NODE_LABEL                       "label"               /* The backup label */
#define NODE_LABELS                      "labels"              /* A list of backup labels */
#define NODE_MANIFEST                    "manifest"            /* The manifest */
#define NODE_PRIMARY                     "primary"             /* Is the server a primary */
#define NODE_RECOVERY_INFO               "recovery_info"       /* The recovery information */
#define NODE_SERVER_BACKUP               "server_backup"       /* The backup directory of the server */
#define NODE_S3_OBJECTS                  "s3_objects"          /* The list of S3 objects */
#define NODE_SERVER_BASE                 "server_base"         /* The base directory of the server */
#define NODE_SERVER_ID                   "server_id"           /* The server number */
#define NODE_TARGET_BASE                 "target_base"         /* The target base directory */
#define NODE_TARGET_FILE                 "target_file"         /* The target file */
#define NODE_TARGET_ROOT                 "target_root"         /* The target root directory */

/* Supplied by the user */
#define USER_DIRECTORY  "directory"  /* The target root directory */
#define USER_FILES      "files"      /* The files that should be checked */
#define USER_IDENTIFIER "identifier" /* The backup identifier (oldest, newest, <timestamp>) */
#define USER_POSITION   "position"   /* The recovery positions */
#define USER_SERVER     "server"     /* The server name */

typedef char* (*name)(void);
typedef int (*setup)(char*, struct art*);
typedef int (*execute)(char*, struct art*);
typedef int (*teardown)(char*, struct art*);

/** @struct workflow
 * Defines a workflow
 */
struct workflow
{
   int type; /**< The type */

   name name;         /**< The name */
   setup setup;       /**< The setup  function pointer */
   execute execute;   /**< The execute function pointer */
   teardown teardown; /**< The teardown function pointer */

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
pgmoneta_workflow_nodes(int server, char* identifier, struct art* nodes, struct backup** backup);

/**
 * Execute a workflow
 * @param workflow The workflow
 * @param nodes The nodes
 * @param error_name The error name if set
 * @param error_code The error code if set
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_workflow_execute(struct workflow* workflow, struct art* nodes,
                          char** error_name, int* error_code);

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

/**
 * Get display name for a phase
 * @param phase The phase constant
 * @return The display name
 */
const char*
pgmoneta_phase_name(int phase);

/**
 * Get display name for a workflow type
 * @param workflow_type The workflow type
 * @return The display name
 */
const char*
pgmoneta_workflow_name(int workflow_type);

#ifdef __cplusplus
}
#endif

#endif
