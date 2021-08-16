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
#include <gzip.h>
#include <info.h>
#include <logging.h>
#include <restore.h>
#include <utils.h>
#include <zstandard.h>

/* system */
#include <stdlib.h>
#include <unistd.h>

static int   create_recovery_info(int server, char* base, bool primary, char* position, int version);
static char* get_user_password(char* username);

void
pgmoneta_restore(int server, char* backup_id, char* position, char* directory, char** argv)
{
   char elapsed[128];
   time_t start_time;
   int total_seconds;
   int hours;
   int minutes;
   int seconds;
   char* output = NULL;
   char* id = NULL;
   struct configuration* config;

   pgmoneta_start_logging();

   config = (struct configuration*)shmem;

   pgmoneta_set_proc_title(1, argv, "restore", config->servers[server].name);

   start_time = time(NULL);

   if (!pgmoneta_restore_backup("Restore", server, backup_id, position, directory, &output, &id))
   {
      total_seconds = (int)difftime(time(NULL), start_time);
      hours = total_seconds / 3600;
      minutes = (total_seconds % 3600) / 60;
      seconds = total_seconds % 60;

      memset(&elapsed[0], 0, sizeof(elapsed));
      sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

      pgmoneta_log_info("Restore: %s/%s (Elapsed: %s)", config->servers[server].name, id, &elapsed[0]);
   }

   pgmoneta_stop_logging();

   free(output);
   free(id);

   free(backup_id);
   free(position);
   free(directory);

   exit(0);
}

int
pgmoneta_restore_backup(char* prefix, int server, char* backup_id, char* position, char* directory, char** output, char** identifier)
{
   char* o = NULL;
   char* ident = NULL;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   struct backup* backup = NULL;
   char* d = NULL;
   char* base = NULL;
   char* from = NULL;
   char* to = NULL;
   char* id = NULL;
   char* origwal = NULL;
   char* waldir = NULL;
   char* waltarget = NULL;
   struct configuration* config;

   *output = NULL;
   *identifier = NULL;

   config = (struct configuration*)shmem;

   if (!strcmp(backup_id, "oldest"))
   {
      d = NULL;
      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[server].name);
      d = pgmoneta_append(d, "/backup/");

      if (pgmoneta_get_backups(d, &number_of_backups, &backups))
      {
         goto error;
      }

      for (int i = 0; id == NULL && i < number_of_backups; i++)
      {
         if (backups[i] != NULL && backups[i]->valid)
         {
            id = backups[i]->label;
         }
      }
   }
   else if (!strcmp(backup_id, "latest") || !strcmp(backup_id, "newest"))
   {
      d = NULL;
      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[server].name);
      d = pgmoneta_append(d, "/backup/");

      if (pgmoneta_get_backups(d, &number_of_backups, &backups))
      {
         goto error;
      }

      for (int i = number_of_backups - 1; id == NULL && i >= 0; i--)
      {
         if (backups[i] != NULL && backups[i]->valid)
         {
            id = backups[i]->label;
         }
      }
   }
   else
   {
      id = backup_id;
   }

   if (id == NULL)
   {
      pgmoneta_log_error("%s: No identifier for %s/%s", prefix, config->servers[server].name, backup_id);
      goto error;
   }

   base = pgmoneta_append(base, config->base_dir);
   base = pgmoneta_append(base, "/");
   base = pgmoneta_append(base, config->servers[server].name);
   base = pgmoneta_append(base, "/backup/");
   base = pgmoneta_append(base, id);
   base = pgmoneta_append(base, "/");

   if (!pgmoneta_exists(base))
   {
      pgmoneta_log_error("%s: Unknown identifier for %s/%s", prefix, config->servers[server].name, id);
      goto error;
   }

   from = pgmoneta_append(from, base);
   from = pgmoneta_append(from, "data");

   to = pgmoneta_append(to, directory);
   to = pgmoneta_append(to, "/");
   to = pgmoneta_append(to, config->servers[server].name);
   to = pgmoneta_append(to, "-");
   to = pgmoneta_append(to, id);
   to = pgmoneta_append(to, "/");

   pgmoneta_delete_directory(to);

   if (pgmoneta_copy_directory(from, to))
   {
      pgmoneta_log_error("%s: Could not restore %s/%s", prefix, config->servers[server].name, id);
      goto error;
   }
   else
   {
      if (position != NULL)
      {
         char tokens[512];
         bool primary = true;
         bool copy_wal = false;
         char* ptr = NULL;

         memset(&tokens[0], 0, sizeof(tokens));
         memcpy(&tokens[0], position, strlen(position));

         ptr = strtok(&tokens[0], ",");

         while (ptr != NULL)
         {
            char key[256];
            char value[256];
            char* equal = NULL;

            memset(&key[0], 0, sizeof(key));
            memset(&value[0], 0, sizeof(value));

            equal = strchr(ptr, '=');

            if (equal == NULL)
            {
               memcpy(&key[0], ptr, strlen(ptr));
            }
            else
            {
               memcpy(&key[0], ptr, strlen(ptr) - strlen(equal));
               memcpy(&value[0], equal + 1, strlen(equal) - 1);
            }

            if (!strcmp(&key[0], "current") ||
                !strcmp(&key[0], "immediate") ||
                !strcmp(&key[0], "name") ||
                !strcmp(&key[0], "xid") ||
                !strcmp(&key[0], "lsn") ||
                !strcmp(&key[0], "time"))
            {
               copy_wal = true;
            }
            else if (!strcmp(&key[0], "primary"))
            {
               primary = true;
            }
            else if (!strcmp(&key[0], "replica"))
            {
               primary = false;
            }
            else if (!strcmp(&key[0], "inclusive") || !strcmp(&key[0], "timeline") || !strcmp(&key[0], "action"))
            {
               /* Ok */
            }

            ptr = strtok(NULL, ",");
         }

         pgmoneta_get_backup(base, &backup);
         create_recovery_info(server, to, primary, position, backup->version);

         if (copy_wal)
         {
            origwal = pgmoneta_append(origwal, base);
            origwal = pgmoneta_append(origwal, "data/pg_wal/");

            waldir = pgmoneta_append(waldir, config->base_dir);
            waldir = pgmoneta_append(waldir, "/");
            waldir = pgmoneta_append(waldir, config->servers[server].name);
            waldir = pgmoneta_append(waldir, "/wal/");

            waltarget = pgmoneta_append(waltarget, directory);
            waltarget = pgmoneta_append(waltarget, "/");
            waltarget = pgmoneta_append(waltarget, config->servers[server].name);
            waltarget = pgmoneta_append(waltarget, "-");
            waltarget = pgmoneta_append(waltarget, id);
            waltarget = pgmoneta_append(waltarget, "/pg_wal/");

            pgmoneta_copy_wal_files(waldir, waltarget, &backup->wal[0]);
         }
      }

      if (config->compression_type == COMPRESSION_GZIP)
      {
         pgmoneta_gunzip_data(to);
      }
      else if (config->compression_type == COMPRESSION_ZSTD)
      {
         pgmoneta_zstandardd_data(to);
      }
   }

   o = pgmoneta_append(o, directory);
   o = pgmoneta_append(o, "/");
   o = pgmoneta_append(o, config->servers[server].name);
   o = pgmoneta_append(o, "-");
   o = pgmoneta_append(o, id);
   o = pgmoneta_append(o, "/");

   ident = pgmoneta_append(ident, id);

   *output = o;
   *identifier = ident;

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(backup);
   free(base);
   free(from);
   free(to);
   free(d);
   free(origwal);
   free(waldir);
   free(waltarget);

   return 0;

error:

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(backup);
   free(base);
   free(from);
   free(to);
   free(d);
   free(origwal);
   free(waldir);
   free(waltarget);

   return 1;
}

static int
create_recovery_info(int server, char* base, bool primary, char* position, int version)
{
   char tokens[256];
   char buffer[256];
   char line[1024];
   char* f = NULL;
   FILE* ffile = NULL;
   char* t = NULL;
   FILE* tfile = NULL;
   bool mode = false;
   char* ptr = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   f = pgmoneta_append(f, base);

   if (version < 12)
   {
      f = pgmoneta_append(f, "/recovery.conf");
   }
   else
   {
      f = pgmoneta_append(f, "/postgresql.conf");
   }

   t = pgmoneta_append(t, f);
   t = pgmoneta_append(t, ".tmp");

   if (pgmoneta_exists(f))
   {
      ffile = fopen(f, "r");
   }

   tfile = fopen(t, "w");

   if (ffile != NULL)
   {
      while ((fgets(&buffer[0], sizeof(buffer), ffile)) != NULL)
      {
         if (pgmoneta_starts_with(&buffer[0], "standby_mode") ||
             pgmoneta_starts_with(&buffer[0], "recovery_target") ||
             pgmoneta_starts_with(&buffer[0], "primary_conninfo") ||
             pgmoneta_starts_with(&buffer[0], "primary_slot_name"))
         {
            memset(&line[0], 0, sizeof(line));
            snprintf(&line[0], sizeof(line), "#%s", &buffer[0]);
            fputs(&line[0], tfile);
         }
         else
         {
            fputs(&buffer[0], tfile);
         }
      }
   }

   memset(&tokens[0], 0, sizeof(tokens));
   memcpy(&tokens[0], position, strlen(position));

   memset(&line[0], 0, sizeof(line));
   snprintf(&line[0], sizeof(line), "#\n");
   fputs(&line[0], tfile);

   memset(&line[0], 0, sizeof(line));
   snprintf(&line[0], sizeof(line), "# Generated by pgmoneta\n");
   fputs(&line[0], tfile);

   memset(&line[0], 0, sizeof(line));
   snprintf(&line[0], sizeof(line), "#\n");
   fputs(&line[0], tfile);

   if (version < 12)
   {
      memset(&line[0], 0, sizeof(line));
      snprintf(&line[0], sizeof(line), "standby_mode = %s\n", primary ? "off" : "on");
      fputs(&line[0], tfile);
   }

   if (!primary)
   {
      if (strlen(config->servers[server].wal_slot) == 0)
      {
         memset(&line[0], 0, sizeof(line));
         snprintf(&line[0], sizeof(line), "primary_conninfo = \'host=%s port=%d user=%s password=%s\'\n",
                  config->servers[server].host, config->servers[server].port, config->servers[server].username,
                  get_user_password(config->servers[server].username));
         fputs(&line[0], tfile);
      }
      else
      {
         memset(&line[0], 0, sizeof(line));
         snprintf(&line[0], sizeof(line), "primary_conninfo = \'host=%s port=%d user=%s password=%s application_name=%s\'\n",
                  config->servers[server].host, config->servers[server].port, config->servers[server].username,
                  get_user_password(config->servers[server].username), config->servers[server].wal_slot);
         fputs(&line[0], tfile);

         memset(&line[0], 0, sizeof(line));
         snprintf(&line[0], sizeof(line), "primary_slot_name = \'%s\'\n", config->servers[server].wal_slot);
         fputs(&line[0], tfile);
      }
   }

   ptr = strtok(&tokens[0], ",");

   while (ptr != NULL)
   {
      char key[256];
      char value[256];
      char* equal = NULL;

      memset(&key[0], 0, sizeof(key));
      memset(&value[0], 0, sizeof(value));

      equal = strchr(ptr, '=');

      if (equal == NULL)
      {
         memcpy(&key[0], ptr, strlen(ptr));
      }
      else
      {
         memcpy(&key[0], ptr, strlen(ptr) - strlen(equal));
         memcpy(&value[0], equal + 1, strlen(equal) - 1);
      }

      if (!strcmp(&key[0], "current") || !strcmp(&key[0], "immediate"))
      {
         if (!mode)
         {
            memset(&line[0], 0, sizeof(line));
            snprintf(&line[0], sizeof(line), "recovery_target = \'immediate\'\n");
            fputs(&line[0], tfile);

            mode = true;
         }
      }
      else if (!strcmp(&key[0], "name"))
      {
         if (!mode)
         {
            memset(&line[0], 0, sizeof(line));
            snprintf(&line[0], sizeof(line), "recovery_target_name = \'%s\'\n", strlen(value) > 0 ? &value[0] : "");
            fputs(&line[0], tfile);

            mode = true;
         }
      }
      else if (!strcmp(&key[0], "xid"))
      {
         if (!mode)
         {
            memset(&line[0], 0, sizeof(line));
            snprintf(&line[0], sizeof(line), "recovery_target_xid = \'%s\'\n", strlen(value) > 0 ? &value[0] : "");
            fputs(&line[0], tfile);

            mode = true;
         }
      }
      else if (!strcmp(&key[0], "lsn"))
      {
         if (!mode)
         {
            memset(&line[0], 0, sizeof(line));
            snprintf(&line[0], sizeof(line), "recovery_target_lsn = \'%s\'\n", strlen(value) > 0 ? &value[0] : "");
            fputs(&line[0], tfile);

            mode = true;
         }
      }
      else if (!strcmp(&key[0], "time"))
      {
         if (!mode)
         {
            memset(&line[0], 0, sizeof(line));
            snprintf(&line[0], sizeof(line), "recovery_target_time = \'%s\'\n", strlen(value) > 0 ? &value[0] : "");
            fputs(&line[0], tfile);

            mode = true;
         }
      }
      else if (!strcmp(&key[0], "primary") || !strcmp(&key[0], "replica"))
      {
         /* Ok */
      }
      else if (!strcmp(&key[0], "inclusive"))
      {
         memset(&line[0], 0, sizeof(line));
         snprintf(&line[0], sizeof(line), "recovery_target_inclusive = %s\n", strlen(value) > 0 ? &value[0] : "on");
         fputs(&line[0], tfile);
      }
      else if (!strcmp(&key[0], "timeline"))
      {
         memset(&line[0], 0, sizeof(line));
         snprintf(&line[0], sizeof(line), "recovery_target_timeline = \'%s\'\n", strlen(value) > 0 ? &value[0] : "latest");
         fputs(&line[0], tfile);
      }
      else if (!strcmp(&key[0], "action"))
      {
         memset(&line[0], 0, sizeof(line));
         snprintf(&line[0], sizeof(line), "recovery_target_action = \'%s\'\n", strlen(value) > 0 ? &value[0] : "pause");
         fputs(&line[0], tfile);
      }
      else
      {
         memset(&line[0], 0, sizeof(line));
         snprintf(&line[0], sizeof(line), "%s = \'%s\'\n", &key[0], strlen(value) > 0 ? &value[0] : "");
         fputs(&line[0], tfile);
      }

      ptr = strtok(NULL, ",");
   }

   if (ffile != NULL)
   {
      fclose(ffile);
   }
   if (tfile != NULL)
   {
      fclose(tfile);
   }

   pgmoneta_move_file(t, f);

   free(f);
   free(t);

   return 0;
}

static char*
get_user_password(char* username)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < config->number_of_users; i++)
   {
      if (!strcmp(&config->users[i].username[0], username))
      {
         return &config->users[i].password[0];
      }
   }

   return NULL;
}
