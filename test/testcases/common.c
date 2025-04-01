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

#include "common.h"

char project_directory[BUFFER_SIZE];

char*
get_executable_path()
{
   char* executable_path = NULL;
   int project_directory_length = strlen(project_directory);
   int executable_trail_length = strlen(PGMONETA_EXECUTABLE_TRAIL);

   executable_path = (char*)calloc(project_directory_length + executable_trail_length + 1, sizeof(char));

   memcpy(executable_path, project_directory, project_directory_length);
   memcpy(executable_path + project_directory_length, PGMONETA_EXECUTABLE_TRAIL, executable_trail_length);

   return executable_path;
}

char*
get_log_path()
{
   char* log_path = NULL;
   int project_directory_length = strlen(project_directory);
   int log_trail_length = strlen(PGMONETA_LOG_FILE_TRAIL);

   log_path = (char*)calloc(project_directory_length + log_trail_length + 1, sizeof(char));

   memcpy(log_path, project_directory, project_directory_length);
   memcpy(log_path + project_directory_length, PGMONETA_LOG_FILE_TRAIL, log_trail_length);

   return log_path;
}

char*
get_restore_path()
{
   char* restore_path = NULL;
   int project_directory_length = strlen(project_directory);
   int restore_trail_length = strlen(PGMONETA_RESTORE_TRAIL);

   restore_path = (char*)calloc(project_directory_length + restore_trail_length + 1, sizeof(char));

   memcpy(restore_path, project_directory, project_directory_length);
   memcpy(restore_path + project_directory_length, PGMONETA_RESTORE_TRAIL, restore_trail_length);

   return restore_path;
}

char*
get_configuration_path()
{
   char* configuration_path = NULL;
   int project_directory_length = strlen(project_directory);
   int configuration_trail_length = strlen(PGMONETA_CONFIGURATION_TRAIL);

   configuration_path = (char*)calloc(project_directory_length + configuration_trail_length + 1, sizeof(char));

   memcpy(configuration_path, project_directory, project_directory_length);
   memcpy(configuration_path + project_directory_length, PGMONETA_CONFIGURATION_TRAIL, configuration_trail_length);

   return configuration_path;
}

int
get_last_log_entry(char* log_path, char** b)
{
   FILE* log_file;
   char tmp[BUFFER_SIZE];
   char* buffer = NULL;
   char* file_pos = NULL;
   char* last_entry = NULL;
   int last_entry_length = 0;

   memset(tmp, 0, BUFFER_SIZE);
   // open the file in read mode
   log_file = fopen(log_path, "r");
   if (!log_file)
   {
      return 1;
   }

   // get the position of file pointer maxlen backwards from end
   fseek(log_file, -(BUFFER_SIZE - 1), SEEK_END);
   fread(tmp, BUFFER_SIZE - 1, 1, log_file);
   fclose(log_file);

   if (tmp[strlen(tmp) - 1] == '\n')  // remove the trailing newline character if exists
   {
      tmp[strlen(tmp) - 1] = '\0';
   }

   file_pos = strrchr(tmp, '\n');

   if (!file_pos)
   {
      last_entry = &tmp[0];
   }
   else
   {
      last_entry = file_pos + 1;
   }

   last_entry_length = strlen(last_entry);
   buffer = (char*)calloc(last_entry_length + 1, sizeof(char));
   memcpy(buffer, last_entry, last_entry_length);

   *b = buffer;

   return 0;
}