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
 *
 */

#include <tsclient_helpers.h>
#include <json.h>
#include <management.h>

#include <string.h>

int
pgmoneta_tsclient_get_backup_count(struct json* response)
{
   struct json* response_obj = NULL;

   if (response == NULL)
   {
      return -1;
   }

   // Response structure: { "Response": { "NumberOfBackups": N, "Backups": [...] } }
   response_obj = (struct json*)pgmoneta_json_get(response, MANAGEMENT_CATEGORY_RESPONSE);
   if (response_obj == NULL)
   {
      return -1;
   }

   if (!pgmoneta_json_contains_key(response_obj, MANAGEMENT_ARGUMENT_NUMBER_OF_BACKUPS))
   {
      return -1;
   }

   return (int)pgmoneta_json_get(response_obj, MANAGEMENT_ARGUMENT_NUMBER_OF_BACKUPS);
}

struct json*
pgmoneta_tsclient_get_backup(struct json* response, int index)
{
   struct json* response_obj = NULL;
   struct json* backups = NULL;
   struct json_iterator* it = NULL;
   struct json* result = NULL;

   if (response == NULL || index < 0)
   {
      return NULL;
   }

   // Navigate to Response
   response_obj = (struct json*)pgmoneta_json_get(response, MANAGEMENT_CATEGORY_RESPONSE);
   if (response_obj == NULL)
   {
      return NULL;
   }

   // Navigate to Backups array
   backups = (struct json*)pgmoneta_json_get(response_obj, MANAGEMENT_ARGUMENT_BACKUPS);
   if (backups == NULL || backups->type != JSONArray)
   {
      return NULL;
   }

   if (index >= pgmoneta_json_array_length(backups))
   {
      return NULL;
   }

   // Iterate to the requested index and return the element
   if (pgmoneta_json_iterator_create(backups, &it))
   {
      return NULL;
   }
   for (int i = 0; i <= index; i++)
   {
      if (!pgmoneta_json_iterator_next(it))
      {
         pgmoneta_json_iterator_destroy(it);
         return NULL;
      }
   }
   result = (struct json*)pgmoneta_value_data(it->value);
   pgmoneta_json_iterator_destroy(it);
   return result;
}

char*
pgmoneta_tsclient_get_backup_label(struct json* backup)
{
   if (backup == NULL)
   {
      return NULL;
   }

   return (char*)pgmoneta_json_get(backup, MANAGEMENT_ARGUMENT_BACKUP);
}

char*
pgmoneta_tsclient_get_backup_type(struct json* backup)
{
   if (backup == NULL)
   {
      return NULL;
   }

   if (!pgmoneta_json_contains_key(backup, MANAGEMENT_ARGUMENT_INCREMENTAL))
   {
      return NULL;
   }

   if ((bool)pgmoneta_json_get(backup, MANAGEMENT_ARGUMENT_INCREMENTAL))
   {
      return "INCREMENTAL";
   }
   else
   {
      return "FULL";
   }
}

char*
pgmoneta_tsclient_get_backup_parent(struct json* backup)
{
   if (backup == NULL)
   {
      return NULL;
   }

   char* parent = (char*)pgmoneta_json_get(backup, MANAGEMENT_ARGUMENT_INCREMENTAL_PARENT);

   // Convert empty string to NULL for FULL backups
   if (parent != NULL && strlen(parent) == 0)
   {
      return NULL;
   }

   return parent;
}

bool
pgmoneta_tsclient_verify_backup_chain(struct json* parent, struct json* child)
{
   char* parent_label = NULL;
   char* child_parent = NULL;

   if (parent == NULL || child == NULL)
   {
      return false;
   }

   parent_label = pgmoneta_tsclient_get_backup_label(parent);
   child_parent = pgmoneta_tsclient_get_backup_parent(child);

   if (parent_label == NULL || child_parent == NULL)
   {
      return false;
   }

   return strcmp(parent_label, child_parent) == 0;
}
