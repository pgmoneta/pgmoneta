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
   DIR *dir = NULL;
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

int
pgmoneta_delete_wal(int srv)
{
   char* id = NULL;
   char* d = NULL;
   int number_of_directories = 0;
   char** dirs = NULL;
   char* srv_wal = NULL;
   int number_of_srv_wal_files = 0;
   char** srv_wal_files = NULL;
   int number_of_wal_files = 0;
   char** wal_files = NULL;
   bool delete;
   struct configuration* config;

   config = (struct configuration*)shmem;

   /* Find the oldest backup */
   d = NULL;
   d = pgmoneta_append(d, config->base_dir);
   d = pgmoneta_append(d, "/");
   d = pgmoneta_append(d, config->servers[srv].name);
   d = pgmoneta_append(d, "/backup/");

   if (pgmoneta_get_directories(d, &number_of_directories, &dirs))
   {
      goto error;
   }

   if (number_of_directories > 0)
   {
      id = dirs[0];
   }

   free(d);

   /* Find the oldest WAL file */
   if (id != NULL)
   {
      d = NULL;
      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[srv].name);
      d = pgmoneta_append(d, "/backup/");
      d = pgmoneta_append(d, id);
      d = pgmoneta_append(d, "/data/pg_wal/");

      number_of_srv_wal_files = 0;
      srv_wal_files = NULL;

      pgmoneta_get_files(d, &number_of_srv_wal_files, &srv_wal_files);

      if (number_of_srv_wal_files > 0)
      {
         srv_wal = srv_wal_files[0];
      }

      free(d);
   }

   /* Find WAL files */
   d = NULL;
   d = pgmoneta_append(d, config->base_dir);
   d = pgmoneta_append(d, "/");
   d = pgmoneta_append(d, config->servers[srv].name);
   d = pgmoneta_append(d, "/wal/");

   number_of_wal_files = 0;
   wal_files = NULL;

   pgmoneta_get_files(d, &number_of_wal_files, &wal_files);

   free(d);

   /* Delete outdated WAL files */
   for (int i = 0; i < number_of_wal_files; i++)
   {
      if (pgmoneta_ends_with(wal_files[i], ".partial"))
      {
         continue;
      }

      delete = false;

      if (id == NULL)
      {
         delete = true;
      }
      else if (srv_wal != NULL)
      {
         if (strcmp(wal_files[i], srv_wal) < 0)
         {
            delete = true;
         }
      }

      if (delete)
      {
         d = NULL;
         d = pgmoneta_append(d, config->base_dir);
         d = pgmoneta_append(d, "/");
         d = pgmoneta_append(d, config->servers[srv].name);
         d = pgmoneta_append(d, "/wal/");
         d = pgmoneta_append(d, wal_files[i]);

         pgmoneta_delete_file(d);

         free(d);
      }
   }

   for (int i = 0; i < number_of_srv_wal_files; i++)
   {
      free(srv_wal_files[i]);
   }
   free(srv_wal_files);

   for (int i = 0; i < number_of_wal_files; i++)
   {
      free(wal_files[i]);
   }
   free(wal_files);

   for (int i = 0; i < number_of_directories; i++)
   {
      free(dirs[i]);
   }
   free(dirs);

   return 0;

error:

   free(d);

   for (int i = 0; i < number_of_srv_wal_files; i++)
   {
      free(srv_wal_files[i]);
   }
   free(srv_wal_files);

   for (int i = 0; i < number_of_wal_files; i++)
   {
      free(wal_files[i]);
   }
   free(wal_files);

   for (int i = 0; i < number_of_directories; i++)
   {
      free(dirs[i]);
   }
   free(dirs);

   return 1;
}
