/*
 * Copyright (C) 2025 The pgexporter community
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

#include <stdio.h>
#include "yaml_utils.h"

int
main(int argc, char* argv[])
{
   if (argc != 2)
   {
      fprintf(stderr, "Usage: %s <yaml_file>\n", argv[0]);
      return 1;
   }

   config_t config;

   if (pgmoneta_parse_yaml_config(argv[1], &config) != 0)
   {
      fprintf(stderr, "Failed to parse configuration\n");
      return 1;
   }

   printf("Configuration:\n");
   printf("  source_dir: %s\n", config.source_dir ? config.source_dir : "NULL");
   printf("  target_dir: %s\n", config.target_dir ? config.target_dir : "NULL");
   printf("  rules (%d):\n", config.rule_count);

   for (int i = 0; i < config.rule_count; i++)
   {
      printf("    rule %d:\n", i);
      printf("      exclude operations (%d):\n",
             config.rules[i].exclude.operation_count);
      for (int j = 0; j < config.rules[i].exclude.operation_count; j++)
      {
         printf("        - %s\n", config.rules[i].exclude.operations[j]);
      }
   }

   cleanup_config(&config);
   return 0;
}
