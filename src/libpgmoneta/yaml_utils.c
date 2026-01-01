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

/* pgmoneta */
#include "yaml_utils.h"

/* system */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>

int
pgmoneta_parse_yaml_config(const char* filename, config_t* config)
{
   memset(config, 0, sizeof(config_t));

   FILE* file = fopen(filename, "rb");
   if (!file)
   {
      fprintf(stderr, "Failed to open file: %s\n", filename);
      return -1;
   }

   yaml_parser_t parser;
   yaml_event_t event;

   if (!yaml_parser_initialize(&parser))
   {
      fprintf(stderr, "Failed to initialize parser\n");
      fclose(file);
      return -1;
   }

   yaml_parser_set_input_file(&parser, file);

   parser_state_t state = STATE_START;
   char* current_key = NULL;
   int done = 0;

   while (!done)
   {
      if (!yaml_parser_parse(&parser, &event))
      {
         fprintf(stderr, "Parser error %d\n", parser.error);
         break;
      }

      switch (event.type)
      {
         case YAML_STREAM_START_EVENT:
            break;

         case YAML_DOCUMENT_START_EVENT:
            state = STATE_ROOT;
            break;

         case YAML_MAPPING_START_EVENT:
            if (state == STATE_ROOT)
            {
               // Stay in root state for top-level mapping
            }
            else if (state == STATE_RULES_SEQUENCE)
            {
               // Starting a new rule mapping (either operations or xids)
               state = STATE_RULE_MAPPING;
            }
            break;

         case YAML_MAPPING_END_EVENT:
            if (state == STATE_RULE_MAPPING)
            {
               state = STATE_RULES_SEQUENCE;
            }
            else if (state == STATE_ROOT)
            {
               // End of document
            }
            break;

         case YAML_SEQUENCE_START_EVENT:
            if (state == STATE_ROOT && current_key &&
                strcmp(current_key, "rules") == 0)
            {
               state = STATE_RULES_SEQUENCE;
               free(current_key);
               current_key = NULL;
            }
            else if (state == STATE_RULE_MAPPING && current_key &&
                     strcmp(current_key, "operations") == 0)
            {
               state = STATE_OPERATIONS_SEQUENCE;
               free(current_key);
               current_key = NULL;
            }
            else if (state == STATE_RULE_MAPPING && current_key &&
                     strcmp(current_key, "xids") == 0)
            {
               state = STATE_XIDS_SEQUENCE;
               free(current_key);
               current_key = NULL;
            }
            break;

         case YAML_SEQUENCE_END_EVENT:
            if (state == STATE_OPERATIONS_SEQUENCE)
            {
               state = STATE_RULE_MAPPING;
            }
            else if (state == STATE_XIDS_SEQUENCE)
            {
               state = STATE_RULE_MAPPING;
            }
            else if (state == STATE_RULES_SEQUENCE)
            {
               state = STATE_ROOT;
            }
            break;

         case YAML_SCALAR_EVENT:
            handle_scalar_event(&event, &state, &current_key, config);
            break;

         case YAML_DOCUMENT_END_EVENT:
         case YAML_STREAM_END_EVENT:
            done = 1;
            break;

         default:
            break;
      }

      yaml_event_delete(&event);
   }

   if (current_key)
   {
      free(current_key);
   }
   yaml_parser_delete(&parser);
   fclose(file);

   return 0;
}

void
handle_scalar_event(yaml_event_t* event, parser_state_t* state,
                    char** current_key, config_t* config)
{
   char* value = (char*)event->data.scalar.value;

   switch (*state)
   {
      case STATE_ROOT:
         if (*current_key == NULL)
         {
            *current_key = strdup(value);
         }
         else
         {
            if (strcmp(*current_key, "source_dir") == 0)
            {
               config->source_dir = strdup(value);
            }
            else if (strcmp(*current_key, "target_dir") == 0)
            {
               config->target_dir = strdup(value);
            }
            else if (strcmp(*current_key, "encryption") == 0)
            {
               config->encryption = strdup(value);
            }
            else if (strcmp(*current_key, "compression") == 0)
            {
               config->compression = strdup(value);
            }
            else if (strcmp(*current_key, "configuration_file") == 0)
            {
               config->configuration_file = strdup(value);
            }
            free(*current_key);
            *current_key = NULL;
         }
         break;

      case STATE_RULE_MAPPING:
         if (*current_key == NULL)
         {
            *current_key = strdup(value);
         }
         else
         {
            // Handle other rule properties if needed
            free(*current_key);
            *current_key = NULL;
         }
         break;

      case STATE_OPERATIONS_SEQUENCE:
         // Add operation to operations array
         config->operations = realloc(
            config->operations,
            (config->operation_count + 1) * sizeof(char*));
         config->operations[config->operation_count] = strdup(value);
         config->operation_count++;
         break;

      case STATE_XIDS_SEQUENCE:
         // Add XID to the XIDs array
         config->xids = realloc(config->xids,
                                (config->xid_count + 1) * sizeof(int));
         config->xids[config->xid_count] = atoi(value);
         config->xid_count++;
         break;

      default:
         break;
   }
}

void
cleanup_config(config_t* config)
{
   if (config->source_dir)
   {
      free(config->source_dir);
   }
   if (config->target_dir)
   {
      free(config->target_dir);
   }
   if (config->encryption)
   {
      free(config->encryption);
   }
   if (config->compression)
   {
      free(config->compression);
   }
   if (config->configuration_file)
   {
      free(config->configuration_file);
   }

   // Clean up operations
   for (int i = 0; i < config->operation_count; i++)
   {
      free(config->operations[i]);
   }
   if (config->operations)
   {
      free(config->operations);
   }

   // Clean up XIDs
   if (config->xids)
   {
      free(config->xids);
   }
}