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

#ifndef CMD_H
#define CMD_H

#include <pgmoneta.h>

#include <stdbool.h>

/**
 * @struct cli_option
 * Struct to hold option definition
 */
typedef struct
{
   char* short_name;    /**< Short option name */
   char* long_name;     /**< Long option name */
   bool requires_arg;   /**< Whether this option requires an argument */
} cli_option;

/**
 * @struct cli_result
 * Struct to hold parsed option result
 */
typedef struct
{
   char* option_name;   /**< The matched option name (short or long) */
   char* argument;      /**< Argument value if applicable, NULL otherwise */
} cli_result;

/**
 * Parse command line arguments based on the provided options
 *
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @param options Array of option definitions
 * @param num_options Number of options in the array
 * @param results Output array for results
 * @param num_results Maximum number of results to store
 * @param use_last_arg_as_filename Whether to use the last argument as a filename
 * @param filename Output parameter for filename if requested
 * @param optind Output parameter for the index of the first non-option argument
 *
 * @return Number of results found, or -1 on error
 */
int cmd_parse(
   int argc,
   char** argv,
   cli_option* options,
   int num_options,
   cli_result* results,
   int num_results,
   bool use_last_arg_as_filename,
   char** filename,
   int* optind
   );

#endif
