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

/* pgmoneta */
#include <pgmoneta.h>
#include <art.h>
#include <deque.h>
#include <info.h>
#include <logging.h>
#include <restore.h>
#include <string.h>
#include <utils.h>
#include <workers.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

static char* restore_name(void);
static int restore_execute(char*, struct art*);

static char* combine_incremental_name(void);
static int combine_incremental_execute(char*, struct art*);

static char*recovery_info_name(void);
static int recovery_info_execute(char*, struct art*);

static char*restore_excluded_files_name(void);
static int restore_excluded_files_execute(char*, struct art*);
static int restore_excluded_files_teardown(char*, struct art*);

static char* get_user_password(char* username);
static void create_standby_signal(char* basedir);

struct workflow*
pgmoneta_create_restore(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->name = &restore_name;
   wf->setup = &pgmoneta_common_setup;
   wf->execute = &restore_execute;
   wf->teardown = &pgmoneta_common_teardown;
   wf->next = NULL;

   return wf;
}

struct workflow*
pgmoneta_create_combine_incremental(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->name = &combine_incremental_name;
   wf->setup = &pgmoneta_common_setup;
   wf->execute = &combine_incremental_execute;
   wf->teardown = &pgmoneta_common_teardown;
   wf->next = NULL;

   return wf;
}

struct workflow*
pgmoneta_create_recovery_info(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->name = &recovery_info_name;
   wf->setup = &pgmoneta_common_setup;
   wf->execute = &recovery_info_execute;
   wf->teardown = &pgmoneta_common_teardown;
   wf->next = NULL;

   return wf;
}

struct workflow*
pgmoneta_restore_excluded_files(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->name = &restore_excluded_files_name;
   wf->setup = &pgmoneta_common_setup;
   wf->execute = &restore_excluded_files_execute;
   wf->teardown = &restore_excluded_files_teardown;
   wf->next = NULL;

   return wf;
}

static char*
restore_name(void)
{
   return "Restore";
}

static int
restore_execute(char* name, struct art* nodes)
{
   int server = -1;
   char* position = NULL;
   char* directory = NULL;
   struct backup* backup = NULL;
   char* label = NULL;
   char* from = NULL;
   char* to = NULL;
   char* origwal = NULL;
   char* waldir = NULL;
   char* waltarget = NULL;
   int number_of_workers = 0;
   struct workers* workers = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

#ifdef DEBUG
   char* a = NULL;
   a = pgmoneta_art_to_string(nodes, FORMAT_TEXT, NULL, 0);
   pgmoneta_log_debug("(Tree)\n%s", a);
   assert(nodes != NULL);
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
   assert(pgmoneta_art_contains_key(nodes, USER_POSITION));
   assert(pgmoneta_art_contains_key(nodes, NODE_TARGET_ROOT));
   assert(pgmoneta_art_contains_key(nodes, NODE_TARGET_BASE));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP));
   free(a);
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);
   position = (char*)pgmoneta_art_search(nodes, USER_POSITION);
   directory = (char*)pgmoneta_art_search(nodes, NODE_TARGET_ROOT);
   backup = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);

   pgmoneta_log_debug("Restore (execute): %s/%s", config->servers[server].name, label);

   from = pgmoneta_get_server_backup_identifier_data(server, label);
   to = (char*)pgmoneta_art_search(nodes, NODE_TARGET_BASE);

   pgmoneta_delete_directory(to);

   number_of_workers = pgmoneta_get_number_of_workers(server);
   if (number_of_workers > 0)
   {
      pgmoneta_workers_initialize(number_of_workers, &workers);
   }

   if (pgmoneta_copy_postgresql_restore(from, to, directory, config->servers[server].name, label, backup, workers))
   {
      pgmoneta_log_error("Restore: Could not restore %s/%s", config->servers[server].name, label);
      goto error;
   }
   else
   {
      if (position != NULL && strlen(position) > 0)
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

         if (pgmoneta_art_insert(nodes, NODE_PRIMARY, primary, ValueBool))
         {
            goto error;
         }

         if (pgmoneta_art_insert(nodes, NODE_RECOVERY_INFO, true, ValueBool))
         {
            goto error;
         }

         if (copy_wal)
         {
            origwal = pgmoneta_get_server_backup_identifier_data_wal(server, label);
            waldir = pgmoneta_get_server_wal(server);

            waltarget = pgmoneta_append(waltarget, directory);
            waltarget = pgmoneta_append(waltarget, "/");
            waltarget = pgmoneta_append(waltarget, config->servers[server].name);
            waltarget = pgmoneta_append(waltarget, "-");
            waltarget = pgmoneta_append(waltarget, label);
            waltarget = pgmoneta_append(waltarget, "/pg_wal/");

            pgmoneta_copy_wal_files(waldir, waltarget, &backup->wal[0], workers);
         }
      }
      else
      {
         if (pgmoneta_art_insert(nodes, NODE_RECOVERY_INFO, false, ValueBool))
         {
            goto error;
         }
      }
   }

   if (number_of_workers > 0)
   {
      pgmoneta_workers_wait(workers);
      if (!workers->outcome)
      {
         goto error;
      }
      pgmoneta_workers_destroy(workers);
   }

   free(from);
   free(origwal);
   free(waldir);
   free(waltarget);

   return 0;

error:

   if (number_of_workers > 0)
   {
      pgmoneta_workers_destroy(workers);
   }

   free(from);
   free(origwal);
   free(waldir);
   free(waltarget);

   return 1;
}

static char*
combine_incremental_name(void)
{
   return "Combine incremental";
}

static int
combine_incremental_execute(char* name, struct art* nodes)
{
   int server = -1;
   char* identifier = NULL;
   struct deque* prior_backups = NULL;
   char* input_dir = NULL;
   char* output_dir;
   char* base = NULL;
   struct backup* bck = NULL;
   struct json* manifest = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

#ifdef DEBUG
   char* a = NULL;
   a = pgmoneta_art_to_string(nodes, FORMAT_TEXT, NULL, 0);
   pgmoneta_log_debug("(Tree)\n%s", a);
   assert(nodes != NULL);
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, USER_IDENTIFIER));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUPS));
   assert(pgmoneta_art_contains_key(nodes, NODE_TARGET_ROOT));
   assert(pgmoneta_art_contains_key(nodes, NODE_MANIFEST));
   free(a);
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   identifier = (char*)pgmoneta_art_search(nodes, USER_IDENTIFIER);

   pgmoneta_log_debug("Combine incremental (execute): %s/%s", config->servers[server].name, identifier);
   bck = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);

   prior_backups = (struct deque*)pgmoneta_art_search(nodes, NODE_BACKUPS);
   if (prior_backups == NULL || pgmoneta_deque_size(prior_backups) < 2)
   {
      pgmoneta_log_error("Combine incremental: should have at least 2 backups");
      goto error;
   }
   input_dir = (char*)pgmoneta_deque_poll(prior_backups, NULL);
   base = (char*) pgmoneta_art_search(nodes, NODE_TARGET_ROOT);
   output_dir = (char*) pgmoneta_art_search(nodes, NODE_TARGET_BASE);
   manifest = (struct json*)pgmoneta_art_search(nodes, NODE_MANIFEST);
   if (manifest == NULL)
   {
      goto error;
   }

   if (pgmoneta_exists(output_dir))
   {
      pgmoneta_delete_directory(output_dir);
   }

   for (int i = 0; i < bck->number_of_tablespaces; i++)
   {
      char tblspc[MAX_PATH];
      memset(tblspc, 0, MAX_PATH);
      snprintf(tblspc, MAX_PATH, "%s/%s-%s-%s",
               base, config->servers[server].name, bck->label, bck->tablespaces[i]);
      if (pgmoneta_exists(tblspc))
      {
         pgmoneta_delete_directory(tblspc);
      }
   }

   if (pgmoneta_combine_backups(server, base, input_dir, output_dir, prior_backups, bck, manifest))
   {
      goto error;
   }

   free(input_dir);
   return 0;

error:
   free(input_dir);
   return 1;
}

static char*
recovery_info_name(void)
{
   return "Recovery info";
}

static int
recovery_info_execute(char* name, struct art* nodes)
{
   int server = -1;
   char* identifier = NULL;
   char* base = NULL;
   char* position = NULL;
   bool primary;
   bool is_recovery_info;
   char tokens[256];
   char buffer[256];
   char line[1024];
   char* f = NULL;
   FILE* ffile = NULL;
   char* t = NULL;
   FILE* tfile = NULL;
   char* path = NULL;
   bool mode = false;
   char* ptr = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

#ifdef DEBUG
   char* a = NULL;
   a = pgmoneta_art_to_string(nodes, FORMAT_TEXT, NULL, 0);
   pgmoneta_log_debug("(Tree)\n%s", a);
   assert(nodes != NULL);
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, USER_IDENTIFIER));
   free(a);
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   identifier = (char*)pgmoneta_art_search(nodes, USER_IDENTIFIER);

   pgmoneta_log_debug("Recovery (execute): %s/%s", config->servers[server].name, identifier);

   is_recovery_info = (bool)pgmoneta_art_search(nodes, NODE_RECOVERY_INFO);

   if (!is_recovery_info)
   {
      goto done;
   }

   base = (char*)pgmoneta_art_search(nodes, NODE_TARGET_BASE);

   if (base == NULL)
   {
      goto error;
   }

   position = (char*)pgmoneta_art_search(nodes, USER_POSITION);
   primary = (bool)pgmoneta_art_search(nodes, NODE_PRIMARY);

   if (!primary)
   {
      f = pgmoneta_append(f, base);
      if (!pgmoneta_ends_with(f, "/"))
      {
         f = pgmoneta_append(f, "/");
      }
      f = pgmoneta_append(f, "postgresql.conf");

      t = pgmoneta_append(t, f);
      t = pgmoneta_append(t, ".tmp");

      if (pgmoneta_exists(f))
      {
         ffile = fopen(f, "r");
      }
      else
      {
         pgmoneta_log_error("%s does not exists", f);
         goto error;
      }

      tfile = fopen(t, "w");

      if (tfile == NULL)
      {
         pgmoneta_log_error("Could not create %s", t);
         goto error;
      }

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

      if (position != NULL)
      {
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

         memset(&line[0], 0, sizeof(line));
         snprintf(&line[0], sizeof(line), "primary_conninfo = \'host=%s port=%d user=%s password=%s application_name=%s\'\n",
                  config->servers[server].host, config->servers[server].port, config->servers[server].username,
                  get_user_password(config->servers[server].username), config->servers[server].wal_slot);
         fputs(&line[0], tfile);

         memset(&line[0], 0, sizeof(line));
         snprintf(&line[0], sizeof(line), "primary_slot_name = \'%s\'\n", config->servers[server].wal_slot);
         fputs(&line[0], tfile);

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

      create_standby_signal(base);
   }
   else
   {
      f = pgmoneta_append(f, base);
      if (!pgmoneta_ends_with(f, "/"))
      {
         f = pgmoneta_append(f, "/");
      }
      f = pgmoneta_append(f, "postgresql.auto.conf");

      t = pgmoneta_append(t, f);
      t = pgmoneta_append(t, ".tmp");

      if (pgmoneta_exists(f))
      {
         ffile = fopen(f, "r");
      }
      else
      {
         pgmoneta_log_error("%s does not exists", f);
         goto error;
      }

      tfile = fopen(t, "w");

      if (tfile == NULL)
      {
         pgmoneta_log_error("Could not create %s", t);
         goto error;
      }

      if (ffile != NULL)
      {
         while ((fgets(&buffer[0], sizeof(buffer), ffile)) != NULL)
         {
            if (pgmoneta_starts_with(&buffer[0], "primary_conninfo"))
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

      if (ffile != NULL)
      {
         fclose(ffile);
      }
      if (tfile != NULL)
      {
         fclose(tfile);
      }

      pgmoneta_move_file(t, f);

      path = pgmoneta_append(path, base);
      if (!pgmoneta_ends_with(path, "/"))
      {
         path = pgmoneta_append(path, "/");
      }
      path = pgmoneta_append(path, "standby.signal");

      if (pgmoneta_exists(path))
      {
         pgmoneta_delete_file(path, NULL);
      }
      else
      {
         pgmoneta_log_debug("%s doesn't exists", path);
      }
   }

done:

   free(f);
   free(t);
   free(path);

   return 0;

error:

   if (ffile != NULL)
   {
      fclose(ffile);
   }
   if (tfile != NULL)
   {
      fclose(tfile);
   }

   free(f);
   free(t);
   free(path);

   return 1;
}

static char*
restore_excluded_files_name(void)
{
   return "Recovery excluded files";
}

static int
restore_excluded_files_execute(char* name, struct art* nodes)
{
   int server = -1;
   char* identifier = NULL;
   char* from = NULL;
   char* to = NULL;
   char* suffix = NULL;
   struct backup* backup = NULL;
   struct workers* workers = NULL;
   int number_of_workers = 0;
   char** restore_last_files_names = NULL;
   struct configuration* config = (struct configuration*)shmem;

#ifdef DEBUG
   char* a = NULL;
   a = pgmoneta_art_to_string(nodes, FORMAT_TEXT, NULL, 0);
   pgmoneta_log_debug("(Tree)\n%s", a);
   assert(nodes != NULL);
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, USER_IDENTIFIER));
   free(a);
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   identifier = (char*)pgmoneta_art_search(nodes, USER_IDENTIFIER);

   pgmoneta_log_debug("Excluded (execute): %s/%s", config->servers[server].name, identifier);

   if (pgmoneta_get_restore_last_files_names(&restore_last_files_names))
   {
      goto error;
   }

   backup = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);

   switch (backup->compression)
   {
      case COMPRESSION_CLIENT_GZIP:
      case COMPRESSION_SERVER_GZIP:
         suffix = pgmoneta_append(suffix, ".gz");
         break;
      case COMPRESSION_CLIENT_ZSTD:
      case COMPRESSION_SERVER_ZSTD:
         suffix = pgmoneta_append(suffix, ".zstd");
         break;
      case COMPRESSION_CLIENT_LZ4:
      case COMPRESSION_SERVER_LZ4:
         suffix = pgmoneta_append(suffix, ".lz4");
         break;
      case COMPRESSION_CLIENT_BZIP2:
         suffix = pgmoneta_append(suffix, ".bz2");
         break;
      case COMPRESSION_NONE:
         break;
      default:
         break;
   }

   switch (backup->encryption)
   {
      case ENCRYPTION_AES_256_CBC:
         suffix = pgmoneta_append(suffix, ".aes");
         break;
      case ENCRYPTION_AES_192_CBC:
         suffix = pgmoneta_append(suffix, ".aes");
         break;
      case ENCRYPTION_AES_128_CBC:
         suffix = pgmoneta_append(suffix, ".aes");
         break;
      case ENCRYPTION_AES_256_CTR:
         suffix = pgmoneta_append(suffix, ".aes");
         break;
      case ENCRYPTION_AES_192_CTR:
         suffix = pgmoneta_append(suffix, ".aes");
         break;
      case ENCRYPTION_AES_128_CTR:
         suffix = pgmoneta_append(suffix, ".aes");
         break;
      case ENCRYPTION_NONE:
         break;
      default:
         break;
   }

   from = pgmoneta_append(from, (char*)pgmoneta_art_search(nodes, NODE_BACKUP_DATA));
   to = pgmoneta_append(to, (char*)pgmoneta_art_search(nodes, NODE_TARGET_BASE));

   number_of_workers = pgmoneta_get_number_of_workers(server);
   if (number_of_workers > 0)
   {
      pgmoneta_workers_initialize(number_of_workers, &workers);
   }

   for (int i = 0; restore_last_files_names[i] != NULL; i++)
   {
      char* from_file = NULL;
      char* to_file = NULL;

      from_file = pgmoneta_append(from_file, from);
      from_file = pgmoneta_append(from_file, restore_last_files_names[i]);
      from_file = pgmoneta_append(from_file, suffix);

      to_file = pgmoneta_append(to_file, to);
      to_file = pgmoneta_append(to_file, restore_last_files_names[i]);
      to_file = pgmoneta_append(to_file, suffix);

      pgmoneta_log_trace("Excluded: %s -> %s", from_file, to_file);

      if (pgmoneta_copy_file(from_file, to_file, workers))
      {
         pgmoneta_log_error("Restore: Could not copy file %s to %s", from_file, to_file);
         goto error;
      }

      free(from_file);
      from_file = NULL;

      free(to_file);
      to_file = NULL;
   }

   if (number_of_workers > 0)
   {
      pgmoneta_workers_wait(workers);
      if (!workers->outcome)
      {
         goto error;
      }
      pgmoneta_workers_destroy(workers);
   }

   for (int i = 0; restore_last_files_names[i] != NULL; i++)
   {
      free(restore_last_files_names[i]);
   }
   free(restore_last_files_names);
   free(from);
   free(to);
   free(suffix);

   return 0;

error:

   if (number_of_workers > 0)
   {
      pgmoneta_workers_destroy(workers);
   }

   for (int i = 0; restore_last_files_names[i] != NULL; i++)
   {
      free(restore_last_files_names[i]);
   }
   free(restore_last_files_names);
   free(from);
   free(to);
   free(suffix);

   return 1;
}

static int
restore_excluded_files_teardown(char* name, struct art* nodes)
{
   int server = -1;
   char* identifier = NULL;
   char** restore_last_files_names = NULL;
   char* to = NULL;
   char* suffix = NULL;
   struct backup* backup = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

#ifdef DEBUG
   char* a = NULL;
   a = pgmoneta_art_to_string(nodes, FORMAT_TEXT, NULL, 0);
   pgmoneta_log_debug("(Tree)\n%s", a);
   assert(nodes != NULL);
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, USER_IDENTIFIER));
   free(a);
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   identifier = (char*)pgmoneta_art_search(nodes, USER_IDENTIFIER);

   pgmoneta_log_debug("Excluded (teardown): %s/%s", config->servers[server].name, identifier);

   backup = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);

   to = pgmoneta_append(to, (char*)pgmoneta_art_search(nodes, NODE_TARGET_BASE));

   switch (backup->compression)
   {
      case COMPRESSION_CLIENT_GZIP:
      case COMPRESSION_SERVER_GZIP:
         suffix = pgmoneta_append(suffix, ".gz");
         break;
      case COMPRESSION_CLIENT_ZSTD:
      case COMPRESSION_SERVER_ZSTD:
         suffix = pgmoneta_append(suffix, ".zstd");
         break;
      case COMPRESSION_CLIENT_LZ4:
      case COMPRESSION_SERVER_LZ4:
         suffix = pgmoneta_append(suffix, ".lz4");
         break;
      case COMPRESSION_CLIENT_BZIP2:
         suffix = pgmoneta_append(suffix, ".bz2");
         break;
      case COMPRESSION_NONE:
         break;
      default:
         break;
   }

   switch (backup->encryption)
   {
      case ENCRYPTION_AES_256_CBC:
         suffix = pgmoneta_append(suffix, ".aes");
         break;
      case ENCRYPTION_AES_192_CBC:
         suffix = pgmoneta_append(suffix, ".aes");
         break;
      case ENCRYPTION_AES_128_CBC:
         suffix = pgmoneta_append(suffix, ".aes");
         break;
      case ENCRYPTION_AES_256_CTR:
         suffix = pgmoneta_append(suffix, ".aes");
         break;
      case ENCRYPTION_AES_192_CTR:
         suffix = pgmoneta_append(suffix, ".aes");
         break;
      case ENCRYPTION_AES_128_CTR:
         suffix = pgmoneta_append(suffix, ".aes");
         break;
      case ENCRYPTION_NONE:
         break;
      default:
         break;
   }

   if (pgmoneta_get_restore_last_files_names(&restore_last_files_names))
   {
      goto error;
   }

   for (int i = 0; restore_last_files_names[i] != NULL; i++)
   {
      char* to_file = NULL;

      to_file = pgmoneta_append(to_file, to);
      to_file = pgmoneta_append(to_file, restore_last_files_names[i]);
      to_file = pgmoneta_append(to_file, suffix);

      if (pgmoneta_exists(to_file))
      {
         pgmoneta_delete_file(to_file, NULL);
      }
      else
      {
         pgmoneta_log_debug("%s doesn't exists", to_file);
      }

      free(to_file);
      to_file = NULL;
   }

   for (int i = 0; restore_last_files_names[i] != NULL; i++)
   {
      free(restore_last_files_names[i]);
   }
   free(restore_last_files_names);
   free(to);
   free(suffix);

   return 0;

error:
   for (int i = 0; restore_last_files_names[i] != NULL; i++)
   {
      free(restore_last_files_names[i]);
   }
   free(restore_last_files_names);
   free(to);
   free(suffix);

   return 1;
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

static void
create_standby_signal(char* basedir)
{
   char* f = NULL;
   int fd = 0;

   f = pgmoneta_append(f, basedir);
   if (!pgmoneta_ends_with(f, "/"))
   {
      f = pgmoneta_append(f, "/");
   }
   f = pgmoneta_append(f, "standby.signal");

   if ((fd = creat(f, 0600)) < 0)
   {
      pgmoneta_log_error("Unable to create: %s", f);
   }

   if (fd > 0)
   {
      close(fd);
   }

   free(f);
}
