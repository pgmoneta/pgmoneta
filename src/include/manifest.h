/*
 * Copyright (C) 2024 The pgmoneta community
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

struct manifest_file
{
   char* path;
   char* checksum;
   char* algorithm;
   size_t size;
   struct manifest_file* next;
} __attribute__ ((aligned (64)));

struct manifest
{
   char* checksum;
   char* content;
   struct manifest_file* files;
} __attribute__ ((aligned (64)));

/**
 * Parse the manifest json into manifest struct
 * @param manifest_path The path to manifest
 * @param manifest The manifest
 * @return 0 on parsing success, otherwise 1
 */
int
pgmoneta_parse_manifest(char* manifest_path, struct manifest** manifest);

/**
 * Verify checksum of the manifest and the checksum
 * @param root The root directory holding the manifest
 * @return 0 if verification turns out ok, 1 otherwise
 */
int
pgmoneta_manifest_checksum_verify(char* root);

/**
 * Free a manifest
 * @param manifest The manifest to be freed
 */
void
pgmoneta_manifest_free(struct manifest* manifest);

#ifdef __cplusplus
}
#endif

#endif
