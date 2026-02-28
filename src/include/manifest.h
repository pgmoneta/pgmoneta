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

#ifndef PGMONETA_MANIFEST_H
#define PGMONETA_MANIFEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>
#include <art.h>
#include <backup.h>
#include <json.h>

#define MANIFEST_CHUNK_SIZE 8192

// simple manifest csv structure definition in case we want to change later
#define MANIFEST_COLUMN_COUNT   2
#define MANIFEST_PATH_INDEX     0
#define MANIFEST_CHECKSUM_INDEX 1

/** @struct manifest_file
 * Defines a manifest file
 */
struct manifest_file
{
   char* path;     /**< The path of the manifest */
   char* checksum; /**< The checksum of the manifest */
};

/** @struct manifest_chunk
 * Defines a manifest chunk
 */
struct manifest_chunk
{
   struct manifest_file files[MANIFEST_CHUNK_SIZE]; /**< The chunk of the manifest */
   int size;                                        /**< The size of the chunk */
};

/**
 * Verify checksum of the manifest and the checksum
 * @param root The root directory holding the manifest
 * @param file_checksums The ART map from relatvie file path to file checksums
 * @param file_sizes The ART map from relative file path to file sizes
 * @return 0 if verification turns out ok, 1 otherwise
 */
int
pgmoneta_manifest_checksum_verify(char* root, struct art* file_checksums, struct art* file_sizes);

/**
 * Compare manifests
 * @param manifest1 The path to the first manifest
 * @param manifest2 The path to the second manifest
 * @param deleted_files The deleted files
 * @param changed_files The changed files
 * @param added_files The added files
 * @return 0 on parsing success, otherwise 1
 */
int
pgmoneta_compare_manifests(char* old_manifest, char* new_manifest, struct art** deleted_files, struct art** changed_files, struct art** added_files);

/**
 * Generate the manifest on disk according to postgres manifest format
 * @param manifest The manifest
 * @param path The path
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_write_postgresql_manifest(struct json* manifest, char* path);

/**
 * Generate the manifest in memory (json format)
 * @param version The manifest file version
 * @param system_id The system identifier, an optional parameter for manifest version 2 and above
 * @param backup_data The data directory
 * @param backup The backup related to the manifest
 * @param manifest [out] The json manifest generated
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_generate_manifest(int version, uint64_t system_id, char* backup_data, struct backup* bck, struct json** manifest);

/**
 * Get the manifest record of a file
 * @param path The system path of the file
 * @param manifest_path The path to be used in manifest record entry
 * @param file [out] The json returning the manifest record
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_get_file_manifest(char* path, char* manifest_path, struct json** file);

/**
 * Generate the files manifest (in json format)
 * @param path The path to walk for manifest creation
 * @param files The json to append results
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_generate_files_manifest(char* path, struct json* files);

#ifdef __cplusplus
}
#endif

#endif
