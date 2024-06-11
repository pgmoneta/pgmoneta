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

/* pgmoneta */
#include <pgmoneta.h>
#include <info.h>
#include <logging.h>
#include <utils.h>
#include <io.h>

/* system */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void
pgmoneta_create_info(char* directory, char* label, int status)
{
   char buffer[128];
   char* s = NULL;
   FILE* sfile = NULL;

   s = pgmoneta_append(s, directory);
   s = pgmoneta_append(s, "/backup.info");

   sfile = pgmoneta_open_file(s, "w");

   memset(&buffer[0], 0, sizeof(buffer));
   snprintf(&buffer[0], sizeof(buffer), "STATUS=%d\n", status);
   fputs(&buffer[0], sfile);

   memset(&buffer[0], 0, sizeof(buffer));
   snprintf(&buffer[0], sizeof(buffer), "LABEL=%s\n", label);
   fputs(&buffer[0], sfile);

   memset(&buffer[0], 0, sizeof(buffer));
   snprintf(&buffer[0], sizeof(buffer), "TABLESPACES=0\n");
   fputs(&buffer[0], sfile);

   memset(&buffer[0], 0, sizeof(buffer));
   snprintf(&buffer[0], sizeof(buffer), "PGMONETA_VERSION=%s\n", VERSION);
   fputs(&buffer[0], sfile);

   pgmoneta_permission(s, 6, 0, 0);

   if (sfile != NULL)
   {
      fclose(sfile);
   }

   free(s);
}

void
pgmoneta_update_info_unsigned_long(char* directory, char* key, unsigned long value)
{
   char buffer[128];
   char line[128];
   bool found = false;
   char* s = NULL;
   FILE* sfile = NULL;
   char* d = NULL;
   FILE* dfile = NULL;

   s = pgmoneta_append(s, directory);
   s = pgmoneta_append(s, "/backup.info");

   d = pgmoneta_append(d, directory);
   d = pgmoneta_append(d, "/backup.info.tmp");

   sfile = pgmoneta_open_file(s, "r");
   dfile = pgmoneta_open_file(d, "w");

   while ((fgets(&buffer[0], sizeof(buffer), sfile)) != NULL)
   {
      char k[MISC_LENGTH];
      char v[MISC_LENGTH];
      char* ptr = NULL;

      memset(&k[0], 0, sizeof(k));
      memset(&v[0], 0, sizeof(v));

      memset(&line[0], 0, sizeof(line));
      memcpy(&line[0], &buffer[0], strlen(&buffer[0]));

      ptr = strtok(&buffer[0], "=");
      memcpy(&k[0], ptr, strlen(ptr));

      ptr = strtok(NULL, "=");
      memcpy(&v[0], ptr, strlen(ptr) - 1);

      if (!strcmp(key, &k[0]))
      {
         memset(&line[0], 0, sizeof(line));
         snprintf(&line[0], sizeof(line), "%s=%lu\n", key, value);
         fputs(&line[0], dfile);
         found = true;
      }
      else
      {
         fputs(&line[0], dfile);
      }
   }

   if (!found)
   {
      memset(&line[0], 0, sizeof(line));
      snprintf(&line[0], sizeof(line), "%s=%lu\n", key, value);
      fputs(&line[0], dfile);
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
   pgmoneta_permission(s, 6, 0, 0);

   free(s);
   free(d);
}

void
pgmoneta_update_info_string(char* directory, char* key, char* value)
{
   char buffer[128];
   char line[128];
   bool found = false;
   char* s = NULL;
   FILE* sfile = NULL;
   char* d = NULL;
   FILE* dfile = NULL;

   s = pgmoneta_append(s, directory);
   s = pgmoneta_append(s, "/backup.info");

   d = pgmoneta_append(d, directory);
   d = pgmoneta_append(d, "/backup.info.tmp");

   sfile = pgmoneta_open_file(s, "r");
   dfile = pgmoneta_open_file(d, "w");

   while ((fgets(&buffer[0], sizeof(buffer), sfile)) != NULL)
   {
      char k[MISC_LENGTH];
      char v[MISC_LENGTH];
      char* ptr = NULL;

      memset(&k[0], 0, sizeof(k));
      memset(&v[0], 0, sizeof(v));

      memset(&line[0], 0, sizeof(line));
      memcpy(&line[0], &buffer[0], strlen(&buffer[0]));

      ptr = strtok(&buffer[0], "=");
      memcpy(&k[0], ptr, strlen(ptr));

      ptr = strtok(NULL, "=");
      memcpy(&v[0], ptr, strlen(ptr) - 1);

      if (!strcmp(key, &k[0]))
      {
         memset(&line[0], 0, sizeof(line));
         snprintf(&line[0], sizeof(line), "%s=%s\n", key, value);
         fputs(&line[0], dfile);
         found = true;
      }
      else
      {
         fputs(&line[0], dfile);
      }
   }

   if (!found)
   {
      memset(&line[0], 0, sizeof(line));
      snprintf(&line[0], sizeof(line), "%s=%s\n", key, value);
      fputs(&line[0], dfile);
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
   pgmoneta_permission(s, 6, 0, 0);

   free(s);
   free(d);
}

void
pgmoneta_update_info_bool(char* directory, char* key, bool value)
{
   pgmoneta_update_info_unsigned_long(directory, key, value ? 1 : 0);
}

int
pgmoneta_get_info_string(struct backup* backup, char* key, char** value)
{
   char* result = NULL;

   if (!strcmp(INFO_LABEL, key))
   {
      result = pgmoneta_append(result, backup->label);
   }
   else if (!strcmp(INFO_WAL, key))
   {
      result = pgmoneta_append(result, backup->wal);
   }
   else if (pgmoneta_starts_with(key, "TABLESPACE"))
   {
      unsigned long number = strtoul(key + 10, NULL, 10);

      result = pgmoneta_append(result, backup->tablespaces[number - 1]);
   }
   else
   {
      goto error;
   }

   *value = result;

   return 0;

error:

   return 1;
}

int
pgmoneta_get_backups(char* directory, int* number_of_backups, struct backup*** backups)
{
   char* d = NULL;
   struct backup** bcks = NULL;
   int number_of_directories;
   char** dirs;

   *number_of_backups = 0;
   *backups = NULL;

   number_of_directories = 0;
   dirs = NULL;

   pgmoneta_get_directories(directory, &number_of_directories, &dirs);

   bcks = (struct backup**)malloc(number_of_directories * sizeof(struct backup*));

   if (bcks == NULL)
   {
      goto error;
   }

   memset(bcks, 0, number_of_directories * sizeof(struct backup*));

   for (int i = 0; i < number_of_directories; i++)
   {
      d = pgmoneta_append(d, directory);

      if (pgmoneta_get_backup(d, dirs[i], &bcks[i]))
      {
         goto error;
      }

      free(d);
      d = NULL;
   }

   for (int i = 0; i < number_of_directories; i++)
   {
      free(dirs[i]);
   }
   free(dirs);

   *number_of_backups = number_of_directories;
   *backups = bcks;

   return 0;

error:

   free(d);

   if (dirs != NULL)
   {
      for (int i = 0; i < number_of_directories; i++)
      {
         free(dirs[i]);
      }
      free(dirs);
   }

   return 1;
}

int
pgmoneta_get_backup(char* directory, char* label, struct backup** backup)
{
   char buffer[MISC_LENGTH];
   char* fn;
   FILE* file = NULL;
   int tbl_idx = 0;
   struct backup* bck;

   *backup = NULL;

   fn = NULL;
   fn = pgmoneta_append(fn, directory);
   fn = pgmoneta_append(fn, "/");
   fn = pgmoneta_append(fn, label);
   fn = pgmoneta_append(fn, "/backup.info");

   file = pgmoneta_open_file(fn, "r");

   bck = (struct backup*)malloc(sizeof(struct backup));

   if (bck == NULL)
   {
      goto error;
   }

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

         if (ptr == NULL)
         {
            goto error;
         }

         memcpy(&key[0], ptr, strlen(ptr));

         ptr = strtok(NULL, "=");

         if (ptr == NULL)
         {
            goto error;
         }

         memcpy(&value[0], ptr, strlen(ptr) - 1);

         if (!strcmp(INFO_STATUS, &key[0]))
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
         else if (!strcmp(INFO_LABEL, &key[0]))
         {
            memcpy(&bck->label[0], &value[0], strlen(&value[0]));
         }
         else if (!strcmp(INFO_WAL, &key[0]))
         {
            memcpy(&bck->wal[0], &value[0], strlen(&value[0]));
         }
         else if (!strcmp(INFO_BACKUP, &key[0]))
         {
            bck->backup_size = strtoul(&value[0], &ptr, 10);
         }
         else if (!strcmp(INFO_RESTORE, &key[0]))
         {
            bck->restore_size = strtoul(&value[0], &ptr, 10);
         }
         else if (!strcmp(INFO_ELAPSED, &key[0]))
         {
            bck->elapsed_time = atoi(&value[0]);
         }
         else if (!strcmp(INFO_VERSION, &key[0]))
         {
            bck->version = atoi(&value[0]);
         }
         else if (!strcmp(INFO_MINOR_VERSION, &key[0]))
         {
            bck->minor_version = atoi(&value[0]);
         }
         else if (!strcmp(INFO_KEEP, &key[0]))
         {
            bck->keep = atoi(&value[0]) == 1 ? true : false;
         }
         else if (!strcmp(INFO_TABLESPACES, &key[0]))
         {
            bck->number_of_tablespaces = strtoul(&value[0], &ptr, 10);
         }
         else if (pgmoneta_starts_with(&key[0], "TABLESPACE"))
         {
            memcpy(&bck->tablespaces[tbl_idx], &value[0], strlen(&value[0]));
            tbl_idx++;
         }
         else if (pgmoneta_starts_with(&key[0], INFO_START_WALPOS))
         {
            sscanf(&value[0], "%X/%X", &bck->start_lsn_hi32, &bck->start_lsn_lo32);
         }
         else if (pgmoneta_starts_with(&key[0], INFO_END_WALPOS))
         {
            sscanf(&value[0], "%X/%X", &bck->end_lsn_hi32, &bck->end_lsn_lo32);
         }
         else if (pgmoneta_starts_with(&key[0], INFO_CHKPT_WALPOS))
         {
            sscanf(&value[0], "%X/%X", &bck->checkpoint_lsn_hi32, &bck->checkpoint_lsn_lo32);
         }
         else if (pgmoneta_starts_with(&key[0], INFO_START_TIMELINE))
         {
            bck->start_timeline = atoi(&value[0]);
         }
         else if (pgmoneta_starts_with(&key[0], INFO_END_TIMELINE))
         {
            bck->end_timeline = atoi(&value[0]);
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

error:

   free(bck);

   if (file != NULL)
   {
      fclose(file);
   }

   free(fn);

   return 1;
}

int
pgmoneta_get_number_of_valid_backups(int server)
{
   char* server_path = NULL;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   int result = 0;

   server_path = pgmoneta_get_server_backup(server);
   if (server_path == NULL)
   {
      goto error;
   }

   if (pgmoneta_get_backups(server_path, &number_of_backups, &backups))
   {
      goto error;
   }

   for (int i = 0; i < number_of_backups; i++)
   {
      if (backups[i] != NULL && backups[i]->valid)
      {
         result++;
      }
   }

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(server_path);

   return result;

error:

   return 0;
}
