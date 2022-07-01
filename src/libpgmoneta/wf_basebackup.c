/*
 * Copyright (C) 2022 Red Hat
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
#include <workflow.h>

/* system */
#include <stdlib.h>

static int basebackup_setup(int, char*, struct node*, struct node**);
static int basebackup_execute(int, char*, struct node*, struct node**);
static int basebackup_teardown(int, char*, struct node*, struct node**);

struct workflow*
pgmoneta_workflow_create_basebackup(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->setup = &basebackup_setup;
   wf->execute = &basebackup_execute;
   wf->teardown = &basebackup_teardown;
   wf->next = NULL;

   return wf;
}

static int
basebackup_setup(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   return 0;
}

static int
basebackup_execute(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   time_t start_time;
   char* root = NULL;
   char* d = NULL;
   char* cmd = NULL;
   int status;
   int usr;
   unsigned long size = 0;
   char* version = NULL;
   char* wal = NULL;
   int total_seconds;
   int hours;
   int minutes;
   int seconds;
   char elapsed[128];
   struct configuration* config;

   config = (struct configuration*)shmem;

   start_time = time(NULL);

   usr = -1;
   for (int i = 0; usr == -1 && i < config->number_of_users; i++)
   {
      if (!strcmp(config->servers[server].username, config->users[i].username))
      {
         usr = i;
      }
   }

   root = pgmoneta_get_server_backup_identifier(server, identifier);

   pgmoneta_mkdir(root);

   d = pgmoneta_get_server_backup_identifier_data(server, identifier);

   pgmoneta_mkdir(d);

   cmd = pgmoneta_append(cmd, "PGPASSWORD=\"");
   cmd = pgmoneta_append(cmd, config->users[usr].password);
   cmd = pgmoneta_append(cmd, "\" ");

   cmd = pgmoneta_append(cmd, config->pgsql_dir);
   if (!pgmoneta_ends_with(config->pgsql_dir, "/"))
   {
      cmd = pgmoneta_append(cmd, "/");
   }
   cmd = pgmoneta_append(cmd, "pg_basebackup ");

   cmd = pgmoneta_append(cmd, "-h ");
   cmd = pgmoneta_append(cmd, config->servers[server].host);
   cmd = pgmoneta_append(cmd, " ");

   cmd = pgmoneta_append(cmd, "-p ");
   cmd = pgmoneta_append_int(cmd, config->servers[server].port);
   cmd = pgmoneta_append(cmd, " ");

   cmd = pgmoneta_append(cmd, "-U ");
   cmd = pgmoneta_append(cmd, config->servers[server].username);
   cmd = pgmoneta_append(cmd, " ");

   if (strlen(config->servers[server].backup_slot) > 0)
   {
      cmd = pgmoneta_append(cmd, "-S ");
      cmd = pgmoneta_append(cmd, config->servers[server].backup_slot);
      cmd = pgmoneta_append(cmd, " ");
   }

   cmd = pgmoneta_append(cmd, "-l ");
   cmd = pgmoneta_append(cmd, identifier);
   cmd = pgmoneta_append(cmd, " ");

   cmd = pgmoneta_append(cmd, "-X stream ");
   cmd = pgmoneta_append(cmd, "--no-password ");
   cmd = pgmoneta_append(cmd, "-c fast ");

   cmd = pgmoneta_append(cmd, "-D ");
   cmd = pgmoneta_append(cmd, d);

   status = system(cmd);

   if (status != 0)
   {
      pgmoneta_log_error("Backup: Could not backup %s", config->servers[server].name);

      pgmoneta_create_info(root, identifier, 0);

      goto error;
   }
   else
   {
      total_seconds = (int)difftime(time(NULL), start_time);
      hours = total_seconds / 3600;
      minutes = (total_seconds % 3600) / 60;
      seconds = total_seconds % 60;

      memset(&elapsed[0], 0, sizeof(elapsed));
      sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

      pgmoneta_log_debug("Base: %s/%s (Elapsed: %s)", config->servers[server].name, identifier, &elapsed[0]);

      pgmoneta_read_version(d, &version);
      size = pgmoneta_directory_size(d);
      pgmoneta_read_wal(d, &wal);

      pgmoneta_create_info(root, identifier, 1);
      pgmoneta_update_info_string(root, INFO_WAL, wal);
      pgmoneta_update_info_unsigned_long(root, INFO_RESTORE, size);
      pgmoneta_update_info_string(root, INFO_VERSION, version);
      pgmoneta_update_info_bool(root, INFO_KEEP, false);
   }

   free(root);
   free(d);
   free(cmd);
   free(wal);
   free(version);

   return 0;

error:

   free(root);
   free(d);
   free(cmd);
   free(wal);
   free(version);

   return 1;
}

static int
basebackup_teardown(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   return 0;
}
