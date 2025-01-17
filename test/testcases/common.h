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
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PGMONETA_TEST_COMMON_H
#define PGMONETA_TEST_COMMON_H

#define BUFFER_SIZE 8192

#define PGMONETA_LOG_FILE_TRAIL      "/log/pgmoneta.log"
#define PGMONETA_EXECUTABLE_TRAIL    "/src/pgmoneta-cli"
#define PGMONETA_CONFIGURATION_TRAIL "/pgmoneta-testsuite/conf/pgmoneta.conf"
#define PGMONETA_RESTORE_TRAIL       "/pgmoneta-testsuite/restore/"

#define PGMONETA_BACKUP_LOG      "INFO  backup.c:195 Backup: primary/"
#define PGMONETA_RESTORE_LOG     "INFO  restore.c:142 Restore: primary/"
#define PGMONETA_DELETE_LOG      "INFO  backup.c:545 Delete: primary/"

#define SUCCESS_STATUS  "Status: true"

extern char project_directory[BUFFER_SIZE];

/**
 * get the executable path from the project directory and its corresponding trail
 * @return executable path
 */
char*
get_executable_path();

/**
 * get the configuration path from the project directory and its corresponding trail
 * @return configuration path
 */
char*
get_configuration_path();

/**
 * get the restore path from the project directory and its corresponding trail
 * @return restore path
 */
char*
get_restore_path();

/**
 * get the log path from the project directory and its corresponding trail
 * @return log path
 */
char*
get_log_path();

/**
 * get the last entry of a log file (remember to free the buffer)
 * @param log_path The path of log file
 * @param buffer The buffer to store the last entry of the log file
 * @return 0 for success otherwise 1
 */
int
get_last_log_entry(char* log_path, char** buffer);

#endif