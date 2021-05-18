/*
 * Copyright (C) 2021 Red Hat
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
#include <pgmoneta.h>
#include <delete.h>
#include <logging.h>
#include <utils.h>

/* system */
#include <dirent.h>

int
pgmoneta_delete(int srv, char* backup_id)
{
   char* id = NULL;
   char* d = NULL;
   int number_of_directories = 0;
   char** array = NULL;
   DIR *dir;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (!strcmp(backup_id, "oldest"))
   {
      d = NULL;
      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[srv].name);
      d = pgmoneta_append(d, "/backup/");

      if (pgmoneta_get_directories(d, &number_of_directories, &array))
      {
         goto error;
      }

      pgmoneta_sort(number_of_directories, array);

      if (number_of_directories > 0)
      {
         id = array[0];
      }

      free(d);
      d = NULL;
   }
   else if (!strcmp(backup_id, "latest") || !strcmp(backup_id, "newest"))
   {
      d = NULL;
      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[srv].name);
      d = pgmoneta_append(d, "/backup/");

      if (pgmoneta_get_directories(d, &number_of_directories, &array))
      {
         goto error;
      }

      pgmoneta_sort(number_of_directories, array);

      if (number_of_directories > 0)
      {
         id = array[number_of_directories - 1];
      }

      free(d);
      d = NULL;
   }
   else
   {
      id = backup_id;
   }

   if (id == NULL)
   {
      goto error;
   }

   d = pgmoneta_append(d, config->base_dir);
   d = pgmoneta_append(d, "/");
   d = pgmoneta_append(d, config->servers[srv].name);
   d = pgmoneta_append(d, "/backup/");
   d = pgmoneta_append(d, id);

   if (!(dir = opendir(d)))
   {
      goto error;
   }

   closedir(dir);

   pgmoneta_delete_directory(d);

   pgmoneta_log_info("Delete: %s/%s", config->servers[srv].name, id);

   for (int i = 0; i < number_of_directories; i++)
   {
      free(array[i]);
   }
   free(array);

   free(d);

   return 0;

error:

   if (dir != NULL)
   {
      closedir(dir);
   }
   
   for (int i = 0; i < number_of_directories; i++)
   {
      free(array[i]);
   }
   free(array);

   free(d);

   return 1;
}
