/*
 * Copyright (C) 2026 The pgexporter community
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

#ifndef YAML_UTILS_H
#define YAML_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>

/**
 * @struct config_t
 * @brief Represents the entire configuration parsed from the YAML file.
 * It includes source and target directories, and rules for operations and xids.
 */
typedef struct
{
   char* source_dir;         /**< Source directory for the configuration */
   char* target_dir;         /**< Target directory for the configuration */
   char* encryption;         /**< Encryption method used in the configuration */
   char* compression;        /**< Compression method used in the configuration */
   char* configuration_file; /**< Path to the configuration file */
   char** operations;        /**< Array of operation names from rules */
   int operation_count;      /**< Number of operations in the array */
   int* xids;                /**< Array of XIDs from rules */
   int xid_count;            /**< Number of XIDs in the array */
} config_t;

/**
 * @enum parser_state_t
 * @brief Represents the state of the YAML parser.
 * This enum is used to track the current parsing context.
 */
typedef enum {
   STATE_START,               /**< Initial state of the parser */
   STATE_ROOT,                /**< Root state of the YAML document */
   STATE_RULES_SEQUENCE,      /**< Parsing the sequence of rules */
   STATE_RULE_MAPPING,        /**< Parsing a rule mapping (operations or xids) */
   STATE_OPERATIONS_SEQUENCE, /**< Parsing the operations sequence */
   STATE_XIDS_SEQUENCE        /**< Parsing the XIDs sequence */
} parser_state_t;

/**
 * Parses a YAML configuration file and populates the config_t structure.
 *
 * @param filename The path to the YAML configuration file.
 * @param config Pointer to the config_t structure to populate.
 * @return 0 on success, non-zero on failure.
 */
int pgmoneta_parse_yaml_config(const char* filename, config_t* config);

/**
 * Handles scalar events during YAML parsing.
 * This function processes scalar values based on the current parser state.
 * @param event The YAML event to handle.
 * @param state Pointer to the parser state.
 * @param current_key Pointer to the current key being processed.
 * @param config Pointer to the configuration structure to populate.
 */
void handle_scalar_event(yaml_event_t* event, parser_state_t* state,
                         char** current_key, config_t* config);

/**
 * Cleans up the configuration structure.
 * This function frees allocated memory for the configuration structure,
 * including source_dir, target_dir, operations, and xids.
 * @param config Pointer to the config_t structure to clean up.
 */
void cleanup_config(config_t* config);

#ifdef __cplusplus
}
#endif

#endif // YAML_UTILS_H