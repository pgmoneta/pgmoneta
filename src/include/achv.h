/*
 * Copyright (C) 2023 Red Hat
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

#ifndef PGMONETA_ARCHIVE_H
#define PGMONETA_ARCHIVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ev.h>
#include <stdlib.h>

/**
 * Create an archive
 * @param client_fd The client
 * @param server The server
 * @param backup_id The backup identifier
 * @param position The position
 * @param directory The target directory
 * @param argv The argv
 */
void
pgmoneta_archive(int client_fd, int server, char* backup_id, char* position, char* directory, char** argv);

/**
 * Extract from a tar file to a given directory
 * @param file_path The tar file path
 * @param destination The destination to extract to
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_extract_tar_file(char* file_path, char* destination);

/**
 * Create a tar archive of the given directory
 * @param src_path The source directory
 * @param dst_path The destination tar file path
 * @param save_path The path within the tar file
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_tar_directory(char* src_path, char* dst_path, char* save_path);

#ifdef __cplusplus
}
#endif

#endif
