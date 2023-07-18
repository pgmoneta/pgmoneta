/*
 * Copyright (C) 2023 Red Hat
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
#include <node.h>
#include <pgmoneta.h>
#include <info.h>
#include <logging.h>
#include <string.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <stdlib.h>

static int restore_setup(int, char*, struct node*, struct node**);
static int restore_execute(int, char*, struct node*, struct node**);
static int restore_teardown(int, char*, struct node*, struct node**);

static int recovery_info_setup(int, char*, struct node*, struct node**);
static int recovery_info_execute(int, char*, struct node*, struct node**);
static int recovery_info_teardown(int, char*, struct node*, struct node**);

static char* get_user_password(char* username);

struct workflow*
pgmoneta_workflow_create_restore(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->setup = &restore_setup;
   wf->execute = &restore_execute;
   wf->teardown = &restore_teardown;
   wf->next = NULL;

   return wf;
}

struct workflow*
pgmoneta_workflow_create_recovery_info(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->setup = &recovery_info_setup;
   wf->execute = &recovery_info_execute;
   wf->teardown = &recovery_info_teardown;
   wf->next = NULL;

   return wf;
}

static int
restore_setup(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   return 0;
}

static int
restore_execute(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* position = NULL;
   char* directory = NULL;
   char* o = NULL;
   char* ident = NULL;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   struct backup* backup = NULL;
   struct backup* verify = NULL;
   char* d = NULL;
   char* root = NULL;
   char* base = NULL;
   char* from = NULL;
   char* to = NULL;
   char* id = NULL;
   char* origwal = NULL;
   char* waldir = NULL;
   char* waltarget = NULL;
   struct node* o_root = NULL;
   struct node* o_output = NULL;
   struct node* o_identifier = NULL;
   struct node* o_to = NULL;
   struct node* o_version = NULL;
   struct node* o_primary = NULL;
   struct node* o_recovery_info = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   position = pgmoneta_get_node_string(i_nodes, "position");

   directory = pgmoneta_get_node_string(i_nodes, "directory");

   if (!strcmp(identifier, "oldest"))
   {
      d = pgmoneta_get_server_backup(server);

      if (pgmoneta_get_backups(d, &number_of_backups, &backups))
      {
         goto error;
      }

      for (int i = 0; id == NULL && i < number_of_backups; i++)
      {
         if (backups[i]->valid == VALID_TRUE)
         {
            id = backups[i]->label;
         }
      }
   }
   else if (!strcmp(identifier, "latest") || !strcmp(identifier, "newest"))
   {
      d = pgmoneta_get_server_backup(server);

      if (pgmoneta_get_backups(d, &number_of_backups, &backups))
      {
         goto error;
      }

      for (int i = number_of_backups - 1; id == NULL && i >= 0; i--)
      {
         if (backups[i]->valid == VALID_TRUE)
         {
            id = backups[i]->label;
         }
      }
   }
   else
   {
      id = identifier;
   }

   if (id == NULL)
   {
      pgmoneta_log_error("Restore: No identifier for %s/%s", config->servers[server].name, identifier);
      goto error;
   }

   root = pgmoneta_get_server_backup(server);

   base = pgmoneta_get_server_backup_identifier(server, id);

   if (!pgmoneta_exists(base))
   {
      if (pgmoneta_get_backups(root, &number_of_backups, &backups))
      {
         goto error;
      }

      bool prefix_found = false;

      for (int i = 0; i < number_of_backups; i++)
      {
         if (backups[i]->valid == VALID_TRUE && pgmoneta_starts_with(backups[i]->label, id))
         {
            prefix_found = true;
            id = backups[i]->label;
            break;
         }
      }

      if (!prefix_found)
      {
         pgmoneta_log_error("Restore: Unknown identifier for %s/%s", config->servers[server].name, id);
         goto error;
      }
   }

   if (pgmoneta_get_backup(root, id, &verify))
   {
      pgmoneta_log_error("Restore: Unable to get backup for %s/%s", config->servers[server].name, id);
      goto error;
   }

   if (!verify->valid)
   {
      pgmoneta_log_error("Restore: Invalid backup for %s/%s", config->servers[server].name, id);
      goto error;
   }

   if (pgmoneta_create_node_string(directory, "root", &o_root))
   {
      goto error;
   }

   pgmoneta_append_node(o_nodes, o_root);

   from = pgmoneta_get_server_backup_identifier_data(server, id);

   to = pgmoneta_append(to, directory);
   to = pgmoneta_append(to, "/");
   to = pgmoneta_append(to, config->servers[server].name);
   to = pgmoneta_append(to, "-");
   to = pgmoneta_append(to, id);
   to = pgmoneta_append(to, "/");

   pgmoneta_delete_directory(to);

   if (pgmoneta_copy_postgresql(from, to, directory, config->servers[server].name, id, verify))
   {
      pgmoneta_log_error("Restore: Could not restore %s/%s", config->servers[server].name, id);
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

         pgmoneta_get_backup(root, id, &backup);

         if (pgmoneta_create_node_bool(primary, "primary", &o_primary))
         {
            goto error;
         }

         pgmoneta_append_node(o_nodes, o_primary);

         if (pgmoneta_create_node_int(backup->version, "version", &o_version))
         {
            goto error;
         }

         pgmoneta_append_node(o_nodes, o_version);

         if (pgmoneta_create_node_bool(true, "recovery info", &o_recovery_info))
         {
            goto error;
         }

         pgmoneta_append_node(o_nodes, o_recovery_info);

         if (copy_wal)
         {
            origwal = pgmoneta_get_server_backup_identifier_data_wal(server, id);
            waldir = pgmoneta_get_server_wal(server);

            waltarget = pgmoneta_append(waltarget, directory);
            waltarget = pgmoneta_append(waltarget, "/");
            waltarget = pgmoneta_append(waltarget, config->servers[server].name);
            waltarget = pgmoneta_append(waltarget, "-");
            waltarget = pgmoneta_append(waltarget, id);
            waltarget = pgmoneta_append(waltarget, "/pg_wal/");

            pgmoneta_copy_wal_files(waldir, waltarget, &backup->wal[0]);
         }
      }

      if (pgmoneta_create_node_string(to, "to", &o_to))
      {
         goto error;
      }

      pgmoneta_append_node(o_nodes, o_to);

   }

   o = pgmoneta_append(o, directory);
   o = pgmoneta_append(o, "/");

   ident = pgmoneta_append(ident, id);

   if (pgmoneta_create_node_string(o, "output", &o_output))
   {
      goto error;
   }

   pgmoneta_append_node(o_nodes, o_output);

   if (pgmoneta_create_node_string(ident, "identifier", &o_identifier))
   {
      goto error;
   }

   pgmoneta_append_node(o_nodes, o_identifier);

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(backup);
   free(verify);
   free(root);
   free(base);
   free(from);
   free(d);
   free(to);
   free(o);
   free(ident);
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
   free(verify);
   free(root);
   free(base);
   free(from);
   free(d);
   free(to);
   free(o);
   free(ident);
   free(origwal);
   free(waldir);
   free(waltarget);

   return 1;
}

static int
restore_teardown(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   return 0;
}

static int
recovery_info_setup(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   return 0;
}

static int
recovery_info_execute(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* base = NULL;
   char* position = NULL;
   bool primary;
   bool is_recovery_info;
   int version;
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

   is_recovery_info = pgmoneta_get_node_bool(*o_nodes, "recovery info");

   if (!is_recovery_info)
   {
      goto done;
   }

   base = pgmoneta_get_node_string(*o_nodes, "to");

   if (base == NULL)
   {
      goto error;
   }

   position = pgmoneta_get_node_string(i_nodes, "position");

   if (position == NULL)
   {
      goto error;
   }

   primary = pgmoneta_get_node_bool(*o_nodes, "primary");

   version = pgmoneta_get_node_int(*o_nodes, "version");

   if (!primary)
   {
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
   }

done:

   free(f);
   free(t);

   return 0;

error:

   free(f);
   free(t);

   return 1;
}

static int
recovery_info_teardown(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
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
