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
#include <info.h>
#include <logging.h>
#include <utils.h>

/* system */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void
pgmoneta_create_info(char* directory, int status, char* label, char* wal, unsigned long size, int elapsed_time, char* version)
{
   char buffer[128];
   char* s = NULL;
   FILE* sfile = NULL;

   s = pgmoneta_append(s, directory);
   s = pgmoneta_append(s, "/backup.info");

   sfile = fopen(s, "w");

   if (status == 1)
   {
      fputs("STATUS=1\n", sfile);

      memset(&buffer[0], 0, sizeof(buffer));
      snprintf(&buffer[0], sizeof(buffer), "LABEL=%s\n", label);
      fputs(&buffer[0], sfile);

      memset(&buffer[0], 0, sizeof(buffer));
      snprintf(&buffer[0], sizeof(buffer), "WAL=%s\n", wal);
      fputs(&buffer[0], sfile);

      memset(&buffer[0], 0, sizeof(buffer));
      snprintf(&buffer[0], sizeof(buffer), "ELAPSED=%d\n", elapsed_time);
      fputs(&buffer[0], sfile);

      memset(&buffer[0], 0, sizeof(buffer));
      snprintf(&buffer[0], sizeof(buffer), "VERSION=%s\n", version);
      fputs(&buffer[0], sfile);

      memset(&buffer[0], 0, sizeof(buffer));
      snprintf(&buffer[0], sizeof(buffer), "KEEP=0\n");
      fputs(&buffer[0], sfile);

      memset(&buffer[0], 0, sizeof(buffer));
      snprintf(&buffer[0], sizeof(buffer), "RESTORE=%lu\n", size);
      fputs(&buffer[0], sfile);
   }
   else
   {
      fputs("STATUS=0\n", sfile);

      memset(&buffer[0], 0, sizeof(buffer));
      snprintf(&buffer[0], sizeof(buffer), "LABEL=%s\n", label);
      fputs(&buffer[0], sfile);
   }

   if (sfile != NULL)
   {
      fclose(sfile);
   }

   free(s);
}

void
pgmoneta_add_backup_info(char* directory, unsigned long size)
{
   char buffer[128];
   char* s = NULL;
   FILE* sfile = NULL;

   s = pgmoneta_append(s, directory);
   s = pgmoneta_append(s, "/backup.info");

   sfile = fopen(s, "a");

   memset(&buffer[0], 0, sizeof(buffer));
   snprintf(&buffer[0], sizeof(buffer), "BACKUP=%lu\n", size);
   fputs(&buffer[0], sfile);

   if (sfile != NULL)
   {
      fclose(sfile);
   }

   free(s);
}

void
pgmoneta_update_backup_info(char* directory, unsigned long size)
{
   char buffer[128];
   char line[128];
   char* s = NULL;
   FILE* sfile = NULL;
   char* d = NULL;
   FILE* dfile = NULL;

   s = pgmoneta_append(s, directory);
   s = pgmoneta_append(s, "/backup.info");

   d = pgmoneta_append(d, directory);
   d = pgmoneta_append(d, "/backup.info.tmp");

   sfile = fopen(s, "r");
   dfile = fopen(d, "w");

   while ((fgets(&buffer[0], sizeof(buffer), sfile)) != NULL)
   {
      char key[MISC_LENGTH];
      char value[MISC_LENGTH];
      char* ptr = NULL;

      memset(&key[0], 0, sizeof(key));
      memset(&value[0], 0, sizeof(value));

      memset(&line[0], 0, sizeof(line));
      memcpy(&line[0], &buffer[0], strlen(&buffer[0]));

      ptr = strtok(&buffer[0], "=");
      memcpy(&key[0], ptr, strlen(ptr));

      ptr = strtok(NULL, "=");
      memcpy(&value[0], ptr, strlen(ptr) - 1);

      if (!strcmp("BACKUP", &key[0]))
      {
         memset(&line[0], 0, sizeof(line));
         snprintf(&line[0], sizeof(line), "BACKUP=%lu\n", size);
         fputs(&line[0], dfile);
      }
      else
      {
         fputs(&line[0], dfile);
      }
   }

   if (sfile != NULL)
   {
      fclose(sfile);
   }

   if (dfile != NULL)
   {
      fclose(dfile);
   }

   pgmoneta_move_file(d, s);

   free(s);
   free(d);
}

void
pgmoneta_update_keep_info(char* directory, bool k)
{
   char buffer[128];
   char line[128];
   char* s = NULL;
   FILE* sfile = NULL;
   char* d = NULL;
   FILE* dfile = NULL;

   s = pgmoneta_append(s, directory);
   s = pgmoneta_append(s, "/backup.info");

   d = pgmoneta_append(d, directory);
   d = pgmoneta_append(d, "/backup.info.tmp");

   sfile = fopen(s, "r");
   dfile = fopen(d, "w");

   while ((fgets(&buffer[0], sizeof(buffer), sfile)) != NULL)
   {
      char key[MISC_LENGTH];
      char value[MISC_LENGTH];
      char* ptr = NULL;

      memset(&key[0], 0, sizeof(key));
      memset(&value[0], 0, sizeof(value));

      memset(&line[0], 0, sizeof(line));
      memcpy(&line[0], &buffer[0], strlen(&buffer[0]));

      ptr = strtok(&buffer[0], "=");
      memcpy(&key[0], ptr, strlen(ptr));

      ptr = strtok(NULL, "=");
      memcpy(&value[0], ptr, strlen(ptr) - 1);

      if (!strcmp("KEEP", &key[0]))
      {
         memset(&line[0], 0, sizeof(line));
         snprintf(&line[0], sizeof(line), "KEEP=%d\n", k ? 1 : 0);
         fputs(&line[0], dfile);
      }
      else
      {
         fputs(&line[0], dfile);
      }
   }

   if (sfile != NULL)
   {
      fclose(sfile);
   }

   if (dfile != NULL)
   {
      fclose(dfile);
   }

   pgmoneta_move_file(d, s);

   free(s);
   free(d);
}

int
pgmoneta_get_backups(char* directory, int* number_of_backups, struct backup*** backups)
{
   char* d;
   struct backup** bcks = NULL;
   int number_of_directories;
   char** dirs;

   *number_of_backups = 0;
   *backups = NULL;

   number_of_directories = 0;
   dirs = NULL;

   pgmoneta_get_directories(directory, &number_of_directories, &dirs);

   bcks = (struct backup**)malloc(number_of_directories * sizeof(struct backup*));

   for (int i = 0; i < number_of_directories; i++)
   {
      d = NULL;

      d = pgmoneta_append(d, directory);

      pgmoneta_get_backup(d, dirs[i], &bcks[i]);

      free(d);
   }

   for (int i = 0; i < number_of_directories; i++)
   {
      free(dirs[i]);
   }
   free(dirs);

   *number_of_backups = number_of_directories;
   *backups = bcks;

   return 0;
}

int
pgmoneta_get_backup(char* directory, char* label, struct backup** backup)
{
   char buffer[MISC_LENGTH];
   char* fn;
   FILE* file = NULL;
   struct backup* bck;

   *backup = NULL;

   fn = NULL;
   fn = pgmoneta_append(fn, directory);
   fn = pgmoneta_append(fn, "/");
   fn = pgmoneta_append(fn, label);
   fn = pgmoneta_append(fn, "/backup.info");

   file = fopen(fn, "r");

   bck = (struct backup*)malloc(sizeof(struct backup));
   memset(bck, 0, sizeof(struct backup));

   memcpy(&bck->label[0], label, strlen(label));
   bck->valid = VALID_UNKNOWN;

   if (file != NULL)
   {
      while ((fgets(&buffer[0], sizeof(buffer), file)) != NULL)
      {
         char key[MISC_LENGTH];
         char value[MISC_LENGTH];
         char* ptr = NULL;

         memset(&key[0], 0, sizeof(key));
         memset(&value[0], 0, sizeof(value));

         ptr = strtok(&buffer[0], "=");
         memcpy(&key[0], ptr, strlen(ptr));

         ptr = strtok(NULL, "=");
         memcpy(&value[0], ptr, strlen(ptr) - 1);

         if (!strcmp("STATUS", &key[0]))
         {
            if (!strcmp("1", &value[0]))
            {
               bck->valid = VALID_TRUE;
            }
            else
            {
               bck->valid = VALID_FALSE;
            }
         }
         else if (!strcmp("LABEL", &key[0]))
         {
            memcpy(&bck->label[0], &value[0], strlen(&value[0]));
         }
         else if (!strcmp("WAL", &key[0]))
         {
            memcpy(&bck->wal[0], &value[0], strlen(&value[0]));
         }
         else if (!strcmp("BACKUP", &key[0]))
         {
            bck->backup_size = strtoul(&value[0], &ptr, 10);
         }
         else if (!strcmp("RESTORE", &key[0]))
         {
            bck->restore_size = strtoul(&value[0], &ptr, 10);
         }
         else if (!strcmp("ELAPSED", &key[0]))
         {
         bck->elapsed_time = atoi(&value[0]);
         }
         else if (!strcmp("VERSION", &key[0]))
         {
            bck->version = atoi(&value[0]);
         }
         else if (!strcmp("KEEP", &key[0]))
         {
            bck->keep = atoi(&value[0]) == 1 ? true : false;
         }
      }
   }

   *backup = bck;

   if (file != NULL)
   {
      fclose(file);
   }

   free(fn);

   return 0;
}

int
pgmoneta_get_number_of_valid_backups(int server)
{
   char* server_path = NULL;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   struct configuration* config = NULL;
   int result = 0;

   config = (struct configuration*)shmem;

   server_path = pgmoneta_append(server_path, config->base_dir);
   if (!pgmoneta_ends_with(config->base_dir, "/"))
   {
      server_path = pgmoneta_append(server_path, "/");
   }
   server_path = pgmoneta_append(server_path, config->servers[server].name);
   server_path = pgmoneta_append(server_path, "/backup/");

   pgmoneta_get_backups(server_path, &number_of_backups, &backups);

   for (int i = 0; i < number_of_backups; i++)
   {
      if (backups[i]->valid)
      {
         result++;
      }
   }

   free(server_path);
   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   return result;
}
